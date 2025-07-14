#include "TerrainOverlayGridManager.h"
#include "TerrainTileManager.h"
#include "TerrainTile.h"

#include <QDebug>
#include <QtMath>

TerrainOverlayGridManager::TerrainOverlayGridManager(QObject* parent)
    : QObject(parent)
    , _model(new TileOverlayModel(this))
{
}

QColor TerrainOverlayGridManager::altitudeToColor(double altitude) const
{
    // Simple gradient mapping
    if (altitude < 100) return QColor("#00FF00");      // Green
    if (altitude < 200) return QColor("#FFFF00");      // Yellow
    return QColor("#FF0000");                          // Red
}

void TerrainOverlayGridManager::loadTilesForViewport(double minLat, double maxLat, double minLon, double maxLon)
{
    qDebug() << "[TerrainOverlay] Loading overlay for viewport:"
             << "Lat:" << minLat << "to" << maxLat
             << "Lon:" << minLon << "to" << maxLon;

    _model->clear();

    auto tiles = TerrainTileManager::instance()->findTilesForBounds(minLat, maxLat, minLon, maxLon);
    qDebug() << "[TerrainOverlay] Found" << tiles.size() << "tiles intersecting viewport";

    for (auto* tile : tiles) {
        if (!tile || !tile->isValid())
            continue;

        const auto& data = tile->elevationData();
        double swLat = tile->tileInfo().swLat;
        double swLon = tile->tileInfo().swLon;
        double cellSizeLat = tile->cellSizeLat();
        double cellSizeLon = tile->cellSizeLon();

        for (int i = 0; i < data.size(); ++i) {
            for (int j = 0; j < data[i].size(); ++j) {
                double cellLat = swLat + i * cellSizeLat;
                double cellLon = swLon + j * cellSizeLon;
                double altitude = static_cast<double>(data[i][j]);

                QColor color = altitudeToColor(altitude);

                _model->addEntry(new TileOverlayEntry(cellLat, cellLon, altitude, color, _model));
            }
        }
    }

    qDebug() << "[TerrainOverlay] Overlay model now has" << _model->rowCount() << "grid cells";
}
