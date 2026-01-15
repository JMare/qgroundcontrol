/****************************************************************************
 *
 * (c) 2009-2024 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "TerrainQueryInterface.h"

#include <QtCore/QHash>
#include <QtCore/QLoggingCategory>
#include <QtCore/QMutex>
#include <QtCore/QObject>
#include <QtCore/QQueue>
#include <QtCore/QSet>
#include <QtPositioning/QGeoCoordinate>

class TerrainTile;
class QNetworkAccessManager;
class UnitTestTerrainQuery;
class QGeoTileSpec;

Q_DECLARE_LOGGING_CATEGORY(TerrainTileManagerLog)

class TerrainTileManager : public QObject
{
    Q_OBJECT

    friend class UnitTestTerrainQuery;
   public:
    explicit TerrainTileManager(QObject *parent = nullptr);
    ~TerrainTileManager();

    static TerrainTileManager *instance();

            /// Either returns altitudes from cache or queues database request
            ///     @param[out] error true: altitude not returned due to error, false: altitudes returned
            ///     @return true: altitude returned (check error as well), false: database query queued (altitudes not returned)
    bool getAltitudesForCoordinates(const QList<QGeoCoordinate> &coordinates, QList<double> &altitudes, bool &error);

    QList<TerrainTile*> findTilesForBounds(double minLat, double maxLat, double minLon, double maxLon) const;

            /// Attempts to build a carpet grid from whatever tiles are currently cached.
            /// If coverage is incomplete, outGrid will contain NaNs and needsDownload=true.
    bool getCarpetForBoundsFromCache(double minLat, double maxLat,
                                     double minLon, double maxLon,
                                     int& outRows, int& outCols,
                                     double& outCellSizeLat, double& outCellSizeLon,
                                     double& outMinHeight, double& outMaxHeight,
                                     QList<float>& outGrid,
                                     bool& needsDownload);

    void addCoordinateQuery(TerrainQueryInterface *terrainQueryInterface, const QList<QGeoCoordinate> &coordinates);
    void addPathQuery(TerrainQueryInterface *terrainQueryInterface, const QGeoCoordinate &startPoint, const QGeoCoordinate &endPoint);

            /// NEW: prefetch ALL tiles overlapping bounds (async, non-blocking).
            /// When complete (or failed), tilesPrefetchComplete(key, success) is emitted.
    void prefetchTilesForBounds(double minLat, double maxLat, double minLon, double maxLon, const QString& key);

   signals:
    void tileCached(const QString& hash);

            /// NEW: emitted when a bounds-prefetch batch completes.
    void tilesPrefetchComplete(const QString& key, bool success);

   private slots:
    void _terrainDone();

   private:
    /// Returns a list of individual coordinates along the requested path spaced according to the terrain tile value spacing
    static QList<QGeoCoordinate> _pathQueryToCoords(const QGeoCoordinate &fromCoord, const QGeoCoordinate &toCoord, double &distanceBetween, double &finalDistanceBetween);
    void _tileFailed();
    void _cacheTile(const QByteArray &data, const QString &hash);
    TerrainTile *_getCachedTile(const QString &hash);

            // --- Prefetch machinery ---
    struct PendingTileRequest {
        int x = 0;
        int y = 0;
        int zoom = 1;
        QString hash;
        QGeoTileSpec* spec = nullptr; // allocated at enqueue time, freed at dequeue
    };

    void _enqueueTile(int x, int y, int zoom, const QString& providerName, const QString& key);
    void _startNextTileDownload();
    void _finishPrefetchBatch(bool success);

            // active batch tracking (single batch at a time)
    QString _activePrefetchKey;
    QString _activeProviderName;
    QQueue<PendingTileRequest> _pendingTileQueue;
    QSet<QString> _pendingTileHashes;
    QSet<QString> _completedTileHashes;
    bool _prefetchFailed = false;

            // --- Existing queued request machinery ---
    struct QueuedRequestInfo_t {
        TerrainQueryInterface *terrainQueryInterface;
        TerrainQuery::QueryMode queryMode;
        double distanceBetween;                         ///< Distance between each returned height
        double finalDistanceBetween;                    ///< Distance between for final height
        QList<QGeoCoordinate> coordinates;
    };

    QQueue<QueuedRequestInfo_t> _requestQueue;
    TerrainQuery::State _state = TerrainQuery::State::Idle;

    mutable QMutex _tilesMutex;
    QHash<QString, TerrainTile*> _tiles;

    QNetworkAccessManager *_networkManager = nullptr;
};
