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
#include <QPointer>
#include <QMetaObject>

#include "HeatmapImageProvider.h"

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayMapLog)

class TerrainOfflineQuery;
class Vehicle;

class TerrainOverlayMapRenderer : public QObject
{
    Q_OBJECT

   public:
    enum class State : int {
        Idle = 0,
        WaitingForHome,
        BuildingFromCache,
        Prefetching,
        StabilizedPartial,
        Ready,
        Failed
    };
    Q_ENUM(State)

    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(QString stateName READ stateName NOTIFY stateChanged)

            // Existing props used by overlay
    Q_PROPERTY(double minLat READ minLat NOTIFY boundsChanged)
    Q_PROPERTY(double minLon READ minLon NOTIFY boundsChanged)
    Q_PROPERTY(double maxLat READ maxLat NOTIFY boundsChanged)
    Q_PROPERTY(double maxLon READ maxLon NOTIFY boundsChanged)
    Q_PROPERTY(double centerLat READ centerLat NOTIFY boundsChanged)
    Q_PROPERTY(double centerLon READ centerLon NOTIFY boundsChanged)

    Q_PROPERTY(double terrainMinMeters READ terrainMinMeters NOTIFY terrainRangeChanged)
    Q_PROPERTY(double terrainMaxMeters READ terrainMaxMeters NOTIFY terrainRangeChanged)
    Q_PROPERTY(double overlayNativeZoomLevel READ overlayNativeZoomLevel NOTIFY boundsChanged)

    Q_PROPERTY(int lastUpdateCounter READ lastUpdateCounter NOTIFY heatmapImageChanged)

    Q_PROPERTY(QVariantList altitudeGrid READ altitudeGrid NOTIFY gridDataChanged)
    Q_PROPERTY(int gridRows READ gridRows NOTIFY gridDataChanged)
    Q_PROPERTY(int gridCols READ gridCols NOTIFY gridDataChanged)

    Q_PROPERTY(int nanCount READ nanCount NOTIFY progressChanged)
    Q_PROPERTY(double nanRatio READ nanRatio NOTIFY progressChanged)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)

            // --------------------------------------------------------------------
            // STEP 1: Preview altitude (for Guided slider / Takeoff slider)
            // --------------------------------------------------------------------
    Q_PROPERTY(bool previewAltitudeEnabled READ previewAltitudeEnabled WRITE setPreviewAltitudeEnabled NOTIFY previewChanged)
    Q_PROPERTY(double previewAltitudeMeters READ previewAltitudeMeters WRITE setPreviewAltitudeMeters NOTIFY previewChanged)

   public:
    static TerrainOverlayMapRenderer* instance();
    static void registerQmlTypes();

    explicit TerrainOverlayMapRenderer(QObject* parent = nullptr);

    Q_INVOKABLE void requestCarpet(double minLat, double maxLat, double minLon, double maxLon, bool statsOnly = false);
    Q_INVOKABLE void clear();

            // Props
    State state() const { return _state; }
    QString stateName() const;

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

    int nanCount() const { return _nanCount; }
    double nanRatio() const { return _nanRatio; }
    double progress() const { return _progress; }

            // Preview altitude API
    bool previewAltitudeEnabled() const { return _previewAltitudeEnabled; }
    double previewAltitudeMeters() const { return _previewAltitudeMeters; }

    void setPreviewAltitudeEnabled(bool enabled);
    void setPreviewAltitudeMeters(double meters);

    void setImageProvider(HeatmapImageProvider* provider);

   signals:
    void boundsChanged();
    void heatmapImageChanged();
    void gridDataChanged();
    void terrainRangeChanged();

    void stateChanged();
    void progressChanged();

            // STEP 1 signal
    void previewChanged();

   private slots:
    void _onTileCached(const QString& hash);

            // Auto-load around home point
    void _activeVehicleChanged(Vehicle* vehicle);
    void _vehicleHomePositionChanged(const QGeoCoordinate& home);

   private:
    // State helpers
    void _setState(State s);

            // UI / output
    void _setBounds(double minLat, double maxLat, double minLon, double maxLon);
    void _generateHeatmapImage();
    void _computeOverlayNativeZoomLevel(int imageWidth, int imageHeight);

            // Carpet pipeline
    void _tryBuildFromCacheOrPrefetch(bool statsOnly);
    void _scheduleRetryDebounced();
    void _startPrefetchForCurrentBounds();
    void _kickPrefetch();

            // Progress helpers
    void _updateProgress(int nanCount, int total);
    void _evaluateCompletionHeuristics(bool needsDownload);

            // Auto home helpers
    void _queueHomeCenteredRequest(const QGeoCoordinate& home);
    void _requestAroundCoordinateMeters(const QGeoCoordinate& center, double radiusMeters);

            // Helpers
    static QString _makeBoundsKey(double minLat, double maxLat, double minLon, double maxLon);

   private:
    HeatmapImageProvider* _imageProvider = nullptr;
    TerrainOfflineQuery*  _terrainQuery  = nullptr;

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
    bool    _carpetRequestInFlight = false;
    QString _lastCarpetKey;

            // Retry control
    int  _retryCount = 0;
    int  _maxRetries = 10;
    int  _lastNanCountInternal = -1;
    int  _stableNanCountHits = 0;
    bool _retryScheduled = false;

            // Prefetch queue
    bool _prefetchActive = false;
    QQueue<QGeoCoordinate> _prefetchProbeQueue;

            // Active vehicle + home tracking
    QPointer<Vehicle> _activeVehicle;
    QMetaObject::Connection _homeConn;
    bool _homeRequestQueued = false;
    bool _homeRequestCompleted = false;
    QGeoCoordinate _lastHomeUsed;

            // Exposed state/progress
    State _state = State::Idle;
    int _nanCount = 0;
    double _nanRatio = 1.0;
    double _progress = 0.0;

            // --------------------------------------------------------------------
            // STEP 1 storage
            // --------------------------------------------------------------------
    bool   _previewAltitudeEnabled = false;
    double _previewAltitudeMeters  = qQNaN();
};
