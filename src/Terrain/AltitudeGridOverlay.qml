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

    anchorPoint.x: shaderOverlay.width / 2
    anchorPoint.y: shaderOverlay.height / 2
    zoomLevel: terrainOverlayRenderer.overlayNativeZoomLevel
    width: shaderOverlay.width
    height: shaderOverlay.height

    sourceItem: ShaderEffect {
        id: shaderOverlay

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

        // === Drones via indexed access ===
        property var vehicle1: QGroundControl.multiVehicleManager.vehicles.count > 0
            ? QGroundControl.multiVehicleManager.vehicles.get(0) : null

        property var vehicle2: QGroundControl.multiVehicleManager.vehicles.count > 1
            ? QGroundControl.multiVehicleManager.vehicles.get(1) : null

        property real droneLat: vehicle1 ? vehicle1.coordinate.latitude : NaN
        property real droneLon: vehicle1 ? vehicle1.coordinate.longitude : NaN
        property real droneAlt: vehicle1 ? vehicle1.altitudeAMSL.value : 250.0

        property real droneLat2: vehicle2 ? vehicle2.coordinate.latitude : NaN
        property real droneLon2: vehicle2 ? vehicle2.coordinate.longitude : NaN
        property real droneAlt2: vehicle2 ? vehicle2.altitudeAMSL.value : 250.0

        // === Terrain bounds
        property real minLat: terrainOverlayRenderer.minLat
        property real maxLat: terrainOverlayRenderer.maxLat
        property real minLon: terrainOverlayRenderer.minLon
        property real maxLon: terrainOverlayRenderer.maxLon

        // === Safe pixel coordinate conversion with lat/lon sanity check
        property real droneX: {
            const lonSpan = maxLon - minLon;
            if (!isFinite(droneLon) || droneLon < -180 || droneLon > 180 || lonSpan <= 0) return NaN;
            return (droneLon - minLon) / lonSpan * gridCols;
        }

        property real droneY: {
            const latSpan = maxLat - minLat;
            if (!isFinite(droneLat) || droneLat < -90 || droneLat > 90 || latSpan <= 0) return NaN;
            return (maxLat - droneLat) / latSpan * gridRows;
        }

        property real droneX2: {
            const lonSpan = maxLon - minLon;
            if (!isFinite(droneLon2) || droneLon2 < -180 || droneLon2 > 180 || lonSpan <= 0) return NaN;
            return (droneLon2 - minLon) / lonSpan * gridCols;
        }

        property real droneY2: {
            const latSpan = maxLat - minLat;
            if (!isFinite(droneLat2) || droneLat2 < -90 || droneLat2 > 90 || latSpan <= 0) return NaN;
            return (maxLat - droneLat2) / latSpan * gridRows;
        }

        // === Debugging logs
        onDroneXChanged: console.log("📍 Drone 1 X:", droneX)
        onDroneYChanged: console.log("📍 Drone 1 Y:", droneY)
        onDroneX2Changed: console.log("📍 Drone 2 X:", droneX2)
        onDroneY2Changed: console.log("📍 Drone 2 Y:", droneY2)

        Component.onCompleted: {
            console.log("🚁 Total vehicles:", QGroundControl.multiVehicleManager.vehicles.count);
        }
    }
}
