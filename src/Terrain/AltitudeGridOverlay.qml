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

        // Point to the new compiled shaders
        fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"
        vertexShader:   "qrc:/shaders/AltitudeColor.vert.qsb"   // can also reuse AltitudeColor.vert.qsb if unchanged

        // --- Texture source (unchanged) ---
        property var heatmap: Image {
            id: heatmapImage
            source: "image://terrainoverlay/terrain?" + terrainOverlayRenderer.lastUpdateCounter
            visible: false
            cache: false

            onStatusChanged: {
                if (status === Image.Ready) {
                    // If the image changes size, recalc overlay placement
                    terrainOverlay.updateGeometry()
                }
            }
        }

        // These remain for shader bindings
        property var source: heatmapImage
        property var altitudeTexture: heatmapImage
        property real gridCols: heatmapImage.width
        property real gridRows: heatmapImage.height

        // --- Single drone (transmitter) ---
        property var vehicle1: QGroundControl.multiVehicleManager.vehicles.count > 0
            ? QGroundControl.multiVehicleManager.vehicles.get(0) : null

        property real droneLat: vehicle1 ? vehicle1.coordinate.latitude : NaN
        property real droneLon: vehicle1 ? vehicle1.coordinate.longitude : NaN
        property real droneAlt: vehicle1 ? vehicle1.altitudeAMSL.value : 250.0

        // --- Terrain bounds (unchanged) ---
        property real minLat: terrainOverlayRenderer.minLat
        property real maxLat: terrainOverlayRenderer.maxLat
        property real minLon: terrainOverlayRenderer.minLon
        property real maxLon: terrainOverlayRenderer.maxLon

        // --- Mapping drone coords into raster pixel space ---
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

        property real metersPerPixelX: {
          const lat0 = (minLat + maxLat) * 0.5
          const mPerDegLon = 111320.0 * Math.cos(lat0 * Math.PI/180.0)
          return (maxLon - minLon) * mPerDegLon / gridCols
        }

        property real metersPerPixelY: {
          const mPerDegLat = 111320.0
          return (maxLat - minLat) * mPerDegLat / gridRows
        }


        // --- RF tuning knobs (new) ---
        // 1W as a relative scalar for now; later we’ll do proper dBm+FSPL
        property real txPower: 0.0001

        property real freqMHz: 1800.0    // LTE typical: 700, 850, 900, 1800, 2100, 2600
        property real minDbm: -120.0
        property real maxDbm: -80.0
        // Safety: if the drone is not valid, hide (prevents NaN from nuking the shader)
        onDroneXChanged: {
            if (!isFinite(droneX) || !isFinite(droneY) || !isFinite(droneAlt)) {
                // Let the outer updateGeometry decide visibility, but avoid spamming logs
            }
        }
    }
}
