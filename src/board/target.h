#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <SPI.h>

class T5RTCClock : public mesh::RTCClock {
    bool valid_ = false;
    uint32_t trusted_gps_time_ = 0;
    uint32_t trusted_gps_until_ = 0;
public:
    void begin();
    uint32_t getCurrentTime() override;
    void setCurrentTime(uint32_t time) override;
    void expectGpsTime(uint32_t time);
    bool isValid() const { return valid_; }
};

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
extern T5RTCClock rtc_clock;
extern EnvironmentSensorManager sensors;
SPIClass& t5_shared_spi();
void t5_gps_power_probe_tick();
uint8_t t5_gps_constellation_mode(); // 0 = leave receiver configuration unchanged
bool t5_gps_set_constellation_mode(uint8_t mode);


bool radio_init();
mesh::LocalIdentity radio_new_identity();
