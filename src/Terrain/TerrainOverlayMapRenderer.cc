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
#include "QGCLoggingCategory.h"

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
    _connectToManager();
    setImageProvider(HeatmapImageProvider::instance());
}

void TerrainOverlayMapRenderer::setImageProvider(HeatmapImageProvider* provider)
{
    _imageProvider = provider;
}

void TerrainOverlayMapRenderer::_connectToManager()
{
    auto* manager = TerrainOverlayGridManager::instance();

    connect(manager, &TerrainOverlayGridManager::gridChanged,
            this, &TerrainOverlayMapRenderer::_onGridChanged);

    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Connected to TerrainOverlayGridManager signals.";
}

void TerrainOverlayMapRenderer::_onGridChanged()
{
    auto* manager = TerrainOverlayGridManager::instance();
    QVariantMap grid = manager->grid().toMap();

    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Received new grid.";

    _computeBounds(grid);

    // NEW: Store altitude grid
    _altitudeGrid = grid.value("altitudes").toList();

    _gridRows = grid.value("rows").toInt();
    _gridCols = grid.value("cols").toInt();

    _altitudeGrid = grid.value("altitudes").toList();
    emit gridDataChanged();

    _generateHeatmapImage(grid);
}

void TerrainOverlayMapRenderer::_computeBounds(const QVariantMap& grid)
{
    double centerLat = grid.value("centerLat").toDouble();
    double centerLon = grid.value("centerLon").toDouble();
    double spacingMeters = grid.value("spacingMeters").toDouble();
    int rows = grid.value("rows").toInt();
    int cols = grid.value("cols").toInt();

    if (rows <= 0 || cols <= 0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid grid dimensions for bounds.";
        return;
    }

    double latSpacing = spacingMeters / 111320.0;
    double centerLatRad = qDegreesToRadians(centerLat);
    double metersPerDegLon = 111320.0 * std::cos(centerLatRad);
    double lonSpacing = spacingMeters / metersPerDegLon;

    int centerRow = rows / 2;
    int centerCol = cols / 2;

    _minLat = centerLat - centerRow * latSpacing;
    _maxLat = centerLat + (rows - centerRow - 1) * latSpacing;
    _minLon = centerLon - centerCol * lonSpacing;
    _maxLon = centerLon + (cols - centerCol - 1) * lonSpacing;

    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Bounds computed:"
                                   << "Lat [" << _minLat << "," << _maxLat << "]"
                                   << "Lon [" << _minLon << "," << _maxLon << "]";

    emit boundsChanged();
}

void TerrainOverlayMapRenderer::_generateHeatmapImage(const QVariantMap& grid)
{
    int rows = grid.value("rows").toInt();
    int cols = grid.value("cols").toInt();
    QVariantList altitudes = grid.value("altitudes").toList();

    if (rows <= 0 || cols <= 0 || altitudes.isEmpty()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid grid dimensions or data.";
        return;
    }

    // Find min/max altitude
    double minAlt = std::numeric_limits<double>::max();
    double maxAlt = std::numeric_limits<double>::lowest();

    for (const QVariant& val : altitudes) {
        double alt = val.toDouble();
        if (!std::isnan(alt)) {
            minAlt = std::min(minAlt, alt);
            maxAlt = std::max(maxAlt, alt);
        }
    }
    // Create image
    QImage image(cols, rows, QImage::Format_ARGB32);

    for (int row = 0; row < rows; ++row) {
        for (int col = 0; col < cols; ++col) {
            int index = row * cols + col;
            double alt = altitudes.value(index).toDouble();

            QColor color = Qt::transparent;
            if (!std::isnan(alt)) {
                constexpr double greenAlt = 200.0;
                constexpr double redAlt = 400.0;
                double norm = (alt - greenAlt) / (redAlt - greenAlt);
                norm = std::clamp(norm, 0.0, 1.0);

                int r = int(255 * norm);
                int g = int(255 * (1.0 - norm));
                int b = 0;

                color = QColor(r, g, b, 200);
            }
            image.setPixelColor(col, rows - 1 - row, color);
        }
    }

    if (_imageProvider) {
        _imageProvider->setImage(image);
        qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Image pushed to provider.";
    }

    // Compute overlayNativeZoomLevel
    _computeOverlayNativeZoomLevel(image.width(), image.height());

    _updateCounter++;
    emit heatmapImageChanged();
}


void TerrainOverlayMapRenderer::_computeOverlayNativeZoomLevel(int imageWidth, int imageHeight)
{
    if (imageWidth <= 0 || imageHeight <= 0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Cannot compute native zoom level: invalid image size.";
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    QGeoCoordinate topLeft(_maxLat, _minLon);
    QGeoCoordinate topRight(_maxLat, _maxLon);
    QGeoCoordinate bottomLeft(_minLat, _minLon);

    double realWidthMeters = topLeft.distanceTo(topRight);
    double realHeightMeters = topLeft.distanceTo(bottomLeft);

    if (realWidthMeters <= 0 || realHeightMeters <= 0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Cannot compute native zoom level: invalid bounds.";
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    double metersPerPixelX = realWidthMeters / imageWidth;
    double metersPerPixelY = realHeightMeters / imageHeight;
    double avgMetersPerPixel = (metersPerPixelX + metersPerPixelY) / 2.0;

    constexpr double earthCircumference = 40075016.686;
    constexpr double tileSize = 256.0;

    double centerLat = (_minLat + _maxLat) / 2.0;
    double cosLat = std::cos(qDegreesToRadians(centerLat));

    if (cosLat <= 0.0) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Cannot compute native zoom level: invalid latitude.";
        _overlayNativeZoomLevel = 0.0;
        return;
    }

    _overlayNativeZoomLevel = std::log2((earthCircumference * cosLat) / (avgMetersPerPixel * tileSize));
    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Computed overlayNativeZoomLevel:" << _overlayNativeZoomLevel;

    emit boundsChanged();
}
