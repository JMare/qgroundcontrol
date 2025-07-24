find_package(Qt6 REQUIRED COMPONENTS Core Qml ShaderTools)

find_package(GDAL REQUIRED)

qt_add_library(TerrainModule STATIC)

qt_add_qml_module(TerrainModule
    URI QGroundControl.Terrain
    VERSION 1.0
    RESOURCE_PREFIX /qml
    QML_FILES
        AltitudeGridOverlay.qml
)

qt6_add_shaders(TerrainModule "terrain_shaders"
    PREFIX
        "/shaders"
    FILES
        AltitudeColor.vert
        AltitudeColor.frag
)

target_sources(${CMAKE_PROJECT_NAME}
    PRIVATE
        Providers/TerrainQueryCopernicus.cc
        Providers/TerrainQueryCopernicus.h
        Providers/TerrainTileCopernicus.cc
        Providers/TerrainTileCopernicus.h
        TerrainQuery.cc
        TerrainQuery.h
        TerrainQueryInterface.cc
        TerrainQueryInterface.h
        TerrainTile.cc
        TerrainTile.h
        TerrainTileManager.cc
        TerrainTileManager.h
        TerrainOverlayGridManager.cc
        TerrainOverlayGridManager.h
        TerrainGridCell.h
        TerrainOverlayGridModel.cc
        TerrainOverlayGridModel.h
        TerrainOverlayMapRenderer.cc
        TerrainOverlayMapRenderer.h
        HeatmapImageProvider.cc
        HeatmapImageProvider.h
)

target_include_directories(${CMAKE_PROJECT_NAME}
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/Providers
)

target_link_libraries(${CMAKE_PROJECT_NAME}
    PRIVATE
        GDAL::GDAL
)
