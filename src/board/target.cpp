#include <Arduino.h>
#include <SPI.h>
#include <Preferences.h>
#include "../hardware/display.h"
#include "../hardware/touch.h"
#include "../hardware/gps.h"
#include "../hardware/rtc.h"
#include "../hardware/power.h"
#include "../hardware/buttons.h"
#include <esp_heap_caps.h>
#include <driver/i2c.h>
#include <driver/gpio.h>
#include <sys/time.h>
#include <RTClib.h>
#include "target.h"
#include "t5_logging.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.0.0"
#endif

#define T5_TRACE(...) T5_DEBUGF(T5_LOG_BOARD, "[T5] " __VA_ARGS__)
#define T5_GPS_TRACE(...) T5_DEBUGF(T5_LOG_GPS, "[T5] " __VA_ARGS__)
#define T5_POWER_TRACE(...) T5_DEBUGF(T5_LOG_POWER, "[T5] " __VA_ARGS__)

T5Board board;

static constexpr uint8_t PCA9535_ADDR=0x20;
static bool pca_read(uint8_t reg,uint8_t& value){
    return i2c_master_write_read_device(I2C_NUM_0,PCA9535_ADDR,&reg,1,&value,1,pdMS_TO_TICKS(50))==ESP_OK;
}
static bool pca_write(uint8_t reg,uint8_t value){
    const uint8_t data[2]={reg,value};
    return i2c_master_write_to_device(I2C_NUM_0,PCA9535_ADDR,data,sizeof(data),pdMS_TO_TICKS(50))==ESP_OK;
}

static bool idf_read(uint8_t address,uint8_t reg,uint8_t* data,size_t len){
    return i2c_master_write_read_device(I2C_NUM_0,address,&reg,1,data,len,pdMS_TO_TICKS(50))==ESP_OK;
}
static bool idf_write(uint8_t address,uint8_t reg,const uint8_t* data,size_t len){
    uint8_t buffer[9];if(len>sizeof(buffer)-1)return false;buffer[0]=reg;memcpy(buffer+1,data,len);
    return i2c_master_write_to_device(I2C_NUM_0,address,buffer,len+1,pdMS_TO_TICKS(50))==ESP_OK;
}
static uint8_t from_bcd(uint8_t v){return (uint8_t)((v>>4)*10+(v&0x0F));}
static uint8_t to_bcd(uint8_t v){return (uint8_t)(((v/10)<<4)|(v%10));}

void T5RTCClock::begin(){
    uint8_t r[7]{};
    if(!idf_read(0x51,0x02,r,sizeof(r))){
        valid_=false;T5_TRACE("rtc: PCF8563 read failed; system fallback active\n");return;
    }
    const bool voltage_low=(r[0]&0x80)!=0;
    const uint8_t second=from_bcd(r[0]&0x7F),minute=from_bcd(r[1]&0x7F),hour=from_bcd(r[2]&0x3F);
    const uint8_t day=from_bcd(r[3]&0x3F),month=from_bcd(r[5]&0x1F),year=from_bcd(r[6]);
    valid_=!voltage_low&&second<60&&minute<60&&hour<24&&day>=1&&day<=31&&month>=1&&month<=12;
    if(valid_){
        const uint32_t utc=DateTime(2000+year,month,day,hour,minute,second).unixtime();
        timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
        T5_TRACE("rtc: PCF8563 valid UTC=%04u-%02u-%02u %02u:%02u:%02u epoch=%lu\n",
            2000+year,month,day,hour,minute,second,(unsigned long)utc);
    }else{
        T5_TRACE("rtc: PCF8563 INVALID voltage-low=%u raw=%02X/%02X/%02X %02X/%02X/%02X\n",
            voltage_low,r[2],r[1],r[0],r[3],r[5],r[6]);
    }
}
uint32_t T5RTCClock::getCurrentTime(){
    if(!valid_)return (uint32_t)time(nullptr);
    uint8_t r[7]{};if(!idf_read(0x51,0x02,r,sizeof(r))||(r[0]&0x80)){valid_=false;return (uint32_t)time(nullptr);}
    return DateTime(2000+from_bcd(r[6]),from_bcd(r[5]&0x1F),from_bcd(r[3]&0x3F),
        from_bcd(r[2]&0x3F),from_bcd(r[1]&0x7F),from_bcd(r[0]&0x7F)).unixtime();
}
void T5RTCClock::setCurrentTime(uint32_t utc){
    const uint32_t current=getCurrentTime();
    const bool trusted_gps=trusted_gps_time_&&millis()<=trusted_gps_until_&&
        (utc>trusted_gps_time_?utc-trusted_gps_time_:trusted_gps_time_-utc)<=3;
    trusted_gps_time_=0;trusted_gps_until_=0;
    if(valid_&&!trusted_gps){
        // Once the hardware RTC is known-good, generic MeshCore/system time
        // sources must not overwrite it. GPS corrections are explicitly
        // authorised by expectGpsTime() above.
        T5_TRACE("rtc: ignored non-GPS set UTC=%lu current=%lu (RTC already valid)\n",
            (unsigned long)utc,(unsigned long)current);
        return;
    }
    const DateTime dt(utc);const uint8_t r[7]={to_bcd(dt.second()),to_bcd(dt.minute()),to_bcd(dt.hour()),
        to_bcd(dt.day()),to_bcd(dt.dayOfTheWeek()),to_bcd(dt.month()),to_bcd((uint8_t)(dt.year()-2000))};
    valid_=idf_write(0x51,0x02,r,sizeof(r));
    timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
    T5_TRACE("rtc: %s sync UTC=%lu hardware-write=%s\n",trusted_gps?"trusted GPS":"system",
        (unsigned long)utc,valid_?"OK":"FAILED");
}
void T5RTCClock::expectGpsTime(uint32_t utc){trusted_gps_time_=utc;trusted_gps_until_=millis()+1500;}

static bool t5_set_radio_gps_rail(bool enabled,uint32_t settle_ms){
#if !T5_BOARD_H752_01
    // Original H752 powers its SX1262 without the H752-01 PCA9535 rail gate.
    (void)enabled;(void)settle_ms;
    return true;
#else
    // LilyGO maps LORA_EN (shared LoRa/GPS 3V3 rail) to PCA9535 port 0 bit 0.
    // Preserve every display-owned bit: update only IO0_0 while the panel is idle.
    constexpr uint8_t OUTPUT_PORT0=0x02,CONFIG_PORT0=0x06,LORA_EN=0x01;
    uint8_t output=0,config=0;
    if(!pca_read(OUTPUT_PORT0,output)||!pca_read(CONFIG_PORT0,config)){
        Serial.println("[T5-ERROR] PCA9535 power-rail read failed");return false;
    }
    const uint8_t requested_output=enabled?(uint8_t)(output|LORA_EN):(uint8_t)(output&~LORA_EN);
    const uint8_t requested_config=(uint8_t)(config&~LORA_EN);
    // Set the output latch first so the rail cannot glitch when direction changes.
    if(!pca_write(OUTPUT_PORT0,requested_output)||!pca_write(CONFIG_PORT0,requested_config)){
        Serial.println("[T5-ERROR] PCA9535 power-rail write failed");return false;
    }
    uint8_t verified_output=0,verified_config=0;
    const bool verified=pca_read(OUTPUT_PORT0,verified_output)&&pca_read(CONFIG_PORT0,verified_config)&&
        (enabled?((verified_output&LORA_EN)!=0):((verified_output&LORA_EN)==0))&&
        !(verified_config&LORA_EN);
    T5_TRACE("power rail: request=%s output0 0x%02X->0x%02X config0 0x%02X->0x%02X verify=%s\n",
        enabled?"ON":"OFF",output,verified_output,config,verified_config,verified?"OK":"FAILED");
    if(!verified)Serial.println("[T5-ERROR] PCA9535 shared radio/GPS rail verification failed");
    if(verified&&settle_ms)delay(settle_ms);
    return verified;
#endif
}

bool T5Board::enableRadioGpsRail(){
    // H752-01 reference LoRa examples allow the shared LoRa/GPS rail 1500 ms
    // to settle before touching the SX1262. This also makes cold and warm
    // boots follow the same deterministic timing.
    return t5_set_radio_gps_rail(true,1500);
}

// Board mapping only. The upstream wrapper controls radio parameters and
// transmit/receive/power state through MeshCore.
static SPIClass radio_spi(FSPI);
SPIClass& t5_shared_spi() { return radio_spi; }

static void t5_radio_shared_bus_idle(bool stop_spi){
    if(stop_spi)radio_spi.end();
    // LilyGO's H752-01 LoRa examples explicitly deselect both devices before
    // powering the shared rail because microSD and SX1262 share this SPI bus.
    pinMode(T5_PIN_LORA_CS,OUTPUT);digitalWrite(T5_PIN_LORA_CS,HIGH);
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    gpio_hold_dis((gpio_num_t)P_LORA_RESET);
    gpio_deep_sleep_hold_dis();
    T5_TRACE("radio bus idle: lora-cs=%d sd-cs=%d busy=%d reset=%d spi-stop=%u\n",
        digitalRead(T5_PIN_LORA_CS),digitalRead(T5_PIN_SD_CS),
        digitalRead(P_LORA_BUSY),digitalRead(P_LORA_RESET),stop_spi?1U:0U);
}

static uint32_t radio_gps_rail_started_at=0;
static bool radio_gps_rail_start_ok=false;

void meshink_board_start_local_radio_settle(){
    // Called immediately after EPDiy has established the board I2C driver.
    // Start the H752-01 rail now, then let framebuffer/preferences/splash work
    // consume the manufacturer-style 1500 ms settling window in parallel.
    t5_radio_shared_bus_idle(true);
    radio_gps_rail_start_ok=t5_set_radio_gps_rail(true,0);
    radio_gps_rail_started_at=radio_gps_rail_start_ok?millis():0;
    Serial.printf("[T5-RADIO] early rail start=%s at=%lums\n",
        radio_gps_rail_start_ok?"OK":"FAILED",(unsigned long)radio_gps_rail_started_at);
}

static bool t5_wait_local_radio_settle(){
#if !T5_BOARD_H752_01
    return true;
#else
    constexpr uint32_t REQUIRED_SETTLE_MS=1500;
    if(!radio_gps_rail_start_ok){
        meshink_board_start_local_radio_settle();
        if(!radio_gps_rail_start_ok)return false;
    }
    const uint32_t elapsed=millis()-radio_gps_rail_started_at;
    const uint32_t remaining=elapsed<REQUIRED_SETTLE_MS?REQUIRED_SETTLE_MS-elapsed:0;
    Serial.printf("[T5-RADIO] rail settle elapsed=%lums remaining=%lums\n",
        (unsigned long)elapsed,(unsigned long)remaining);
    if(remaining)delay(remaining);
    return true;
#endif
}

// Local UI keeps EPDiy's already-installed GPIO ISR service alive. Companion
// mode tears EPDiy down before radio startup, so Arduino must install/own the
// ISR service on its first radio attachInterrupt(). Select the path explicitly
// to avoid probing the wrong state and emitting a false ESP-IDF error.
static bool companion_radio_uses_arduino_irq=false;

// EPDiy's LilyGo-S3 board init installs the ESP-IDF GPIO ISR service for its
// TPS65185 interrupt before MeshCore starts. Arduino's first attachInterrupt()
// then tries to install the same global service again and ESP-IDF prints
// "GPIO isr service already installed". Keep both handlers on the one service:
// use it directly when already present, but fall back to ArduinoHal in
// companion mode where EPDiy has been deinitialized and removed the service.
class T5RadioHal final : public ArduinoHal {
    static void (*callbacks_[GPIO_NUM_MAX])(void);
    static bool arduino_owned_[GPIO_NUM_MAX];
    static void irq_bridge(void* arg) {
        const uint32_t pin=(uint32_t)(uintptr_t)arg;
        if(pin<GPIO_NUM_MAX&&callbacks_[pin])callbacks_[pin]();
    }
public:
    explicit T5RadioHal(SPIClass& spi):ArduinoHal(spi) {}

    void attachInterrupt(uint32_t interruptNum,void (*interruptCb)(void),
                         uint32_t mode) override {
        if(interruptNum==RADIOLIB_NC||interruptNum>=GPIO_NUM_MAX)return;
        const gpio_num_t pin=(gpio_num_t)interruptNum;
        gpio_int_type_t type=GPIO_INTR_ANYEDGE;
        if(mode==GpioInterruptRising)type=GPIO_INTR_POSEDGE;
        else if(mode==GpioInterruptFalling)type=GPIO_INTR_NEGEDGE;

        if(callbacks_[interruptNum]||arduino_owned_[interruptNum])
            detachInterrupt(interruptNum);

        if(companion_radio_uses_arduino_irq){
            callbacks_[interruptNum]=nullptr;
            arduino_owned_[interruptNum]=true;
            ArduinoHal::attachInterrupt(interruptNum,interruptCb,mode);
            return;
        }

        callbacks_[interruptNum]=interruptCb;
        arduino_owned_[interruptNum]=false;
        gpio_set_intr_type(pin,type);
        const esp_err_t added=gpio_isr_handler_add(
            pin,irq_bridge,(void*)(uintptr_t)interruptNum);
        if(added==ESP_OK)return;

        // No global IDF service is active (normal in companion mode after
        // meshink_display_deinit), so let Arduino install and own it in the usual way.
        if(added==ESP_ERR_INVALID_STATE){
            callbacks_[interruptNum]=nullptr;
            arduino_owned_[interruptNum]=true;
            ArduinoHal::attachInterrupt(interruptNum,interruptCb,mode);
            return;
        }
        callbacks_[interruptNum]=nullptr;
        Serial.printf("[T5-ERROR] radio DIO interrupt attach failed gpio=%lu err=%d\n",
                      (unsigned long)interruptNum,(int)added);
    }

    void detachInterrupt(uint32_t interruptNum) override {
        if(interruptNum==RADIOLIB_NC||interruptNum>=GPIO_NUM_MAX)return;
        if(!callbacks_[interruptNum]&&!arduino_owned_[interruptNum])return;
        if(arduino_owned_[interruptNum]){
            ArduinoHal::detachInterrupt(interruptNum);
            arduino_owned_[interruptNum]=false;
        }else{
            gpio_isr_handler_remove((gpio_num_t)interruptNum);
            gpio_set_intr_type((gpio_num_t)interruptNum,GPIO_INTR_DISABLE);
        }
        callbacks_[interruptNum]=nullptr;
    }
};
void (*T5RadioHal::callbacks_[GPIO_NUM_MAX])(void)={};
bool T5RadioHal::arduino_owned_[GPIO_NUM_MAX]={};

static T5RadioHal radio_hal(radio_spi);
static CustomSX1262 radio = new Module(
    &radio_hal,P_LORA_NSS,P_LORA_DIO_1,P_LORA_RESET,P_LORA_BUSY);
CustomSX1262Wrapper radio_driver(radio, board);

static T5RTCClock& t5_rtc_clock(){
    static T5RTCClock clock;
    return clock;
}

mesh::RTCClock& meshink_rtc_meshcore(){return t5_rtc_clock();}
void meshink_rtc_begin(){t5_rtc_clock().begin();}
void meshink_rtc_tick(){t5_rtc_clock().tick();}
uint32_t meshink_rtc_current_time(){return t5_rtc_clock().getCurrentTime();}
bool meshink_rtc_valid(){return t5_rtc_clock().isValid();}
static uint32_t detected_gps_baud = 9600;
static bool gps_baud_locked = false;
enum class GpsModule : uint8_t { Unknown, L76K, MiaM10Q };
static GpsModule detected_gps_module = GpsModule::Unknown;
// Tracks the MeshCore provider's stopped state. GPS is NOT electrically
// switched off: LoRa and GPS share the same power rail.
static bool gps_command_sleeping = false;
static uint32_t gps_last_byte_at = 0;

// LoRa and GPS share VCC3V3. Constellation selection remains user-controlled;
// compact GGA+RMC NMEA output is always configured on the inferred L76K.
// Neither setting shuts down receiver power or changes the 1 Hz fix rate.
// 0 leaves the receiver constellation unchanged; 1/3/5/7 are PCAS04 modes.
#ifndef T5_GPS_FULL_NMEA_DIAGNOSTIC
#define T5_GPS_FULL_NMEA_DIAGNOSTIC 0
#endif
static MeshInkGpsConstellationMode gps_constellation_mode=MeshInkGpsConstellationMode::Unchanged;
static bool gps_tuning_loaded=false;
static bool gps_constellation_dirty=false;
static bool gps_nmea_dirty=true;  // apply automatic compact output each boot

static void gps_load_tuning(){
    if(gps_tuning_loaded)return;
    gps_tuning_loaded=true;
    Preferences pref;
    if(pref.begin("t5-gnss",true)){
        const auto mode=static_cast<MeshInkGpsConstellationMode>(
            pref.getUChar("constellation",0));
        gps_constellation_mode=meshink_gps_constellation_mode_valid(mode)
            ? mode : MeshInkGpsConstellationMode::Unchanged;
        pref.end();
    }
    // Ignore the pre-1.5.0 "compact" preference: full output is now a
    // developer-only diagnostic build option, not an on-device toggle.
    gps_constellation_dirty=gps_constellation_mode!=MeshInkGpsConstellationMode::Unchanged;
    gps_nmea_dirty=true;
}
static void gps_send_pcas(const char* payload) {
    uint8_t checksum=0;
    for(const char* p=payload;*p;++p)checksum^=(uint8_t)*p;
    Serial1.printf("$%s*%02X\r\n",payload,checksum);
    Serial1.flush();
    T5_GPS_TRACE("gps tuning: TX $%s*%02X (receiver acceptance not confirmed)\n",payload,checksum);
}
static void gps_apply_tuning(){
    if(detected_gps_module!=GpsModule::L76K||!gps_baud_locked)return;
    if(gps_constellation_dirty){
        gps_constellation_dirty=false;
        if(gps_constellation_mode!=MeshInkGpsConstellationMode::Unchanged){
            char payload[16];
            snprintf(payload,sizeof(payload),"PCAS04,%u",
                     (unsigned)static_cast<uint8_t>(gps_constellation_mode));
            gps_send_pcas(payload);
        }
        // "UNCHANGED" does not restore factory configuration; it sends nothing.
    }
    if(gps_nmea_dirty){
        gps_nmea_dirty=false;
#if T5_GPS_FULL_NMEA_DIAGNOSTIC
        gps_send_pcas("PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0");
#else
        gps_send_pcas("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
#endif
    }
}
const char* meshink_gps_backend_name(){return "T5 GNSS";}
const char* meshink_gps_tuning_note(){
    return "COMPACT NMEA IS AUTOMATIC FOR L76K. CONSTELLATION POWER SAVINGS ARE UNMEASURED. GPS STAYS POWERED WHILE LORA IS ON.";
}
MeshInkGpsConstellationMode meshink_gps_constellation_mode(){
    gps_load_tuning();
    return gps_constellation_mode;
}
bool meshink_gps_set_constellation_mode(MeshInkGpsConstellationMode mode){
    if(!meshink_gps_constellation_mode_valid(mode))return false;
    gps_load_tuning();
    if(gps_constellation_mode==mode)return true;
    const uint8_t stored=static_cast<uint8_t>(mode);
    Preferences pref;
    if(!pref.begin("t5-gnss",false))return false;
    const bool saved=pref.putUChar("constellation",stored)==1;
    pref.end();
    if(!saved)return false;
    gps_constellation_mode=mode;
    gps_constellation_dirty=mode!=MeshInkGpsConstellationMode::Unchanged;
    if(mode==MeshInkGpsConstellationMode::Unchanged)
        T5_GPS_TRACE("gps tuning: constellation UNCHANGED; no command sent\n");
    else gps_apply_tuning();
    return true;
}

static uint32_t gps_sleep_requested_at = 0;
static uint32_t gps_sleep_bytes_after = 0;
static uint32_t gps_sleep_window_bytes = 0;
static uint32_t gps_sleep_last_report = 0;
static uint32_t gps_wake_requested_at = 0;
static uint32_t gps_wake_started_at = 0;
static uint32_t gps_wake_previous_stamp = 0;
static bool gps_wake_logged_nmea = false;
static bool gps_wake_logged_fix = false;

static const char* gps_module_name() {
    switch (detected_gps_module) {
        case GpsModule::L76K: return "L76K";
        case GpsModule::MiaM10Q: return "MIA-M10Q";
        default: return "UNKNOWN";
    }
}

static void gps_wake_command() {
    if (!gps_command_sleeping) return;
    // Resume receiving NMEA without sending an unverified receiver command.
    if (detected_gps_module == GpsModule::L76K) {
        const uint32_t elapsed=gps_sleep_requested_at?millis()-gps_sleep_requested_at:0;
        gps_wake_requested_at=gps_wake_started_at=millis();
        gps_wake_logged_nmea=gps_wake_logged_fix=false;
        gps_sleep_requested_at=0;
        T5_GPS_TRACE("gps probe: GPS provider ON after %lums OFF; no GPS power/wake command sent\n",
            (unsigned long)elapsed);
    }
    gps_command_sleeping = false;
}

static void gps_sleep_command() {
    if (gps_command_sleeping) return;
    if (detected_gps_module == GpsModule::L76K) {
        // PCAS12 testing coincided with loss of GPS fixes, and OFF-state
        // UART traffic showed no 10-second quiet period. Do not send PCAS12,
        // PMTK161, or any other unverified GPS sleep command.
        // Discard stale UART bytes only while MeshCore's provider is stopped.
        while (Serial1.available() > 0) Serial1.read();
        gps_command_sleeping = true;  // provider stopped; receiver still powered
        gps_sleep_requested_at=gps_sleep_last_report=millis();
        gps_sleep_bytes_after=gps_sleep_window_bytes=0;
        gps_wake_started_at=gps_wake_requested_at=0;
        T5_GPS_TRACE("gps probe: GPS provider OFF; no GPS standby command sent; shared LoRa/GPS power rail remains ON\n");
    } else {
        T5_GPS_TRACE("gps power: sleep skipped module=%s (radio/GPS rail is shared)\n", gps_module_name());
    }
}

// Observes the same bytes MicroNMEA consumes, allowing baud detection without
// stealing data from MeshCore's parser.
class NMEAProbeStream : public Stream {
    HardwareSerial& serial;
    bool collecting = false;
    bool after_star = false;
    bool talker_g = false;
    bool sentence_valid = false;
    uint8_t checksum = 0;
    uint8_t expected = 0;
    uint8_t checksum_digits = 0;
    uint8_t payload_chars = 0;

    static int hexValue(char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    }
    void observe(char c) {
        if (c == '$') {
            collecting = true;
            after_star = false;
            talker_g = false;
            checksum = expected = checksum_digits = payload_chars = 0;
            return;
        }
        if (!collecting) return;
        if (!after_star) {
            if (c == '*') { after_star = true; return; }
            if (c == '\r' || c == '\n' || c < 32 || c > 126) { collecting = false; return; }
            if (payload_chars++ == 0) talker_g = (c == 'G');
            checksum ^= static_cast<uint8_t>(c);
            return;
        }
        const int nibble = hexValue(c);
        if (nibble < 0 || checksum_digits >= 2) { collecting = false; return; }
        expected = static_cast<uint8_t>((expected << 4) | nibble);
        if (++checksum_digits == 2) {
            sentence_valid = talker_g && checksum == expected;
            collecting = false;
        }
    }

public:
    explicit NMEAProbeStream(HardwareSerial& source) : serial(source) {}
    using Print::write;
    int available() override { return serial.available(); }
    int read() override { const int c = serial.read(); if (c >= 0) observe(static_cast<char>(c)); return c; }
    int peek() override { return serial.peek(); }
    void flush() override { serial.flush(); }
    size_t write(uint8_t value) override { return serial.write(value); }
    void clearValidation() { sentence_valid = collecting = after_star = talker_g = false; checksum = expected = checksum_digits = payload_chars = 0; }
    bool hasValidSentence() const { return sentence_valid; }
};

static NMEAProbeStream gps_stream(Serial1);
class T5GPS : public MicroNMEALocationProvider {
    bool active = false;
    uint32_t next_baud_retry = 0;
public:
    T5GPS() : MicroNMEALocationProvider(gps_stream, &t5_rtc_clock()) {}
    bool isActive() const { return active; }
    void begin() override {
        Serial1.updateBaudRate(detected_gps_baud);
        gps_wake_previous_stamp=(uint32_t)getTimestamp();
        gps_stream.clearValidation();
        gps_wake_command();
        MicroNMEALocationProvider::begin();
        active = true;
        gps_load_tuning();
        gps_apply_tuning();
        next_baud_retry = millis() + 6000;
        T5_GPS_TRACE("gps: enabled by MeshCore sensor setting\n");
    }
    void stop() override {
        MicroNMEALocationProvider::stop();
        active = false;
        gps_sleep_command();
        T5_GPS_TRACE("gps: disabled by MeshCore sensor setting\n");
    }
    void loop() override {
        meshink_power_diagnostics_tick();
        // Keep receiver-data watchdog operational in production. Previously
        // this timestamp was updated only inside the diagnostics build.
        const int pending = Serial1.available();
        if(pending>0)gps_last_byte_at=millis();
#if T5_LOG_GPS
        if (pending>0&&gps_wake_requested_at){
            T5_GPS_TRACE("gps probe: first UART bytes %lums after wake request (%d pending)\n",
                (unsigned long)(millis()-gps_wake_requested_at),pending);
            gps_wake_requested_at=0;
        }
#endif
        // MeshCore's provider may call RTCClock::setCurrentTime whenever it sees
        // valid GPS time. Only mark a GPS write as trusted when the RTC is
        // invalid, or when our deliberate hourly correction is due.
        static uint32_t last_gps_clock_sync_ms = 0;
        if (!last_gps_clock_sync_ms) {
            // Start the hourly correction window at boot. A valid RTC
            // must not be reset merely because the first GPS fix arrived.
            last_gps_clock_sync_ms = millis() ? millis() : 1;
        }
        if (isValid()) {
            const uint32_t now_ms = millis();
            const bool rtc_needs_time = !t5_rtc_clock().isValid();
            const bool hourly_correction_due = last_gps_clock_sync_ms == 0 ||
                now_ms - last_gps_clock_sync_ms >= 3600000UL;
            if (rtc_needs_time || hourly_correction_due) {
                t5_rtc_clock().expectGpsTime((uint32_t)getTimestamp());
                last_gps_clock_sync_ms = now_ms ? now_ms : 1;
            }
        }
        MicroNMEALocationProvider::loop();
#if T5_LOG_GPS
        if (gps_wake_started_at && !gps_wake_logged_nmea && gps_stream.hasValidSentence()) {
            gps_wake_logged_nmea=true;
            T5_GPS_TRACE("gps probe: first checksum-valid NMEA %lums after wake request\n",
                (unsigned long)(millis()-gps_wake_started_at));
        }
        if (gps_wake_started_at && !gps_wake_logged_fix && isValid() &&
            (uint32_t)getTimestamp()!=gps_wake_previous_stamp) {
            gps_wake_logged_fix=true;
            T5_GPS_TRACE("gps probe: first fresh GPS fix %lums after wake request (sats=%ld)\n",
                (unsigned long)(millis()-gps_wake_started_at),(long)satellitesCount());
        }
#endif
        if (active && !gps_baud_locked && gps_stream.hasValidSentence()) {
            gps_baud_locked = true;
            detected_gps_baud = Serial1.baudRate();
            detected_gps_module = detected_gps_baud == 9600 ? GpsModule::L76K : GpsModule::MiaM10Q;
            T5_GPS_TRACE("gps: background probe locked %u baud module=%s with valid NMEA\n", detected_gps_baud, gps_module_name());
            gps_apply_tuning();
        } else if (active && !gps_baud_locked && millis() >= next_baud_retry) {
            detected_gps_baud = Serial1.baudRate() == 9600 ? 38400 : 9600;
            Serial1.updateBaudRate(detected_gps_baud);
            gps_stream.clearValidation();
            MicroNMEALocationProvider::syncTime();
            next_baud_retry = millis() + 6000;
            T5_GPS_TRACE("gps: background probe trying %u baud\n", detected_gps_baud);
        }
        if (active && gps_baud_locked && !gps_command_sleeping && gps_last_byte_at && millis() - gps_last_byte_at > 30000) {
            T5_GPS_TRACE("gps: NMEA watchdog expired after %lu ms; waking and reprobe enabled\n", (unsigned long)(millis() - gps_last_byte_at));
            // Loss of NMEA is not proof of standby. Retry baud detection
            // without sending any unverified GPS wake or sleep command.
            gps_stream.clearValidation();
            gps_baud_locked = false;
            next_baud_retry = millis() + 6000;
            gps_last_byte_at = millis();
        }
#if T5_LOG_GPS
        static uint32_t last_report = 0;
        if (millis() - last_report >= 15000) {
            last_report = millis();
            T5_GPS_TRACE("gps: baud=%u incoming=%d fix=%d satellites=%ld lat=%ld lon=%ld\n",
                     Serial1.baudRate(), pending, isValid(), satellitesCount(),
                     getLatitude(), getLongitude());
        }
#endif
    }
};
static T5GPS gps;
EnvironmentSensorManager sensors(gps);

void meshink_gps_service_begin(){
    sensors.begin();
}

void meshink_gps_service_loop(){
    sensors.loop();
}

void meshink_gps_set_provider_enabled(bool enabled){
    sensors.setSettingValue("gps",enabled?"1":"0");
}

MeshInkGpsStatus meshink_gps_read_status(){
    MeshInkGpsStatus status{};
#if ENV_INCLUDE_GPS == 1
    auto* location=sensors.getLocationProvider();
    status.available=location!=nullptr;
    if(location){
        status.valid=location->isValid();
        status.waiting_time_sync=location->waitingTimeSync();
        status.satellites=(int32_t)location->satellitesCount();
        status.latitude=location->getLatitude();
        status.longitude=location->getLongitude();
        status.timestamp=(uint32_t)location->getTimestamp();
    }
#endif
    return status;
}

void meshink_gps_shutdown(){
#if ENV_INCLUDE_GPS == 1
    if(auto* location=sensors.getLocationProvider())location->stop();
    Serial1.end();
#endif
}


// Run independently of T5GPS::loop(): MeshCore stops calling the provider
// when GPS is OFF. Consume UART bytes ONLY while the provider is inactive,
// so we can distinguish a quiet receiver from a stopped parser.
void meshink_gps_background_tick(){
    if(gps.isActive()||!gps_sleep_requested_at)return;
    // Do not disable the OFF-state UART draining with diagnostics: otherwise
    // the powered receiver fills the RX buffer before GPS is re-enabled.
    uint32_t drained=0;
    while(Serial1.available()>0&&drained<512){Serial1.read();++drained;}
#if T5_LOG_GPS
    const uint32_t now=millis();
    // Ignore the first second (bytes already in flight after the stop request).
    if(now-gps_sleep_requested_at>=1000){
        gps_sleep_bytes_after+=drained;
        gps_sleep_window_bytes+=drained;
    }
    if(now-gps_sleep_last_report>=5000){
        gps_sleep_last_report=now;
        const uint32_t off_ms=now-gps_sleep_requested_at;
        T5_GPS_TRACE("gps probe: OFF +%lus UART bytes last ~5s=%lu total after 1s=%lu (%s; power not measured)\n",
            (unsigned long)(off_ms/1000),
            (unsigned long)gps_sleep_window_bytes,(unsigned long)gps_sleep_bytes_after,
            gps_sleep_window_bytes?"UART ACTIVE":"UART QUIET");
        gps_sleep_window_bytes=0;
    }
#else
    (void)drained;
#endif
}

uint16_t T5Board::getBattMilliVolts() {
    // MeshCore and the local UI share the same board-selected power backend.
    // Keep MeshCore's 30 s cache so repeated app requests do not create
    // unnecessary fuel-gauge traffic.
    static uint16_t cached_mv = 0;
    static uint32_t sampled_at = 0;
    const uint32_t now = millis();
    if (sampled_at != 0 && now - sampled_at < 30000) return cached_mv;
    sampled_at = now == 0 ? 1 : now;

    uint16_t voltage = 0;
    if (meshink_power_read_battery_mv(voltage)) {
        cached_mv = voltage;
        uint8_t soc = 0;
        if (meshink_power_read_battery_percent(soc))
            T5_POWER_TRACE("battery: backend voltage=%u mV SOC=%u%%\n", cached_mv, soc);
        else
            T5_POWER_TRACE("battery: backend voltage=%u mV; SOC unavailable\n", cached_mv);
    } else {
        T5_POWER_TRACE("battery: backend read failed or voltage invalid, cached=%u mV\n", cached_mv);
    }
    return cached_mv;
}

void meshink_board_companion_exit_feedback_begin() {
    // Companion startup performs additional board/radio initialization after
    // the splash, so do not assume the early LEDC attachment is still intact.
    // Reassert the board frontlight backend before driving the visible exit
    // acknowledgement.
    meshink_power_frontlight_begin();
    meshink_power_frontlight_set(100);
    Serial.println("[T5-LIGHT] companion exit acknowledgement brightness=100%");
}

void meshink_board_companion_release_resources() {
    // Release the radio's own IRQ handler and shared SPI bus cleanly. The
    // global Arduino GPIO ISR service can remain until the imminent reset;
    // unlike test10, no same-boot EPDiy reinitialization needs that service.
    radio_hal.detachInterrupt(P_LORA_DIO_1);
    radio_spi.end();
    companion_radio_uses_arduino_irq=false;
    T5_TRACE("companion exit: radio IRQ/SPI resources released\n");
}

void meshink_board_begin_companion(){board.begin();}
void meshink_board_begin_local(){board.beginLocal();}
void meshink_board_boot_complete(){board.onBootComplete();}

void T5Board::begin() {
    // The application renders and tears down the companion splash before this
    // board lifecycle entry. EPDiy has released I2C/GPIO resources, so the
    // upstream ESP32 board setup can safely take ownership here.
    companion_radio_uses_arduino_irq=true;
    T5_TRACE("board: companion display released; MeshCore board/I2C begin\n");
    ESP32Board::begin();
    T5_TRACE("board: MeshCore I2C ready\n");
    t5_radio_shared_bus_idle(true);
    enableRadioGpsRail();
    meshink_power_prepare_board();
    getBattMilliVolts();
    T5_TRACE("board: disabling touch and frontlight\n");
    meshink_touch_set_power(false);
    meshink_power_frontlight_set(0); // frontlight remains disabled in companion mode
#if ENV_INCLUDE_GPS == 1
    // MeshCore's historical macro names are counterintuitive here:
    // HardwareSerial::setPins() takes (RX, TX).
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    T5_TRACE("board: GPS UART ready; internal heap=%u\n", ESP.getFreeHeap());
#endif
}

void T5Board::beginLocal() {
    companion_radio_uses_arduino_irq=false;
    // The local UI initialized EPDiy and I2C first. Reinstalling the legacy
    // I2C driver here would abort; only perform MeshCore's remaining board work.
    startup_reason = BD_STARTUP_NORMAL;
    // ui_setup() normally started this rail while preparing the splash. Only
    // wait for the remainder here; recover by starting it now if early start failed.
    t5_wait_local_radio_settle();
    // Unified/local mode calls beginLocal(), not begin(). Without this call
    // the 1500mAh factory-profile migration ran only in BLE companion mode.
    meshink_power_prepare_board();
    getBattMilliVolts();
#if ENV_INCLUDE_GPS == 1
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
#endif
    T5_TRACE("board: local UI handoff complete; shared I2C retained\n");
}

bool radio_init() {
    T5_TRACE("radio: begin clock and RTC\n");
    meshink_rtc_begin();
    T5_TRACE("radio: SX1262 init SPI=%d/%d/%d ctrl=%d/%d/%d/%d\n",
        P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI,P_LORA_NSS,P_LORA_DIO_1,P_LORA_RESET,P_LORA_BUSY);

    bool ready=false;
    for(uint8_t attempt=1;attempt<=3&&!ready;++attempt){
        if(attempt==2){
            // Escalation 1: discard any partial IRQ/SPI state and give the
            // SX1262 a clean hardware reset without disturbing the GPS rail.
            radio_hal.detachInterrupt(P_LORA_DIO_1);
            t5_radio_shared_bus_idle(true);
            pinMode(P_LORA_RESET,OUTPUT);
            digitalWrite(P_LORA_RESET,LOW);delay(20);
            digitalWrite(P_LORA_RESET,HIGH);delay(120);
            Serial.println("[T5-RADIO] recovery=spi-reset+sx1262-reset");
        }else if(attempt==3){
            // Escalation 2: this still occurs before removable storage is
            // mounted, so the H752-01 shared LoRa/GPS rail can be safely
            // power-cycled without invalidating any SD file or bus state.
            radio_hal.detachInterrupt(P_LORA_DIO_1);
            t5_radio_shared_bus_idle(true);
            const bool off=t5_set_radio_gps_rail(false,250);
            const bool on=off&&t5_set_radio_gps_rail(true,1500);
            radio_gps_rail_start_ok=on;
            radio_gps_rail_started_at=on?millis():0;
            Serial.printf("[T5-RADIO] recovery=rail-cycle off=%u on=%u\n",off?1U:0U,on?1U:0U);
            if(!on)continue;
        }else{
            t5_radio_shared_bus_idle(true);
        }

        const uint32_t attempt_started=millis();
        Serial.printf("[T5-RADIO] init attempt=%u/3 lora-cs=%d sd-cs=%d busy=%d reset=%d\n",
            attempt,digitalRead(T5_PIN_LORA_CS),digitalRead(T5_PIN_SD_CS),
            digitalRead(P_LORA_BUSY),digitalRead(P_LORA_RESET));
        // CustomSX1262::std_init prints RadioLib's numeric failure code on error.
        ready=radio.std_init(&radio_spi);
        Serial.printf("[T5-RADIO] init attempt=%u result=%s elapsed=%lums busy=%d\n",
            attempt,ready?"OK":"FAILED",(unsigned long)(millis()-attempt_started),
            digitalRead(P_LORA_BUSY));
        if(!ready&&attempt<3)delay(500);
    }

    T5_TRACE("radio: SX1262 init=%d, heap=%u\n", ready, ESP.getFreeHeap());
    if(!ready)Serial.println("[T5-ERROR] SX1262 radio initialization failed after recovery attempts");
#if ENV_INCLUDE_GPS == 1
    // LoRa and GPS share the PCA9535-controlled rail; radio initialization
    // ensures power is available before probing GPS. T5 boards carry either
    // a 9600-baud L76K or a 38400-baud MIA-M10Q. Sample NMEA here before
    // upstream sensors.begin() owns the UART, without changing radio state.
    if (ready) {
        bool found = false;
        for (uint8_t pass = 0; pass < 2 && !found; ++pass) {
            for (const uint32_t baud : {9600UL, 38400UL}) {
                Serial1.updateBaudRate(baud);
                gps_stream.clearValidation();
                const uint32_t started = millis();
                while (millis() - started < 1800) {
                    while (gps_stream.available()) gps_stream.read();
                    if (gps_stream.hasValidSentence()) { found = true; break; }
                    delay(5);
                }
                T5_GPS_TRACE("gps: probe pass=%u baud=%lu valid-NMEA=%d\n", pass + 1, baud, found);
                if (found) {
                    detected_gps_baud = baud;
                    gps_baud_locked = true;
                    detected_gps_module = baud == 9600 ? GpsModule::L76K : GpsModule::MiaM10Q;
                    gps_last_byte_at = millis();
                    break;
                }
            }
        }
        if (!found) {
            detected_gps_baud = 9600;
            Serial1.updateBaudRate(detected_gps_baud);
            gps_stream.clearValidation();
            T5_GPS_TRACE("gps: startup probe inconclusive; background retry enabled\n");
        }
        Serial.printf("[T5-BOOT] GPS module=%s baud=%lu%s\n",
            gps_module_name(),(unsigned long)Serial1.baudRate(),
            gps_baud_locked?"":" (NMEA not yet confirmed)");
        T5_GPS_TRACE("gps: selected baud=%u locked=%d module=%s; MeshCore owns position and settings\n",
                 Serial1.baudRate(), gps_baud_locked, gps_module_name());
    }
#endif
    return ready;
}

MeshInkRadioFailureClass t5_classify_radio_failure() {
#if T5_BOARD_H752_01
    // The H752-01 Pro Lite shares the Pro PCB but leaves both SX1262 and GNSS
    // unpopulated. There is no dedicated Lite ID pin, so this is deliberately
    // a hardware-match classification rather than an absolute identity claim.
    uint8_t pca_config=0;
    if(!pca_read(0x06,pca_config)) {
        Serial.println("[T5-HW] radio failure: PCA9535 absent; not classifying as H752-01 Lite");
        return MeshInkRadioFailureClass::Unknown;
    }

    // If valid NMEA is present, this is a GPS-equipped Pro whose SX1262 failed.
    // Probe both modules/baud rates independently of MeshCore's GPS UI setting.
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    if(!Serial1.baudRate())Serial1.begin(9600);
    bool gps_present=false;
    for(uint8_t pass=0;pass<2&&!gps_present;++pass) {
        for(const uint32_t baud : {9600UL,38400UL}) {
            Serial1.updateBaudRate(baud);
            gps_stream.clearValidation();
            while(Serial1.available()>0)Serial1.read();
            const uint32_t started=millis();
            while(millis()-started<1600) {
                while(gps_stream.available())gps_stream.read();
                if(gps_stream.hasValidSentence()){gps_present=true;break;}
                delay(5);
            }
            if(gps_present)break;
        }
    }
    Serial.printf("[T5-HW] radio failure classification: H752-01=yes GPS-NMEA=%s\n",
                  gps_present?"yes":"no");
    return gps_present?MeshInkRadioFailureClass::RadioFault:MeshInkRadioFailureClass::MissingHardwareVariant;
#else
    return MeshInkRadioFailureClass::Unknown;
#endif
}

mesh::LocalIdentity radio_new_identity() {
    T5_TRACE("identity: collecting SX1262 radio noise\n");
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}
