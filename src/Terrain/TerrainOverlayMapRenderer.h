// TerrainOverlayMapRenderer.h
/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QGeoCoordinate>
#include <QLoggingCategory>
#include <QVariantList>
#include <QQueue>

#include "HeatmapImageProvider.h"

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayMapLog)

class TerrainOfflineQuery;

class TerrainOverlayMapRenderer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double minLat READ minLat NOTIFY boundsChanged)
    Q_PROPERTY(double minLon READ minLon NOTIFY boundsChanged)
    Q_PROPERTY(double maxLat READ maxLat NOTIFY boundsChanged)
    Q_PROPERTY(double maxLon READ maxLon NOTIFY boundsChanged)
    Q_PROPERTY(double terrainMinMeters READ terrainMinMeters NOTIFY terrainRangeChanged)
    Q_PROPERTY(double terrainMaxMeters READ terrainMaxMeters NOTIFY terrainRangeChanged)
    Q_PROPERTY(double overlayNativeZoomLevel READ overlayNativeZoomLevel NOTIFY boundsChanged)
    Q_PROPERTY(int lastUpdateCounter READ lastUpdateCounter NOTIFY heatmapImageChanged)
    Q_PROPERTY(QVariantList altitudeGrid READ altitudeGrid NOTIFY gridDataChanged)
    Q_PROPERTY(int gridRows READ gridRows NOTIFY gridDataChanged)
    Q_PROPERTY(int gridCols READ gridCols NOTIFY gridDataChanged)
    Q_PROPERTY(double centerLat READ centerLat NOTIFY boundsChanged)
    Q_PROPERTY(double centerLon READ centerLon NOTIFY boundsChanged)

   public:
    static TerrainOverlayMapRenderer* instance();
    static void registerQmlTypes();

    explicit TerrainOverlayMapRenderer(QObject* parent = nullptr);

            // Public API
    Q_INVOKABLE void requestCarpet(double minLat, double maxLat, double minLon, double maxLon, bool statsOnly = false);
    Q_INVOKABLE void clear();

            // Props
    double minLat() const { return _minLat; }
    double minLon() const { return _minLon; }
    double maxLat() const { return _maxLat; }
    double maxLon() const { return _maxLon; }
    double centerLat() const { return _centerLat; }
    double centerLon() const { return _centerLon; }
    double terrainMinMeters() const { return _terrainMinMeters; }
    double terrainMaxMeters() const { return _terrainMaxMeters; }
    double overlayNativeZoomLevel() const { return _overlayNativeZoomLevel; }
    int lastUpdateCounter() const { return _updateCounter; }
    QVariantList altitudeGrid() const { return _altitudeGrid; }
    int gridRows() const { return _gridRows; }
    int gridCols() const { return _gridCols; }

    void setImageProvider(HeatmapImageProvider* provider);

   signals:
    void boundsChanged();
    void heatmapImageChanged();
    void gridDataChanged();
    void terrainRangeChanged();

   private slots:
    void _onTileCached(const QString& hash);

   private:
    // UI / output
    void _setBounds(double minLat, double maxLat, double minLon, double maxLon);
    void _generateHeatmapImage();
    void _computeOverlayNativeZoomLevel(int imageWidth, int imageHeight);

            // Carpet pipeline (cache-first + prefetch)
    void _requestStartupCarpet();
    void _tryBuildFromCacheOrPrefetch(bool statsOnly);
    void _scheduleRetryDebounced();
    void _startPrefetchForCurrentBounds();
    void _kickPrefetch();
    void _debugCarpetSummary_5pt() const;

            // Helpers
    static QString _makeBoundsKey(double minLat, double maxLat, double minLon, double maxLon);
    static int _clampInt(int v, int lo, int hi);
    double _carpetValueAt(int row, int col) const;

   private:
    HeatmapImageProvider* _imageProvider = nullptr;
    TerrainOfflineQuery*  _terrainQuery  = nullptr; // kept for future, not required for this pipeline

            // Bounds
    double _minLat = 0.0;
    double _minLon = 0.0;
    double _maxLat = 0.0;
    double _maxLon = 0.0;
    double _centerLat = 0.0;
    double _centerLon = 0.0;

            // Grid
    int _gridRows = 0;
    int _gridCols = 0;
    QVariantList _altitudeGrid;

            // Range / zoom
    double _terrainMinMeters = 0.0;
    double _terrainMaxMeters = 0.0;
    double _overlayNativeZoomLevel = 0.0;

    int _updateCounter = 0;

            // Request state
    bool   _startupCarpetRequested = false;
    bool   _carpetRequestInFlight  = false;
    QString _lastCarpetKey;

            // Retry control (prevents spam loops)
    int  _retryCount = 0;
    int  _maxRetries = 10;
    int  _lastNanCount = -1;
    int  _stableNanCountHits = 0;
    bool _retryScheduled = false;

            // Prefetch queue (drives downloads one-tile-at-a-time)
    bool _prefetchActive = false;
    QQueue<QGeoCoordinate> _prefetchProbeQueue;
};
