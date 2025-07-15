/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#pragma once

#include <QObject>
#include <QVariant>
#include <QGeoCoordinate>
#include <QTimer>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayLog)

class Vehicle;

class TerrainOverlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList model READ model NOTIFY modelChanged)

public:
    explicit TerrainOverlayGridManager(QObject* parent = nullptr);
    static TerrainOverlayGridManager* instance();
    static void registerQmlTypes();

    QVariantList model() const { return _gridModel; }

signals:
    void modelChanged();

private:
    void _connectActiveVehicle();
    void _activeVehicleChanged(Vehicle* vehicle);
    void _vehicleCoordinateChanged(const QGeoCoordinate& newCoord);

    void _tryInitialGridSetup();
    void _generateGridAroundHome(const QGeoCoordinate& center);

    void _requestTerrainAltitudes();
    void _buildInitialModel();
    void _updateColorsForVehiclePosition(double vehicleAlt);

    bool _hasAllTerrainData() const;
    void _startRetryTimer();
    void _stopRetryTimer();

    Vehicle* _activeVehicle = nullptr;

    QVariantList _gridModel;
    QList<QGeoCoordinate> _gridPoints;
    QList<double> _terrainAltitudes;

    double _lastVehicleAltitude = 0.0;
    QGeoCoordinate _homeCoord;

    QTimer _retryTimer;
    bool _modelBuilt = false;
};
