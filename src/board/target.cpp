#include <Arduino.h>
#include <SPI.h>
#include <epdiy.h>
#include <esp_heap_caps.h>
#include <driver/i2c.h>
#include <sys/time.h>
#include <RTClib.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.0.0"
#endif

#if T5_DIAGNOSTICS
#define T5_TRACE(...) Serial.printf("[T5] " __VA_ARGS__)
#else
#define T5_TRACE(...) do {} while (0)
#endif

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

bool T5Board::enableRadioGpsRail(){
    // LilyGO maps LORA_EN (shared LoRa/GPS 3V3 rail) to PCA9535 port 0 bit 0.
    // Preserve every display-owned bit: update only IO0_0 while the panel is idle.
    constexpr uint8_t OUTPUT_PORT0=0x02,CONFIG_PORT0=0x06,LORA_EN=0x01;
    uint8_t output=0,config=0;
    if(!pca_read(OUTPUT_PORT0,output)||!pca_read(CONFIG_PORT0,config)){
        T5_TRACE("power rail: PCA9535 read failed\n");return false;
    }
    const uint8_t requested_output=(uint8_t)(output|LORA_EN);
    const uint8_t requested_config=(uint8_t)(config&~LORA_EN);
    // Set the output latch first so the rail cannot glitch low when direction changes.
    if(!pca_write(OUTPUT_PORT0,requested_output)||!pca_write(CONFIG_PORT0,requested_config)){
        T5_TRACE("power rail: PCA9535 write failed output=0x%02X config=0x%02X\n",requested_output,requested_config);return false;
    }
    uint8_t verified_output=0,verified_config=0;
    const bool verified=pca_read(OUTPUT_PORT0,verified_output)&&pca_read(CONFIG_PORT0,verified_config)&&
        (verified_output&LORA_EN)&&!(verified_config&LORA_EN);
    T5_TRACE("power rail: PCA9535 output0 0x%02X->0x%02X config0 0x%02X->0x%02X verify=%s\n",
        output,verified_output,config,verified_config,verified?"OK":"FAILED");
    if(verified)delay(150);
    return verified;
}

// Board mapping only. The upstream wrapper controls radio parameters and
// transmit/receive/power state through MeshCore.
static SPIClass radio_spi(FSPI);
SPIClass& t5_shared_spi() { return radio_spi; }
static CustomSX1262 radio = new Module(
    P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, radio_spi);
CustomSX1262Wrapper radio_driver(radio, board);

T5RTCClock rtc_clock;
static uint32_t detected_gps_baud = 9600;
static bool gps_baud_locked = false;
enum class GpsModule : uint8_t { Unknown, L76K, MiaM10Q };
static GpsModule detected_gps_module = GpsModule::Unknown;
static bool gps_command_sleeping = false;
static uint32_t gps_last_byte_at = 0;
static uint32_t gps_sleep_requested_at = 0;
static uint32_t gps_sleep_bytes_after = 0;
static uint32_t gps_wake_requested_at = 0;
static void t5_power_diagnostics_tick();
static void t5_power_diagnostics_report(const char* reason);

static const char* gps_module_name() {
    switch (detected_gps_module) {
        case GpsModule::L76K: return "L76K";
        case GpsModule::MiaM10Q: return "MIA-M10Q";
        default: return "UNKNOWN";
    }
}

static void gps_wake_command() {
    if (!gps_command_sleeping) return;
    // L76K exits PMTK standby on any UART activity. Do not send an
    // unverified binary command to an unknown/u-blox receiver.
    if (detected_gps_module == GpsModule::L76K) {
        Serial1.write((uint8_t)'\r'); Serial1.write((uint8_t)'\n'); Serial1.flush();
        delay(120);
        gps_wake_requested_at=millis();
        T5_TRACE("gps probe: L76K wake bytes sent; watching for NMEA resume\n");
    }
    gps_command_sleeping = false;
}

static void gps_sleep_command() {
    if (gps_command_sleeping) return;
    if (detected_gps_module == GpsModule::L76K) {
        // PMTK161 standby retains data for a fast warm start.
        Serial1.print("$PMTK161,0*28\r\n"); Serial1.flush();
        gps_command_sleeping = true;
        gps_sleep_requested_at=millis();gps_sleep_bytes_after=0;
        T5_TRACE("gps probe: PMTK161 standby requested; this is EXPERIMENTAL on L76K, watching UART to verify actual sleep\n");
    } else {
        T5_TRACE("gps power: sleep skipped module=%s (radio/GPS rail is shared)\n", gps_module_name());
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
    T5GPS() : MicroNMEALocationProvider(gps_stream, &rtc_clock) {}
    void begin() override {
        Serial1.updateBaudRate(detected_gps_baud);
        gps_wake_command();
        MicroNMEALocationProvider::begin();
        active = true;
        next_baud_retry = millis() + 6000;
        T5_TRACE("gps: enabled by MeshCore sensor setting\n");
    }
    void stop() override {
        MicroNMEALocationProvider::stop();
        active = false;
        gps_sleep_command();
        T5_TRACE("gps: disabled by MeshCore sensor setting\n");
    }
    void loop() override {
        t5_power_diagnostics_tick();
#if T5_DIAGNOSTICS
        const int pending = Serial1.available();
        if (pending > 0) {
            gps_last_byte_at = millis();
            if(gps_command_sleeping)gps_sleep_bytes_after+=(uint32_t)pending;
            if(gps_wake_requested_at){
                T5_TRACE("gps probe: UART resumed %lums after wake request (%d bytes pending)\n",
                    (unsigned long)(millis()-gps_wake_requested_at),pending);
                gps_wake_requested_at=0;
            }
        }
        static uint32_t last_power_probe=0;
        if(gps_command_sleeping&&gps_sleep_requested_at&&millis()-last_power_probe>=5000){
            last_power_probe=millis();
            T5_TRACE("gps probe: %lums after sleep request, UART bytes observed=%lu, pending=%d; %s\n",
                (unsigned long)(millis()-gps_sleep_requested_at),(unsigned long)gps_sleep_bytes_after,pending,
                gps_sleep_bytes_after?"receiver still talking (not asleep)":"UART quiet (sleep plausible, not electrical proof)");
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
            const bool rtc_needs_time = !rtc_clock.isValid();
            const bool hourly_correction_due = last_gps_clock_sync_ms == 0 ||
                now_ms - last_gps_clock_sync_ms >= 3600000UL;
            if (rtc_needs_time || hourly_correction_due) {
                rtc_clock.expectGpsTime((uint32_t)getTimestamp());
                last_gps_clock_sync_ms = now_ms ? now_ms : 1;
            }
        }
        MicroNMEALocationProvider::loop();
        if (active && !gps_baud_locked && gps_stream.hasValidSentence()) {
            gps_baud_locked = true;
            detected_gps_baud = Serial1.baudRate();
            detected_gps_module = detected_gps_baud == 9600 ? GpsModule::L76K : GpsModule::MiaM10Q;
            T5_TRACE("gps: background probe locked %u baud module=%s with valid NMEA\n", detected_gps_baud, gps_module_name());
        } else if (active && !gps_baud_locked && millis() >= next_baud_retry) {
            detected_gps_baud = Serial1.baudRate() == 9600 ? 38400 : 9600;
            Serial1.updateBaudRate(detected_gps_baud);
            gps_stream.clearValidation();
            MicroNMEALocationProvider::syncTime();
            next_baud_retry = millis() + 6000;
            T5_TRACE("gps: background probe trying %u baud\n", detected_gps_baud);
        }
        if (active && gps_baud_locked && !gps_command_sleeping && gps_last_byte_at && millis() - gps_last_byte_at > 30000) {
            T5_TRACE("gps: NMEA watchdog expired after %lu ms; waking and reprobe enabled\n", (unsigned long)(millis() - gps_last_byte_at));
            gps_command_sleeping = true;
            gps_wake_command();
            gps_stream.clearValidation();
            gps_baud_locked = false;
            next_baud_retry = millis() + 6000;
            gps_last_byte_at = millis();
        }
#if T5_DIAGNOSTICS
        static uint32_t last_report = 0;
        if (millis() - last_report >= 15000) {
            last_report = millis();
            T5_TRACE("gps: baud=%u incoming=%d fix=%d satellites=%ld lat=%ld lon=%ld\n",
                     Serial1.baudRate(), pending, isValid(), satellitesCount(),
                     getLatitude(), getLongitude());
        }
#endif
    }
};
static T5GPS gps;
EnvironmentSensorManager sensors(gps);

// BQ27220 on the shared I2C bus. Standard telemetry is read-only. The
// profile audit selects data-memory addresses for READING only; it never
// enters config-update, unseals the gauge or changes its battery model.
// Do not change capacity before the installed cell's voltage/charge limits
// and active profile have been verified on the physical board.
static constexpr uint8_t BQ27220_ADDR = 0x55;
static bool gauge_word(uint8_t command, uint16_t& result) {
    uint8_t data[2]{};
    if(!idf_read(BQ27220_ADDR,command,data,sizeof(data)))return false;
    const uint8_t lo=data[0],hi=data[1];
    result = static_cast<uint16_t>(lo | (static_cast<uint16_t>(hi) << 8));
    // SLUUBD4A section 5.3 requires at least 66 us bus-free time between
    // packets at 400 kHz. Keep diagnostic snapshots comfortably compliant.
    delayMicroseconds(70);
    return true;
}

// LILYGO H752-01 1500-mAh CEDV profile, from the manufacturer's
// lib/BQ27220/bq27220_data_memory.c (H752-01 branch).
// This is RAM/data-memory configuration only: no OTP programming, cell
// charging-voltage changes, or modification of BQ25896 charger registers.
struct GaugeProfileField {uint16_t address,value;uint8_t bytes;};
static constexpr GaugeProfileField T5_FACTORY_GAUGE_PROFILE[]={
    {0x929B,0x0D31,2}, // CEDV config: CCT, SC, FIXED_EDV0, FCC_LIM, FC_FOR_VDQ, IGNORE_SD
    {0x9206,0x0C8C,2},{0x9208,0x004C,1},
    {0x929D,1500,2},{0x929F,1500,2}, // initial FCC, design capacity
    {0x92A3,3743,2},{0x92A9,149,2},{0x92AB,867,2},
    {0x92AD,4030,2},{0x92AF,316,2},{0x92B1,9,1},{0x92B2,0,1},
    {0x92BD,4173,2},{0x92BF,4043,2},{0x92C1,3925,2},
    {0x92C3,3821,2},{0x92C5,3725,2},{0x92C7,3665,2},
    {0x92C9,3619,2},{0x92CB,3585,2},{0x92CD,3515,2},
    {0x92CF,3439,2},{0x92D1,2713,2},
    {0x92B4,3031,2},{0x92B7,3385,2},{0x92BA,3501,2},
    {0x91DE,1,1},{0x9217,1,2}
};
static constexpr size_t T5_PROFILE_COUNT=sizeof(T5_FACTORY_GAUGE_PROFILE)/sizeof(T5_FACTORY_GAUGE_PROFILE[0]);

static bool gauge_control_command(uint16_t cmd) {
    const uint8_t bytes[2]={(uint8_t)cmd,(uint8_t)(cmd>>8)};
    if(!idf_write(BQ27220_ADDR,0x00,bytes,sizeof(bytes)))return false;
    delay(5);
    return true;
}
static bool gauge_security_state(uint8_t& sec,bool& cfgupdate) {
    uint16_t status=0;if(!gauge_word(0x3A,status))return false;
    sec=(status>>1)&3;cfgupdate=(status&0x0400)!=0;
    return true;
}
static bool gauge_wait_state(uint8_t security,int cfgupdate,uint32_t timeout_ms) {
    const uint32_t start=millis();
    do {
        uint8_t sec=0;bool cfg=false;
        if(gauge_security_state(sec,cfg)&&sec==security&&
           (cfgupdate<0||(int)cfg==cfgupdate))return true;
        delay(10);
    }while(millis()-start<timeout_ms);
    return false;
}
static bool gauge_profile_read(const GaugeProfileField& field,uint16_t& value) {
    const uint8_t address[2]={(uint8_t)field.address,(uint8_t)(field.address>>8)};
    uint8_t bytes[2]{};
    if(!idf_write(BQ27220_ADDR,0x3E,address,sizeof(address)))return false;
    delay(2);
    if(!idf_read(BQ27220_ADDR,0x40,bytes,field.bytes))return false;
    value=field.bytes==1?bytes[0]:((uint16_t)bytes[0]<<8)|bytes[1];
    delayMicroseconds(70);
    return true;
}
static bool gauge_profile_write(const GaugeProfileField& field,uint16_t value) {
    // TI's BQ27220 MAC transaction: address in little-endian, parameter
    // in big-endian, then checksum and packet length at 0x60/0x61.
    uint8_t packet[4]={(uint8_t)field.address,(uint8_t)(field.address>>8),0,0};
    if(field.bytes==1)packet[2]=(uint8_t)value;
    else {packet[2]=(uint8_t)(value>>8);packet[3]=(uint8_t)value;}
    uint16_t sum=0;for(unsigned i=0;i<(unsigned)field.bytes+2;++i)sum+=packet[i];
    const uint8_t commit[2]={(uint8_t)(0xFF-(sum&0xFF)),(uint8_t)(field.bytes+4)};
    if(!idf_write(BQ27220_ADDR,0x3E,packet,field.bytes+2))return false;
    delayMicroseconds(300);
    if(!idf_write(BQ27220_ADDR,0x60,commit,sizeof(commit)))return false;
    delay(12);
    uint16_t readback=0;
    return gauge_profile_read(field,readback)&&readback==value;
}
static void gauge_apply_factory_profile_if_needed() {
    uint16_t design=0,voltage=0,battery_status=0,operation=0,control=0;
    if(!gauge_word(0x3C,design)||!gauge_word(0x08,voltage)||
       !gauge_word(0x0A,battery_status)||!gauge_word(0x3A,operation)||
       !gauge_word(0x00,control)) {
        T5_TRACE("gauge profile: telemetry unavailable; no changes made\n");return;
    }
    if(design==1500) {
        T5_TRACE("gauge profile: design=1500mAh; no configuration write needed\n");return;
    }
    // Only migrate the board's known factory-default 3000-mAh mismatch.
    // Do not overwrite an unknown/replacement battery or unexpected gauge state.
    if(design!=3000||voltage<3000||voltage>4250||!(battery_status&0x0008)||
       !(operation&0x0020)||(operation&0x0400)||(control&0x0007)) {
        T5_TRACE("gauge profile: SKIP unexpected state design=%u voltage=%u battery=0x%04X operation=0x%04X control=0x%04X\n",
                 design,voltage,battery_status,operation,control);return;
    }
    T5_TRACE("gauge profile: factory 1500mAh migration requested (reported=%umAh)\n",design);
    uint8_t sec=(operation>>1)&3;bool cfg=(operation&0x0400)!=0;
    bool unlocked=false,config_entered=false,profile_ok=false,rollback_ok=true;
    uint16_t originals[T5_PROFILE_COUNT]{};
    bool modified[T5_PROFILE_COUNT]{};
    size_t changed=0;
    do {
        if(sec==3) {
            if(!gauge_control_command(0x0414)||!gauge_control_command(0x3672)||
               !gauge_wait_state(2,0,500)) {
                T5_TRACE("gauge profile: UNSEAL failed\n");break;
            }
        } else if(sec!=2&&sec!=1) {
            T5_TRACE("gauge profile: unexpected security state=%u\n",sec);break;
        }
        unlocked=true;
        if(sec!=1) {
            if(!gauge_control_command(0xFFFF)||!gauge_control_command(0xFFFF)||
               !gauge_wait_state(1,0,500)) {
                T5_TRACE("gauge profile: FULL ACCESS failed\n");break;
            }
        }
        for(size_t i=0;i<T5_PROFILE_COUNT;++i) {
            if(!gauge_profile_read(T5_FACTORY_GAUGE_PROFILE[i],originals[i])) {
                T5_TRACE("gauge profile: preflight read failed at 0x%04X; NO WRITES\n",
                         T5_FACTORY_GAUGE_PROFILE[i].address);break;
            }
            ++changed;
        }
        if(changed!=T5_PROFILE_COUNT)break;
        if(originals[4]!=3000) {
            T5_TRACE("gauge profile: preflight design RAM=%u differs from live design=3000; aborting\n",originals[4]);
            break;
        }
        changed=0;
        for(size_t i=0;i<T5_PROFILE_COUNT;++i)
            if(originals[i]!=T5_FACTORY_GAUGE_PROFILE[i].value)++changed;
        T5_TRACE("gauge profile: verified %u fields, %u differ from LILYGO profile\n",
                 (unsigned)T5_PROFILE_COUNT,(unsigned)changed);
        if(!gauge_control_command(0x0090)||!gauge_wait_state(1,1,1200)) {
            T5_TRACE("gauge profile: CONFIG UPDATE entry failed\n");break;
        }
        config_entered=true;
        bool write_ok=true;
        for(size_t i=0;i<T5_PROFILE_COUNT;++i) {
            const auto& field=T5_FACTORY_GAUGE_PROFILE[i];
            if(originals[i]==field.value)continue;
            // A failed verify can occur AFTER the hardware committed data.
            // Mark it first so even that field is included in rollback.
            modified[i]=true;
            if(!gauge_profile_write(field,field.value)) {
                T5_TRACE("gauge profile: WRITE OR VERIFY FAILED address=0x%04X\n",field.address);
                write_ok=false;break;
            }
        }
        if(!write_ok) {
            // Best-effort rollback of fields already changed, while still in
            // CFGUPDATE. A failed rollback is reported loudly, never hidden.
            for(size_t i=0;i<T5_PROFILE_COUNT;++i)if(modified[i])
                if(!gauge_profile_write(T5_FACTORY_GAUGE_PROFILE[i],originals[i]))rollback_ok=false;
            T5_TRACE("gauge profile: update failed; rollback=%s\n",rollback_ok?"OK":"FAILED");
            break;
        }
        if(!gauge_control_command(0x0091)) {
            T5_TRACE("gauge profile: EXIT/REINIT command failed\n");break;
        }
        config_entered=false;
        delay(2000);
        uint8_t post_sec=0;bool post_cfg=true;
        uint16_t result=0,fcc=0,soc=0;
        if(gauge_security_state(post_sec,post_cfg)&&!post_cfg&&
           gauge_word(0x3C,result)&&gauge_word(0x12,fcc)&&gauge_word(0x2C,soc)&&
           result==1500&&fcc<=1500&&soc<=100) {
            profile_ok=true;
            T5_TRACE("gauge profile: SUCCESS design=%umAh FCC=%umAh SOC=%u%%\n",result,fcc,soc);
        } else T5_TRACE("gauge profile: POST-UPDATE VALIDATION FAILED design=%u FCC=%u SOC=%u cfg=%u\n",
                       result,fcc,soc,post_cfg);
    }while(false);
    if(config_entered) {
        // Whether update or rollback failed, leave the gauge out of CFGUPDATE.
        if(!gauge_control_command(0x0092))T5_TRACE("gauge profile: EMERGENCY CONFIG EXIT FAILED\n");
        delay(300);
    }
    if(unlocked) {
        if(!gauge_control_command(0x0030)||!gauge_wait_state(3,0,1000))
            T5_TRACE("gauge profile: WARNING reseal failed; inspect gauge before reboot\n");
    } else {
        // An interrupted unseal attempt may have succeeded; seal defensively.
        gauge_control_command(0x0030);
    }
    if(!profile_ok)T5_TRACE("gauge profile: NOT VERIFIED; do not trust battery percentage\n");
}

#if T5_DIAGNOSTICS
static bool gauge_dm_read_word(uint16_t address,uint16_t& result) {
    // TI SLUUBD4A section 6.1: select the RAM address at 0x3E/0x3F,
    // then read its big-endian value from the MAC data window at 0x40.
    // The address selector is LOW byte first (for 0x929F: 0x9F,0x92).
    // Merely selecting an address does NOT commit a change to data memory.
    const uint8_t pointer[2]={(uint8_t)address,(uint8_t)(address>>8)};
    uint8_t bytes[2]{};
    if(!idf_write(BQ27220_ADDR,0x3E,pointer,sizeof(pointer)))return false;
    delay(2);
    if(!idf_read(BQ27220_ADDR,0x40,bytes,sizeof(bytes)))return false;
    result=((uint16_t)bytes[0]<<8)|bytes[1];
    delayMicroseconds(70);
    return true;
}
#endif

static bool pmic_byte(uint8_t reg, uint8_t& result) {
    constexpr uint8_t BQ25896_ADDR = 0x6B;
    return idf_read(BQ25896_ADDR,reg,&result,1);
}

struct GaugeDiagnosticSnapshot {
    uint16_t control_status=0;
    uint16_t temperature=0;
    uint16_t voltage=0;
    uint16_t battery_status=0;
    uint16_t current_raw=0;
    uint16_t remaining=0;
    uint16_t full=0;
    uint16_t cycle_count=0;
    uint16_t soc=0;
    uint16_t soh=0;
    uint16_t charging_voltage=0;
    uint16_t charging_current=0;
    uint16_t operation_status=0;
    uint16_t design_capacity=0;
};

static bool gauge_diagnostic_snapshot(GaugeDiagnosticSnapshot& s) {
    // Registers and byte order are BQ27220-specific (TI SLUUBD4A, table 2-1).
    return gauge_word(0x00,s.control_status)&&gauge_word(0x06,s.temperature)&&
        gauge_word(0x08,s.voltage)&&gauge_word(0x0A,s.battery_status)&&
        gauge_word(0x0C,s.current_raw)&&gauge_word(0x10,s.remaining)&&
        gauge_word(0x12,s.full)&&gauge_word(0x2A,s.cycle_count)&&
        gauge_word(0x2C,s.soc)&&gauge_word(0x2E,s.soh)&&
        gauge_word(0x30,s.charging_voltage)&&gauge_word(0x32,s.charging_current)&&
        gauge_word(0x3A,s.operation_status)&&gauge_word(0x3C,s.design_capacity);
}

static uint8_t last_charger_state=0xFF;

static void t5_power_diagnostics_report(const char* reason) {
#if T5_DIAGNOSTICS
    uint8_t reg00=0,reg04=0,reg0b=0,reg0c=0,reg0e=0,reg10=0,reg11=0,reg12=0;
    const bool charger_ok=pmic_byte(0x00,reg00)&&pmic_byte(0x04,reg04)&&
        pmic_byte(0x0B,reg0b)&&pmic_byte(0x0C,reg0c)&&pmic_byte(0x0E,reg0e)&&
        pmic_byte(0x10,reg10)&&pmic_byte(0x11,reg11)&&pmic_byte(0x12,reg12);
    if(charger_ok){
        static const char* charge_names[]={"IDLE","PRECHARGE","FAST","DONE"};
        const uint8_t charge=(reg0b>>3)&0x03;
        const unsigned input_limit=100U+50U*(reg00&0x3F);
        const unsigned target_current=64U*(reg04&0x7F);
        const unsigned adc_battery=2304U+20U*(reg0e&0x7F);
        const unsigned adc_vbus=2600U+100U*(reg11&0x7F);
        const unsigned adc_charge=50U*(reg12&0x7F);
        T5_TRACE("power snapshot reason=%s uptime=%lums\n",reason,(unsigned long)millis());
        T5_TRACE("charger: VBUS=%umV good=%u source=%u state=%s(%u) IINLIM=%umA ICHG_TARGET=%umA ICHG_ADC=%umA BAT_ADC=%umV TS=0x%02X fault=0x%02X\n",
            adc_vbus,(reg11>>7)&1,(reg0b>>5)&7,charge_names[charge],charge,input_limit,target_current,
            adc_charge,adc_battery,reg10,reg0c);
        last_charger_state=charge;
    }else T5_TRACE("charger: BQ25896 diagnostic read failed\n");

    GaugeDiagnosticSnapshot s{};
    if(gauge_diagnostic_snapshot(s)){
        const int current=(int16_t)s.current_raw;
        const int temp_c10=(int)s.temperature-2731;
        T5_TRACE("gauge: voltage=%umV current=%dmA SOC=%u%% SOH=%u%% RM=%umAh FCC=%umAh Design=%umAh cycles=%u temp=%d.%dC\n",
            s.voltage,current,s.soc,s.soh,s.remaining,s.full,s.design_capacity,s.cycle_count,
            temp_c10/10,abs(temp_c10%10));
        T5_TRACE("gauge request: charging_voltage=%umV charging_current=%umA\n",
            s.charging_voltage,s.charging_current);
        if(s.design_capacity!=1500)
            T5_TRACE("gauge: CAPACITY MISMATCH fitted=1500mAh reported=%umAh; SOC and FCC not yet trustworthy\n",s.design_capacity);
        if(!strcmp(reason,"early-boot")) {
            const uint8_t security=(s.operation_status>>1)&3;
            if(security==3) {
                T5_TRACE("gauge profile audit: SEALED (SEC=3), data-memory values NOT readable; previous 0 readings were invalid, not settings\n");
            } else {
                uint16_t full=0,design=0,nominal=0,charge_current=0,charge_voltage=0,taper=0;
                const bool profile_ok=gauge_dm_read_word(0x929D,full)&&
                    gauge_dm_read_word(0x929F,design)&&gauge_dm_read_word(0x92A3,nominal);
                const bool charge_ok=gauge_dm_read_word(0x91FB,charge_current)&&
                    gauge_dm_read_word(0x91FD,charge_voltage)&&gauge_dm_read_word(0x9201,taper);
                if(profile_ok&&design>=100&&design<=32000&&nominal>=2500&&nominal<=5000)
                    T5_TRACE("gauge profile1 RAM: initial_FCC=%umAh design=%umAh nominal=%umV\n",full,design,nominal);
                else T5_TRACE("gauge profile1 RAM: invalid or unavailable; do not infer values\n");
                if(charge_ok&&charge_voltage>=2500&&charge_voltage<=4600)
                    T5_TRACE("gauge profile RAM: requested_charge=%umA charge_voltage=%umV taper=%umA\n",
                        charge_current,charge_voltage,taper);
                else T5_TRACE("gauge profile RAM: invalid or unavailable; do not infer values\n");
            }
            T5_TRACE("gauge profile audit: no unseal, config change or calibration performed\n");
        }
        T5_TRACE("gauge BatteryStatus=0x%04X FC=%u TCA=%u OCVCOMP=%u OCVFAIL=%u OCVGD=%u BATTPRES=%u SLEEP=%u SYSDWN=%u DSG=%u\n",
            s.battery_status,(s.battery_status>>9)&1,(s.battery_status>>6)&1,
            (s.battery_status>>14)&1,(s.battery_status>>13)&1,(s.battery_status>>5)&1,
            (s.battery_status>>3)&1,(s.battery_status>>12)&1,(s.battery_status>>1)&1,
            s.battery_status&1);
        T5_TRACE("gauge OperationStatus=0x%04X INITCOMP=%u CFGUPDATE=%u VDQ=%u SMTH=%u SEC=%u CALMD=%u ControlStatus=0x%04X CCA=%u BCA=%u SNOOZE=%u BATT_ID=%u\n",
            s.operation_status,(s.operation_status>>5)&1,(s.operation_status>>10)&1,
            (s.operation_status>>4)&1,(s.operation_status>>6)&1,
            (s.operation_status>>1)&3,s.operation_status&1,s.control_status,
            (s.control_status>>5)&1,(s.control_status>>4)&1,(s.control_status>>3)&1,
            s.control_status&7);
    }else T5_TRACE("gauge: BQ27220 diagnostic read failed\n");
#else
    (void)reason;
#endif
}

static void t5_power_diagnostics_tick() {
#if T5_DIAGNOSTICS
    static uint32_t probed_at=0;
    static uint32_t reported_at=0;
    const uint32_t now=millis();
    if(probed_at&&now-probed_at<5000)return;
    probed_at=now?now:1;

    uint8_t reg0b=0;
    if(!pmic_byte(0x0B,reg0b))return;
    const uint8_t state=(reg0b>>3)&0x03;
    const bool first=last_charger_state==0xFF;
    const bool changed=!first&&state!=last_charger_state;
    const bool fast_to_done=last_charger_state==2&&state==3;
    const bool periodic=!reported_at||now-reported_at>=60000;
    if(changed||periodic){
        const char* reason=fast_to_done?"charger-FAST-to-DONE":
            (changed?"charger-state-change":"periodic");
        t5_power_diagnostics_report(reason);
        reported_at=now?now:1;
    }else{
        last_charger_state=state;
    }
#endif
}

uint16_t T5Board::getBattMilliVolts() {
    // MeshCore may ask repeatedly while composing device responses. Avoid
    // repetitive I2C traffic and keep the last valid voltage on read errors.
    static uint16_t cached_mv = 0;
    static uint32_t sampled_at = 0;
    const uint32_t now = millis();
    if (sampled_at != 0 && now - sampled_at < 30000) return cached_mv;
    sampled_at = now == 0 ? 1 : now;

    uint16_t voltage = 0;
    if (gauge_word(0x08, voltage) && voltage >= 2500 && voltage <= 5000) {
        cached_mv = voltage;
        uint16_t soc = 0;
        if (gauge_word(0x2C, soc) && soc <= 100)
            T5_TRACE("battery: BQ27220 voltage=%u mV SOC=%u%%\n", cached_mv, soc);
        else
            T5_TRACE("battery: BQ27220 voltage=%u mV; SOC unavailable\n", cached_mv);
    } else {
        T5_TRACE("battery: BQ27220 read failed or voltage invalid, cached=%u mV\n", cached_mv);
    }
    return cached_mv;
}

// A tiny fixed glyph set avoids loading a font, a graphics task, or a UI
// framework into the companion-only image. Each row is a five-bit bitmap.
struct Glyph { char letter; uint8_t rows[7]; };
static constexpr Glyph notice_glyphs[] = {
    {'0',{14,17,19,21,25,17,14}}, {'1',{4,12,4,4,4,4,14}},
    {'2',{14,17,1,2,4,8,31}}, {'3',{30,1,1,14,1,1,30}},
    {'4',{2,6,10,18,31,2,2}},
    {'5',{31,16,16,30,1,1,30}}, {'6',{14,16,16,30,17,17,14}},
    {'7',{31,1,2,4,8,8,8}}, {'8',{14,17,17,14,17,17,14}},
    {'9',{14,17,17,15,1,1,14}},
    {'.',{0,0,0,0,0,6,6}},
    {'A',{14,17,17,31,17,17,17}}, {'B',{30,17,17,30,17,17,30}},
    {'C',{14,17,16,16,16,17,14}}, {'D',{30,17,17,17,17,17,30}},
    {'E',{31,16,16,30,16,16,31}}, {'F',{31,16,16,30,16,16,16}},
    {'G',{14,17,16,23,17,17,15}}, {'H',{17,17,17,31,17,17,17}},
    {'I',{31,4,4,4,4,4,31}}, {'L',{16,16,16,16,16,16,31}},
    {'M',{17,27,21,21,17,17,17}},
    {'N',{17,25,21,19,17,17,17}}, {'O',{14,17,17,17,17,17,14}},
    {'P',{30,17,17,30,16,16,16}}, {'R',{30,17,17,30,20,18,17}},
    {'S',{15,16,16,14,1,1,30}}, {'T',{31,4,4,4,4,4,4}},
    {'U',{17,17,17,17,17,17,14}}, {'X',{17,17,10,4,10,17,17}},
};

static void notice_text(const char* message, int x, int y, int scale, uint8_t* fb, bool bold = false) {
    for (const char* c = message; *c; ++c, x += 6 * scale) {
        for (const Glyph& glyph : notice_glyphs) {
            if (glyph.letter != *c) continue;
            for (int row = 0; row < 7; ++row) {
                for (int col = 0; col < 5; ++col) {
                    if (!(glyph.rows[row] & (1 << (4 - col)))) continue;
                    for (int dy = 0; dy < scale; ++dy) {
                        for (int dx = 0; dx < scale; ++dx) {
                            epd_draw_pixel(x + col * scale + dx,
                                           y + row * scale + dy, 0, fb);
                            if (bold) epd_draw_pixel(x + col * scale + dx + 1,
                                                     y + row * scale + dy, 0, fb);
                        }
                    }
                }
            }
            break;
        }
    }
}

static void notice_centred(const char* message, int y, int scale, uint8_t* fb, bool bold = false) {
    notice_text(message, (540 - (int)strlen(message) * 6 * scale) / 2, y, scale, fb, bold);
}

static void show_companion_notice() {
    T5_TRACE("notice: epd_init, internal heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
    epd_init(&epd_board_v7, &ED047TC1, EPD_LUT_64K);
    T5_TRACE("notice: panel initialized\n");
    epd_set_rotation(EPD_ROT_INVERTED_PORTRAIT);
    epd_set_lcd_pixel_clock_MHz(17);
    EpdiyHighlevelState display = epd_hl_init(EPD_BUILTIN_WAVEFORM);
    uint8_t* fb = epd_hl_get_framebuffer(&display);
    T5_TRACE("notice: framebuffer=%p, heap=%u, psram=%u\n", fb, ESP.getFreeHeap(), ESP.getFreePsram());
    if (fb) {
        epd_hl_set_all_white(&display);
        notice_centred("MESHCORE", 290, 7, fb, true);
        notice_centred("BT COMPANION MODE", 410, 4, fb);
        notice_centred("PRESS AND HOLD BOOT BUTTON", 770, 2, fb);
        notice_centred("2 SECONDS TO EXIT", 805, 2, fb);
        notice_centred(T5_FIRMWARE_VERSION, 900, 2, fb);
        T5_TRACE("notice: text rendered, powering panel on\n");
        epd_poweron();
        T5_TRACE("notice: full panel clear start\n");
        epd_clear();
        T5_TRACE("notice: full panel clear complete\n");
        T5_TRACE("notice: refresh start\n");
        const EpdDrawError result = epd_hl_update_screen(
            &display, MODE_GL16, static_cast<int>(epd_ambient_temperature()));
        epd_poweroff();
        T5_TRACE("notice: refresh result=%d; panel power off\n", result);
    } else {
        epd_poweroff();
        T5_TRACE("notice: framebuffer unavailable; panel power off\n");
    }
    // EPDiy 2.0 has no high-level teardown API. Its one-time buffers are
    // no longer needed after the panel update; reclaim PSRAM and DRAM for BLE.
    heap_caps_free(display.front_fb);
    heap_caps_free(display.back_fb);
    heap_caps_free(display.difference_fb);
    free(display.dirty_lines);
    heap_caps_free(display.dirty_columns);
    T5_TRACE("notice: framebuffers reclaimed\n");
    // LCD data lines overlap the SX1262 SPI pins: release every display
    // peripheral before upstream MeshCore calls radio_init().
    epd_deinit();
    T5_TRACE("notice: display deinitialized, heap=%u, psram=%u\n", ESP.getFreeHeap(), ESP.getFreePsram());
}

void T5Board::begin() {
    // EPDiy owns I2C bus 0 while it refreshes the panel. The upstream board
    // calls Wire.begin() on this same bus, so initialize MeshCore only after
    // epd_deinit() releases EPDiy's driver and interrupts.
    pinMode(11, OUTPUT);
    digitalWrite(11, HIGH);
    T5_TRACE("board: begin; frontlight on; display notice before MeshCore I2C\n");
    show_companion_notice();
    digitalWrite(11, LOW);
    T5_TRACE("board: display rendered; frontlight off; handing control to MeshCore\n");
    T5_TRACE("board: notice complete; MeshCore board/I2C begin\n");
    ESP32Board::begin();
    T5_TRACE("board: MeshCore I2C ready\n");
    enableRadioGpsRail();
    gauge_apply_factory_profile_if_needed();
    getBattMilliVolts();
    t5_power_diagnostics_report("early-boot");
    T5_TRACE("board: disabling touch and frontlight\n");
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);  // GT911 disabled in companion mode
    digitalWrite(11, LOW); // frontlight remains disabled in companion mode
    // MeshCore's historical macro names are counterintuitive here:
    // HardwareSerial::setPins() takes (RX, TX), so these values must be
    // PIN_GPS_TX=44 (MCU RX) and PIN_GPS_RX=43 (MCU TX).
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    T5_TRACE("board: GPS UART ready; internal heap=%u\n", ESP.getFreeHeap());
}

void T5Board::beginLocal() {
    // The local UI initialized EPDiy and I2C first. Reinstalling the legacy
    // I2C driver here would abort; only perform MeshCore's remaining board work.
    startup_reason = BD_STARTUP_NORMAL;
    enableRadioGpsRail();
    // Unified/local mode calls beginLocal(), not begin(). Without this call
    // the 1500mAh factory-profile migration ran only in BLE companion mode.
    gauge_apply_factory_profile_if_needed();
    getBattMilliVolts();
    t5_power_diagnostics_report("early-boot");
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    T5_TRACE("board: local UI handoff complete; shared I2C retained\n");
}

bool radio_init() {
    T5_TRACE("radio: begin clock and RTC\n");
    rtc_clock.begin();
    T5_TRACE("radio: SX1262 init on SPI pins 14/21/13\n");
    const bool ready = radio.std_init(&radio_spi);
    T5_TRACE("radio: SX1262 init=%d, heap=%u\n", ready, ESP.getFreeHeap());
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
                T5_TRACE("gps: probe pass=%u baud=%lu valid-NMEA=%d\n", pass + 1, baud, found);
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
            T5_TRACE("gps: startup probe inconclusive; background retry enabled\n");
        }
        T5_TRACE("gps: selected baud=%u locked=%d module=%s; MeshCore owns position and settings\n",
                 Serial1.baudRate(), gps_baud_locked, gps_module_name());
    }
    return ready;
}

mesh::LocalIdentity radio_new_identity() {
    T5_TRACE("identity: collecting SX1262 radio noise\n");
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}
