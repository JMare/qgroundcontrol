/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "TerrainOverlayMapRenderer.h"

#include "HeatmapImageProvider.h"
#include "TerrainTileManager.h"
#include "TerrainTile.h"
#include "QGCLoggingCategory.h"

#include "SettingsManager.h"
#include "FlightMapSettings.h"
#include "ElevationMapProvider.h"
#include "QGCMapUrlEngine.h"

#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtCore/qapplicationstatic.h>
#include <QTimer>
#include <QImage>
#include <QtMath>

#include <algorithm>
#include <limits>

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

QString TerrainOverlayMapRenderer::stateName() const
{
    switch (_state) {
        case State::Idle:              return QStringLiteral("Idle");
        case State::WaitingForHome:    return QStringLiteral("WaitingForHome");
        case State::BuildingFromCache: return QStringLiteral("BuildingFromCache");
        case State::Prefetching:       return QStringLiteral("Prefetching");
        case State::StabilizedPartial: return QStringLiteral("StabilizedPartial");
        case State::Ready:             return QStringLiteral("Ready");
        case State::Failed:            return QStringLiteral("Failed");
    }
    return QStringLiteral("Unknown");
}

TerrainOverlayMapRenderer::TerrainOverlayMapRenderer(QObject* parent)
    : QObject(parent)
{
    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Initializing singleton (terrain overlay).";
    setImageProvider(HeatmapImageProvider::instance());

    connect(TerrainTileManager::instance(), &TerrainTileManager::tileCached,
            this, &TerrainOverlayMapRenderer::_onTileCached);

    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
            this, &TerrainOverlayMapRenderer::_activeVehicleChanged);

    QTimer::singleShot(0, this, [this]() {
        _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
    });

    _setState(State::Idle);
}

void TerrainOverlayMapRenderer::setImageProvider(HeatmapImageProvider* provider)
{
    _imageProvider = provider;
}

void TerrainOverlayMapRenderer::_setState(State s)
{
    if (_state == s) return;
    _state = s;
    emit stateChanged();
}

void TerrainOverlayMapRenderer::clear()
{
    _altitudeGrid.clear();
    _gridRows = 0;
    _gridCols = 0;

    _terrainMinMeters = 0.0;
    _terrainMaxMeters = 0.0;
    emit terrainRangeChanged();

    _overlayNativeZoomLevel = 0.0;
    emit boundsChanged();

    _updateCounter++;
    emit gridDataChanged();
    emit heatmapImageChanged();

    if (_imageProvider) {
        _imageProvider->setImage(QImage());
    }

    _carpetRequestInFlight = false;
    _lastCarpetKey.clear();

    _retryCount = 0;
    _lastNanCountInternal = -1;
    _stableNanCountHits = 0;
    _retryScheduled = false;

    _prefetchActive = false;
    _prefetchProbeQueue.clear();

    _homeRequestQueued = false;
    _homeRequestCompleted = false;
    _lastHomeUsed = QGeoCoordinate();

    _nanCount = 0;
    _nanRatio = 1.0;
    _progress = 0.0;
    emit progressChanged();

    _setState(State::Idle);

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Cleared overlay.";
}

void TerrainOverlayMapRenderer::_activeVehicleChanged(Vehicle* vehicle)
{
    if (_homeConn) {
        disconnect(_homeConn);
        _homeConn = QMetaObject::Connection();
    }

    _activeVehicle = vehicle;

    _homeRequestQueued = false;
    _homeRequestCompleted = false;
    _lastHomeUsed = QGeoCoordinate();

    if (!vehicle) {
        _setState(State::Idle);
        return;
    }

    if (vehicle->homePosition().isValid()) {
        _queueHomeCenteredRequest(vehicle->homePosition());
        return;
    }

    _setState(State::WaitingForHome);

    _homeConn = connect(vehicle, &Vehicle::homePositionChanged,
                        this, &TerrainOverlayMapRenderer::_vehicleHomePositionChanged);
}

void TerrainOverlayMapRenderer::_vehicleHomePositionChanged(const QGeoCoordinate& home)
{
    if (!home.isValid()) return;

    if (_homeConn) {
        disconnect(_homeConn);
        _homeConn = QMetaObject::Connection();
    }

    _queueHomeCenteredRequest(home);
}

void TerrainOverlayMapRenderer::_queueHomeCenteredRequest(const QGeoCoordinate& home)
{
    if (!home.isValid()) return;

    if (_homeRequestCompleted) {
        const double movedM = _lastHomeUsed.isValid() ? _lastHomeUsed.distanceTo(home) : 1e9;
        if (movedM < 25.0) return;
        _homeRequestCompleted = false;
    }

    if (_homeRequestQueued) return;
    _homeRequestQueued = true;

    QTimer::singleShot(0, this, [this, home]() {
        _homeRequestQueued = false;
        _lastHomeUsed = home;

        constexpr double kRadiusMeters = 2000.0; // tune
        _requestAroundCoordinateMeters(home, kRadiusMeters);

        _homeRequestCompleted = true;
    });
}

void TerrainOverlayMapRenderer::_requestAroundCoordinateMeters(const QGeoCoordinate& center, double radiusMeters)
{
    if (!center.isValid() || radiusMeters <= 0.0) return;

    const double latRad = qDegreesToRadians(center.latitude());
    const double metersPerDegLat = 111320.0;
    const double metersPerDegLon = std::max(1.0, 111320.0 * std::cos(latRad));

    const double dLat = radiusMeters / metersPerDegLat;
    const double dLon = radiusMeters / metersPerDegLon;

    const double minLat = center.latitude()  - dLat;
    const double maxLat = center.latitude()  + dLat;
    const double minLon = center.longitude() - dLon;
    const double maxLon = center.longitude() + dLon;

    requestCarpet(minLat, maxLat, minLon, maxLon, false);
}

void TerrainOverlayMapRenderer::_setBounds(double minLat, double maxLat, double minLon, double maxLon)
{
    _minLat = std::min(minLat, maxLat);
    _maxLat = std::max(minLat, maxLat);
    _minLon = std::min(minLon, maxLon);
    _maxLon = std::max(minLon, maxLon);
    _centerLat = (_minLat + _maxLat) * 0.5;
    _centerLon = (_minLon + _maxLon) * 0.5;

    emit boundsChanged();
}

QString TerrainOverlayMapRenderer::_makeBoundsKey(double minLat, double maxLat, double minLon, double maxLon)
{
    const double aMinLat = std::min(minLat, maxLat);
    const double aMaxLat = std::max(minLat, maxLat);
    const double aMinLon = std::min(minLon, maxLon);
    const double aMaxLon = std::max(minLon, maxLon);

    return QString::number(aMinLat, 'f', 6) + "," +
           QString::number(aMaxLat, 'f', 6) + "," +
           QString::number(aMinLon, 'f', 6) + "," +
           QString::number(aMaxLon, 'f', 6);
}

void TerrainOverlayMapRenderer::_updateProgress(int nanCount, int total)
{
    if (total <= 0) {
        _nanCount = nanCount;
        _nanRatio = 1.0;
        _progress = 0.0;
        emit progressChanged();
        return;
    }

    const int clampedNan = std::max(0, std::min(nanCount, total));
    const double ratio = static_cast<double>(clampedNan) / static_cast<double>(total);
    const double prog = 1.0 - ratio;

    bool changed = false;
    if (_nanCount != clampedNan) { _nanCount = clampedNan; changed = true; }
    if (!qFuzzyCompare(_nanRatio + 1.0, ratio + 1.0)) { _nanRatio = ratio; changed = true; }
    if (!qFuzzyCompare(_progress + 1.0, prog + 1.0)) { _progress = prog; changed = true; }

    if (changed) emit progressChanged();
}

void TerrainOverlayMapRenderer::requestCarpet(double minLat, double maxLat, double minLon, double maxLon, bool statsOnly)
{
    Q_UNUSED(statsOnly);

    if (minLat == maxLat || minLon == maxLon) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] requestCarpet degenerate bounds.";
        _setState(State::Failed);
        return;
    }

    const QString key = _makeBoundsKey(minLat, maxLat, minLon, maxLon);

    if (_carpetRequestInFlight && key == _lastCarpetKey) {
        return;
    }

    _carpetRequestInFlight = true;
    _lastCarpetKey = key;

    _retryCount = 0;
    _lastNanCountInternal = -1;
    _stableNanCountHits = 0;

    _setBounds(minLat, maxLat, minLon, maxLon);

    _setState(State::BuildingFromCache);
    _tryBuildFromCacheOrPrefetch(false);

    _carpetRequestInFlight = false;
}

void TerrainOverlayMapRenderer::_evaluateCompletionHeuristics(bool needsDownload)
{
    if (!needsDownload) {
        _setState(State::Ready);
        return;
    }

            // still needs tiles
    if (_stableNanCountHits >= 3) {
        _setState(State::StabilizedPartial);
        return;
    }
    if (_retryCount >= _maxRetries) {
        _setState(State::StabilizedPartial);
        return;
    }

    _setState(State::Prefetching);
}

void TerrainOverlayMapRenderer::_tryBuildFromCacheOrPrefetch(bool statsOnly)
{
    Q_UNUSED(statsOnly);

    int rows = 0, cols = 0;
    double cellLat = 0.0, cellLon = 0.0;
    double minH = 0.0, maxH = 0.0;
    QList<float> grid;
    bool needsDownload = false;

    const bool ok = TerrainTileManager::instance()->getCarpetForBoundsFromCache(
        _minLat, _maxLat, _minLon, _maxLon,
        rows, cols,
        cellLat, cellLon,
        minH, maxH,
        grid,
        needsDownload
        );

    if (!ok) {
        _setState(State::Prefetching);
        _startPrefetchForCurrentBounds();
        _evaluateCompletionHeuristics(true);
        return;
    }

    _gridRows = rows;
    _gridCols = cols;

    _altitudeGrid.clear();
    _altitudeGrid.reserve(_gridRows * _gridCols);

    int nanCount = 0;
    for (float v : grid) {
        if (std::isnan(v)) nanCount++;
        _altitudeGrid.append(static_cast<double>(v));
    }

    const int total = _gridRows * _gridCols;
    _updateProgress(nanCount, total);

    const bool rangeChanged =
        !qFuzzyCompare(_terrainMinMeters + 1.0, minH + 1.0) ||
        !qFuzzyCompare(_terrainMaxMeters + 1.0, maxH + 1.0);

    _terrainMinMeters = minH;
    _terrainMaxMeters = maxH;
    if (rangeChanged) emit terrainRangeChanged();

    if (_lastNanCountInternal == nanCount) {
        _stableNanCountHits++;
    } else {
        _stableNanCountHits = 0;
    }
    _lastNanCountInternal = nanCount;

    emit gridDataChanged();
    _generateHeatmapImage();

    if (needsDownload) {
        if (_stableNanCountHits < 3 && _retryCount < _maxRetries) {
            _retryCount++;
            _setState(State::Prefetching);
            _startPrefetchForCurrentBounds();
        }
    }

    _evaluateCompletionHeuristics(needsDownload);
}

void TerrainOverlayMapRenderer::_onTileCached(const QString& /*hash*/)
{
    if (_prefetchActive) {
        _kickPrefetch();
    }
    _scheduleRetryDebounced();
}

void TerrainOverlayMapRenderer::_scheduleRetryDebounced()
{
    if (_retryScheduled) return;
    if (_lastCarpetKey.isEmpty()) return;

    _retryScheduled = true;
    QTimer::singleShot(60, this, [this]() {
        _retryScheduled = false;
        if (_maxLat == _minLat || _maxLon == _minLon) return;
        _setState(State::BuildingFromCache);
        _tryBuildFromCacheOrPrefetch(false);
    });
}

void TerrainOverlayMapRenderer::_startPrefetchForCurrentBounds()
{
    const QString elevationProviderName =
        SettingsManager::instance()->flightMapSettings()->elevationMapProvider()->rawValue().toString();
    const SharedMapProvider provider = UrlFactory::getMapProviderFromProviderType(elevationProviderName);

    if (!provider) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] No elevation provider configured.";
        _setState(State::Failed);
        return;
    }

    const int z = 1;
    const int x0 = provider->long2tileX(_minLon, z);
    const int x1 = provider->long2tileX(_maxLon, z);
    const int y0 = provider->lat2tileY(_minLat, z);
    const int y1 = provider->lat2tileY(_maxLat, z);

    const int xMin = std::min(x0, x1);
    const int xMax = std::max(x0, x1);
    const int yMin = std::min(y0, y1);
    const int yMax = std::max(y0, y1);

    if (_prefetchActive && !_prefetchProbeQueue.isEmpty()) {
        return;
    }

    _prefetchProbeQueue.clear();

    for (int y = yMin; y <= yMax; ++y) {
        for (int x = xMin; x <= xMax; ++x) {
            constexpr double tileSize = 0.01;
            const double swLat = (static_cast<double>(y) * tileSize) - 90.0;
            const double swLon = (static_cast<double>(x) * tileSize) - 180.0;
            const double neLat = (static_cast<double>(y + 1) * tileSize) - 90.0;
            const double neLon = (static_cast<double>(x + 1) * tileSize) - 180.0;

            const double cLat = (swLat + neLat) * 0.5;
            const double cLon = (swLon + neLon) * 0.5;

            _prefetchProbeQueue.enqueue(QGeoCoordinate(cLat, cLon));
        }
    }

    _prefetchActive = true;
    _setState(State::Prefetching);
    _kickPrefetch();
}

void TerrainOverlayMapRenderer::_kickPrefetch()
{
    if (!_prefetchActive) return;

    while (!_prefetchProbeQueue.isEmpty()) {
        const QGeoCoordinate c = _prefetchProbeQueue.dequeue();

        bool error = false;
        QList<double> alts;
        const QList<QGeoCoordinate> one{c};

        const bool gotNow = TerrainTileManager::instance()->getAltitudesForCoordinates(one, alts, error);

        if (error) {
            continue;
        }

        if (!gotNow) {
            return; // download queued
        }
    }

    _prefetchActive = false;
}

void TerrainOverlayMapRenderer::_generateHeatmapImage()
{
    if (_gridRows <= 0 || _gridCols <= 0 || _altitudeGrid.isEmpty()) return;
    if (!_imageProvider) return;

    double minAlt = _terrainMinMeters;
    double maxAlt = _terrainMaxMeters;

    const bool rangeLooksValid = std::isfinite(minAlt) && std::isfinite(maxAlt) && (maxAlt > minAlt);
    if (!rangeLooksValid) {
        minAlt = std::numeric_limits<double>::max();
        maxAlt = std::numeric_limits<double>::lowest();
        for (const QVariant& v : _altitudeGrid) {
            const double a = v.toDouble();
            if (std::isnan(a)) continue;
            minAlt = std::min(minAlt, a);
            maxAlt = std::max(maxAlt, a);
        }
        if (minAlt == std::numeric_limits<double>::max() || maxAlt == std::numeric_limits<double>::lowest()) return;
        if (qFuzzyCompare(minAlt, maxAlt)) maxAlt = minAlt + 1.0;
        _terrainMinMeters = minAlt;
        _terrainMaxMeters = maxAlt;
        emit terrainRangeChanged();
    }

    const double invRange = 1.0 / (maxAlt - minAlt);

    QImage rawImage(_gridCols, _gridRows, QImage::Format_ARGB32);
    for (int row = 0; row < _gridRows; ++row) {
        QRgb* scanline = reinterpret_cast<QRgb*>(rawImage.scanLine(row));
        for (int col = 0; col < _gridCols; ++col) {
            const int idx = row * _gridCols + col;
            const double alt = _altitudeGrid[idx].toDouble();

            if (std::isnan(alt)) {
                scanline[col] = qRgba(0, 0, 0, 0);
                continue;
            }

            const double norm = std::clamp((alt - minAlt) * invRange, 0.0, 1.0);
            const uint32_t q16 = static_cast<uint32_t>(std::lround(norm * 65535.0));
            const int r = static_cast<int>(q16 & 0xFF);
            const int g = static_cast<int>((q16 >> 8) & 0xFF);
            scanline[col] = qRgba(r, g, 0, 255);
        }
    }

    _imageProvider->setImage(rawImage);
    _computeOverlayNativeZoomLevel(rawImage.width(), rawImage.height());

    _updateCounter++;
    emit heatmapImageChanged();
}

void TerrainOverlayMapRenderer::_computeOverlayNativeZoomLevel(int imageWidth, int imageHeight)
{
    if (imageWidth <= 0 || imageHeight <= 0) {
        _overlayNativeZoomLevel = 0.0;
        emit boundsChanged();
        return;
    }

    QGeoCoordinate topLeft(_maxLat, _minLon);
    QGeoCoordinate topRight(_maxLat, _maxLon);
    QGeoCoordinate bottomLeft(_minLat, _minLon);

    const double realWidthMeters = topLeft.distanceTo(topRight);
    const double realHeightMeters = topLeft.distanceTo(bottomLeft);

    if (realWidthMeters <= 0.0 || realHeightMeters <= 0.0) {
        _overlayNativeZoomLevel = 0.0;
        emit boundsChanged();
        return;
    }

    const double metersPerPixelX = realWidthMeters / imageWidth;
    const double metersPerPixelY = realHeightMeters / imageHeight;
    const double avgMetersPerPixel = (metersPerPixelX + metersPerPixelY) * 0.5;

    constexpr double earthCircumference = 40075016.686;
    constexpr double tileSize = 256.0;

    const double cosLat = std::cos(qDegreesToRadians(_centerLat));
    if (cosLat <= 0.0) {
        _overlayNativeZoomLevel = 0.0;
        emit boundsChanged();
        return;
    }

    _overlayNativeZoomLevel = std::log2((earthCircumference * cosLat) / (avgMetersPerPixel * tileSize));
    emit boundsChanged();
}
