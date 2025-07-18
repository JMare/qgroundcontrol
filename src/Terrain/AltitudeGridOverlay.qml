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
            visible: false

            onPaint: {
                const ctx = getContext("2d");
                ctx.clearRect(0, 0, width, height);

                const altitudes = terrainOverlayRenderer.altitudeGrid;
                let minAlt = Infinity;
                let maxAlt = -Infinity;

                for (let i = 0; i < altitudes.length; i++) {
                    const val = altitudes[i];
                    if (!isNaN(val)) {
                        minAlt = Math.min(minAlt, val);
                        maxAlt = Math.max(maxAlt, val);
                    }
                }

                const range = maxAlt - minAlt || 1;

                for (let row = 0; row < height; row++) {
                    for (let col = 0; col < width; col++) {
                        const i = row * width + col;
                        let val = altitudes[i];
                        if (isNaN(val)) val = minAlt;
                        const norm = Math.max(0, Math.min(1, (val - minAlt) / range));
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
                onGridDataChanged: altitudeCanvas.requestPaint()
            }
        }

        Rectangle {
            id: borderRect
            width: terrainOverlayRenderer.gridCols
            height: terrainOverlayRenderer.gridRows
            color: "transparent"
            border.color: "white"
            border.width: 1
            visible: true

            Text {
                anchors.top: parent.top
                anchors.left: parent.left
                text: `W: ${parent.width}, H: ${parent.height}`
                color: "white"
                font.pixelSize: 14
            }
            ShaderEffect {
                anchors.fill: parent

                property real minLat: terrainOverlayRenderer.minLat
                property real maxLat: terrainOverlayRenderer.maxLat
                property real minLon: terrainOverlayRenderer.minLon
                property real maxLon: terrainOverlayRenderer.maxLon
                property real overlayZoom: terrainOverlayRenderer.overlayNativeZoomLevel
                property var altitudeTexture: altitudeCanvas.canvasTexture

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
