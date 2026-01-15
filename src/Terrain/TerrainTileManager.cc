/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainTileManager.h"
#include "TerrainTile.h"
#include "TerrainTileCopernicus.h"
#include "QGeoTileFetcherQGC.h"
#include "QGeoMapReplyQGC.h"
#include "QGCMapUrlEngine.h"
#include "ElevationMapProvider.h"
#include "SettingsManager.h"
#include "FlightMapSettings.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QtNumeric>           // qQNaN
#include <QtCore/QTimer>
#include <algorithm>
#include <limits>

#include <QtLocation/private/qgeotilespec_p.h>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>
#include <QtNetwork/QNetworkRequest>

QGC_LOGGING_CATEGORY(TerrainTileManagerLog, "qgc.terrain.terraintilemanager")

Q_GLOBAL_STATIC(TerrainTileManager, _terrainTileManager)

TerrainTileManager *TerrainTileManager::instance()
{
    return _terrainTileManager();
}

TerrainTileManager::TerrainTileManager(QObject *parent)
    : QObject(parent)
      , _networkManager(new QNetworkAccessManager(this))
{
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QNetworkProxy proxy = _networkManager->proxy();
    proxy.setType(QNetworkProxy::DefaultProxy);
    _networkManager->setProxy(proxy);
#endif
}

TerrainTileManager::~TerrainTileManager()
{
    // free pending specs
    while (!_pendingTileQueue.isEmpty()) {
        PendingTileRequest req = _pendingTileQueue.dequeue();
        delete req.spec;
    }

    qDeleteAll(_tiles);
}

bool TerrainTileManager::getAltitudesForCoordinates(const QList<QGeoCoordinate> &coordinates, QList<double> &altitudes, bool &error)
{
    error = false;

    const QString elevationProviderName = SettingsManager::instance()->flightMapSettings()->elevationMapProvider()->rawValue().toString();
    const SharedMapProvider provider = UrlFactory::getMapProviderFromProviderType(elevationProviderName);

    if (!provider || !provider->isElevationProvider()) {
        qCWarning(TerrainTileManagerLog) << Q_FUNC_INFO << "No valid elevation provider:" << elevationProviderName;
        error = true;
        return true; // "returned" but error=true
    }

    for (const QGeoCoordinate &coordinate: coordinates) {
        const QString tileHash = UrlFactory::getTileHash(
            provider->getMapName(),
            provider->long2tileX(coordinate.longitude(), 1),
            provider->lat2tileY(coordinate.latitude(), 1),
            1
            );
        qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "hash:coordinate" << tileHash << coordinate;

        TerrainTile* const tile = _getCachedTile(tileHash);
        if (tile) {
            const double elevation = tile->elevation(coordinate);
            if (qIsNaN(elevation)) {
                error = true;
                qCWarning(TerrainTileManagerLog) << Q_FUNC_INFO << "Internal Error: missing elevation in tile cache";
            } else {
                qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "returning elevation from tile cache" << elevation;
            }
            altitudes.push_back(elevation);
        } else if (_state != TerrainQuery::State::Downloading) {
            QGeoTileSpec spec;
            spec.setX(provider->long2tileX(coordinate.longitude(), 1));
            spec.setY(provider->lat2tileY(coordinate.latitude(), 1));
            spec.setZoom(1);
            spec.setMapId(provider->getMapId());

            const QNetworkRequest request = QGeoTileFetcherQGC::getNetworkRequest(spec.mapId(), spec.x(), spec.y(), spec.zoom());
            QGeoTiledMapReplyQGC* const reply = new QGeoTiledMapReplyQGC(_networkManager, request, spec, this);
            (void) connect(reply, &QGeoTiledMapReplyQGC::finished, this, &TerrainTileManager::_terrainDone);
            _state = TerrainQuery::State::Downloading;
            // NOTE: legacy path triggers one download at a time
            return false;
        } else {
            return false;
        }
    }

    return true;
}

void TerrainTileManager::addCoordinateQuery(TerrainQueryInterface *terrainQueryInterface, const QList<QGeoCoordinate> &coordinates)
{
    qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "count" << coordinates.count();

    if (coordinates.isEmpty()) {
        return;
    }

    bool error;
    QList<double> altitudes;
    if (!getAltitudesForCoordinates(coordinates, altitudes, error)) {
        qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "queue count" << _requestQueue.count();
        const QueuedRequestInfo_t queuedRequestInfo = {
            terrainQueryInterface,
            TerrainQuery::QueryMode::QueryModeCoordinates,
            0,
            0,
            coordinates
        };
        _requestQueue.enqueue(queuedRequestInfo);
        return;
    }

    if (error) {
        QList<double> noAltitudes;
        qCWarning(TerrainTileManagerLog) << Q_FUNC_INFO << "signalling failure due to internal error";
        terrainQueryInterface->signalCoordinateHeights(false, noAltitudes);
        return;
    }

    qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "all altitudes taken from cached data";
    terrainQueryInterface->signalCoordinateHeights((coordinates.count() == altitudes.count()), altitudes);
}

void TerrainTileManager::addPathQuery(TerrainQueryInterface *terrainQueryInterface, const QGeoCoordinate &startPoint, const QGeoCoordinate &endPoint)
{
    double distanceBetween;
    double finalDistanceBetween;
    const QList<QGeoCoordinate> coordinates = _pathQueryToCoords(startPoint, endPoint, distanceBetween, finalDistanceBetween);

    bool error;
    QList<double> altitudes;
    if (!getAltitudesForCoordinates(coordinates, altitudes, error)) {
        qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "queue count" << _requestQueue.count();
        const QueuedRequestInfo_t queuedRequestInfo = {
            terrainQueryInterface,
            TerrainQuery::QueryMode::QueryModePath,
            distanceBetween,
            finalDistanceBetween,
            coordinates
        };
        _requestQueue.enqueue(queuedRequestInfo);
        return;
    }

    if (error) {
        QList<double> noAltitudes;
        qCWarning(TerrainTileManagerLog) << Q_FUNC_INFO << "signalling failure due to internal error";
        terrainQueryInterface->signalPathHeights(false, distanceBetween, finalDistanceBetween, noAltitudes);
        return;
    }

    qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO << "all altitudes taken from cached data";
    terrainQueryInterface->signalPathHeights((coordinates.count() == altitudes.count()), distanceBetween, finalDistanceBetween, altitudes);
}

QList<QGeoCoordinate> TerrainTileManager::_pathQueryToCoords(const QGeoCoordinate &fromCoord, const QGeoCoordinate &toCoord, double &distanceBetween, double &finalDistanceBetween)
{
    const double lat = fromCoord.latitude();
    const double lon = fromCoord.longitude();
    const int steps = qCeil(toCoord.distanceTo(fromCoord) / TerrainTileCopernicus::kTileValueSpacingMeters);
    const double latDiff = toCoord.latitude() - lat;
    const double lonDiff = toCoord.longitude() - lon;

    QList<QGeoCoordinate> coordinates;
    if (steps == 0) {
        (void) coordinates.append(fromCoord);
        (void) coordinates.append(toCoord);
        distanceBetween = finalDistanceBetween = coordinates[0].distanceTo(coordinates[1]);
    } else {
        for (int i = 0; i <= steps; i++) {
            const double latStep = lat + ((latDiff * static_cast<double>(i)) / static_cast<double>(steps));
            const double lonStep = lon + ((lonDiff * static_cast<double>(i)) / static_cast<double>(steps));
            (void) coordinates.append(QGeoCoordinate(latStep, lonStep));
        }

        coordinates.last() = toCoord;
        distanceBetween = coordinates[0].distanceTo(coordinates[1]);
        finalDistanceBetween = coordinates[coordinates.count() - 2].distanceTo(coordinates.last());
    }

    qCDebug(TerrainTileManagerLog) << Q_FUNC_INFO
                                   << "fromCoord:toCoord:distanceBetween:finalDisanceBetween:coordCount"
                                   << fromCoord << toCoord << distanceBetween << finalDistanceBetween << coordinates.count();

    return coordinates;
}

void TerrainTileManager::_tileFailed()
{
    QList<double> noAltitudes;

    for (const QueuedRequestInfo_t &requestInfo: _requestQueue) {
        switch (requestInfo.queryMode) {
            case TerrainQuery::QueryMode::QueryModeCoordinates:
                requestInfo.terrainQueryInterface->signalCoordinateHeights(false, noAltitudes);
                break;
            case TerrainQuery::QueryMode::QueryModePath:
                requestInfo.terrainQueryInterface->signalPathHeights(false, requestInfo.distanceBetween, requestInfo.finalDistanceBetween, noAltitudes);
                break;
            default:
                continue;
        }
    }

    _requestQueue.clear();
}

/*===========================================================================
 * Prefetch: download all tiles covering AOI (0.01° Copernicus tile grid)
 *===========================================================================*/

void TerrainTileManager::prefetchTilesForBounds(double minLat, double maxLat, double minLon, double maxLon, const QString& key)
{
    // Normalize bounds
    const double loLat = std::min(minLat, maxLat);
    const double hiLat = std::max(minLat, maxLat);
    const double loLon = std::min(minLon, maxLon);
    const double hiLon = std::max(minLon, maxLon);

    const QString providerName = SettingsManager::instance()->flightMapSettings()->elevationMapProvider()->rawValue().toString();
    const SharedMapProvider provider = UrlFactory::getMapProviderFromProviderType(providerName);

    if (!provider || !provider->isElevationProvider()) {
        qCWarning(TerrainTileManagerLog) << "[prefetchTilesForBounds] No valid elevation provider:" << providerName;
        emit tilesPrefetchComplete(key, false);
        return;
    }

    if (!_activePrefetchKey.isEmpty()) {
        qCInfo(TerrainTileManagerLog) << "[prefetchTilesForBounds] Batch already active:"
                                      << _activePrefetchKey << "ignoring new key:" << key;
        return;
    }

    _activePrefetchKey = key;
    _activeProviderName = providerName;
    _prefetchFailed = false;
    _completedTileHashes.clear();
    _pendingTileHashes.clear();

            // Clear old queue (and delete spec ptrs)
    while (!_pendingTileQueue.isEmpty()) {
        PendingTileRequest req = _pendingTileQueue.dequeue();
        delete req.spec;
    }

    constexpr int zoom = 1;

    const int x0 = provider->long2tileX(loLon, zoom);
    const int x1 = provider->long2tileX(hiLon, zoom);
    const int y0 = provider->lat2tileY(loLat, zoom);
    const int y1 = provider->lat2tileY(hiLat, zoom);

    const int minX = std::min(x0, x1);
    const int maxX = std::max(x0, x1);
    const int minY = std::min(y0, y1);
    const int maxY = std::max(y0, y1);

    const int total = (maxX - minX + 1) * (maxY - minY + 1);

    qCInfo(TerrainTileManagerLog) << "[prefetchTilesForBounds] key=" << key
                                  << "tileX:[" << minX << "," << maxX << "]"
                                  << "tileY:[" << minY << "," << maxY << "]"
                                  << "count=" << total;

    for (int y = minY; y <= maxY; ++y) {
        for (int x = minX; x <= maxX; ++x) {
            _enqueueTile(x, y, zoom, providerName, key);
        }
    }

    if (_pendingTileQueue.isEmpty()) {
        qCInfo(TerrainTileManagerLog) << "[prefetchTilesForBounds] All tiles already cached. key=" << key;
        const QString doneKey = _activePrefetchKey;
        _activePrefetchKey.clear();
        _activeProviderName.clear();
        emit tilesPrefetchComplete(doneKey, true);
        return;
    }

            // Start downloading (non-blocking, sequential)
    _startNextTileDownload();
}

void TerrainTileManager::_enqueueTile(int x, int y, int zoom, const QString& providerName, const QString& key)
{
    Q_UNUSED(key)

    const SharedMapProvider provider = UrlFactory::getMapProviderFromProviderType(providerName);
    if (!provider) {
        _prefetchFailed = true;
        return;
    }

    const QString hash = UrlFactory::getTileHash(provider->getMapName(), x, y, zoom);

    if (_getCachedTile(hash)) {
        return;
    }
    if (_pendingTileHashes.contains(hash)) {
        return;
    }

    auto* spec = new QGeoTileSpec();
    spec->setX(x);
    spec->setY(y);
    spec->setZoom(zoom);
    spec->setMapId(provider->getMapId());

    PendingTileRequest req;
    req.x = x;
    req.y = y;
    req.zoom = zoom;
    req.hash = hash;
    req.spec = spec;

    _pendingTileQueue.enqueue(req);
    _pendingTileHashes.insert(hash);
}

void TerrainTileManager::_startNextTileDownload()
{
    if (_activePrefetchKey.isEmpty()) {
        return;
    }

    if (_state == TerrainQuery::State::Downloading) {
        // current download still in flight
        return;
    }

    if (_pendingTileQueue.isEmpty()) {
        _finishPrefetchBatch(!_prefetchFailed);
        return;
    }

    PendingTileRequest req = _pendingTileQueue.dequeue();

            // If it got cached while waiting, skip
    if (_getCachedTile(req.hash)) {
        _completedTileHashes.insert(req.hash);
        _pendingTileHashes.remove(req.hash);
        delete req.spec;
        QTimer::singleShot(0, this, &TerrainTileManager::_startNextTileDownload);
        return;
    }

    const QGeoTileSpec specCopy = *req.spec; // copy to stack for reply ctor
    delete req.spec;

    const QNetworkRequest request = QGeoTileFetcherQGC::getNetworkRequest(specCopy.mapId(), specCopy.x(), specCopy.y(), specCopy.zoom());
    QGeoTiledMapReplyQGC* const reply = new QGeoTiledMapReplyQGC(_networkManager, request, specCopy, this);

    qCDebug(TerrainTileManagerLog) << "[prefetch] downloading tile"
                                   << "x=" << specCopy.x()
                                   << "y=" << specCopy.y()
                                   << "z=" << specCopy.zoom()
                                   << "hash=" << req.hash
                                   << "pendingLeft=" << _pendingTileQueue.size()
                                   << "key=" << _activePrefetchKey;

    (void) connect(reply, &QGeoTiledMapReplyQGC::finished, this, &TerrainTileManager::_terrainDone);
    _state = TerrainQuery::State::Downloading;
}

void TerrainTileManager::_finishPrefetchBatch(bool success)
{
    const QString doneKey = _activePrefetchKey;
    _activePrefetchKey.clear();
    _activeProviderName.clear();

            // Clean queue (delete any remaining specs)
    while (!_pendingTileQueue.isEmpty()) {
        PendingTileRequest req = _pendingTileQueue.dequeue();
        delete req.spec;
    }
    _pendingTileHashes.clear();

    qCInfo(TerrainTileManagerLog) << "[prefetchTilesForBounds] COMPLETE key=" << doneKey
                                  << "success=" << success
                                  << "completed=" << _completedTileHashes.size();

    emit tilesPrefetchComplete(doneKey, success);
}

/*===========================================================================
 * Network completion handler (shared by legacy + prefetch)
 *===========================================================================*/

void TerrainTileManager::_terrainDone()
{
    _state = TerrainQuery::State::Idle;

    QGeoTiledMapReplyQGC* const reply = qobject_cast<QGeoTiledMapReplyQGC*>(QObject::sender());
    if (!reply) {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetched but invalid reply data type.";
        return;
    }

    const QByteArray responseBytes = reply->mapImageData();
    const QGeoTileSpec spec = reply->tileSpec();

    const bool ok = (reply->error() == QGeoTiledMapReplyQGC::NoError) && !responseBytes.isEmpty();
    if (!ok) {
        qCWarning(TerrainTileManagerLog) << "Elevation tile fetch error:" << reply->errorString();

                // legacy queued queries fail hard
        _tileFailed();

                // prefetch should keep going but mark failure
        if (!_activePrefetchKey.isEmpty()) {
            _prefetchFailed = true;
        }

        reply->deleteLater();

                // keep prefetch pumping
        if (!_activePrefetchKey.isEmpty()) {
            QTimer::singleShot(0, this, &TerrainTileManager::_startNextTileDownload);
        }
        return;
    }

    reply->deleteLater();

    qCDebug(TerrainTileManagerLog) << "Received some bytes of terrain data:" << responseBytes.size();

    const QString hash = UrlFactory::getTileHash(UrlFactory::getProviderTypeFromQtMapId(spec.mapId()), spec.x(), spec.y(), spec.zoom());
    _cacheTile(responseBytes, hash);

            // Prefetch bookkeeping: mark completed and keep pumping
    if (!_activePrefetchKey.isEmpty()) {
        _completedTileHashes.insert(hash);
        _pendingTileHashes.remove(hash);
        QTimer::singleShot(0, this, &TerrainTileManager::_startNextTileDownload);
    }

            // Existing queued coord/path requests: retry if now satisfied
    for (qsizetype i = _requestQueue.count() - 1; i >= 0; i--) {
        bool error;
        QList<double> altitudes;
        QueuedRequestInfo_t &requestInfo = _requestQueue[i];

        if (!getAltitudesForCoordinates(requestInfo.coordinates, altitudes, error)) {
            continue;
        }

        switch (requestInfo.queryMode) {
            case TerrainQuery::QueryMode::QueryModeCoordinates:
                if (error) {
                    qCWarning(TerrainTileManagerLog) << "signalling failure due to internal error";
                    QList<double> noAltitudes;
                    requestInfo.terrainQueryInterface->signalCoordinateHeights(false, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "All altitudes taken from cached data";
                    requestInfo.terrainQueryInterface->signalCoordinateHeights(requestInfo.coordinates.count() == altitudes.count(), altitudes);
                }
                break;
            case TerrainQuery::QueryMode::QueryModePath:
                if (error) {
                    qCWarning(TerrainTileManagerLog) << "signalling failure due to internal error";
                    QList<double> noAltitudes;
                    requestInfo.terrainQueryInterface->signalPathHeights(false, requestInfo.distanceBetween, requestInfo.finalDistanceBetween, noAltitudes);
                } else {
                    qCDebug(TerrainTileManagerLog) << "All altitudes taken from cached data";
                    requestInfo.terrainQueryInterface->signalPathHeights(requestInfo.coordinates.count() == altitudes.count(), requestInfo.distanceBetween, requestInfo.finalDistanceBetween, altitudes);
                }
                break;
            default:
                break;
        }

        _requestQueue.removeAt(i);
    }
}

void TerrainTileManager::_cacheTile(const QByteArray &data, const QString &hash)
{
    TerrainTile* const terrainTile = new TerrainTile(data);
    if (!terrainTile->isValid()) {
        delete terrainTile;
        qCWarning(TerrainTileManagerLog) << "Received invalid tile";
        return;
    }

    bool inserted = false;

    {
        QMutexLocker locker(&_tilesMutex);
        if (!_tiles.contains(hash)) {
            _tiles.insert(hash, terrainTile);
            inserted = true;
        } else {
            delete terrainTile;
        }
    } // mutex released

    if (inserted) {
        qCDebug(TerrainTileManagerLog) << "Tile cached:" << hash;
        emit tileCached(hash);
    }
}

TerrainTile *TerrainTileManager::_getCachedTile(const QString &hash)
{
    QMutexLocker locker(&_tilesMutex);

    if (!_tiles.contains(hash)) {
        return nullptr;
    }

    TerrainTile* const tile = _tiles[hash];
    if (!tile || !tile->isValid()) {
        return nullptr;
    }

    return tile;
}

QList<TerrainTile*> TerrainTileManager::findTilesForBounds(double minLat, double maxLat, double minLon, double maxLon) const
{
    QMutexLocker locker(&_tilesMutex);
    QList<TerrainTile*> result;

    for (auto* tile : _tiles) {
        if (!tile || !tile->isValid()) {
            continue;
        }

        const auto& info = tile->tileInfo();

                // overlap checks
        if (info.neLat < minLat || info.swLat > maxLat) {
            continue;
        }
        if (info.neLon < minLon || info.swLon > maxLon) {
            continue;
        }

        result.append(tile);
    }

    return result;
}

bool TerrainTileManager::getCarpetForBoundsFromCache(double minLat,
                                                     double maxLat,
                                                     double minLon,
                                                     double maxLon,
                                                     int& outRows,
                                                     int& outCols,
                                                     double& outCellSizeLat,
                                                     double& outCellSizeLon,
                                                     double& outMinHeight,
                                                     double& outMaxHeight,
                                                     QList<float>& outGrid,
                                                     bool& needsDownload)
{
    // Normalize bounds
    const double aMinLat = std::min(minLat, maxLat);
    const double aMaxLat = std::max(minLat, maxLat);
    const double aMinLon = std::min(minLon, maxLon);
    const double aMaxLon = std::max(minLon, maxLon);

    outRows = 0;
    outCols = 0;
    outCellSizeLat = 0.0;
    outCellSizeLon = 0.0;
    outMinHeight = std::numeric_limits<double>::infinity();
    outMaxHeight = -std::numeric_limits<double>::infinity();
    outGrid.clear();
    needsDownload = false;

    const QList<TerrainTile*> tiles = findTilesForBounds(aMinLat, aMaxLat, aMinLon, aMaxLon);
    if (tiles.isEmpty()) {
        qCDebug(TerrainTileManagerLog) << "[getCarpetForBoundsFromCache] No cached tiles overlap bounds.";
        needsDownload = true;
        return false;
    }

            // Use the finest (smallest) cell sizes among tiles
    double cellLat = std::numeric_limits<double>::infinity();
    double cellLon = std::numeric_limits<double>::infinity();
    for (TerrainTile* t : tiles) {
        if (!t || !t->isValid()) continue;
        cellLat = std::min(cellLat, t->cellSizeLat());
        cellLon = std::min(cellLon, t->cellSizeLon());
    }

    if (!std::isfinite(cellLat) || !std::isfinite(cellLon) || cellLat <= 0.0 || cellLon <= 0.0) {
        qCWarning(TerrainTileManagerLog) << "[getCarpetForBoundsFromCache] Invalid tile cell sizes."
                                         << "cellLat" << cellLat << "cellLon" << cellLon;
        needsDownload = true;
        return false;
    }

    outCellSizeLat = cellLat;
    outCellSizeLon = cellLon;

            // Epsilons:
            //  - epsTile: inclusive tile membership guard (ULP-ish)
            //  - epsSample: keep sample points away from exact AOI edges (avoid NE-edge == gridSize index)
    const double epsTileLat  = std::max(1e-12, outCellSizeLat * 1e-9);
    const double epsTileLon  = std::max(1e-12, outCellSizeLon * 1e-9);
    const double epsSampleLat = std::max(1e-12, outCellSizeLat * 1e-6);
    const double epsSampleLon = std::max(1e-12, outCellSizeLon * 1e-6);

    const double latSpan = (aMaxLat - aMinLat);
    const double lonSpan = (aMaxLon - aMinLon);

    outRows = std::max(1, static_cast<int>(std::ceil(latSpan / outCellSizeLat)));
    outCols = std::max(1, static_cast<int>(std::ceil(lonSpan / outCellSizeLon)));

    outGrid.resize(outRows * outCols);
    std::fill(outGrid.begin(), outGrid.end(), static_cast<float>(qQNaN()));

    int validCount = 0;
    int missingCount = 0;

    auto containsInclusive = [&](const TerrainTile* t, double lat, double lon) -> bool {
        if (!t || !t->isValid()) return false;
        const auto info = t->tileInfo();
        // Inclusive with epsilon to avoid falling *just* outside due to float
        if (lat < info.swLat - epsTileLat || lat > info.neLat + epsTileLat) return false;
        if (lon < info.swLon - epsTileLon || lon > info.neLon + epsTileLon) return false;
        return true;
    };

    auto clampToTileInterior = [&](const TerrainTile* t, double lat, double lon, double& outLat, double& outLon) {
        const auto info = t->tileInfo();
        // Clamp away from exact edges to avoid qFloor(span/cell)==gridSize
        outLat = std::clamp(lat, info.swLat + epsSampleLat, info.neLat - epsSampleLat);
        outLon = std::clamp(lon, info.swLon + epsSampleLon, info.neLon - epsSampleLon);
    };

    for (int r = 0; r < outRows; ++r) {
        // Row 0 is north (maxLat). Sample at pixel centers.
        double rawLat = aMaxLat - (static_cast<double>(r) + 0.5) * outCellSizeLat;

                // Keep sample inside AOI interior (avoid exact bounds / rounding to NE edge)
        double sampleLat = std::clamp(rawLat, aMinLat + epsSampleLat, aMaxLat - epsSampleLat);

        for (int c = 0; c < outCols; ++c) {
            double rawLon = aMinLon + (static_cast<double>(c) + 0.5) * outCellSizeLon;
            double sampleLon = std::clamp(rawLon, aMinLon + epsSampleLon, aMaxLon - epsSampleLon);

                    // Find tile for this point
            TerrainTile* found = nullptr;
            for (TerrainTile* t : tiles) {
                if (containsInclusive(t, sampleLat, sampleLon)) {
                    found = t;
                    break;
                }
            }

            if (!found) {
                missingCount++;
                continue;
            }

                    // First try at sample point
            double elev = found->elevation(QGeoCoordinate(sampleLat, sampleLon));

                    // If NaN, clamp point into tile interior and retry once
            if (qIsNaN(elev)) {
                double nudgedLat, nudgedLon;
                clampToTileInterior(found, sampleLat, sampleLon, nudgedLat, nudgedLon);
                elev = found->elevation(QGeoCoordinate(nudgedLat, nudgedLon));
            }

            if (qIsNaN(elev)) {
                missingCount++;
                continue;
            }

            outGrid[r * outCols + c] = static_cast<float>(elev);
            validCount++;

            outMinHeight = std::min(outMinHeight, elev);
            outMaxHeight = std::max(outMaxHeight, elev);
        }
    }

    if (validCount == 0) {
        qCWarning(TerrainTileManagerLog) << "[getCarpetForBoundsFromCache] No valid samples generated."
                                         << "rows" << outRows << "cols" << outCols
                                         << "missingCount" << missingCount;
        needsDownload = true;
        return false;
    }

    if (!std::isfinite(outMinHeight) || !std::isfinite(outMaxHeight) || outMaxHeight <= outMinHeight) {
        outMaxHeight = outMinHeight + 1.0;
    }

            // If we missed anything, we likely still need more tiles
    if (missingCount > 0) {
        needsDownload = true;
    }

    qCInfo(TerrainTileManagerLog) << "[getCarpetForBoundsFromCache] Built carpet from cache."
                                  << "bounds lat[" << aMinLat << "," << aMaxLat << "]"
                                  << "lon[" << aMinLon << "," << aMaxLon << "]"
                                  << "grid" << outCols << "x" << outRows
                                  << "cellLat" << outCellSizeLat << "cellLon" << outCellSizeLon
                                  << "valid" << validCount << "missing" << missingCount
                                  << "min" << outMinHeight << "max" << outMaxHeight
                                  << "needsDownload" << needsDownload;

    return true;
}
