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
#include "HeatmapImageProvider.h"

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayMapLog)

class TerrainOverlayMapRenderer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(double minLat READ minLat NOTIFY boundsChanged)
    Q_PROPERTY(double minLon READ minLon NOTIFY boundsChanged)
    Q_PROPERTY(double maxLat READ maxLat NOTIFY boundsChanged)
    Q_PROPERTY(double maxLon READ maxLon NOTIFY boundsChanged)
    Q_PROPERTY(double overlayNativeZoomLevel READ overlayNativeZoomLevel NOTIFY boundsChanged)
    Q_PROPERTY(int lastUpdateCounter READ lastUpdateCounter NOTIFY heatmapImageChanged)
    Q_PROPERTY(QVariantList altitudeGrid READ altitudeGrid NOTIFY gridDataChanged)  // NEW
    Q_PROPERTY(int gridRows READ gridRows NOTIFY gridDataChanged)
    Q_PROPERTY(int gridCols READ gridCols NOTIFY gridDataChanged)

public:
    int _gridRows = 0;
    int _gridCols = 0;

    int gridRows() const { return _gridRows; }
    int gridCols() const { return _gridCols; }
    static TerrainOverlayMapRenderer* instance();
    static void registerQmlTypes();

    explicit TerrainOverlayMapRenderer(QObject* parent = nullptr);

    double minLat() const { return _minLat; }
    double minLon() const { return _minLon; }
    double maxLat() const { return _maxLat; }
    double maxLon() const { return _maxLon; }
    double overlayNativeZoomLevel() const { return _overlayNativeZoomLevel; }
    int lastUpdateCounter() const { return _updateCounter; }
    QVariantList altitudeGrid() const { return _altitudeGrid; }  // NEW

    void setImageProvider(HeatmapImageProvider* provider);

signals:
    void boundsChanged();
    void heatmapImageChanged();
    void gridDataChanged();  // NEW

private:
    void _connectToManager();
    void _onGridChanged();
    void _computeBounds(const QVariantMap& grid);
    void _generateHeatmapImage(const QVariantMap& grid);
    void _computeOverlayNativeZoomLevel(int imageWidth, int imageHeight);

    HeatmapImageProvider* _imageProvider = nullptr;

    double _minLat = 0.0;
    double _minLon = 0.0;
    double _maxLat = 0.0;
    double _maxLon = 0.0;
    double _overlayNativeZoomLevel = 0.0;

    int _updateCounter = 0;

    QVariantList _altitudeGrid;  // NEW
};
