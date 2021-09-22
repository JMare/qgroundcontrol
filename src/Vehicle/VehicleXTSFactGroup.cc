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
#include <QString>

const char* VehicleXTSFactGroup::_hvm0StatusFactName =      "hvm0status";
const char* VehicleXTSFactGroup::_hvm1StatusFactName =      "hvm1status";
const char* VehicleXTSFactGroup::_hvm2StatusFactName =      "hvm2status";
const char* VehicleXTSFactGroup::_hvm0TempFactName =      "hvm0temp";
const char* VehicleXTSFactGroup::_hvm1TempFactName =      "hvm1temp";
const char* VehicleXTSFactGroup::_hvm2TempFactName =      "hvm2temp";
const char* VehicleXTSFactGroup::_hvm0PowerFactName =      "hvm0power";
const char* VehicleXTSFactGroup::_hvm1PowerFactName =      "hvm1power";
const char* VehicleXTSFactGroup::_hvm2PowerFactName =      "hvm2power";

VehicleXTSFactGroup::VehicleXTSFactGroup(QObject* parent)
    : FactGroup(1000, ":/json/Vehicle/XTSFact.json", parent)
    , _hvm0StatusFact    (0, _hvm0StatusFactName,     FactMetaData::valueTypeString)
    , _hvm1StatusFact    (0, _hvm1StatusFactName,     FactMetaData::valueTypeString)
    , _hvm2StatusFact    (0, _hvm2StatusFactName,     FactMetaData::valueTypeString)
    , _hvm0TempFact    (0, _hvm0TempFactName,     FactMetaData::valueTypeUint16)
    , _hvm1TempFact    (0, _hvm1TempFactName,     FactMetaData::valueTypeUint16)
    , _hvm2TempFact    (0, _hvm2TempFactName,     FactMetaData::valueTypeUint16)
    , _hvm0PowerFact    (0, _hvm0PowerFactName,     FactMetaData::valueTypeUint16)
    , _hvm1PowerFact    (0, _hvm1PowerFactName,     FactMetaData::valueTypeUint16)
    , _hvm2PowerFact    (0, _hvm2PowerFactName,     FactMetaData::valueTypeUint16)
{
    _addFact(&_hvm0StatusFact,       _hvm0StatusFactName);
    _addFact(&_hvm1StatusFact,       _hvm1StatusFactName);
    _addFact(&_hvm2StatusFact,       _hvm2StatusFactName);
    _addFact(&_hvm0TempFact,       _hvm0TempFactName);
    _addFact(&_hvm1TempFact,       _hvm1TempFactName);
    _addFact(&_hvm2TempFact,       _hvm2TempFactName);
    _addFact(&_hvm0PowerFact,       _hvm0PowerFactName);
    _addFact(&_hvm1PowerFact,       _hvm1PowerFactName);
    _addFact(&_hvm2PowerFact,       _hvm2PowerFactName);

    // Start out as not available "--.--"
    _hvm0StatusFact.setRawValue      ("No Data");
    _hvm1StatusFact.setRawValue      ("No Data");
    _hvm2StatusFact.setRawValue      ("No Data");
    _hvm0TempFact.setRawValue      (0);
    _hvm1TempFact.setRawValue      (0);
    _hvm2TempFact.setRawValue      (0);
    _hvm0PowerFact.setRawValue      (0);
    _hvm1PowerFact.setRawValue      (0);
    _hvm2PowerFact.setRawValue      (0);
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
    if(data16.len != 16)
        return;

    /* Check for APU ready placeholder messages */
    if(data16.type == 0x99)
    {
        _handleReady();
        return;
    }

    /* Ensure our message has a valid busid */
    if(data16.type != 0x50 && data16.type != 0x51 && data16.type != 0x52)
        return;

    /* If we got this far we have a valid hvm telemetry message to unpack */
    hvmData_t tmpdata;
    tmpdata.bus_id = data16.type;
    tmpdata.status_word = data16.data[0] | (data16.data[1] << 8);
    tmpdata.temp = data16.data[2] | (data16.data[3] << 8);
    tmpdata.vin = data16.data[4] | (data16.data[5] << 8);
    tmpdata.vout = data16.data[6] | (data16.data[7] << 8);
    tmpdata.iin = data16.data[8] | (data16.data[9] << 8);
    tmpdata.iout = data16.data[10] | (data16.data[10] << 8);
    tmpdata.pout = data16.data[12] | (data16.data[13] << 8);

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

    _setStatus(bus_num,tmpdata.status_word);
    getTempFact(bus_num)->setRawValue(tmpdata.temp);
    getPowerFact(bus_num)->setRawValue(tmpdata.pout);

    _setTelemetryAvailable(true);

}

void VehicleXTSFactGroup::_handleReady(void)
{
    QString status = QString("Ready");
    getStatusFact(0)->setRawValue(status);
    getStatusFact(1)->setRawValue(status);
    getStatusFact(2)->setRawValue(status);
    _setTelemetryAvailable(true);
}

void VehicleXTSFactGroup::_setStatus(unsigned int busnum, uint16_t status_word)
{
    if((status_word & HVM_ERROR_BM) != 0)
    {
        QString status = QString("Fail 0x%1").arg(status_word,4,16,QLatin1Char('0'));
        getStatusFact(busnum)->setRawValue(status);
    }
    else if((status_word & HVM_OFF_BM) != 0)
    {
        QString status = QString("Off 0x%1").arg(status_word,4,16,QLatin1Char('0'));
        getStatusFact(busnum)->setRawValue(status);
    }
    else
    {
        QString status = QString("On 0x%1").arg(status_word,4,16,QLatin1Char('0'));
        getStatusFact(busnum)->setRawValue(status);
    }
}

Fact* VehicleXTSFactGroup::getStatusFact(unsigned int hvmnum)
{
    if(hvmnum == 0)
        return &_hvm0StatusFact;
    else if(hvmnum == 1)
        return &_hvm1StatusFact;
    else
        return &_hvm2StatusFact;
}

Fact* VehicleXTSFactGroup::getPowerFact(unsigned int hvmnum)
{
    if(hvmnum == 0)
        return &_hvm0PowerFact;
    else if(hvmnum == 1)
        return &_hvm1PowerFact;
    else
        return &_hvm2PowerFact;
}

Fact* VehicleXTSFactGroup::getTempFact(unsigned int hvmnum)
{
    if(hvmnum == 0)
        return &_hvm0TempFact;
    else if(hvmnum == 1)
        return &_hvm1TempFact;
    else
        return &_hvm2TempFact;
}
