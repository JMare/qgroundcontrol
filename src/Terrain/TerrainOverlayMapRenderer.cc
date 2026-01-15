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

TerrainOverlayMapRenderer::TerrainOverlayMapRenderer(QObject* parent)
    : QObject(parent)
{
    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Initializing singleton (terrain overlay).";
    setImageProvider(HeatmapImageProvider::instance());

            // Tile arrivals drive retries/prefetch completion (no recursive requestCarpet loops)
    connect(TerrainTileManager::instance(), &TerrainTileManager::tileCached,
            this, &TerrainOverlayMapRenderer::_onTileCached);

            // Active vehicle -> watch home position
    connect(MultiVehicleManager::instance(), &MultiVehicleManager::activeVehicleChanged,
            this, &TerrainOverlayMapRenderer::_activeVehicleChanged);

            // If there is already an active vehicle at startup, handle it (next tick)
    QTimer::singleShot(0, this, [this]() {
        _activeVehicleChanged(MultiVehicleManager::instance()->activeVehicle());
    });
}

void TerrainOverlayMapRenderer::setImageProvider(HeatmapImageProvider* provider)
{
    _imageProvider = provider;
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

            // Reset request/prefetch state
    _carpetRequestInFlight = false;
    _lastCarpetKey.clear();

    _retryCount = 0;
    _lastNanCount = -1;
    _stableNanCountHits = 0;
    _retryScheduled = false;

    _prefetchActive = false;
    _prefetchProbeQueue.clear();

            // Home load state
    _homeRequestQueued = false;
    _homeRequestCompleted = false;
    _lastHomeUsed = QGeoCoordinate();

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Cleared overlay.";
}

void TerrainOverlayMapRenderer::_activeVehicleChanged(Vehicle* vehicle)
{
    if (_homeConn) {
        disconnect(_homeConn);
        _homeConn = QMetaObject::Connection();
    }

    _activeVehicle = vehicle;

            // Reset home request state when switching vehicles
    _homeRequestQueued = false;
    _homeRequestCompleted = false;
    _lastHomeUsed = QGeoCoordinate();

    if (!vehicle) {
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] activeVehicleChanged -> null";
        return;
    }

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] activeVehicleChanged ->" << vehicle;

            // If home is already valid, request immediately (debounced)
    if (vehicle->homePosition().isValid()) {
        _queueHomeCenteredRequest(vehicle->homePosition());
        return;
    }

            // Otherwise, wait for homePositionChanged
    _homeConn = connect(vehicle, &Vehicle::homePositionChanged,
                        this, &TerrainOverlayMapRenderer::_vehicleHomePositionChanged);
}

void TerrainOverlayMapRenderer::_vehicleHomePositionChanged(const QGeoCoordinate& home)
{
    if (!home.isValid()) {
        return;
    }

            // If we got home once, we can stop listening (home typically stabilizes after first valid)
    if (_homeConn) {
        disconnect(_homeConn);
        _homeConn = QMetaObject::Connection();
    }

    _queueHomeCenteredRequest(home);
}

void TerrainOverlayMapRenderer::_queueHomeCenteredRequest(const QGeoCoordinate& home)
{
    if (!home.isValid()) return;

            // Avoid repeated triggers if home bounces
    if (_homeRequestCompleted) {
        // If home moved a lot, allow re-request (optional)
        const double movedM = _lastHomeUsed.isValid() ? _lastHomeUsed.distanceTo(home) : 1e9;
        if (movedM < 25.0) { // tiny jitter
            return;
        }
        // If it moved meaningfully, allow a fresh request
        _homeRequestCompleted = false;
    }

    if (_homeRequestQueued) {
        return;
    }

    _homeRequestQueued = true;

            // Debounce into the event loop (and avoid doing work inline during vehicle signal)
    QTimer::singleShot(0, this, [this, home]() {
        _homeRequestQueued = false;
        _lastHomeUsed = home;

                // Pick a default radius. Tune as you like.
        constexpr double kRadiusMeters = 4000.0; // ~4 km box around home
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Home set -> requesting terrain around home:"
                                     << home << "radius(m)=" << kRadiusMeters;

        _requestAroundCoordinateMeters(home, kRadiusMeters);

        _homeRequestCompleted = true;
    });
}

void TerrainOverlayMapRenderer::_requestAroundCoordinateMeters(const QGeoCoordinate& center, double radiusMeters)
{
    if (!center.isValid() || radiusMeters <= 0.0) return;

            // Convert meters to degrees (approx)
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

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Bounds set:"
                                 << "Lat[" << _minLat << "," << _maxLat << "]"
                                 << "Lon[" << _minLon << "," << _maxLon << "]"
                                 << "Center" << _centerLat << _centerLon;
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

void TerrainOverlayMapRenderer::requestCarpet(double minLat, double maxLat, double minLon, double maxLon, bool statsOnly)
{
    if (minLat == maxLat || minLon == maxLon) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] requestCarpet degenerate bounds."
                                        << "minLat/maxLat/minLon/maxLon" << minLat << maxLat << minLon << maxLon;
        return;
    }

    const QString key = _makeBoundsKey(minLat, maxLat, minLon, maxLon);

            // Dedupe exact same request while in-flight
    if (_carpetRequestInFlight && key == _lastCarpetKey) {
        qCDebug(TerrainOverlayMapLog) << "[MapRenderer] requestCarpet duplicate/in-flight ignored:" << key;
        return;
    }

    _carpetRequestInFlight = true;
    _lastCarpetKey = key;

            // New request resets retry heuristics
    _retryCount = 0;
    _lastNanCount = -1;
    _stableNanCountHits = 0;

    _setBounds(minLat, maxLat, minLon, maxLon);

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Requesting terrain carpet (cache-first): key=" << key
                                 << "Lat[" << _minLat << "," << _maxLat << "]"
                                 << "Lon[" << _minLon << "," << _maxLon << "]";

            // Cache-first pipeline
    _tryBuildFromCacheOrPrefetch(statsOnly);

            // Mark not “in-flight” from UI standpoint after cache-first attempt.
            // Future updates are tile-driven via tileCached.
    _carpetRequestInFlight = false;
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
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Cache miss for carpet; starting prefetch: key=" << _lastCarpetKey;
        _startPrefetchForCurrentBounds();
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

    const bool rangeChanged =
        !qFuzzyCompare(_terrainMinMeters + 1.0, minH + 1.0) ||
        !qFuzzyCompare(_terrainMaxMeters + 1.0, maxH + 1.0);

    _terrainMinMeters = minH;
    _terrainMaxMeters = maxH;
    if (rangeChanged) emit terrainRangeChanged();

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Carpet grid ready from cache:"
                                 << _gridCols << "x" << _gridRows
                                 << "min=" << _terrainMinMeters
                                 << "max=" << _terrainMaxMeters
                                 << "NaNs=" << nanCount
                                 << "needsDownload=" << needsDownload;

            // Stop infinite retries if NaNs aren’t improving.
    if (_lastNanCount == nanCount) {
        _stableNanCountHits++;
    } else {
        _stableNanCountHits = 0;
    }
    _lastNanCount = nanCount;

            // Publish what we have (even if partial)
    emit gridDataChanged();
    _generateHeatmapImage();
    _debugCarpetSummary_5pt();

    if (needsDownload) {
        if (_stableNanCountHits >= 3) {
            qCInfo(TerrainOverlayMapLog) << "[MapRenderer] NaNs not improving; stopping retries."
                                         << "nanCount=" << nanCount << "stableHits=" << _stableNanCountHits;
            return;
        }
        if (_retryCount >= _maxRetries) {
            qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Retry cap reached; stopping."
                                         << "retryCount=" << _retryCount << "nanCount=" << nanCount;
            return;
        }

        _retryCount++;
        qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Carpet partial coverage; prefetching more tiles:"
                                     << "retry=" << _retryCount << "/" << _maxRetries
                                     << "key=" << _lastCarpetKey;
        _startPrefetchForCurrentBounds();
    }
}

void TerrainOverlayMapRenderer::_onTileCached(const QString& /*hash*/)
{
    // If we’re prefetching, continue draining the queue.
    if (_prefetchActive) {
        _kickPrefetch();
    }

            // Debounced retry (prevents “spam new heatmap ready”)
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

        qCDebug(TerrainOverlayMapLog) << "[MapRenderer] tileCached -> retry cache-first build: key=" << _lastCarpetKey;
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

            // If already prefetching with a non-empty queue, don’t rebuild endlessly.
    if (_prefetchActive && !_prefetchProbeQueue.isEmpty()) {
        return;
    }

    _prefetchProbeQueue.clear();

            // Probe tile centers in stable order (row-major)
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

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Prefetch start:"
                                 << "tiles=" << _prefetchProbeQueue.size()
                                 << "x[" << xMin << "," << xMax << "]"
                                 << "y[" << yMin << "," << yMax << "]";

    _prefetchActive = true;
    _kickPrefetch();
}

void TerrainOverlayMapRenderer::_kickPrefetch()
{
    if (!_prefetchActive) return;

            // Drain any coords that are already cached; stop when we schedule a download.
    while (!_prefetchProbeQueue.isEmpty()) {
        const QGeoCoordinate c = _prefetchProbeQueue.dequeue();

        bool error = false;
        QList<double> alts;
        const QList<QGeoCoordinate> one{c};

        const bool gotNow = TerrainTileManager::instance()->getAltitudesForCoordinates(one, alts, error);

        if (error) {
            qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Prefetch probe error at" << c;
            continue;
        }

        if (!gotNow) {
            // Download was queued; continue when tileCached fires.
            return;
        }
        // gotNow==true => already cached, keep draining
    }

    _prefetchActive = false;
    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Prefetch queue drained.";
}

void TerrainOverlayMapRenderer::_generateHeatmapImage()
{
    if (_gridRows <= 0 || _gridCols <= 0 || _altitudeGrid.isEmpty()) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] Invalid grid for heatmap generation."
                                        << "rows:" << _gridRows
                                        << "cols:" << _gridCols
                                        << "empty:" << _altitudeGrid.isEmpty();
        return;
    }
    if (!_imageProvider) {
        qCWarning(TerrainOverlayMapLog) << "[MapRenderer] No image provider set; cannot publish heatmap.";
        return;
    }

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
        if (minAlt == std::numeric_limits<double>::max() || maxAlt == std::numeric_limits<double>::lowest()) {
            qCWarning(TerrainOverlayMapLog) << "[MapRenderer] All samples NaN; cannot build heatmap.";
            return;
        }
        if (qFuzzyCompare(minAlt, maxAlt)) maxAlt = minAlt + 1.0;
        _terrainMinMeters = minAlt;
        _terrainMaxMeters = maxAlt;
        emit terrainRangeChanged();
    }

    const double invRange = 1.0 / (maxAlt - minAlt);

    QImage rawImage(_gridCols, _gridRows, QImage::Format_ARGB32);
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
            const uint32_t q16 = static_cast<uint32_t>(std::lround(norm * 65535.0));
            const int r = static_cast<int>(q16 & 0xFF);
            const int g = static_cast<int>((q16 >> 8) & 0xFF);
            scanline[col] = qRgba(r, g, 0, 255);
        }
    }

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] Heatmap carrier generated:"
                                 << "size=" << rawImage.size()
                                 << "transparent(NaN)=" << transparentCount
                                 << "range(m)=" << minAlt << "to" << maxAlt;

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

    qCInfo(TerrainOverlayMapLog) << "[MapRenderer] overlayNativeZoomLevel="
                                 << _overlayNativeZoomLevel
                                 << "avgMetersPerPixel=" << avgMetersPerPixel
                                 << "centerLat=" << _centerLat;

    emit boundsChanged();
}

int TerrainOverlayMapRenderer::_clampInt(int v, int lo, int hi)
{
    return std::max(lo, std::min(v, hi));
}

double TerrainOverlayMapRenderer::_carpetValueAt(int row, int col) const
{
    if (_gridRows <= 0 || _gridCols <= 0) return qQNaN();
    row = _clampInt(row, 0, _gridRows - 1);
    col = _clampInt(col, 0, _gridCols - 1);
    const int idx = row * _gridCols + col;
    if (idx < 0 || idx >= _altitudeGrid.size()) return qQNaN();
    return _altitudeGrid[idx].toDouble();
}

void TerrainOverlayMapRenderer::_debugCarpetSummary_5pt() const
{
    if (_gridRows <= 0 || _gridCols <= 0 || _altitudeGrid.isEmpty()) {
        qCWarning(TerrainOverlayMapLog) << "[Probe] No carpet grid to summarize.";
        return;
    }

    double minAlt = std::numeric_limits<double>::max();
    double maxAlt = std::numeric_limits<double>::lowest();
    int nanCount = 0;

    for (const QVariant& v : _altitudeGrid) {
        const double a = v.toDouble();
        if (std::isnan(a)) { nanCount++; continue; }
        minAlt = std::min(minAlt, a);
        maxAlt = std::max(maxAlt, a);
    }

    if (minAlt == std::numeric_limits<double>::max() ||
        maxAlt == std::numeric_limits<double>::lowest()) {
        qCWarning(TerrainOverlayMapLog) << "[Probe] All values NaN.";
        return;
    }

    const int rTop = 0;
    const int rBot = _gridRows - 1;
    const int cLeft = 0;
    const int cRight = _gridCols - 1;
    const int rMid = _gridRows / 2;
    const int cMid = _gridCols / 2;

    const double altNW = _carpetValueAt(rTop, cLeft);
    const double altNE = _carpetValueAt(rTop, cRight);
    const double altSW = _carpetValueAt(rBot, cLeft);
    const double altSE = _carpetValueAt(rBot, cRight);
    const double altC  = _carpetValueAt(rMid, cMid);

    qCInfo(TerrainOverlayMapLog) << "[Probe] Carpet summary:"
                                 << "grid=" << _gridCols << "x" << _gridRows
                                 << "min=" << minAlt
                                 << "max=" << maxAlt
                                 << "NaNs=" << nanCount;

    qCInfo(TerrainOverlayMapLog) << "[Probe] 5pt:"
                                 << "NW(r0,c0)=" << altNW
                                 << "NE(r0,cMax)=" << altNE
                                 << "SW(rMax,c0)=" << altSW
                                 << "SE(rMax,cMax)=" << altSE
                                 << "C(rMid,cMid)=" << altC;

    qCInfo(TerrainOverlayMapLog) << "[Probe] Bounds:"
                                 << "Lat[" << _minLat << "," << _maxLat << "]"
                                 << "Lon[" << _minLon << "," << _maxLon << "]"
                                 << "Center" << _centerLat << _centerLon;
}
