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

    Q_PROPERTY(Fact* aputemperature     READ aputemperature       CONSTANT)
    Q_PROPERTY(Fact* apuhvurrent       READ apuhvcurrent       CONSTANT)

    Fact* aputemperature () { return &_apuTemperatureFact; }
    Fact* apuhvcurrent () { return &_apuHVCurrentFact; }

    // Overrides from FactGroup
    void handleMessage(Vehicle* vehicle, mavlink_message_t& message) override;

    static const char* _apuTemperatureFactName;
    static const char* _apuHVCurrentFactName;

    static const char* _settingsGroup;

    static const double _temperatureUnavailable;

private:
    void _handleData16  (mavlink_message_t& message);
    void _handleBCMTelemetry(void);
    Fact            _apuTemperatureFact;
    Fact            _apuHVCurrentFact;


    bool bcmgotvalues[3];
    BCMData_t bcmdata[3];
};
