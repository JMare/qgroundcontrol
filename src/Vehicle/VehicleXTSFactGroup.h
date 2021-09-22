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

#define HVM_ERROR_BM 0x681C
#define HVM_OFF_BM 0x0040

typedef struct hvmData{
    uint8_t bus_id;
    uint16_t status_word;
    uint16_t temp;
    uint16_t vin;
    uint16_t vout;
    uint16_t iin;
    uint16_t iout;
    uint16_t pout;
    } hvmData_t;

class VehicleXTSFactGroup : public FactGroup
{
    Q_OBJECT

public:
    VehicleXTSFactGroup(QObject* parent = nullptr);

    Q_PROPERTY(Fact* hvm0status       READ hvm0status       CONSTANT)
    Q_PROPERTY(Fact* hvm1status       READ hvm1status       CONSTANT)
    Q_PROPERTY(Fact* hvm2status       READ hvm2status       CONSTANT)
    Q_PROPERTY(Fact* hvm0temp       READ hvm0temp       CONSTANT)
    Q_PROPERTY(Fact* hvm1temp       READ hvm1temp       CONSTANT)
    Q_PROPERTY(Fact* hvm2temp       READ hvm2temp       CONSTANT)
    Q_PROPERTY(Fact* hvm0power       READ hvm0power       CONSTANT)
    Q_PROPERTY(Fact* hvm1power       READ hvm1power       CONSTANT)
    Q_PROPERTY(Fact* hvm2power       READ hvm2power       CONSTANT)

    Fact* hvm0status () { return &_hvm0StatusFact; }
    Fact* hvm1status () { return &_hvm1StatusFact; }
    Fact* hvm2status () { return &_hvm2StatusFact; }
    Fact* hvm0temp () { return &_hvm0TempFact; }
    Fact* hvm1temp () { return &_hvm1TempFact; }
    Fact* hvm2temp () { return &_hvm2TempFact; }
    Fact* hvm0power () { return &_hvm0PowerFact; }
    Fact* hvm1power () { return &_hvm1PowerFact; }
    Fact* hvm2power () { return &_hvm2PowerFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle* vehicle, mavlink_message_t& message) override;

    static const char* _hvm0StatusFactName;
    static const char* _hvm1StatusFactName;
    static const char* _hvm2StatusFactName;
    static const char* _hvm0TempFactName;
    static const char* _hvm1TempFactName;
    static const char* _hvm2TempFactName;
    static const char* _hvm0PowerFactName;
    static const char* _hvm1PowerFactName;
    static const char* _hvm2PowerFactName;

    static const char* _settingsGroup;

    static const double _temperatureUnavailable;

private:
    void _handleData16  (mavlink_message_t& message);

    Fact            _hvm0StatusFact;
    Fact            _hvm1StatusFact;
    Fact            _hvm2StatusFact;
    Fact            _hvm0TempFact;
    Fact            _hvm1TempFact;
    Fact            _hvm2TempFact;
    Fact            _hvm0PowerFact;
    Fact            _hvm1PowerFact;
    Fact            _hvm2PowerFact;

    Fact* getStatusFact(unsigned int hvmnum);
    Fact* getPowerFact(unsigned int hvmnum);
    Fact* getTempFact(unsigned int hvmnum);
    void _setStatus(unsigned int busnum, uint16_t status_word);
    void _handleReady(void);

};
