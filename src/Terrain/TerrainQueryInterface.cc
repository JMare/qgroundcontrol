/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "TerrainQueryInterface.h"
#include "TerrainTileManager.h"
#include "QGCLoggingCategory.h"

#include <QtCore/QtNumeric>
#include <QtPositioning/QGeoCoordinate>
#include <QtNetwork/QNetworkAccessManager>
#include <QtNetwork/QNetworkProxy>

QGC_LOGGING_CATEGORY(TerrainQueryInterfaceLog, "qgc.terrain.terrainqueryinterface")

TerrainQueryInterface::TerrainQueryInterface(QObject *parent)
    : QObject(parent)
{
}

TerrainQueryInterface::~TerrainQueryInterface()
{
}

void TerrainQueryInterface::requestCoordinateHeights(const QList<QGeoCoordinate> &coordinates)
{
    Q_UNUSED(coordinates);
    qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "Not Supported";
}

void TerrainQueryInterface::requestPathHeights(const QGeoCoordinate &fromCoord, const QGeoCoordinate &toCoord)
{
    Q_UNUSED(fromCoord);
    Q_UNUSED(toCoord);
    qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "Not Supported";
}

void TerrainQueryInterface::requestCarpetHeights(const QGeoCoordinate &swCoord, const QGeoCoordinate &neCoord, bool statsOnly)
{
    Q_UNUSED(swCoord);
    Q_UNUSED(neCoord);
    Q_UNUSED(statsOnly);
    qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "Not Supported";
}

void TerrainQueryInterface::signalCoordinateHeights(bool success, const QList<double> &heights)
{
    emit coordinateHeightsReceived(success, heights);
}

void TerrainQueryInterface::signalPathHeights(bool success, double distanceBetween, double finalDistanceBetween, const QList<double> &heights)
{
    emit pathHeightsReceived(success, distanceBetween, finalDistanceBetween, heights);
}

void TerrainQueryInterface::signalCarpetHeights(bool success, double minHeight, double maxHeight, const QList<QList<double>> &carpet)
{
    emit carpetHeightsReceived(success, minHeight, maxHeight, carpet);
}

void TerrainQueryInterface::_requestFailed()
{
    switch (_queryMode) {
        case TerrainQuery::QueryModeCoordinates:
            emit coordinateHeightsReceived(false, QList<double>());
            break;
        case TerrainQuery::QueryModePath:
            emit pathHeightsReceived(false, qQNaN(), qQNaN(), QList<double>());
            break;
        case TerrainQuery::QueryModeCarpet:
            emit carpetHeightsReceived(false, qQNaN(), qQNaN(), QList<QList<double>>());
            break;
        default:
            qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "Query Mode Not Supported";
            break;
    }
}

/*===========================================================================*/

TerrainOfflineQuery::TerrainOfflineQuery(QObject *parent)
    : TerrainQueryInterface(parent)
{
}

TerrainOfflineQuery::~TerrainOfflineQuery()
{
    if (_tileCachedConn) {
        QObject::disconnect(_tileCachedConn);
    }
}

void TerrainOfflineQuery::requestCoordinateHeights(const QList<QGeoCoordinate> &coordinates)
{
    if (coordinates.isEmpty()) {
        return;
    }

    _queryMode = TerrainQuery::QueryModeCoordinates;
    TerrainTileManager::instance()->addCoordinateQuery(this, coordinates);
}

void TerrainOfflineQuery::requestPathHeights(const QGeoCoordinate &fromCoord, const QGeoCoordinate &toCoord)
{
    _queryMode = TerrainQuery::QueryModePath;
    TerrainTileManager::instance()->addPathQuery(this, fromCoord, toCoord);
}

void TerrainOfflineQuery::requestCarpetHeights(const QGeoCoordinate &swCoord, const QGeoCoordinate &neCoord, bool statsOnly)
{
    _queryMode = TerrainQuery::QueryModeCarpet;

            // Normalize bounds (lat/lon ordering)
    const double minLat = std::min(swCoord.latitude(), neCoord.latitude());
    const double maxLat = std::max(swCoord.latitude(), neCoord.latitude());
    const double minLon = std::min(swCoord.longitude(), neCoord.longitude());
    const double maxLon = std::max(swCoord.longitude(), neCoord.longitude());

    _carpetSw = QGeoCoordinate(minLat, minLon);
    _carpetNe = QGeoCoordinate(maxLat, maxLon);
    _carpetStatsOnly = statsOnly;
    _carpetPending = true;

    qCInfo(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] requestCarpetHeights:"
                                     << "SW:" << _carpetSw
                                     << "NE:" << _carpetNe
                                     << "statsOnly:" << _carpetStatsOnly;

            // Listen for new tiles arriving so we can retry without polling
    if (!_tileCachedConn) {
        _tileCachedConn = QObject::connect(
            TerrainTileManager::instance(),
            &TerrainTileManager::tileCached,
            this,
            &TerrainOfflineQuery::_tileCached
            );
    }

            // Try immediately
    _startOrRetryCarpet();
}

void TerrainOfflineQuery::_tileCached(const QString& hash)
{
    Q_UNUSED(hash);
    if (!_carpetPending) {
        return;
    }

            // A tile arrived; try to rebuild the carpet
    qCDebug(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] tileCached -> retry carpet";
    _startOrRetryCarpet();
}

void TerrainOfflineQuery::_triggerPrefetchProbes()
{
    // Small probe set to kick the existing tile download mechanism
    // without doing per-pixel coordinate queries.
    QList<QGeoCoordinate> probes;
    probes.reserve(4 + 25);

    probes.append(QGeoCoordinate(_carpetSw.latitude(), _carpetSw.longitude()));
    probes.append(QGeoCoordinate(_carpetSw.latitude(), _carpetNe.longitude()));
    probes.append(QGeoCoordinate(_carpetNe.latitude(), _carpetSw.longitude()));
    probes.append(QGeoCoordinate(_carpetNe.latitude(), _carpetNe.longitude()));

    const int nx = 5;
    const int ny = 5;
    for (int iy = 0; iy < ny; ++iy) {
        const double tY = (iy + 0.5) / ny;
        const double lat = _carpetSw.latitude() + tY * (_carpetNe.latitude() - _carpetSw.latitude());
        for (int ix = 0; ix < nx; ++ix) {
            const double tX = (ix + 0.5) / nx;
            const double lon = _carpetSw.longitude() + tX * (_carpetNe.longitude() - _carpetSw.longitude());
            probes.append(QGeoCoordinate(lat, lon));
        }
    }

    QList<double> alts;
    bool error = false;
    const bool gotNow = TerrainTileManager::instance()->getAltitudesForCoordinates(probes, alts, error);

    qCInfo(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] prefetch probes:"
                                     << "count=" << probes.count()
                                     << "gotNow=" << gotNow
                                     << "error=" << error
                                     << "altsReturned=" << alts.count();
}

static QList<QList<double>> _gridToCarpetList(const QVector<float>& grid, int rows, int cols)
{
    QList<QList<double>> carpet;
    carpet.reserve(rows);
    for (int r = 0; r < rows; ++r) {
        QList<double> row;
        row.reserve(cols);
        const int base = r * cols;
        for (int c = 0; c < cols; ++c) {
            const float v = grid[base + c];
            row.append(static_cast<double>(v));
        }
        carpet.append(row);
    }
    return carpet;
}

void TerrainOfflineQuery::_startOrRetryCarpet()
{
    if (!_carpetPending) {
        return;
    }

    const double minLat = _carpetSw.latitude();
    const double maxLat = _carpetNe.latitude();
    const double minLon = _carpetSw.longitude();
    const double maxLon = _carpetNe.longitude();

    int rows = 0;
    int cols = 0;
    double cellLatDeg = qQNaN();
    double cellLonDeg = qQNaN();
    double minH = qQNaN();
    double maxH = qQNaN();
    bool complete = false;

    QVector<float> grid;

            // IMPORTANT: This call must match what you added in TerrainTileManager.
            //
            // Expected behavior:
            // - returns true if it could produce a grid (even if incomplete)
            // - sets rows/cols
            // - fills grid with NaNs where missing (or some sentinel)
            // - sets minH/maxH from available samples
            // - sets complete=true if fully covered
            //
    const bool ok = TerrainTileManager::instance()->getCarpetForBoundsFromCache(
        minLat, maxLat, minLon, maxLon,
        rows, cols,
        cellLatDeg, cellLonDeg,
        minH, maxH,
        grid,
        complete
        );

    if (!ok) {
        qCInfo(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] carpet cache not ready yet; triggering prefetch";
        _triggerPrefetchProbes();
        return;
    }

    if (rows <= 0 || cols <= 0 || grid.size() != rows * cols) {
        qCWarning(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] carpet returned invalid dimensions:"
                                            << "rows=" << rows
                                            << "cols=" << cols
                                            << "gridSize=" << grid.size();
        _requestFailed();
        _carpetPending = false;
        return;
    }

            // Compute fill ratio for logging
    int filled = 0;
    for (int i = 0; i < grid.size(); ++i) {
        const float v = grid[i];
        if (v == v) { // NaN check
            filled++;
        }
    }
    const double fillRatio = static_cast<double>(filled) / static_cast<double>(grid.size());

    qCInfo(TerrainQueryInterfaceLog) << "[TerrainOfflineQuery] carpet built:"
                                     << "size=" << cols << "x" << rows
                                     << "cellDeg(lat,lon)=" << cellLatDeg << cellLonDeg
                                     << "minH/maxH=" << minH << maxH
                                     << "filled=" << filled << "/" << grid.size()
                                     << "fillRatio=" << fillRatio
                                     << "complete=" << complete;

            // If statsOnly, we don't need to return carpet data
    if (_carpetStatsOnly) {
        signalCarpetHeights(true, minH, maxH, QList<QList<double>>());
    } else {
        // Convert to legacy signal type
        const QList<QList<double>> carpet = _gridToCarpetList(grid, rows, cols);
        signalCarpetHeights(true, minH, maxH, carpet);
    }

            // If not complete yet, keep pending and wait for more tiles to arrive
    if (!complete) {
        _triggerPrefetchProbes();
        return;
    }

            // Done: stop listening / mark complete
    _carpetPending = false;
}

/*===========================================================================*/

TerrainOnlineQuery::TerrainOnlineQuery(QObject *parent)
    : TerrainQueryInterface(parent)
      , _networkManager(new QNetworkAccessManager(this))
{
    qCDebug(TerrainQueryInterfaceLog) << "supportsSsl" << QSslSocket::supportsSsl()
    << "sslLibraryBuildVersionString" << QSslSocket::sslLibraryBuildVersionString();

#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    QNetworkProxy proxy = _networkManager->proxy();
    proxy.setType(QNetworkProxy::DefaultProxy);
    _networkManager->setProxy(proxy);
#endif
}

TerrainOnlineQuery::~TerrainOnlineQuery()
{
}

void TerrainOnlineQuery::_requestFinished()
{
    QNetworkReply* const reply = qobject_cast<QNetworkReply*>(QObject::sender());
    if (!reply) {
        qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "null reply";
        return;
    }

    if (reply->error() != QNetworkReply::NoError) {
        qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "error:url:data"
                                            << reply->error() << reply->url() << reply->readAll();
        reply->deleteLater();
        _requestFailed();
        return;
    }

    const QByteArray responseBytes = reply->readAll();
    Q_UNUSED(responseBytes);
    reply->deleteLater();

    qCDebug(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "success";
}

void TerrainOnlineQuery::_requestError(QNetworkReply::NetworkError code)
{
    if (code != QNetworkReply::NoError) {
        QNetworkReply* const reply = qobject_cast<QNetworkReply*>(QObject::sender());
        qCWarning(TerrainQueryInterfaceLog) << Q_FUNC_INFO << "error:url:data"
                                            << reply->error() << reply->url() << reply->readAll();
    }
}

void TerrainOnlineQuery::_sslErrors(const QList<QSslError> &errors)
{
    for (const QSslError &error : errors) {
        qCWarning(TerrainQueryInterfaceLog) << "SSL error:" << error.errorString();

        const QSslCertificate &certificate = error.certificate();
        if (!certificate.isNull()) {
            qCWarning(TerrainQueryInterfaceLog) << "SSL Certificate problem:" << certificate.toText();
        }
    }
}
