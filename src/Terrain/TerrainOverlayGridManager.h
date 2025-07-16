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
#include <QTimer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayLog)

class Vehicle;

class TerrainOverlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QList<QVariant> gridData READ gridData NOTIFY gridDataChanged)

public:
    explicit TerrainOverlayGridManager(QObject* parent = nullptr);
    static TerrainOverlayGridManager* instance();
    static void registerQmlTypes();

    // Exposed grid data: list of QVariantMap {latitude, longitude, altitude}
    QList<QVariant> gridData() const;

signals:
    void gridDataChanged();

private slots:
    void _requestTerrainAltitudes();
    void _activeVehicleChanged(Vehicle* vehicle);
    void _vehicleCoordinateChanged(const QGeoCoordinate& newCoord);

private:
    void _connectActiveVehicle();
    void _generateGridAroundHome(const QGeoCoordinate& center);
    bool _hasAllTerrainData() const;
    void _startRetryTimer();
    void _stopRetryTimer();

    QList<QGeoCoordinate> _gridPoints;
    QList<double> _terrainAltitudes;
    QTimer _retryTimer;

    Vehicle* _activeVehicle = nullptr;
    QGeoCoordinate _homeCoord;
    bool _gridInitialized = false;
};
