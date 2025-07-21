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
#include "QGCCorePlugin.h"
#include "MultiVehicleManager.h"
#include "Vehicle.h"
#include "QGCLoggingCategory.h"

#include <QtMath>
#include <QVariant>
#include <QVariantMap>
#include <QtCore/qapplicationstatic.h>

QGC_LOGGING_CATEGORY(TerrainOverlayLog, "qgc.terrainoverlay")

Q_APPLICATION_STATIC(TerrainOverlayGridManager, _instance);

// Simulated terrain toggle
#define SIMULATED_TERRAIN

// Custom test terrain params (meters AMSL)
constexpr double kSimMinAltitude = 200.0;
constexpr double kSimMaxAltitude = 300.0;
constexpr int kBlockRow = 70;  // Row for raised hill
constexpr int kBlockCol = 70;  // Col for raised hill
constexpr int kBlockSize = 10;  // Width/height of square block

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

    // Connect to active vehicle management
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
    _center = QGeoCoordinate();

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
    _center = newCoord;

    _generateGrid(_center);
    _requestTerrainAltitudes();
    _startRetryTimer();
    _gridInitialized = true;
}

void TerrainOverlayGridManager::_generateGrid(const QGeoCoordinate& center)
{
    _altitudes.clear();

    constexpr double halfWidthMeters = 2000.0;

    double approxLatSpacing = _spacingMeters / 111320.0;
    double approxLatHalf = halfWidthMeters / 111320.0;

    double centerLatRad = qDegreesToRadians(center.latitude());
    double metersPerDegLon = 111320.0 * std::cos(centerLatRad);
    double approxLonSpacing = _spacingMeters / metersPerDegLon;
    double approxLonHalf = halfWidthMeters / metersPerDegLon;

    double minLat = center.latitude() - approxLatHalf;
    double maxLat = center.latitude() + approxLatHalf;
    double minLon = center.longitude() - approxLonHalf;
    double maxLon = center.longitude() + approxLonHalf;

    _rows = int(((maxLat - minLat) / approxLatSpacing) + 1);
    _cols = int(((maxLon - minLon) / approxLonSpacing) + 1);

    qCDebug(TerrainOverlayLog) << "[Overlay] Grid dimensions:" << _rows << "rows x" << _cols << "cols.";

    // Fill altitudes with NaN
    _altitudes.fill(qQNaN(), _rows * _cols);
}

void TerrainOverlayGridManager::_requestTerrainAltitudes()
{
    if (_rows == 0 || _cols == 0) {
        qCWarning(TerrainOverlayLog) << "[Overlay] Grid is empty. Skipping altitude request.";
        _stopRetryTimer();
        return;
    }

#ifdef SIMULATED_TERRAIN
    qCDebug(TerrainOverlayLog) << "[Overlay] Generating simulated terrain data.";

    _altitudes.fill(kSimMinAltitude);  // Set all to base level

    // Raise block at center-ish
    for (int row = 0; row < _rows; ++row) {
        for (int col = 0; col < _cols; ++col) {
            if (std::abs(row - kBlockRow) <= kBlockSize / 2 &&
                std::abs(col - kBlockCol) <= kBlockSize / 2) {
                int idx = row * _cols + col;
                _altitudes[idx] = kSimMaxAltitude;
            }
        }
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Simulated terrain populated.";
    _stopRetryTimer();
    emit gridChanged();
    return;
#endif

    // Regular (real-world) terrain fetch
    QList<QGeoCoordinate> requestPoints;
    double approxLatSpacing = _spacingMeters / 111320.0;
    double centerLatRad = qDegreesToRadians(_center.latitude());
    double metersPerDegLon = 111320.0 * std::cos(centerLatRad);
    double approxLonSpacing = _spacingMeters / metersPerDegLon;

    int centerRow = _rows / 2;
    int centerCol = _cols / 2;

    for (int row = 0; row < _rows; ++row) {
        double lat = _center.latitude() + (row - centerRow) * approxLatSpacing;
        for (int col = 0; col < _cols; ++col) {
            double lon = _center.longitude() + (col - centerCol) * approxLonSpacing;
            requestPoints.append(QGeoCoordinate(lat, lon));
        }
    }

    bool error = false;
    QList<double> newAltitudes;
    bool haveAllData = TerrainTileManager::instance()->getAltitudesForCoordinates(requestPoints, newAltitudes, error);

    if (error) {
        qCWarning(TerrainOverlayLog) << "[Overlay] Error requesting altitudes! Will retry.";
        return;
    }

    bool updated = false;
    int count = qMin(_altitudes.size(), newAltitudes.size());
    for (int i = 0; i < count; ++i) {
        if (!std::isnan(newAltitudes[i]) && std::isnan(_altitudes[i])) {
            _altitudes[i] = newAltitudes[i];
            updated = true;
        }
    }

    if (_hasAllTerrainData()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] All terrain altitudes fetched.";
        _stopRetryTimer();
        emit gridChanged();
    } else if (updated) {
        int fetched = _countFetchedAltitudes();
        qCDebug(TerrainOverlayLog) << "[Overlay] Partial altitudes updated:"
                                   << fetched << "/" << _altitudes.size()
                                   << "points complete. Still retrying.";
    }
}

bool TerrainOverlayGridManager::_hasAllTerrainData() const
{
    for (double alt : _altitudes) {
        if (std::isnan(alt)) return false;
    }
    return true;
}

int TerrainOverlayGridManager::_countFetchedAltitudes() const
{
    int count = 0;
    for (double alt : _altitudes) {
        if (!std::isnan(alt)) {
            count++;
        }
    }
    return count;
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

QVariant TerrainOverlayGridManager::grid() const
{
    QVariantMap result;
    result["centerLat"] = _center.latitude();
    result["centerLon"] = _center.longitude();
    result["spacingMeters"] = _spacingMeters;
    result["rows"] = _rows;
    result["cols"] = _cols;

    QVariantList altitudeList;
    for (double alt : _altitudes) {
        altitudeList.append(alt);
    }
    result["altitudes"] = altitudeList;

    return result;
}
