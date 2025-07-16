/****************************************************************************
 *
 * (c) 2024 QGroundControl
 *
 * QGroundControl is licensed according to the terms in the file COPYING.md
 *
 ****************************************************************************/

#include "TerrainOverlayMapRenderer.h"
#include "TerrainOverlayGridManager.h"

#include <QVariant>
#include <QVariantMap>
#include <QtCore/qapplicationstatic.h>
#include "QGCLoggingCategory.h"

QGC_LOGGING_CATEGORY(TerrainOverlayMapLog, "qgc.terrainoverlay.maprenderer");

Q_APPLICATION_STATIC(TerrainOverlayMapRenderer, _instance);

TerrainOverlayMapRenderer* TerrainOverlayMapRenderer::instance()
{
    return _instance();
}

void TerrainOverlayMapRenderer::registerQmlTypes()
{
    qmlRegisterUncreatableType<TerrainOverlayMapRenderer>(
        "QGroundControl.TerrainOverlayMapRenderer", 1, 0,
        "TerrainOverlayMapRenderer",
        "Reference only"
    );
}

TerrainOverlayMapRenderer::TerrainOverlayMapRenderer(QObject* parent)
    : QObject(parent)
{
    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Initializing singleton.";
    _connectToManager();
}

void TerrainOverlayMapRenderer::_connectToManager()
{
    auto* manager = TerrainOverlayGridManager::instance();

    connect(manager, &TerrainOverlayGridManager::gridChanged,
            this, &TerrainOverlayMapRenderer::_onGridDataAvailable);

    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Connected to TerrainOverlayGridManager signals.";
}

void TerrainOverlayMapRenderer::_onGridDataAvailable()
{
    auto* manager = TerrainOverlayGridManager::instance();
    QVariantMap grid = manager->grid().toMap();

    int rows = grid.value("rows").toInt();
    int cols = grid.value("cols").toInt();
    qCDebug(TerrainOverlayMapLog) << "[MapRenderer] Received grid with" << rows << "rows x" << cols << "cols.";

    // Placeholder: Here we would prepare data for map rendering.
    // For now, just log that we received it.
}
