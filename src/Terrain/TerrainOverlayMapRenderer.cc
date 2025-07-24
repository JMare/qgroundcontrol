/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "TerrainOverlayMapRenderer.h"
#include "TerrainOverlayGridManager.h"
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

void TerrainOverlayMapRenderer::_generateHeatmapImage() {
    if (_gridRows <= 0 || _gridCols <= 0 || _altitudeGrid.isEmpty()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid grid dimensions or altitude data.";
        return;
    }

    QImage rawImage(_gridCols, _gridRows, QImage::Format_ARGB32);

    double minAlt = std::numeric_limits<double>::max();
    double maxAlt = std::numeric_limits<double>::lowest();

    for (const QVariant& val : _altitudeGrid) {
        double alt = val.toDouble();
        if (!std::isnan(alt)) {
            minAlt = std::min(minAlt, alt);
            maxAlt = std::max(maxAlt, alt);
        }
    }

    if (minAlt == maxAlt) {
        maxAlt += 1.0; // avoid division by zero
    }

    // 🔲 Generate grayscale image (r = g = b)
    for (int row = 0; row < _gridRows; ++row) {
        for (int col = 0; col < _gridCols; ++col) {
            int idx = row * _gridCols + col;
            double alt = _altitudeGrid[idx].toDouble();

            QColor color = Qt::transparent;
            if (!std::isnan(alt)) {
                double norm = std::clamp((alt - minAlt) / (maxAlt - minAlt), 0.0, 1.0);
                int gray = static_cast<int>(norm * 255.0);
                color = QColor(gray, gray, gray, 255);  // full opacity
            }

            rawImage.setPixelColor(col, row, color);
        }
    }

    // 2. Stretch image horizontally to match real-world aspect
    double latSpan = _maxLat - _minLat;
    double lonSpan = _maxLon - _minLon;
    double cosLat = std::cos(qDegreesToRadians(_centerLat));

    double aspectCorrection = lonSpan * cosLat / latSpan;
    int stretchedWidth = std::round(_gridRows * aspectCorrection); // keep height fixed

    QImage stretchedImage(stretchedWidth, _gridRows, QImage::Format_ARGB32);
    stretchedImage.fill(Qt::transparent);

    QPainter p(&stretchedImage);
    p.setRenderHint(QPainter::SmoothPixmapTransform, true);
    p.drawImage(QRect(0, 0, stretchedWidth, _gridRows), rawImage);
    p.end();

    if (_imageProvider) {
        _imageProvider->setImage(stretchedImage);
    }

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Final grayscale heatmap stretched:"
                                  << stretchedImage.size()
                                  << " (aspectCorrection:" << aspectCorrection << ")";

    _computeOverlayNativeZoomLevel(stretchedImage.width(), stretchedImage.height());

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
