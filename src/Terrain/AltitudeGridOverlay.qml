// AltitudeGridOverlay.qml
import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl
import QGroundControl.TerrainOverlayMapRenderer 1.0

Item {
    id: terrainOverlay

    // Pass the Map instance in from FlightMap.qml
    property var map

    // Your C++ renderer object (TerrainOverlayMapRenderer)
    property var terrainOverlayRenderer

    // Manual override for debug mode (0 means "auto")
    // 0 = auto (state-driven), 1 = altitude grayscale, 2 = NaN mask
    property real userDebugMode: 0.0

    // Auto-driven debugMode when userDebugMode==0:
    // - if not ready -> show altitude grayscale (1)
    // - if ready     -> normal RF (0)
    readonly property real effectiveDebugMode: {
        if (!terrainOverlayRenderer) return 1.0
        if (userDebugMode !== 0.0) return userDebugMode
        return (terrainOverlayRenderer.state === TerrainOverlayMapRenderer.Ready) ? 0.0 : 1.0
    }

    // Keep item alive once we have a renderer. We don't require an image yet.
    // Geometry logic will hide shaderOverlay if bounds aren't valid.
    visible: !!terrainOverlayRenderer

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
        const topLeft     = map.fromCoordinate(QtPositioning.coordinate(maxLat, minLon), false)
        const bottomRight = map.fromCoordinate(QtPositioning.coordinate(minLat, maxLon), false)

        const x0 = Math.min(topLeft.x, bottomRight.x)
        const y0 = Math.min(topLeft.y, bottomRight.y)
        const w  = Math.abs(bottomRight.x - topLeft.x)
        const h  = Math.abs(bottomRight.y - topLeft.y)

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
        function onBoundsChanged()            { terrainOverlay.updateGeometry() }   // <— preferred
        function onGridDataChanged()          { terrainOverlay.updateGeometry() }   // <— optional
        function onStateChanged()             { terrainOverlay.updateGeometry() }   // <— if you add state
    }

    Component.onCompleted: updateGeometry()
    onVisibleChanged: updateGeometry()

    ShaderEffect {
        id: shaderOverlay
        visible: false   // enabled by updateGeometry()

        fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"   // your RadioLOS_Attitude.qsb
        vertexShader:   "qrc:/shaders/AltitudeColor.vert.qsb"

        // --- Texture source ---
        // Keep this Image alive even if it isn't ready yet.
        // It will update whenever lastUpdateCounter increments.
        property var heatmap: Image {
            id: heatmapImage
            source: terrainOverlayRenderer
                ? ("image://terrainoverlay/terrain?" + terrainOverlayRenderer.lastUpdateCounter)
                : ""
            visible: false
            cache: false

            onStatusChanged: {
                if (status === Image.Ready) {
                    console.log("✅ Heatmap ready:", width, "x", height,
                                "terrainMin/Max:", shaderOverlay.terrainMinMeters, shaderOverlay.terrainMaxMeters,
                                "state:", terrainOverlayRenderer ? terrainOverlayRenderer.stateName : "n/a")
                    terrainOverlay.updateGeometry()
                }
            }
        }

        // Shader bindings
        property var source: heatmapImage
        property var altitudeTexture: heatmapImage

        // IMPORTANT: drive these from renderer (not image), so uniforms are stable
        property real gridCols: terrainOverlayRenderer ? terrainOverlayRenderer.gridCols : 0.0
        property real gridRows: terrainOverlayRenderer ? terrainOverlayRenderer.gridRows : 0.0

        // Decode range for altitudeTexture (meters AMSL)
        property real terrainMinMeters: terrainOverlayRenderer ? terrainOverlayRenderer.terrainMinMeters : 0.0
        property real terrainMaxMeters: terrainOverlayRenderer ? terrainOverlayRenderer.terrainMaxMeters : 1.0

        // --- Single drone ---
        property var vehicle1: QGroundControl.multiVehicleManager.vehicles.count > 0
            ? QGroundControl.multiVehicleManager.vehicles.get(0) : null

        property real droneLat: vehicle1 ? vehicle1.coordinate.latitude : NaN
        property real droneLon: vehicle1 ? vehicle1.coordinate.longitude : NaN
        property real droneAlt: vehicle1 ? vehicle1.altitudeAMSL.value : 250.0

        // --- Terrain bounds ---
        property real minLat: terrainOverlayRenderer ? terrainOverlayRenderer.minLat : 0.0
        property real maxLat: terrainOverlayRenderer ? terrainOverlayRenderer.maxLat : 0.0
        property real minLon: terrainOverlayRenderer ? terrainOverlayRenderer.minLon : 0.0
        property real maxLon: terrainOverlayRenderer ? terrainOverlayRenderer.maxLon : 0.0

        // --- Mapping drone coords into raster pixel space ---
        property real droneX: {
            const lonSpan = maxLon - minLon;
            if (!isFinite(droneLon) || droneLon < -180 || droneLon > 180 || lonSpan <= 0 || gridCols <= 1) return NaN;
            return (droneLon - minLon) / lonSpan * gridCols;
        }

        property real droneY: {
            const latSpan = maxLat - minLat;
            if (!isFinite(droneLat) || droneLat < -90 || droneLat > 90 || latSpan <= 0 || gridRows <= 1) return NaN;
            return (maxLat - droneLat) / latSpan * gridRows;
        }

        // meters per pixel based on bounds and grid size
        property real metersPerPixelX: {
            if (gridCols <= 1) return 1.0
            const lat0 = (minLat + maxLat) * 0.5
            const mPerDegLon = 111320.0 * Math.cos(lat0 * Math.PI/180.0)
            return (maxLon - minLon) * mPerDegLon / gridCols
        }

        property real metersPerPixelY: {
            if (gridRows <= 1) return 1.0
            const mPerDegLat = 111320.0
            return (maxLat - minLat) * mPerDegLat / gridRows
        }

        // RF params
        property real freqMHz: 1800.0
        property real minDbm: -120.0
        property real maxDbm: -80.0
        property real systemLoss_dB: 30.0

        // Attitude inputs (degrees)
        property real rollDeg:  vehicle1 && vehicle1.roll  ? vehicle1.roll.value  : 0.0
        property real pitchDeg: vehicle1 && vehicle1.pitch ? vehicle1.pitch.value : 0.0
        property real yawDeg:   vehicle1 && vehicle1.heading ? vehicle1.heading.value : 0.0

        function deg2rad(d) { return d * Math.PI / 180.0; }

        function rotateBodyAxisToENU(vx, vy, vz, rollR, pitchR, yawR) {
            const cr = Math.cos(rollR),  sr = Math.sin(rollR);
            const cp = Math.cos(pitchR), sp = Math.sin(pitchR);
            const cy = Math.cos(yawR),   sy = Math.sin(yawR);

            const x1 = vx;
            const y1 = cr * vy - sr * vz;
            const z1 = sr * vy + cr * vz;

            const x2 =  cp * x1 + sp * z1;
            const y2 =  y1;
            const z2 = -sp * x1 + cp * z1;

            const x3 = cy * x2 - sy * y2;
            const y3 = sy * x2 + cy * y2;
            const z3 = z2;

            return Qt.vector3d(x3, y3, z3);
        }

        property vector3d antennaAxisBody: Qt.vector3d(0, 0, 1)

        property vector3d antennaAxisENU: {
            const r = deg2rad(rollDeg);
            const p = deg2rad(pitchDeg);
            const y = deg2rad(yawDeg);
            return rotateBodyAxisToENU(antennaAxisBody.x, antennaAxisBody.y, antennaAxisBody.z, r, p, y);
        }

        property real antAxisX: antennaAxisENU.x
        property real antAxisY: antennaAxisENU.y
        property real antAxisZ: antennaAxisENU.z

        // Debug toggle:
        // 0 = normal RF, 1 = show altitude grayscale, 2 = show NaN mask
        // FINAL value is state-driven unless user overrides.
        property real debugMode: terrainOverlay.effectiveDebugMode

        // Helpful range expansion for grayscale view (meters). If 0, uses terrainMin/Max.
        property real debugMinAlt: 0.0
        property real debugMaxAlt: 0.0

        Keys.onPressed: (e) => {
            if (e.key === Qt.Key_D) {
                // Cycle manual override: 0(auto) -> 1 -> 2 -> 0(auto)
                terrainOverlay.userDebugMode = (terrainOverlay.userDebugMode + 1) % 3
                console.log("debug override now:", terrainOverlay.userDebugMode,
                            "effective:", terrainOverlay.effectiveDebugMode,
                            "state:", terrainOverlayRenderer ? terrainOverlayRenderer.stateName : "n/a")
            }
        }
    }
}
