import QtQuick
import QtQuick.Controls
import QtLocation
import QtPositioning

import QGroundControl

MapQuickItem {
    id: terrainOverlay

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

    // The custom shader-based source item
    sourceItem: Item {
        id: shaderOverlay
        width: altitudeCanvas.width
        height: altitudeCanvas.height

        // 👇 Hidden Canvas to create grayscale altitude texture
        Canvas {
            id: altitudeCanvas
            width: terrainOverlayRenderer.gridCols
            height: terrainOverlayRenderer.gridRows
            renderTarget: Canvas.FramebufferObject
            visible: false

            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                const altitudes = terrainOverlayRenderer.altitudeGrid;

                // 🔢 Fixed encoding range
                const floorAlt = 200.0;
                const ceilingAlt = 455.0;
                const range = ceilingAlt - floorAlt;

                for (let row = 0; row < height; row++) {
                    for (let col = 0; col < width; col++) {
                        const i = row * width + col;
                        let val = altitudes[i];

                        if (isNaN(val)) {
                            val = floorAlt;
                        }

                        // ⛓️ Clamp to [floor, ceiling]
                        val = Math.max(floorAlt, Math.min(ceilingAlt, val));

                        // 📉 Normalize to 0–1
                        const norm = (val - floorAlt) / range;

                        // 🎨 Encode as grayscale
                        const gray = Math.round(norm * 255);
                        ctx.fillStyle = `rgb(${gray},${gray},${gray})`;
                        ctx.fillRect(col, row, 1, 1);
                    }
                }
            }

            onWidthChanged: requestPaint()
            onHeightChanged: requestPaint()
            onVisibleChanged: requestPaint()

            Connections {
                target: terrainOverlayRenderer
                onGridDataChanged:
                {
                    altitudeCanvas.requestPaint()
                    console.log("Canvas texture:", altitudeCanvas.canvasTexture);
                }
            }
        }

        Rectangle {
            id: borderRect
            width: terrainOverlayRenderer.gridCols
            height: terrainOverlayRenderer.gridRows
            color: "transparent"
            border.color: "white"
            border.width: 0
            visible: true

            ShaderEffect {
                anchors.fill: parent
                opacity: 0.005
                property real gridCols: terrainOverlayRenderer.gridCols
                property real gridRows: terrainOverlayRenderer.gridRows

                property var activeVehicleCoordinate: _activeVehicle ? _activeVehicle.coordinate : QtPositioning.coordinate()
                property real droneLat: activeVehicleCoordinate.latitude
                property real droneLon: activeVehicleCoordinate.longitude
                onActiveVehicleCoordinateChanged: {
                    console.log(`📍 Active Vehicle Coordinate changed: lat=${_activeVehicleCoordinate.latitude.toFixed(6)}, lon=${_activeVehicleCoordinate.longitude.toFixed(6)}, alt=${_activeVehicleCoordinate.altitude.toFixed(2)}`)
                }
                property real minLat: terrainOverlayRenderer.minLat
                property real maxLat: terrainOverlayRenderer.maxLat
                property real minLon: terrainOverlayRenderer.minLon
                property real maxLon: terrainOverlayRenderer.maxLon
                property real overlayZoom: terrainOverlayRenderer.overlayNativeZoomLevel
                property var altitudeTexture: altitudeCanvas

                property real droneX: {
                    // Normalize longitude to grid width
                    const lonSpan = maxLon - minLon
                    if (lonSpan === 0) return 0
                    return (droneLon - minLon) / lonSpan * gridCols
                }

                property real droneY: {
                    // Normalize latitude (note: Y axis usually goes *down*, so reverse)
                    const latSpan = maxLat - minLat
                    if (latSpan === 0) return 0
                    return (maxLat - droneLat) / latSpan * gridRows
                }
                    onDroneXChanged: {
                        console.log("📍 Drone pixel X:", droneX.toFixed(2))
                    }
                    onDroneYChanged: {
                        console.log("📍 Drone pixel Y:", droneY.toFixed(2))
                    }
                property real droneAlt: QGroundControl.multiVehicleManager.activeVehicle ?
                        QGroundControl.multiVehicleManager.activeVehicle.altitudeAMSL.value : 250.0

                vertexShader: "qrc:/shaders/AltitudeColor.vert.qsb"
                fragmentShader: "qrc:/shaders/AltitudeColor.frag.qsb"
                Component.onCompleted: {
                    console.log("ShaderEffect initialized")
                    console.log("Texture size:", altitudeCanvas.width, altitudeCanvas.height)
                    console.log("ShaderEffect bounds:", width, height)
                }
                    onWidthChanged: console.log("ShaderEffect width:", width)
                    onHeightChanged: console.log("ShaderEffect height:", height)
                    onMinLatChanged: console.log("minLat changed to:", minLat)
            }
            }
    }
}
