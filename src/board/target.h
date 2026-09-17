#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/AutoDiscoverRTCClock.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

class T5Board : public ESP32Board {
public:
    void begin();
    void beginLocal();
    bool enableRadioGpsRail();
    uint16_t getBattMilliVolts() override;
    const char* getManufacturerName() const override { return "LILYGO T5 E-Paper S3 Pro"; }
};

extern T5Board board;
extern CustomSX1262Wrapper radio_driver;
extern AutoDiscoverRTCClock rtc_clock;
extern EnvironmentSensorManager sensors;

bool radio_init();
mesh::LocalIdentity radio_new_identity();
