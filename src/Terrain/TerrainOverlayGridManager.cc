/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "QGCApplication.h"

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
    qCDebug(TerrainOverlayLog) << "Hello";
}

QList<QGeoCoordinate> TerrainOverlayGridManager::generateGridPoints(double minLat, double maxLat, double minLon, double maxLon)
{
    QList<QGeoCoordinate> grid;
    constexpr double spacingMeters = 50.0;

    if (minLat > maxLat || minLon > maxLon) {
        qCWarning(TerrainOverlayLog) << "Invalid viewport bounds";
        return grid;
    }

    double approxLatSpacing = spacingMeters / 111320.0;
    double centerLat = (minLat + maxLat) / 2.0;
    double approxLonSpacing = spacingMeters / (111320.0 * std::cos(qDegreesToRadians(centerLat)));

    qCDebug(TerrainOverlayLog) << "Generating grid with spacing approx"
                               << approxLatSpacing << "deg lat,"
                               << approxLonSpacing << "deg lon";

    for (double lat = minLat; lat <= maxLat; lat += approxLatSpacing) {
        for (double lon = minLon; lon <= maxLon; lon += approxLonSpacing) {
            grid.append(QGeoCoordinate(lat, lon));
        }
    }

    qCDebug(TerrainOverlayLog) << "Generated grid points:" << grid.count();
    return grid;
}

void TerrainOverlayGridManager::loadTilesForViewport(double minLat, double maxLat, double minLon, double maxLon)
{
    qCDebug(TerrainOverlayLog) << "[Overlay] Loading for viewport:"
                               << "Lat:" << minLat << "to" << maxLat
                               << "Lon:" << minLon << "to" << maxLon;

    // Approximate size in meters
    constexpr double metersPerDegLat = 111320.0;
    double centerLat = (minLat + maxLat) / 2.0;
    double metersPerDegLon = metersPerDegLat * std::cos(qDegreesToRadians(centerLat));

    double widthMeters = std::abs(maxLon - minLon) * metersPerDegLon;
    double heightMeters = std::abs(maxLat - minLat) * metersPerDegLat;

    qCDebug(TerrainOverlayLog) << "[Overlay] Viewport dimensions (approx meters):"
                                << widthMeters << "x" << heightMeters;

    // Reject too-large requests
    constexpr double maxWidthMeters = 20000.0;
    constexpr double maxHeightMeters = 20000.0;

    if (widthMeters > maxWidthMeters || heightMeters > maxHeightMeters) {
        qCWarning(TerrainOverlayLog) << "[Overlay] Viewport too large, skipping overlay calculation!";
        _gridModel.clear();
        emit modelChanged();
        return;
    }
    auto gridPoints = generateGridPoints(minLat, maxLat, minLon, maxLon);

    if (gridPoints.isEmpty()) {
        qCDebug(TerrainOverlayLog) << "[Overlay] No grid points generated!";
        _gridModel.clear();
        emit modelChanged();
        return;
    }

    QList<double> altitudes;
    bool error = false;
    bool haveAllData = TerrainTileManager::instance()->getAltitudesForCoordinates(gridPoints, altitudes, error);

    qCDebug(TerrainOverlayLog) << "[Overlay] Requested altitudes. Immediate result:" << haveAllData;

    if (!haveAllData) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Some tiles missing - will fill in on next call after download.";
    }

    updateGridModel(gridPoints, altitudes);
}

void TerrainOverlayGridManager::updateGridModel(const QList<QGeoCoordinate>& points, const QList<double>& altitudes)
{
    _gridModel.clear();

    int numCells = qMin(points.count(), altitudes.count());
    for (int i = 0; i < numCells; ++i) {
        double alt = altitudes[i];
        if (std::isnan(alt)) {
            continue;
        }

        const auto& coord = points[i];
        QColor color = altitudeToColor(alt);

        // Only append fully defined cells
        QVariantMap cell;
        cell["lat"] = coord.latitude();
        cell["lon"] = coord.longitude();
        cell["altitude"] = alt;
        double normalized = qBound(0.0, (alt - 250.0) / 150.0 * 100.0, 100.0);
        cell["value"] = normalized;

        _gridModel.append(cell);
        qCDebug(TerrainOverlayLog) << "[Overlay] Adding cell:" << coord << "alt:" << alt << "color:" << color;
    }

    qCDebug(TerrainOverlayLog) << "[Overlay] Model now has" << _gridModel.count() << "cells.";
    for (const QVariant& v : _gridModel) {
        qCDebug(TerrainOverlayLog) << "[Overlay] Cell contents:" << v;
    }
    emit modelChanged();
}

QColor TerrainOverlayGridManager::altitudeToColor(double altitude) const
{
    if (std::isnan(altitude) || altitude < 0) {
        return QColor("gray");
    }
    if (altitude < 50)  return QColor("green");
    if (altitude < 150) return QColor("yellow");
    if (altitude < 300) return QColor("orange");
    return QColor("red");
}
