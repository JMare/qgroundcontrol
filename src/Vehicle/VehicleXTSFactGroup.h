/****************************************************************************
 *
 * (c) 2009-2020 QGROUNDCONTROL PROJECT <http://www.qgroundcontrol.org>
 *
 * QGroundControl is licensed according to the terms in the file
 * COPYING.md in the root of the source code directory.
 *
 ****************************************************************************/

#pragma once

#include "FactGroup.h"
#include "QGCMAVLink.h"

#define BCM_ERROR_BM 0x685C

typedef struct BCMData{
    uint8_t bus_id;
    uint16_t status_word;
    uint16_t temp;
    uint16_t vin;
    uint16_t vout;
    uint16_t iin;
    uint16_t iout;
    uint16_t pout;
    } BCMData_t;

class VehicleXTSFactGroup : public FactGroup
{
    Q_OBJECT

public:
    VehicleXTSFactGroup(QObject* parent = nullptr);

    Q_PROPERTY(Fact* bcm0status       READ bcm0status       CONSTANT)
    Q_PROPERTY(Fact* bcm1status       READ bcm1status       CONSTANT)
    Q_PROPERTY(Fact* bcm2status       READ bcm2status       CONSTANT)
    Q_PROPERTY(Fact* bcm0temp       READ bcm0temp       CONSTANT)
    Q_PROPERTY(Fact* bcm1temp       READ bcm1temp       CONSTANT)
    Q_PROPERTY(Fact* bcm2temp       READ bcm2temp       CONSTANT)
    Q_PROPERTY(Fact* bcm0power       READ bcm0power       CONSTANT)
    Q_PROPERTY(Fact* bcm1power       READ bcm1power       CONSTANT)
    Q_PROPERTY(Fact* bcm2power       READ bcm2power       CONSTANT)

    Fact* bcm0status () { return &_bcm0StatusFact; }
    Fact* bcm1status () { return &_bcm1StatusFact; }
    Fact* bcm2status () { return &_bcm2StatusFact; }
    Fact* bcm0temp () { return &_bcm0TempFact; }
    Fact* bcm1temp () { return &_bcm1TempFact; }
    Fact* bcm2temp () { return &_bcm2TempFact; }
    Fact* bcm0power () { return &_bcm0PowerFact; }
    Fact* bcm1power () { return &_bcm1PowerFact; }
    Fact* bcm2power () { return &_bcm2PowerFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle* vehicle, mavlink_message_t& message) override;

    static const char* _bcm0StatusFactName;
    static const char* _bcm1StatusFactName;
    static const char* _bcm2StatusFactName;
    static const char* _bcm0TempFactName;
    static const char* _bcm1TempFactName;
    static const char* _bcm2TempFactName;
    static const char* _bcm0PowerFactName;
    static const char* _bcm1PowerFactName;
    static const char* _bcm2PowerFactName;

    static const char* _settingsGroup;

    static const double _temperatureUnavailable;

private:
    void _handleData16  (mavlink_message_t& message);
    void _handleBCMTelemetry(void);

    Fact            _bcm0StatusFact;
    Fact            _bcm1StatusFact;
    Fact            _bcm2StatusFact;
    Fact            _bcm0TempFact;
    Fact            _bcm1TempFact;
    Fact            _bcm2TempFact;
    Fact            _bcm0PowerFact;
    Fact            _bcm1PowerFact;
    Fact            _bcm2PowerFact;

    Fact* getStatusFact(unsigned int bcmnum);
    Fact* getPowerFact(unsigned int bcmnum);
    Fact* getTempFact(unsigned int bcmnum);
    void _setStatus(unsigned int busnum, uint16_t status_word);
    bool bcmgotvalues[3];
    BCMData_t bcmdata[3];
};
