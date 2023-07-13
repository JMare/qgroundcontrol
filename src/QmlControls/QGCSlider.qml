/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

import QtQuick                  2.3
import QtQuick.Controls         1.2
import QtQuick.Controls.Styles  1.4
import QtQuick.Controls.Private 1.0

import QGroundControl.Palette       1.0
import QGroundControl.ScreenTools   1.0

Slider {
    id:             _root
    implicitHeight: ScreenTools.implicitSliderHeight

    // Value indicator starts display from zero instead of min value
    property bool zeroCentered: false
    property double snapValue: 0
    property bool displayValue: false
    property bool indicatorBarVisible: true

    stepSize: snapValue

    tickmarksEnabled: true

    Repeater {
     id: ticksrepeater
     //model: 20
     anchors.fill: parent
     model: stepSize > 0 ? 1 + (maximumValue - minimumValue) / stepSize : 0
     Rectangle {
         property real _hw: Math.round(_root.implicitHeight / 2)*2

         color: "white"
         width: 10 ; height: 2
         anchors.left: parent.horizontalCenter
         anchors.leftMargin: _hw
         //x: Math.round(ScreenTools.defaultFontPixelHeight * 0.3)
         //y: repeater.height
         y: -1 + _hw / 2 + index * ((ticksrepeater.height - _hw) / (ticksrepeater.count-1))
         //x: styleData.handleWidth / 2 + index * ((repeater.width - styleData.handleWidth) / (repeater.count-1))
     }
    }

    /*Rectangle {
        color: "red"
        width: 5 ; height: 10
        x: parent.width
        //x: styleData.handleWidth / 2 + index * ((repeater.width - styleData.handleWidth) / (repeater.count-1))
    }*/

    style: SliderStyle {


        groove: Item {
            anchors.verticalCenter: parent.verticalCenter
            implicitWidth:          Math.round(ScreenTools.defaultFontPixelHeight * 4.5)
            implicitHeight:         Math.round(ScreenTools.defaultFontPixelHeight * 0.3)

            Rectangle {
                radius:         height / 2
                anchors.fill:   parent
                color:          qgcPal.button
                border.width:   1
                border.color:   qgcPal.buttonText
            }

            Item {
                id:     indicatorBar
                clip:   true
                visible: indicatorBarVisible
                x:      _root.zeroCentered ? zeroCenteredIndicatorStart : 0
                width:  _root.zeroCentered ? centerIndicatorWidth : styleData.handlePosition
                height: parent.height

                property real zeroValuePosition:            (Math.abs(control.minimumValue) / (control.maximumValue - control.minimumValue)) * parent.width
                property real zeroCenteredIndicatorStart:   Math.min(styleData.handlePosition, zeroValuePosition)
                property real zeroCenteredIndicatorStop:    Math.max(styleData.handlePosition, zeroValuePosition)
                property real centerIndicatorWidth:         zeroCenteredIndicatorStop - zeroCenteredIndicatorStart

                Rectangle {
                    anchors.fill:   parent
                    color:          qgcPal.colorBlue
                    border.color:   Qt.darker(color, 1.2)
                    radius:         height/2
                }
            }

            Rectangle {
                radius:         height / 2
                anchors.left:   parent.left
                anchors.bottom:  parent.bottom
                anchors.top:    parent.top
                width:  parent.width/10
                color: "red"
            }


        }

        handle: Rectangle {
            anchors.centerIn: parent
            color:          qgcPal.button
            border.color:   qgcPal.buttonText
            border.width:   1
            implicitWidth:  _radius * 2
            implicitHeight: _radius * 2
            radius:         _radius

            property real _radius: Math.round(_root.implicitHeight / 2)

            Label {
                text:               _root.value.toFixed( _root.maximumValue <= 1 ? 1 : 0)
                visible:            _root.displayValue
                anchors.centerIn:   parent
                font.family:        ScreenTools.normalFontFamily
                font.pointSize:     ScreenTools.smallFontPointSize
                color:              qgcPal.buttonText
            }
        }


   /*     tickmarks: Repeater {
            id: repeater
            model: 5
            //model: control.stepSize > 0 ? 1 + (control.maximumValue - control.minimumValue) / control.stepSize : 0
            Rectangle {
                color: "red"
                width: 30 ; height: 50
                y: repeater.height
                x: styleData.handleWidth / 2 + index * ((repeater.width - styleData.handleWidth) / (repeater.count-1))
            }
        } */
    }
}
