import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

MapQuickItem {
    id: terrainOverlay

    // 👇 This must be passed in from the parent
    property var terrainOverlayRenderer

    z: QGroundControl.zOrderMapItems

    coordinate: QtPositioning.coordinate(
        terrainOverlayRenderer.maxLat,
        terrainOverlayRenderer.minLon
    )

    anchorPoint.x: 0
    anchorPoint.y: 0

    zoomLevel: terrainOverlayRenderer.overlayNativeZoomLevel
    visible: terrainOverlayRenderer.lastUpdateCounter > 0

    sourceItem: Image {
        id: heatmapImage
        source: "image://terrainoverlay/heatmap?" + terrainOverlayRenderer.lastUpdateCounter
        opacity: 0.5
        fillMode: Image.Stretch
    }
}
