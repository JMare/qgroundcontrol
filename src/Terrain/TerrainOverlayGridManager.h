/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <qloggingcategory.h>
#include <QObject>
#include <QGeoCoordinate>
#include <QTimer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayLog);

class Vehicle;

class TerrainOverlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariant grid READ grid NOTIFY gridChanged)

public:
    explicit TerrainOverlayGridManager(QObject* parent = nullptr);
    static TerrainOverlayGridManager* instance();
    static void registerQmlTypes();

    // Returns a QVariantMap containing:
    // - centerLat
    // - centerLon
    // - spacingMeters
    // - rows
    // - cols
    // - altitudes (flattened list)
    QVariant grid() const;

signals:
    void gridChanged();

private slots:
    void _requestTerrainAltitudes();
    void _activeVehicleChanged(Vehicle* vehicle);
    void _vehicleCoordinateChanged(const QGeoCoordinate& newCoord);

private:
    void _connectActiveVehicle();
    void _generateGrid(const QGeoCoordinate& center);
    bool _hasAllTerrainData() const;
    void _startRetryTimer();
    void _stopRetryTimer();
    int _countFetchedAltitudes() const;

    // Grid definition
    QGeoCoordinate _center;
    double _spacingMeters = 100.0;
    int _rows = 0;
    int _cols = 0;
    QVector<double> _altitudes;

    QTimer _retryTimer;
    Vehicle* _activeVehicle = nullptr;
    bool _gridInitialized = false;
};
