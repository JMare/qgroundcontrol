/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "TerrainOverlayMapRenderer.h"
#include "HeatmapImageProvider.h"

#include <QVariant>
#include <QVariantMap>
#include <QtMath>
#include <QtCore/qapplicationstatic.h>
#include <qfileinfo.h>
#include "QGCLoggingCategory.h"

#include "gdal_priv.h"
#include "cpl_conv.h"

QGC_LOGGING_CATEGORY(TerrainOverlayMapLog, "qgc.terrainoverlay.maprenderer");

Q_APPLICATION_STATIC(TerrainOverlayMapRenderer, _instance);

TerrainOverlayMapRenderer* TerrainOverlayMapRenderer::instance()
{
    return _instance;
}

void TerrainOverlayMapRenderer::registerQmlTypes()
{
    qmlRegisterUncreatableType<TerrainOverlayMapRenderer>(
        "QGroundControl.TerrainOverlayMapRenderer", 1, 0,
        "TerrainOverlayMapRenderer",
        "Reference only"
    );
}

TerrainOverlayMapRenderer::TerrainOverlayMapRenderer(QObject* parent)
    : QObject(parent)
{
    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Initializing singleton.";
    setImageProvider(HeatmapImageProvider::instance());

    GDALAllRegister();
    loadGeoTiff("/home/james/Downloads/output_AW3D30_small.tif");
}

void TerrainOverlayMapRenderer::setImageProvider(HeatmapImageProvider* provider)
{
    _imageProvider = provider;
}

void TerrainOverlayMapRenderer::loadGeoTiff(const QString& filePath) {
    QFileInfo file(filePath);
    if (!file.exists()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] GeoTIFF file does not exist:" << filePath;
        return;
    }

    GDALDataset* dataset = static_cast<GDALDataset*>(GDALOpen(filePath.toUtf8().constData(), GA_ReadOnly));
    if (!dataset) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Failed to open GeoTIFF:" << filePath;
        return;
    }

    GDALRasterBand* band = dataset->GetRasterBand(1);
    _gridCols = band->GetXSize();
    _gridRows = band->GetYSize();

    float* rasterData = new float[_gridCols * _gridRows];
    if (band->RasterIO(GF_Read, 0, 0, _gridCols, _gridRows, rasterData, _gridCols, _gridRows, GDT_Float32, 0, 0) != CE_None) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Failed to read raster data.";
        delete[] rasterData;
        GDALClose(dataset);
        return;
    }

    _altitudeGrid.clear();
    for (int i = 0; i < _gridCols * _gridRows; ++i) {
        _altitudeGrid.append(rasterData[i]);
    }
    delete[] rasterData;

    double geoTransform[6];
    if (dataset->GetGeoTransform(geoTransform) == CE_None) {
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] GeoTransform obtained:";
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Origin (top-left): Lat =" << geoTransform[3] << " Lon =" << geoTransform[0];
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Pixel Size: Width =" << geoTransform[1] << " Height =" << geoTransform[5];
        _computeBoundsFromGeoTransform(geoTransform, _gridRows, _gridCols);
    } else {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Failed to get GeoTransform.";
    }

    GDALClose(dataset);

    emit gridDataChanged();
    _generateHeatmapImage();
}

void TerrainOverlayMapRenderer::_computeBoundsFromGeoTransform(double* gt, int rows, int cols) {
    const double originLon = gt[0];    // top-left pixel center X
    const double originLat = gt[3];    // top-left pixel center Y
    const double pixelWidth = gt[1];   // X pixel size
    const double pixelHeight = gt[5];  // Y pixel size (usually negative)

    // Adjust by -0.5 to get top-left *corner*, and +cols/rows to get full bounds
    const double minLon = originLon - pixelWidth * 0.5;
    const double maxLon = originLon + pixelWidth * (cols - 0.5);
    const double maxLat = originLat - pixelHeight * 0.5;
    const double minLat = originLat + pixelHeight * (rows - 0.5);

    _minLat = std::min(minLat, maxLat);
    _maxLat = std::max(minLat, maxLat);
    _minLon = std::min(minLon, maxLon);
    _maxLon = std::max(minLon, maxLon);

    _centerLat = (_minLat + _maxLat) / 2.0;
    _centerLon = (_minLon + _maxLon) / 2.0;

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Adjusted bounds based on pixel edges:";
    qCInfo(TerrainOverlayMapLog) << "  Lat: [" << _minLat << ", " << _maxLat << "]";
    qCInfo(TerrainOverlayMapLog) << "  Lon: [" << _minLon << ", " << _maxLon << "]";
    qCInfo(TerrainOverlayMapLog) << "  Center: " << _centerLat << "," << _centerLon;

    emit boundsChanged();
}

void TerrainOverlayMapRenderer::_generateHeatmapImage()
{
    if (_gridRows <= 0 || _gridCols <= 0 || _altitudeGrid.isEmpty()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid grid dimensions or altitude data."
                                        << "rows:" << _gridRows
                                        << "cols:" << _gridCols
                                        << "altitudeGrid empty:" << _altitudeGrid.isEmpty();
        return;
    }

    QImage rawImage(_gridCols, _gridRows, QImage::Format_ARGB32);

    double minAlt = std::numeric_limits<double>::max();
    double maxAlt = std::numeric_limits<double>::lowest();
    int nanCount = 0;

            // Compute min/max altitude (meters)
    for (const QVariant& val : _altitudeGrid) {
        const double alt = val.toDouble();
        if (std::isnan(alt)) {
            nanCount++;
            continue;
        }
        minAlt = std::min(minAlt, alt);
        maxAlt = std::max(maxAlt, alt);
    }

            // If everything was NaN, bail
    if (minAlt == std::numeric_limits<double>::max() || maxAlt == std::numeric_limits<double>::lowest()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] All altitude samples are NaN. Cannot build heatmap."
                                        << "nanCount:" << nanCount
                                        << "total:" << _altitudeGrid.size();
        return;
    }

            // Avoid division by zero / degenerate range
    if (qFuzzyCompare(minAlt, maxAlt)) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Degenerate altitude range (min==max). Forcing +1m span."
                                        << "minAlt:" << minAlt
                                        << "maxAlt:" << maxAlt;
        maxAlt = minAlt + 1.0;
    }

            // Store terrain min/max for shader decode (meters)
    const bool terrainRangeDidChange =
        !qFuzzyCompare(_terrainMinMeters + 1.0, minAlt + 1.0) ||
        !qFuzzyCompare(_terrainMaxMeters + 1.0, maxAlt + 1.0);

    _terrainMinMeters = minAlt;
    _terrainMaxMeters = maxAlt;

    if (terrainRangeDidChange) {
        emit terrainRangeChanged();
    }

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Terrain altitude range (meters):"
                                 << "min =" << _terrainMinMeters
                                 << "max =" << _terrainMaxMeters
                                 << "range =" << (_terrainMaxMeters - _terrainMinMeters)
                                 << "NaNs =" << nanCount
                                 << "total =" << _altitudeGrid.size();

    if (_altitudeGrid.size() >= 3) {
        const int idx0 = 0;
        const int idx1 = _altitudeGrid.size() / 2;
        const int idx2 = _altitudeGrid.size() - 1;

        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Sample altitudes (raw float meters):"
                                     << "a[0]=" << _altitudeGrid[idx0].toDouble()
                                     << "a[mid]=" << _altitudeGrid[idx1].toDouble()
                                     << "a[last]=" << _altitudeGrid[idx2].toDouble();
    }

            // Generate grayscale carrier image normalized across [minAlt, maxAlt]
            // IMPORTANT: shader must decode back to meters using terrainMinMeters/terrainMaxMeters.
    const double invRange = 1.0 / (maxAlt - minAlt);
    int transparentCount = 0;

    for (int row = 0; row < _gridRows; ++row) {
        QRgb* scanline = reinterpret_cast<QRgb*>(rawImage.scanLine(row));
        for (int col = 0; col < _gridCols; ++col) {
            const int idx = row * _gridCols + col;
            const double alt = _altitudeGrid[idx].toDouble();

            if (std::isnan(alt)) {
                scanline[col] = qRgba(0, 0, 0, 0);
                transparentCount++;
                continue;
            }

            const double norm = std::clamp((alt - minAlt) * invRange, 0.0, 1.0);
            const int gray = static_cast<int>(norm * 255.0 + 0.5);
            scanline[col] = qRgba(gray, gray, gray, 255);
        }
    }

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Heatmap carrier image generated (NO STRETCH):"
                                 << "size:" << rawImage.size()
                                 << "transparent pixels (NaNs):" << transparentCount;

    if (_imageProvider) {
        _imageProvider->setImage(rawImage);
    } else {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] No image provider set; cannot publish heatmap.";
        return;
    }

    _computeOverlayNativeZoomLevel(rawImage.width(), rawImage.height());

    _updateCounter++;
    emit heatmapImageChanged();
}



void TerrainOverlayMapRenderer::_computeOverlayNativeZoomLevel(int imageWidth, int imageHeight)
{
    if (imageWidth <= 0 || imageHeight <= 0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid image size for zoom level computation.";
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    // Compute real world size in meters
    QGeoCoordinate topLeft(_maxLat, _minLon);
    QGeoCoordinate topRight(_maxLat, _maxLon);
    QGeoCoordinate bottomLeft(_minLat, _minLon);

    double realWidthMeters = topLeft.distanceTo(topRight);
    double realHeightMeters = topLeft.distanceTo(bottomLeft);

    if (realWidthMeters <= 0.0 || realHeightMeters <= 0.0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid real-world size. width:" << realWidthMeters << " height:" << realHeightMeters;
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    // Calculate average meters per pixel
    double metersPerPixelX = realWidthMeters / imageWidth;
    double metersPerPixelY = realHeightMeters / imageHeight;
    double avgMetersPerPixel = (metersPerPixelX + metersPerPixelY) / 2.0;

    // Calculate zoom level using Web Mercator formula
    constexpr double earthCircumference = 40075016.686; // meters
    constexpr double tileSize = 256.0;

    double centerLat = (_minLat + _maxLat) / 2.0;
    double cosLat = std::cos(qDegreesToRadians(centerLat));
    if (cosLat <= 0.0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid center latitude:" << centerLat;
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    double zoom = std::log2((earthCircumference * cosLat) / (avgMetersPerPixel * tileSize));
    _overlayNativeZoomLevel = zoom;

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Computed native zoom level:" << zoom
                                  << "(centerLat:" << centerLat << ", avgMetersPerPixel:" << avgMetersPerPixel << ")";

    emit boundsChanged();
}
