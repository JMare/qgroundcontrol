import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

MapQuickItem {
    id: terrainOverlay
    property var terrainOverlayRenderer

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

    sourceItem:
    ShaderEffect {
        id: shaderOverlay

        // 🔍 Load terrain image
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

        fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"
        vertexShader: "qrc:/shaders/AltitudeColor.vert.qsb"

        // === SHADER UNIFORMS ===
        property var source: heatmapImage
        property var altitudeTexture: heatmapImage
        property real gridCols: heatmapImage.width
        property real gridRows: heatmapImage.height

        // === First Drone (Active) ===
        property var activeVehicleCoordinate: _activeVehicle ? _activeVehicle.coordinate : QtPositioning.coordinate()
        property real droneLat: activeVehicleCoordinate.latitude
        property real droneLon: activeVehicleCoordinate.longitude
        property real droneAlt: QGroundControl.multiVehicleManager.activeVehicle ?
            QGroundControl.multiVehicleManager.activeVehicle.altitudeAMSL.value : 250.0

        // === Second Drone (if available) ===
        property var secondVehicle: QGroundControl.multiVehicleManager.vehicles.count > 1 ?
            QGroundControl.multiVehicleManager.vehicles.get(1) : null

        property real droneLat2: secondVehicle ? secondVehicle.coordinate.latitude : 0.0
        property real droneLon2: secondVehicle ? secondVehicle.coordinate.longitude : 0.0
        property real droneAlt2: secondVehicle ? secondVehicle.altitudeAMSL.value : 0.0

        // === Terrain Bounds ===
        property real minLat: terrainOverlayRenderer.minLat
        property real maxLat: terrainOverlayRenderer.maxLat
        property real minLon: terrainOverlayRenderer.minLon
        property real maxLon: terrainOverlayRenderer.maxLon

        // === Coordinate to Pixel Conversion ===
        property real droneX: {
            const lonSpan = maxLon - minLon;
            return lonSpan > 0 ? (droneLon - minLon) / lonSpan * gridCols : 0;
        }

        property real droneY: {
            const latSpan = maxLat - minLat;
            return latSpan > 0 ? (maxLat - droneLat) / latSpan * gridRows : 0;
        }

        property real droneX2: {
            const lonSpan = maxLon - minLon;
            return secondVehicle && lonSpan > 0 ? (droneLon2 - minLon) / lonSpan * gridCols : 0;
        }

        property real droneY2: {
            const latSpan = maxLat - minLat;
            return secondVehicle && latSpan > 0 ? (maxLat - droneLat2) / latSpan * gridRows : 0;
        }

        onDroneXChanged: console.log("📍 Drone 1 X:", droneX.toFixed(2))
        onDroneYChanged: console.log("📍 Drone 1 Y:", droneY.toFixed(2))
        onDroneX2Changed: console.log("📍 Drone 2 X:", droneX2.toFixed(2))
        onDroneY2Changed: console.log("📍 Drone 2 Y:", droneY2.toFixed(2))

        Component.onCompleted: {
            console.log("🚁 Total vehicles:", QGroundControl.multiVehicleManager.vehicles.length);
        }
        onSecondVehicleChanged: {
        if (secondVehicle) {
            console.log("✅ Second drone detected at:", secondVehicle.coordinate);
        } else {
            console.log("❌ No second drone available");
        }
}
    }
}
