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

#include <QtConcurrent>
#include <QtMath>
#include <QVariantMap>
#include <QtCore/QMetaObject>
#include <QtCore/qapplicationstatic.h>

Q_LOGGING_CATEGORY(TerrainOverlayLog, "qgc.qgcapplication")

Q_APPLICATION_STATIC(TerrainOverlayGridManager, _instance);

TerrainOverlayGridManager* TerrainOverlayGridManager::instance()
{
    return _instance();
}

void TerrainOverlayGridManager::registerQmlTypes()
{
    qmlRegisterUncreatableType<TerrainOverlayGridManager>(
        "QGroundControl.TerrainOverlayGridManager", 1, 0,
        "TerrainOverlayGridManager",
        "Reference only"
    );

    qmlRegisterType<TerrainOverlayGridModel>("QGroundControl.TerrainOverlayGridModel", 1, 0, "TerrainOverlayGridModel");
}

TerrainOverlayGridManager::TerrainOverlayGridManager(QObject* parent)
    : QObject(parent)
{

    _gridModel = new TerrainOverlayGridModel(this);

    qCDebug(TerrainOverlayLog) << "TerrainOverlayGridManager initialized";

    connect(&_retryTimer, &QTimer::timeout, this, &TerrainOverlayGridManager::_requestTerrainAltitudes);
    _retryTimer.setInterval(1000);

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
        _gridModel->setCells({});
        _gridPoints.clear();
        _terrainAltitudes.clear();
        _modelBuilt = false;

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
    } else if (_modelBuilt) {
        qCDebug(TerrainOverlayLog) << "Model already built, updating";
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
    _modelBuilt = false;

    constexpr double spacingMeters = 200.0;
    constexpr double halfWidthMeters = 2000.0; // 2km x 2km grid

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
    bool updated = false;
    for (int i = 0; i < numCells; ++i) {
        if (!std::isnan(newAltitudes[i]) && std::isnan(_terrainAltitudes[i])) {
            _terrainAltitudes[i] = newAltitudes[i];
            updated = true;
        }
    }

    if (_hasAllTerrainData()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] All terrain data received. Building model.";
        _stopRetryTimer();
        _buildInitialModel();
    } else if (updated) {
        _updateColorsForVehiclePosition(_lastVehicleAltitude);
    }
}

void TerrainOverlayGridManager::_buildInitialModel()
{
    QList<TerrainGridCell> cells;
    int numCells = qMin(_gridPoints.count(), _terrainAltitudes.count());

    for (int i = 0; i < numCells; ++i) {
        double groundAlt = _terrainAltitudes[i];
        if (std::isnan(groundAlt)) {
            continue;
        }

        double relativeAlt = _lastVehicleAltitude - groundAlt;
        double value = (relativeAlt + 50.0) / 100.0;
        value = qBound(0.0, value, 1.0) * 100.0;

        TerrainGridCell cell;
        cell.latitude = _gridPoints[i].latitude();
        cell.longitude = _gridPoints[i].longitude();
        cell.altitude = groundAlt;
        cell.value = value;

        cells.append(cell);
    }

    _modelBuilt = true;
    _gridModel->setCells(cells);

    qCDebug(TerrainOverlayLog) << "[Overlay] Initial model built with" << cells.count() << "cells.";
}

void TerrainOverlayGridManager::_updateColorsForVehiclePosition(double vehicleAlt)
{
    if (!_modelBuilt) {
        return;
    }

    QGeoCoordinate dronePos = _activeVehicle->coordinate();
    int numCells = _gridPoints.size();

    QVector<int> indices;
    indices.reserve(numCells);
    for (int i = 0; i < numCells; ++i) {
        indices.append(i);
    }

    QVector<double> newValues = QtConcurrent::blockingMapped(indices, [this, &dronePos, vehicleAlt](int i) {
        double groundAlt = _terrainAltitudes[i];
        if (std::isnan(groundAlt)) return 0.0;

        double clearance = _computeClearanceMargin(dronePos, vehicleAlt, _gridPoints[i], groundAlt);
        double value;
        if (clearance >= 20.0) {
            value = 0.0;
        } else if (clearance <= 0.0) {
            value = 100.0;
        } else {
            value = (20.0 - clearance) / 20.0 * 100.0;
        }
        return value;
    });

    for (int i = 0; i < numCells; ++i) {
        _gridModel->updateCellValue(i, newValues[i]);
    }
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
double TerrainOverlayGridManager::_interpolatedTerrainAltitude(const QGeoCoordinate& coord) const
{
    // Find 4 surrounding grid points
    // Bilinearly interpolate their altitudes

    // For simplicity, you can even do nearest-neighbor for first version:
    double bestDist = std::numeric_limits<double>::max();
    double bestAlt = qQNaN();

    for (int i = 0; i < _gridPoints.size(); ++i) {
        double d = coord.distanceTo(_gridPoints[i]);
        if (d < bestDist && !std::isnan(_terrainAltitudes[i])) {
            bestDist = d;
            bestAlt = _terrainAltitudes[i];
        }
    }

    return bestAlt;
}

bool TerrainOverlayGridManager::_hasLineOfSight(
    const QGeoCoordinate& dronePos,
    double droneAlt,
    const QGeoCoordinate& targetPos,
    double targetGroundAlt)
{
    // How many steps? E.g., sample every 50m
    constexpr double stepMeters = 50.0;

    double groundDist = dronePos.distanceTo(targetPos);
    int numSteps = int(groundDist / stepMeters);

    if (numSteps < 1) return true; // Very close

    // Interpolate along path
    for (int i = 1; i < numSteps; ++i) {
        double t = double(i) / numSteps;

        double lat = dronePos.latitude() * (1 - t) + targetPos.latitude() * t;
        double lon = dronePos.longitude() * (1 - t) + targetPos.longitude() * t;
        QGeoCoordinate sampleCoord(lat, lon);

        double terrainAlt = _interpolatedTerrainAltitude(sampleCoord);
        if (std::isnan(terrainAlt)) continue;

        double expectedAlt = droneAlt * (1 - t) + (targetGroundAlt + 10) * t;
        // +10m buffer over ground at target to allow clearance

        if (terrainAlt > expectedAlt) {
            return false; // Blocked
        }
    }

    return true; // Clear
}

double TerrainOverlayGridManager::_computeClearanceMargin(
    const QGeoCoordinate& dronePos,
    double droneAlt,
    const QGeoCoordinate& targetPos,
    double targetGroundAlt) const
{
    constexpr double stepMeters = 200.0;
    constexpr double bufferAtTarget = 10.0;

    double groundDist = dronePos.distanceTo(targetPos);
    int numSteps = int(groundDist / stepMeters);
    if (numSteps < 1) {
        return droneAlt - targetGroundAlt - bufferAtTarget;
    }

    double minClearance = std::numeric_limits<double>::max();

    for (int i = 1; i < numSteps; ++i) {
        double t = double(i) / numSteps;
        double lat = dronePos.latitude() * (1 - t) + targetPos.latitude() * t;
        double lon = dronePos.longitude() * (1 - t) + targetPos.longitude() * t;
        QGeoCoordinate sampleCoord(lat, lon);

        double terrainAlt = _interpolatedTerrainAltitude(sampleCoord);
        if (std::isnan(terrainAlt)) continue;

        double lineAlt = droneAlt * (1 - t) + (targetGroundAlt + bufferAtTarget) * t;
        double clearance = lineAlt - terrainAlt;

        minClearance = std::min(minClearance, clearance);
    }

    return minClearance;
}
