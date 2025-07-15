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
#include <QColor>
#include <QLoggingCategory>

Q_DECLARE_LOGGING_CATEGORY(TerrainOverlayLog)

class TerrainOverlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList model READ model NOTIFY modelChanged)

public:
    explicit TerrainOverlayGridManager(QObject* parent = nullptr);
    static TerrainOverlayGridManager* instance();
    static void registerQmlTypes();

    Q_INVOKABLE void loadTilesForViewport(double minLat, double maxLat, double minLon, double maxLon);
    QVariantList model() const { return _gridModel; }

signals:
    void modelChanged();

private:
    QList<QGeoCoordinate> generateGridPoints(double minLat, double maxLat, double minLon, double maxLon);
    void updateGridModel(const QList<QGeoCoordinate>& points, const QList<double>& altitudes);
    QColor altitudeToColor(double altitude) const;

    QVariantList _gridModel;
    static TerrainOverlayGridManager* _instance;
};
