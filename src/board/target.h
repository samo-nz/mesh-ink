#pragma once

#include <helpers/ESP32Board.h>
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>
#include <SPI.h>
#include "board_profile.h"
#include "../hardware/radio_types.h"
#include "../hardware/rtc_types.h"

class T5RTCClock : public mesh::RTCClock {
    bool valid_ = false;
    // begin() is called only after the shared display/I2C lifecycle is active.
    // Headless deep-sleep MeshCore startup intentionally skips that lifecycle.
    bool i2c_ready_ = false;
    uint32_t deferred_hardware_time_ = 0;
    uint32_t trusted_gps_time_ = 0;
    uint32_t trusted_gps_until_ = 0;
    uint32_t expected_companion_time_ = 0;
    uint32_t expected_companion_until_ = 0;
    uint32_t last_gps_sync_utc_ = 0;
    MeshInkTimeSource time_source_ = MeshInkTimeSource::Unknown;
    bool metadata_loaded_ = false;

    void loadMetadata();
    void saveMetadata();
    bool gpsAuthorityActive(uint32_t current) const;
    bool writeAcceptedTime(uint32_t utc,MeshInkTimeSource source);
public:
    void begin();
    uint32_t getCurrentTime() override;
    void setCurrentTime(uint32_t time) override;
    void expectGpsTime(uint32_t time);
    void expectCompanionTime(uint32_t time);
    bool setManualTime(uint32_t time);
    MeshInkTimeSource timeSource();
    bool gpsAuthoritative();
    bool isValid() const { return valid_; }
};

class MeshInkSX1262Wrapper final : public CustomSX1262Wrapper {
    uint8_t wake_packet_[MAX_TRANS_UNIT]{};
    uint16_t wake_packet_len_=0;
    float wake_rssi_=0.0f;
    float wake_snr_=0.0f;
    bool wake_metrics_active_=false;
public:
    MeshInkSX1262Wrapper(CustomSX1262& radio, mesh::MainBoard& board)
        : CustomSX1262Wrapper(radio, board) {}

    void stageWakePacket(const uint8_t* data,uint16_t len,float rssi,float snr);
    bool captureRetainedWakePacket(float rssi,float snr);
    bool hasWakePacket() const { return wake_packet_len_!=0; }
    int recvRaw(uint8_t* bytes,int sz) override;
    float getLastRSSI() const override;
    float getLastSNR() const override;
};

class T5Board : public ESP32Board {
public:
    void begin();
    void beginLocal();
    void beginLocalRxWake(bool packet_wake);
    void finishLocalRxWakeCapture();
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
extern MeshInkSX1262Wrapper radio_driver;
extern T5EnvironmentSensorManager sensors;
SPIClass& t5_shared_spi();


bool radio_init();
bool radio_resume_rx_wake();
bool radio_resume_retained_wake();
MeshInkRadioFailureClass t5_classify_radio_failure();
mesh::LocalIdentity radio_new_identity();
