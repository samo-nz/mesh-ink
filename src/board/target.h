#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <SPI.h>
#include "board_profile.h"
#include "../hardware/radio_types.h"

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
    void beginLocalRxWake();
    bool enableRadioGpsRail();
    uint16_t getBattMilliVolts() override;
    const char* getManufacturerName() const override { return T5_BOARD_H752_01 ? "LILYGO T5 E-Paper S3 Pro (H752-01)" : "LILYGO T5 E-Paper S3 Pro (H752)"; }
};

// H752/H752-01 builds enable only MeshCore's GPS environment provider. This
// board-specific manager reuses target.cpp's already-completed UART/NMEA probe
// instead of repeating upstream's fixed one-second GPS detection wait.
class T5EnvironmentSensorManager final : public EnvironmentSensorManager {
public:
    explicit T5EnvironmentSensorManager(LocationProvider& location)
        : EnvironmentSensorManager(location) {}
    bool begin() override;
};

extern T5Board board;
extern CustomSX1262Wrapper radio_driver;
extern T5EnvironmentSensorManager sensors;
SPIClass& t5_shared_spi();


bool radio_init();
bool radio_resume_rx_wake();
MeshInkRadioFailureClass t5_classify_radio_failure();
mesh::LocalIdentity radio_new_identity();
