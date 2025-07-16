/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QImage>
#include <QLoggingCategory>

class HeatmapImageProvider;

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayMapLog)

class TerrainOverlayMapRenderer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int lastUpdateCounter READ lastUpdateCounter NOTIFY heatmapImageChanged)
    Q_PROPERTY(double minLat READ minLat NOTIFY boundsChanged)
    Q_PROPERTY(double minLon READ minLon NOTIFY boundsChanged)
    Q_PROPERTY(double maxLat READ maxLat NOTIFY boundsChanged)
    Q_PROPERTY(double maxLon READ maxLon NOTIFY boundsChanged)

public:
    explicit TerrainOverlayMapRenderer(QObject* parent = nullptr);

    static TerrainOverlayMapRenderer* instance();
    static void registerQmlTypes();

    void setImageProvider(HeatmapImageProvider* provider);

    int lastUpdateCounter() const { return _updateCounter; }
    double minLat() const { return _minLat; }
    double minLon() const { return _minLon; }
    double maxLat() const { return _maxLat; }
    double maxLon() const { return _maxLon; }

signals:
    void heatmapImageChanged();
    void boundsChanged();

private slots:
    void _onGridChanged();

private:
    void _connectToManager();
    void _generateHeatmapImage(const QVariantMap& grid);
    void _computeBounds(const QVariantMap& grid);

    HeatmapImageProvider* _imageProvider = nullptr;
    int _updateCounter = 0;

    double _minLat = 0.0;
    double _minLon = 0.0;
    double _maxLat = 0.0;
    double _maxLon = 0.0;
};
