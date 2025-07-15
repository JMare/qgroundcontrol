/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "QGCApplication.h"
#include "QGCCorePlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "TerrainOverlayGridManager.h"
#include "TerrainTileManager.h"

#include <QtMath>
#include <QVariantMap>
#include <QtCore/QMetaObject>
#include <QtCore/qapplicationstatic.h>

TerrainOverlayGridManager* TerrainOverlayGridManager::_instance = nullptr;

Q_LOGGING_CATEGORY(TerrainOverlayLog, "qgc.qgcapplication")

Q_APPLICATION_STATIC(TerrainOverlayGridManager, _instance);

TerrainOverlayGridManager* TerrainOverlayGridManager::instance()
{
    if (!_instance) {
        _instance = new TerrainOverlayGridManager(qgcApp());
    }
    return _instance;
}

void TerrainOverlayGridManager::registerQmlTypes()
{
    qmlRegisterUncreatableType<TerrainOverlayGridManager>(
        "QGroundControl.TerrainOverlayGridManager", 1, 0,
        "TerrainOverlayGridManager",
        "Reference only"
    );
}

TerrainOverlayGridManager::TerrainOverlayGridManager(QObject* parent)
    : QObject(parent)
{
    qCDebug(TerrainOverlayLog) << "TerrainOverlayGridManager initialized";

    connect(&_retryTimer, &QTimer::timeout, this, &TerrainOverlayGridManager::_requestTerrainAltitudes);
    _retryTimer.setInterval(1000); // Retry every 3 seconds

    _connectActiveVehicle();

    auto* manager = MultiVehicleManager::instance();
    connect(manager, &MultiVehicleManager::activeVehicleChanged,
            this, &TerrainOverlayGridManager::_activeVehicleChanged);
}

void TerrainOverlayGridManager::_connectActiveVehicle()
{
    auto* manager = MultiVehicleManager::instance();
    _activeVehicle = manager->activeVehicle();

    if (_activeVehicle) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Connected to active vehicle";
        connect(_activeVehicle, &Vehicle::coordinateChanged,
                this, &TerrainOverlayGridManager::_vehicleCoordinateChanged,
                Qt::UniqueConnection);
        _vehicleCoordinateChanged(_activeVehicle->coordinate());
    }
}

void TerrainOverlayGridManager::_activeVehicleChanged(Vehicle* vehicle)
{
    if (_activeVehicle) {
        disconnect(_activeVehicle, nullptr, this, nullptr);
    }

    _activeVehicle = vehicle;

    if (_activeVehicle) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Active vehicle changed. Subscribing.";
        connect(_activeVehicle, &Vehicle::coordinateChanged,
                this, &TerrainOverlayGridManager::_vehicleCoordinateChanged,
                Qt::UniqueConnection);

        _vehicleCoordinateChanged(_activeVehicle->coordinate());
    } else {
        qCDebug(TerrainOverlayLog) << "[Overlay] No active vehicle. Clearing grid.";
        _gridModel.clear();
        _gridPoints.clear();
        _terrainAltitudes.clear();
        _stopRetryTimer();
        emit modelChanged();
    }
}

void TerrainOverlayGridManager::_vehicleCoordinateChanged(const QGeoCoordinate& newCoord)
{
    if (!_activeVehicle || !newCoord.isValid()) {
        return;
    }


    double vehicleAlt = _activeVehicle->coordinate().altitude();

    if (std::isnan(vehicleAlt)) {
        vehicleAlt = 0;
    }

    _lastVehicleAltitude = vehicleAlt;

    if (_gridPoints.isEmpty()) {
        _homeCoord = newCoord;
        _tryInitialGridSetup();
    } else {
        _updateColorsForVehiclePosition(vehicleAlt);
    }
}

void TerrainOverlayGridManager::_tryInitialGridSetup()
{
    if (!_homeCoord.isValid()) {
        qCWarning(TerrainOverlayLog) << "[Overlay] No valid home coordinate to initialize grid!";
        return;
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Generating grid around home:" << _homeCoord;
    _generateGridAroundHome(_homeCoord);

    _requestTerrainAltitudes();
    _startRetryTimer();
}

void TerrainOverlayGridManager::_generateGridAroundHome(const QGeoCoordinate& center)
{
    _gridPoints.clear();
    _terrainAltitudes.clear();

    constexpr double spacingMeters = 50.0;
    constexpr double halfWidthMeters = 1000.0; // 4km x 4km grid

    double approxLatSpacing = spacingMeters / 111320.0;
    double approxLatHalf = halfWidthMeters / 111320.0;
    double centerLatRad = qDegreesToRadians(center.latitude());
    double metersPerDegLon = 111320.0 * std::cos(centerLatRad);
    double approxLonSpacing = spacingMeters / metersPerDegLon;
    double approxLonHalf = halfWidthMeters / metersPerDegLon;

    double minLat = center.latitude() - approxLatHalf;
    double maxLat = center.latitude() + approxLatHalf;
    double minLon = center.longitude() - approxLonHalf;
    double maxLon = center.longitude() + approxLonHalf;

    for (double lat = minLat; lat <= maxLat; lat += approxLatSpacing) {
        for (double lon = minLon; lon <= maxLon; lon += approxLonSpacing) {
            _gridPoints.append(QGeoCoordinate(lat, lon));
            _terrainAltitudes.append(qQNaN());
        }
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Generated" << _gridPoints.count() << "grid points.";
}

void TerrainOverlayGridManager::_requestTerrainAltitudes()
{
    if (_gridPoints.isEmpty()) {
        _stopRetryTimer();
        return;
    }

    bool error = false;
    QList<double> newAltitudes;
    bool haveAllData = TerrainTileManager::instance()->getAltitudesForCoordinates(_gridPoints, newAltitudes, error);

    if (error) {
        qCWarning(TerrainOverlayLog) << "[Overlay] Error requesting altitudes!";
        return;
    }

    int numCells = qMin(_gridPoints.count(), newAltitudes.count());
    for (int i = 0; i < numCells; ++i) {
        if (!std::isnan(newAltitudes[i])) {
            _terrainAltitudes[i] = newAltitudes[i];
        }
    }

    _updateColorsForVehiclePosition(_lastVehicleAltitude);

    if (_hasAllTerrainData()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] All terrain data received. Stopping retry.";
        _stopRetryTimer();
    } else {
        qCDebug(TerrainOverlayLog) << "[Overlay] Still missing terrain tiles. Retrying.";
    }
}

void TerrainOverlayGridManager::_updateColorsForVehiclePosition(double vehicleAlt)
{
    _gridModel.clear();

    int numCells = qMin(_gridPoints.count(), _terrainAltitudes.count());
    for (int i = 0; i < numCells; ++i) {
        double groundAlt = _terrainAltitudes[i];
        if (std::isnan(groundAlt)) {
            continue;
        }

        double relativeAlt = vehicleAlt - groundAlt;
        double value = (relativeAlt + 50) / 100.0;
        value = qBound(0.0, value, 1.0) * 100.0;

        QVariantMap cell;
        cell["lat"] = _gridPoints[i].latitude();
        cell["lon"] = _gridPoints[i].longitude();
        cell["altitude"] = groundAlt;
        cell["value"] = value;

        _gridModel.append(cell);
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Model updated with" << _gridModel.count() << "cells.";
    emit modelChanged();
}

bool TerrainOverlayGridManager::_hasAllTerrainData() const
{
    for (double alt : _terrainAltitudes) {
        if (std::isnan(alt)) {
            return false;
        }
    }
    return true;
}

void TerrainOverlayGridManager::_startRetryTimer()
{
    if (!_retryTimer.isActive()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Starting retry timer.";
        _retryTimer.start();
    }
}

void TerrainOverlayGridManager::_stopRetryTimer()
{
    if (_retryTimer.isActive()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Stopping retry timer.";
        _retryTimer.stop();
    }
}
