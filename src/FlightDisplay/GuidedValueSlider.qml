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

import QGroundControl               1.0
import QGroundControl.Controls      1.0
import QGroundControl.Vehicle       1.0
import QGroundControl.Palette       1.0

Rectangle {
    id:                 _root

    property var  _flyViewSettings:     QGroundControl.settingsManager.flyViewSettings
    property real _vehicleAltitude:     _activeVehicle ? _activeVehicle.altitudeRelative.rawValue : 0
    property bool _fixedWing:           _activeVehicle ? _activeVehicle.fixedWing : false
    property real _sliderMaxVal:        _flyViewSettings ? _flyViewSettings.guidedMaximumAltitude.rawValue : 0
    property real _sliderMinVal:        _flyViewSettings ? _flyViewSettings.guidedMinimumAltitude.rawValue : 0
    property real _sliderCenterValue:   _vehicleAltitude
    property string _displayText:       ""
    property bool _altSlider:         true

    property var sliderValue : valueSlider.value

    onSliderValueChanged: {
        valueField.updateFunction(sliderValue)
    }

    on_SliderCenterValueChanged: {
        valueField.updateFunction(sliderValue)
    }

    function setValue(val) {
        valueSlider.value = val
        valueField.updateFunction(valueSlider.value)
    }

    function setMinVal(min_val) {
        _sliderMinVal = min_val
    }

    function setMaxVal(max_val) {
        _sliderMaxVal = max_val
    }

    function setCenterValue(center) {
        _sliderCenterValue = center
    }

    function setDisplayText(text) {
        _displayText = text
    }

    function getOutputValue() {
        return valueField.newValue - _vehicleAltitude
    }

    Column {
        id:                 headerColumn
        anchors.margins:    _margins
        anchors.top:        parent.top
        anchors.left:       parent.left
        anchors.right:      parent.right

        QGCLabel {
            anchors.left:           parent.left
            anchors.right:          parent.right
            wrapMode:               Text.WordWrap
            horizontalAlignment:    Text.AlignHCenter
            text:                   _displayText
        }

        QGCLabel {
            id:                         valueField
            anchors.horizontalCenter:   parent.horizontalCenter
            text:                       newValueAppUnits + " " + QGroundControl.unitsConversion.appSettingsHorizontalDistanceUnitsString

            property real   newValue
            property string newValueAppUnits

            property var updateFunction : valueField.updateLinear

            function updateLinear(value) {
                newValue = valueSlider.value
                newValueAppUnits = QGroundControl.unitsConversion.metersToAppSettingsHorizontalDistanceUnits(newValue).toFixed(1)
            }
        }
    }

    QGCSlider {
        id:                 valueSlider
        anchors.margins:    _margins
        anchors.top:        headerColumn.bottom
        anchors.bottom:     parent.bottom
        anchors.left:       parent.left
        anchors.right:      parent.right
        orientation:        Qt.Vertical
        minimumValue:       _sliderMinVal
        maximumValue:       _sliderMaxVal
        zeroCentered:       false
        rotation:           180

        // We want slide up to be positive values
        transform: Rotation {
            origin.x:   valueSlider.width  / 2
            origin.y:   valueSlider.height / 2
            angle:      180
        }
    }
}
