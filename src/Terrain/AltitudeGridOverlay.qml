// AltitudeGridOverlay.qml
import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

Item {
    id: terrainOverlay

    // Pass the Map instance in from FlightMap.qml
    property var map

    // Your C++ renderer object (TerrainOverlayMapRenderer)
    property var terrainOverlayRenderer

    // Show only after first update / image is ready-ish
    visible: terrainOverlayRenderer && terrainOverlayRenderer.lastUpdateCounter > 0

    // This Item sits on top of the map viewport
    // (FlightMap.qml's Map will usually size children to the map automatically,
    // but anchors.fill is safer if you wrap this in an Item layer.)
    anchors.fill: parent

    // --- Internal helpers ---
    function _validBounds() {
        if (!terrainOverlayRenderer) return false
        const minLat = terrainOverlayRenderer.minLat
        const maxLat = terrainOverlayRenderer.maxLat
        const minLon = terrainOverlayRenderer.minLon
        const maxLon = terrainOverlayRenderer.maxLon
        return isFinite(minLat) && isFinite(maxLat) && isFinite(minLon) && isFinite(maxLon) &&
               (maxLat > minLat) && (maxLon > minLon)
    }

    function updateGeometry() {
        if (!map || !visible || !_validBounds()) {
            shaderOverlay.visible = false
            return
        }

        // Geo bounds from renderer
        const minLat = terrainOverlayRenderer.minLat
        const maxLat = terrainOverlayRenderer.maxLat
        const minLon = terrainOverlayRenderer.minLon
        const maxLon = terrainOverlayRenderer.maxLon

        // Project top-left and bottom-right into map item pixels
        // NOTE: this is an axis-aligned rectangle in screen space.
        // For north-up 2D maps (no rotation/tilt), this aligns very well.
        const topLeft     = map.fromCoordinate(QtPositioning.coordinate(maxLat, minLon), false)
        const bottomRight = map.fromCoordinate(QtPositioning.coordinate(minLat, maxLon), false)

        const x0 = Math.min(topLeft.x, bottomRight.x)
        const y0 = Math.min(topLeft.y, bottomRight.y)
        const w  = Math.abs(bottomRight.x - topLeft.x)
        const h  = Math.abs(bottomRight.y - topLeft.y)

        // Avoid degenerate geometry
        if (!isFinite(x0) || !isFinite(y0) || !isFinite(w) || !isFinite(h) || w < 1 || h < 1) {
            shaderOverlay.visible = false
            return
        }

        shaderOverlay.x = x0
        shaderOverlay.y = y0
        shaderOverlay.width  = w
        shaderOverlay.height = h
        shaderOverlay.visible = true
    }

    // Recompute placement whenever the map camera moves
    Connections {
        target: map
        function onCenterChanged()    { terrainOverlay.updateGeometry() }
        function onZoomLevelChanged() { terrainOverlay.updateGeometry() }
        function onBearingChanged()   { terrainOverlay.updateGeometry() }
        function onTiltChanged()      { terrainOverlay.updateGeometry() }
        function onFieldOfViewChanged(){ terrainOverlay.updateGeometry() }
        function onVisibleRegionChanged(){ terrainOverlay.updateGeometry() }
        function onWidthChanged()     { terrainOverlay.updateGeometry() }
        function onHeightChanged()    { terrainOverlay.updateGeometry() }
    }

    // Also recompute when renderer updates
    Connections {
        target: terrainOverlayRenderer
        function onLastUpdateCounterChanged() { terrainOverlay.updateGeometry() }
        function onMinLatChanged() { terrainOverlay.updateGeometry() }
        function onMaxLatChanged() { terrainOverlay.updateGeometry() }
        function onMinLonChanged() { terrainOverlay.updateGeometry() }
        function onMaxLonChanged() { terrainOverlay.updateGeometry() }
    }

    Component.onCompleted: updateGeometry()
    onVisibleChanged: updateGeometry()

    ShaderEffect {
        id: shaderOverlay
        visible: false   // enabled by updateGeometry()

        fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"
        vertexShader:   "qrc:/shaders/AltitudeColor.vert.qsb"

        // --- Texture source (unchanged) ---
        property var heatmap: Image {
            id: heatmapImage
            source: "image://terrainoverlay/terrain?" + terrainOverlayRenderer.lastUpdateCounter
            visible: false
            cache: false

            onStatusChanged: {
                if (status === Image.Ready) {
                    // If the image changes size, recalc. If it's constant, harmless.
                    terrainOverlay.updateGeometry()
                }
            }
        }

        // These remain for your shader bindings
        property var source: heatmapImage
        property var altitudeTexture: heatmapImage
        property real gridCols: heatmapImage.width
        property real gridRows: heatmapImage.height

        // --- Drones (unchanged) ---
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

        // --- Terrain bounds (unchanged) ---
        property real minLat: terrainOverlayRenderer.minLat
        property real maxLat: terrainOverlayRenderer.maxLat
        property real minLon: terrainOverlayRenderer.minLon
        property real maxLon: terrainOverlayRenderer.maxLon

        // --- Mapping drone coords into raster pixel space (unchanged) ---
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

        // Optional debug
        // onDroneXChanged: console.log("📍 Drone 1 X:", droneX)
        // onDroneYChanged: console.log("📍 Drone 1 Y:", droneY)
        // onDroneX2Changed: console.log("📍 Drone 2 X:", droneX2)
        // onDroneY2Changed: console.log("📍 Drone 2 Y:", droneY2)
    }
}
