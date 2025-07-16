/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "TerrainOverlayGridManager.h"
#include "TerrainTileManager.h"
#include "QGCApplication.h"
#include "QGCLoggingCategory.h"
#include "QGCCorePlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"

#include <QtMath>
#include <QVariant>
#include <QVariantMap>
#include <QtCore/qapplicationstatic.h>

QGC_LOGGING_CATEGORY(TerrainOverlayLog, "qgc.terrainoverlay")

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
}

TerrainOverlayGridManager::TerrainOverlayGridManager(QObject* parent)
    : QObject(parent)
{
    qCDebug(TerrainOverlayLog) << "TerrainOverlayGridManager initialized";

    _retryTimer.setInterval(1000);
    connect(&_retryTimer, &QTimer::timeout, this, &TerrainOverlayGridManager::_requestTerrainAltitudes);

    // Listen to MultiVehicleManager for active vehicle changes
    auto* manager = MultiVehicleManager::instance();
    connect(manager, &MultiVehicleManager::activeVehicleChanged,
            this, &TerrainOverlayGridManager::_activeVehicleChanged);

    _connectActiveVehicle();
}

void TerrainOverlayGridManager::_connectActiveVehicle()
{
    auto* manager = MultiVehicleManager::instance();
    _activeVehicle = manager->activeVehicle();

    if (_activeVehicle) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Connected to active vehicle.";
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
    _gridInitialized = false;
    _homeCoord = QGeoCoordinate();

    if (_activeVehicle) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Active vehicle changed. Subscribing.";
        connect(_activeVehicle, &Vehicle::coordinateChanged,
                this, &TerrainOverlayGridManager::_vehicleCoordinateChanged,
                Qt::UniqueConnection);

        _vehicleCoordinateChanged(_activeVehicle->coordinate());
    } else {
        qCDebug(TerrainOverlayLog) << "[Overlay] No active vehicle.";
    }
}

void TerrainOverlayGridManager::_vehicleCoordinateChanged(const QGeoCoordinate& newCoord)
{
    if (_gridInitialized) {
        return;
    }

    if (!newCoord.isValid()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Invalid coordinate received. Waiting.";
        return;
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Got valid home coordinate:" << newCoord;
    _homeCoord = newCoord;

    _generateGridAroundHome(_homeCoord);
    _requestTerrainAltitudes();
    _startRetryTimer();
    _gridInitialized = true;
}

void TerrainOverlayGridManager::_generateGridAroundHome(const QGeoCoordinate& center)
{
    _gridPoints.clear();
    _terrainAltitudes.clear();

    constexpr double spacingMeters = 200.0;
    constexpr double halfWidthMeters = 2000.0;

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
        qCWarning(TerrainOverlayLog) << "[Overlay] No grid points for altitude request!";
        _stopRetryTimer();
        return;
    }

    bool error = false;
    QList<double> newAltitudes;
    bool haveAllData = TerrainTileManager::instance()->getAltitudesForCoordinates(_gridPoints, newAltitudes, error);

    if (error) {
        qCWarning(TerrainOverlayLog) << "[Overlay] Error requesting altitudes! Will retry.";
        return;
    }

    bool updated = false;
    int count = qMin(_gridPoints.count(), newAltitudes.count());
    for (int i = 0; i < count; ++i) {
        if (!std::isnan(newAltitudes[i]) && std::isnan(_terrainAltitudes[i])) {
            _terrainAltitudes[i] = newAltitudes[i];
            updated = true;
        }
    }

    if (_hasAllTerrainData()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] All terrain altitudes fetched.";
        _stopRetryTimer();
        emit gridDataChanged();
    } else if (updated) {
            int fetchedCount = 0;
            for (double alt : _terrainAltitudes) {
                if (!std::isnan(alt)) {
                    fetchedCount++;
                }
            }
            qCDebug(TerrainOverlayLog) << "[Overlay] Partial altitudes updated:"
                                        << fetchedCount << "/" << _terrainAltitudes.size()
                                        << "points complete. Still retrying.";
    }
}

bool TerrainOverlayGridManager::_hasAllTerrainData() const
{
    for (double alt : _terrainAltitudes) {
        if (std::isnan(alt)) return false;
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

QList<QVariant> TerrainOverlayGridManager::gridData() const
{
    QList<QVariant> data;
    int count = qMin(_gridPoints.count(), _terrainAltitudes.count());
    for (int i = 0; i < count; ++i) {
        QVariantMap entry;
        entry["latitude"] = _gridPoints[i].latitude();
        entry["longitude"] = _gridPoints[i].longitude();
        entry["altitude"] = _terrainAltitudes[i];
        data.append(entry);
    }
    return data;
}
