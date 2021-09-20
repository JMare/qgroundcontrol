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

const char* VehicleXTSFactGroup::_bcm0StatusFactName =      "bcm0status";
const char* VehicleXTSFactGroup::_bcm1StatusFactName =      "bcm1status";
const char* VehicleXTSFactGroup::_bcm2StatusFactName =      "bcm2status";
const char* VehicleXTSFactGroup::_bcm0TempFactName =      "bcm0temp";
const char* VehicleXTSFactGroup::_bcm1TempFactName =      "bcm1temp";
const char* VehicleXTSFactGroup::_bcm2TempFactName =      "bcm2temp";
const char* VehicleXTSFactGroup::_bcm0PowerFactName =      "bcm0power";
const char* VehicleXTSFactGroup::_bcm1PowerFactName =      "bcm1power";
const char* VehicleXTSFactGroup::_bcm2PowerFactName =      "bcm2power";

VehicleXTSFactGroup::VehicleXTSFactGroup(QObject* parent)
    : FactGroup(1000, ":/json/Vehicle/XTSFact.json", parent)
    , _bcm0StatusFact    (0, _bcm0StatusFactName,     FactMetaData::valueTypeString)
    , _bcm1StatusFact    (0, _bcm1StatusFactName,     FactMetaData::valueTypeString)
    , _bcm2StatusFact    (0, _bcm2StatusFactName,     FactMetaData::valueTypeString)
    , _bcm0TempFact    (0, _bcm0TempFactName,     FactMetaData::valueTypeUint16)
    , _bcm1TempFact    (0, _bcm1TempFactName,     FactMetaData::valueTypeUint16)
    , _bcm2TempFact    (0, _bcm2TempFactName,     FactMetaData::valueTypeUint16)
    , _bcm0PowerFact    (0, _bcm0PowerFactName,     FactMetaData::valueTypeUint16)
    , _bcm1PowerFact    (0, _bcm1PowerFactName,     FactMetaData::valueTypeUint16)
    , _bcm2PowerFact    (0, _bcm2PowerFactName,     FactMetaData::valueTypeUint16)
{
    _addFact(&_bcm0StatusFact,       _bcm0StatusFactName);
    _addFact(&_bcm1StatusFact,       _bcm1StatusFactName);
    _addFact(&_bcm2StatusFact,       _bcm2StatusFactName);
    _addFact(&_bcm0TempFact,       _bcm0TempFactName);
    _addFact(&_bcm1TempFact,       _bcm1TempFactName);
    _addFact(&_bcm2TempFact,       _bcm2TempFactName);
    _addFact(&_bcm0PowerFact,       _bcm0PowerFactName);
    _addFact(&_bcm1PowerFact,       _bcm1PowerFactName);
    _addFact(&_bcm2PowerFact,       _bcm2PowerFactName);

    // Start out as not available "--.--"
    _bcm0StatusFact.setRawValue      ("No Data");
    _bcm1StatusFact.setRawValue      ("No Data");
    _bcm2StatusFact.setRawValue      ("No Data");
    _bcm0TempFact.setRawValue      (0);
    _bcm1TempFact.setRawValue      (0);
    _bcm2TempFact.setRawValue      (0);
    _bcm0PowerFact.setRawValue      (0);
    _bcm1PowerFact.setRawValue      (0);
    _bcm2PowerFact.setRawValue      (0);
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

    /* If we got this far we have a valid BCM telemetry message to unpack */
    BCMData_t tmpdata;
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
    if((status_word & BCM_ERROR_BM) != 0)
    {
        QString status = QString("Fail 0x%1").arg(status_word,4,16,QLatin1Char('0'));
        getStatusFact(busnum)->setRawValue(status);
    }
    else
    {
        QString status = QString("Ok 0x%1").arg(status_word,4,16,QLatin1Char('0'));
        getStatusFact(busnum)->setRawValue(status);
    }
}

Fact* VehicleXTSFactGroup::getStatusFact(unsigned int bcmnum)
{
    if(bcmnum == 0)
        return &_bcm0StatusFact;
    else if(bcmnum == 1)
        return &_bcm1StatusFact;
    else
        return &_bcm2StatusFact;
}

Fact* VehicleXTSFactGroup::getPowerFact(unsigned int bcmnum)
{
    if(bcmnum == 0)
        return &_bcm0PowerFact;
    else if(bcmnum == 1)
        return &_bcm1PowerFact;
    else
        return &_bcm2PowerFact;
}

Fact* VehicleXTSFactGroup::getTempFact(unsigned int bcmnum)
{
    if(bcmnum == 0)
        return &_bcm0TempFact;
    else if(bcmnum == 1)
        return &_bcm1TempFact;
    else
        return &_bcm2TempFact;
}
