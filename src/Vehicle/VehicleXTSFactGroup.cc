/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#include "VehicleXTSFactGroup.h"
#include "Vehicle.h"

const char* VehicleXTSFactGroup::_apuTemperatureFactName =      "apuemperature";
const char* VehicleXTSFactGroup::_apuHVCurrentFactName =      "apuhvcurrent";


VehicleXTSFactGroup::VehicleXTSFactGroup(QObject* parent)
    : FactGroup(1000, ":/json/Vehicle/XTSFact.json", parent)
    , _apuTemperatureFact    (0, _apuTemperatureFactName,     FactMetaData::valueTypeDouble)
    , _apuHVCurrentFact    (0, _apuHVCurrentFactName,     FactMetaData::valueTypeDouble)
{
    _addFact(&_apuTemperatureFact,       _apuTemperatureFactName);
    _addFact(&_apuHVCurrentFact,       _apuHVCurrentFactName);

    // Start out as not available "--.--"
    _apuTemperatureFact.setRawValue      (qQNaN());
    _apuHVCurrentFact.setRawValue      (qQNaN());

}

void VehicleXTSFactGroup::handleMessage(Vehicle* /* vehicle */, mavlink_message_t& message)
{
    switch (message.msgid) {
    case MAVLINK_MSG_ID_DATA16:
        if(message.sysid==1 && message.compid==5)
            _handleData16(message);
        break;
    default:
        break;
    }
}

void VehicleXTSFactGroup::_handleData16(mavlink_message_t& message)
{
    mavlink_data16_t data16;
    mavlink_msg_data16_decode(&message, &data16);
    BCMData_t tmpdata;
    tmpdata.bus_id = data16.type;
    tmpdata.status_word = data16.data[0];
    tmpdata.temp = data16.data[2];
    tmpdata.vin = data16.data[4];
    tmpdata.vout = data16.data[6];
    tmpdata.iin = data16.data[8];
    tmpdata.iout = data16.data[10];
    tmpdata.pout = data16.data[12];

    unsigned int bus_num;

    switch(data16.type){
    case 0x50:
        bus_num = 0;
        break;
    case 0x51:
        bus_num = 1;
        break;
    case 0x52:
        bus_num = 2;
        break;
    default:
        return;
        break;
    }

    bcmdata[bus_num] = tmpdata;
    bcmgotvalues[bus_num] = true;

    if(bcmgotvalues[0] && bcmgotvalues[1] && bcmgotvalues[2])
    {
        _handleBCMTelemetry();
    }
    //aputemperature()->setRawValue(45);
    //apuhvcurrent()->setRawValue(5);
    //_setTelemetryAvailable(true);
}

void VehicleXTSFactGroup::_handleBCMTelemetry(void)
{
    aputemperature()->setRawValue(bcmdata[0].temp);
    for(int i = 0; i < 3; i++)
        bcmgotvalues[i] = false;
}
