#pragma once

#include <QObject>
#include "TileOverlayModel.h"

class TerrainOverlayGridManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(TileOverlayModel* model READ model CONSTANT)

public:
    explicit TerrainOverlayGridManager(QObject* parent = nullptr);

    TileOverlayModel* model() const { return _model; }

    /// Loads all terrain tiles intersecting a viewport
    /// minLat, maxLat, minLon, maxLon define the visible map region
    Q_INVOKABLE void loadTilesForViewport(double minLat, double maxLat, double minLon, double maxLon);

private:
    TileOverlayModel* _model;

    QColor altitudeToColor(double altitude) const;
};
