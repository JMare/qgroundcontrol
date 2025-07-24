import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

MapQuickItem {
    id: terrainOverlay

    property var terrainOverlayRenderer

    z: QGroundControl.zOrderMapItems
    visible: terrainOverlayRenderer.lastUpdateCounter > 0

    coordinate: QtPositioning.coordinate(
        terrainOverlayRenderer.centerLat,
        terrainOverlayRenderer.centerLon
    )

    // ✅ anchor to center of rendered image
    anchorPoint.x: shaderOverlay.width / 2
    anchorPoint.y: shaderOverlay.height / 2

    zoomLevel: terrainOverlayRenderer.overlayNativeZoomLevel

    // ✅ Dynamically resize based on shader content
    width: shaderOverlay.width
    height: shaderOverlay.height

    sourceItem: ShaderEffect {
        id: shaderOverlay
        // Load the grayscale image from provider
        property var heatmap: Image {
            id: heatmapImage
            source: "image://terrainoverlay/terrain?" + terrainOverlayRenderer.lastUpdateCounter
            visible: false
            cache: false

            onStatusChanged: {
                if (status === Image.Ready) {
                    console.log("✅ Heatmap image loaded:", width, "x", height);
                }
            }
        }

        width: heatmapImage.width
        height: heatmapImage.height
        opacity: 0.05

        fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"
        vertexShader: "qrc:/shaders/AltitudeColor.vert.qsb"

        // === UNIFORMS for Shader ===
        property var source: heatmapImage
        property var altitudeTexture: heatmapImage

        property real gridCols: heatmapImage.width
        property real gridRows: heatmapImage.height

        property var activeVehicleCoordinate: _activeVehicle ? _activeVehicle.coordinate : QtPositioning.coordinate()
        property real droneLat: activeVehicleCoordinate.latitude
        property real droneLon: activeVehicleCoordinate.longitude
        property real droneAlt: QGroundControl.multiVehicleManager.activeVehicle ?
            QGroundControl.multiVehicleManager.activeVehicle.altitudeAMSL.value : 250.0

        property real minLat: terrainOverlayRenderer.minLat
        property real maxLat: terrainOverlayRenderer.maxLat
        property real minLon: terrainOverlayRenderer.minLon
        property real maxLon: terrainOverlayRenderer.maxLon

        // Compute pixel coordinates
        property real droneX: {
            const lonSpan = maxLon - minLon;
            return lonSpan > 0 ? (droneLon - minLon) / lonSpan * gridCols : 0;
        }

        property real droneY: {
            const latSpan = maxLat - minLat;
            return latSpan > 0 ? (maxLat - droneLat) / latSpan * gridRows : 0;
        }

        onWidthChanged: console.log("ShaderEffect width:", width)
        onHeightChanged: console.log("ShaderEffect height:", height)
        onDroneXChanged: console.log("📍 Drone pixel X:", droneX.toFixed(2))
        onDroneYChanged: console.log("📍 Drone pixel Y:", droneY.toFixed(2))

        Component.onCompleted: {
            console.log("ShaderEffect initialized");
        }
    }
}
