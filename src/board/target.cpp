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
#include <driver/rtc_io.h>
#include <esp_sleep.h>
#include <sys/time.h>
#include <stdarg.h>
#include <RTClib.h>
#include "target.h"
#include "t5_board_backend.h"
#include "t5_logging.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

#ifndef T5_FIRMWARE_VERSION
#define T5_FIRMWARE_VERSION "1.0.0"
#endif

#define T5_TRACE(...) T5_DEBUGF(T5_LOG_BOARD, "[T5] " __VA_ARGS__)
#define T5_GPS_TRACE(...) T5_DEBUGF(T5_LOG_GPS, "[T5] " __VA_ARGS__)

T5Board board;

// A small RTC-retained transcript captures the early deep-sleep wake path while
// native USB CDC is still disconnected. It is never written to flash. The
// transcript survives serial reconnects and is replaced only when the device
// enters the next deep-sleep interval.
static constexpr uint32_t T5_WAKE_LOG_MAGIC=0x574C4731; // "WLG1"
static constexpr size_t T5_WAKE_LOG_BYTES=2048;
struct T5WakeLogState {
    uint32_t magic=0;
    uint16_t length=0;
    uint16_t dropped=0;
    char data[T5_WAKE_LOG_BYTES]{};
};
RTC_DATA_ATTR T5WakeLogState t5_wake_log_state{};

static void t5_wake_log_reset_for_sleep(){
    t5_wake_log_state.magic=T5_WAKE_LOG_MAGIC;
    t5_wake_log_state.length=0;
    t5_wake_log_state.dropped=0;
    t5_wake_log_state.data[0]=0;
}
void meshink_board_wake_log_append(const char* line){
    if(!line||t5_wake_log_state.magic!=T5_WAKE_LOG_MAGIC)return;
    const size_t input_len=strlen(line);
    size_t copied=0;
    while(copied<input_len&&t5_wake_log_state.length+1<T5_WAKE_LOG_BYTES){
        t5_wake_log_state.data[t5_wake_log_state.length++]=line[copied++];
    }
    if(copied<input_len)++t5_wake_log_state.dropped;
    if(t5_wake_log_state.length+1<T5_WAKE_LOG_BYTES&&
       (t5_wake_log_state.length==0||t5_wake_log_state.data[t5_wake_log_state.length-1]!='\n'))
        t5_wake_log_state.data[t5_wake_log_state.length++]='\n';
    t5_wake_log_state.data[t5_wake_log_state.length]=0;
}
void meshink_board_wake_log_appendf(const char* format,...){
    if(!format||t5_wake_log_state.magic!=T5_WAKE_LOG_MAGIC)return;
    char line[256]{};
    va_list args;
    va_start(args,format);
    vsnprintf(line,sizeof(line),format,args);
    va_end(args);
    meshink_board_wake_log_append(line);
}
void meshink_board_wake_log_replay(){
    if(t5_wake_log_state.magic!=T5_WAKE_LOG_MAGIC||!t5_wake_log_state.length){
        Serial.println("[T5-WAKELOG] no retained wake transcript");
        return;
    }
    Serial.println("[T5-WAKELOG] ---- retained wake transcript ----");
    Serial.write(reinterpret_cast<const uint8_t*>(t5_wake_log_state.data),
                 t5_wake_log_state.length);
    if(t5_wake_log_state.data[t5_wake_log_state.length-1]!='\n')Serial.println();
    if(t5_wake_log_state.dropped)
        Serial.printf("[T5-WAKELOG] WARNING: %u transcript fragments truncated\n",
                      (unsigned)t5_wake_log_state.dropped);
    Serial.println("[T5-WAKELOG] ---- end retained wake transcript ----");
}

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

static constexpr uint32_t T5_GPS_TIME_AUTHORITY_SECONDS=24UL*60UL*60UL;

void T5RTCClock::loadMetadata(){
    if(metadata_loaded_)return;
    metadata_loaded_=true;
    Preferences pref;
    if(pref.begin("t5-rtc",true)){
        last_gps_sync_utc_=pref.getULong("gps_sync",0);
        const uint8_t raw_source=pref.getUChar("source",(uint8_t)MeshInkTimeSource::Unknown);
        time_source_=raw_source<=(uint8_t)MeshInkTimeSource::Manual
            ?(MeshInkTimeSource)raw_source:MeshInkTimeSource::Unknown;
        const uint8_t default_mode=time_source_==MeshInkTimeSource::Manual
            ?(uint8_t)MeshInkTimeMode::Manual:(uint8_t)MeshInkTimeMode::Auto;
        const uint8_t raw_mode=pref.getUChar("mode",default_mode);
        time_mode_=raw_mode<=(uint8_t)MeshInkTimeMode::Manual
            ?(MeshInkTimeMode)raw_mode:MeshInkTimeMode::Auto;
        pref.end();
    }
}
void T5RTCClock::saveMetadata(){
    Preferences pref;
    if(pref.begin("t5-rtc",false)){
        pref.putULong("gps_sync",last_gps_sync_utc_);
        pref.putUChar("source",(uint8_t)time_source_);
        pref.putUChar("mode",(uint8_t)time_mode_);
        pref.end();
    }
}
bool T5RTCClock::gpsAuthorityActive(uint32_t current) const{
    return last_gps_sync_utc_&&current>=last_gps_sync_utc_&&
           current-last_gps_sync_utc_<=T5_GPS_TIME_AUTHORITY_SECONDS;
}
bool T5RTCClock::writeAcceptedTime(uint32_t utc,MeshInkTimeSource source){
    timeval tv{(time_t)utc,0};
    settimeofday(&tv,nullptr);

    if(source==MeshInkTimeSource::Gps)last_gps_sync_utc_=utc;
    else if(source==MeshInkTimeSource::Manual)last_gps_sync_utc_=0;
    time_source_=source;
    saveMetadata();

    if(!i2c_ready_){
        // MeshCore bootstraps a plausible clock from contact timestamps during
        // retained radio-first startup. Keep that as system time only: the
        // headless path deliberately has no shared I2C driver yet.
        deferred_hardware_time_=utc;
        T5_TRACE("rtc: deferred hardware write UTC=%lu source=%u; I2C lifecycle not initialized\n",
            (unsigned long)utc,(unsigned)source);
        return true;
    }

    const DateTime dt(utc);
    if(dt.year()<2000||dt.year()>2099){
        Serial.printf("[T5-WARN] rtc write outside PCF8563 year range UTC=%lu\n",
                      (unsigned long)utc);
        return false;
    }
    const uint8_t r[7]={to_bcd(dt.second()),to_bcd(dt.minute()),to_bcd(dt.hour()),
        to_bcd(dt.day()),to_bcd(dt.dayOfTheWeek()),to_bcd(dt.month()),
        to_bcd((uint8_t)(dt.year()-2000))};
    valid_=idf_write(0x51,0x02,r,sizeof(r));
    if(!valid_)Serial.println("[T5-WARN] rtc hardware write failed; system time remains active");
    return valid_;
}

void T5RTCClock::begin(){
    // begin() is only reached once the shared I2C lifecycle is active.
    // Retained headless wakes intentionally skip it.
    i2c_ready_=true;
    loadMetadata();
    uint8_t r[7]{};
    const bool read_ok=idf_read(0x51,0x02,r,sizeof(r));
    if(read_ok){
        const bool voltage_low=(r[0]&0x80)!=0;
        const uint8_t second=from_bcd(r[0]&0x7F),minute=from_bcd(r[1]&0x7F),hour=from_bcd(r[2]&0x3F);
        const uint8_t day=from_bcd(r[3]&0x3F),month=from_bcd(r[5]&0x1F),year=from_bcd(r[6]);
        valid_=!voltage_low&&second<60&&minute<60&&hour<24&&day>=1&&day<=31&&month>=1&&month<=12;
        if(valid_){
            deferred_hardware_time_=0;
            const uint32_t utc=DateTime(2000+year,month,day,hour,minute,second).unixtime();
            timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
            if(time_source_==MeshInkTimeSource::Unknown){
                time_source_=MeshInkTimeSource::HardwareRtc;
                saveMetadata();
            }
            Serial.println("[T5-INIT] rtc=PCF8563 OK");
            return;
        }
    }else{
        valid_=false;
    }

    // MeshCore may already have bootstrapped software time from saved contact
    // timestamps while this retained wake was headless. Only use that deferred
    // value when the hardware RTC itself is unavailable/invalid.
    const uint32_t deferred=deferred_hardware_time_;
    deferred_hardware_time_=0;
    if(deferred){
        setCurrentTime(deferred);
        if(valid_){
            Serial.println("[T5-INIT] rtc=PCF8563 restored from deferred startup time");
            return;
        }
    }

    Serial.println(read_ok
        ?"[T5-WARN] rtc=PCF8563 invalid; system/GPS fallback active"
        :"[T5-WARN] rtc=PCF8563 unavailable; system/GPS fallback active");
}
uint32_t T5RTCClock::getCurrentTime(){
    if(!valid_)return (uint32_t)time(nullptr);
    uint8_t r[7]{};if(!idf_read(0x51,0x02,r,sizeof(r))||(r[0]&0x80)){
        valid_=false;
        Serial.println("[T5-WARN] rtc read failed; system/GPS fallback active");
        return (uint32_t)time(nullptr);
    }
    return DateTime(2000+from_bcd(r[6]),from_bcd(r[5]&0x1F),from_bcd(r[3]&0x3F),
        from_bcd(r[2]&0x3F),from_bcd(r[1]&0x7F),from_bcd(r[0]&0x7F)).unixtime();
}
void T5RTCClock::setCurrentTime(uint32_t utc){
    loadMetadata();
    const uint32_t now_ms=millis();
    const uint32_t current=getCurrentTime();
    const bool trusted_gps=trusted_gps_time_&&now_ms<=trusted_gps_until_&&
        (utc>trusted_gps_time_?utc-trusted_gps_time_:trusted_gps_time_-utc)<=3;
    const bool trusted_companion=expected_companion_time_&&now_ms<=expected_companion_until_&&
        (utc>expected_companion_time_?utc-expected_companion_time_:expected_companion_time_-utc)<=3;
    trusted_gps_time_=0;trusted_gps_until_=0;
    expected_companion_time_=0;expected_companion_until_=0;

    if(time_mode_==MeshInkTimeMode::Manual){
        T5_TRACE("rtc: manual mode rejected external set UTC=%lu\n",(unsigned long)utc);
        return;
    }

    if(trusted_gps){
        writeAcceptedTime(utc,MeshInkTimeSource::Gps);
        return;
    }

    if(trusted_companion){
        if(valid_&&gpsAuthorityActive(current)){
            T5_TRACE("rtc: ignored companion UTC=%lu; GPS authority age=%lus\n",
                (unsigned long)utc,(unsigned long)(current-last_gps_sync_utc_));
            return;
        }
        writeAcceptedTime(utc,MeshInkTimeSource::Companion);
        return;
    }

    // Preserve MeshInk's protection against unsolicited provider/bootstrap
    // writes once the hardware RTC is valid. Companion time is the explicit
    // exception above, and keeps MeshCore's own command validation intact.
    if(valid_){
        T5_TRACE("rtc: ignored untrusted set UTC=%lu current=%lu\n",
            (unsigned long)utc,(unsigned long)current);
        return;
    }
    writeAcceptedTime(utc,MeshInkTimeSource::Protocol);
}
void T5RTCClock::expectGpsTime(uint32_t utc){
    loadMetadata();
    if(time_mode_==MeshInkTimeMode::Manual)return;
    trusted_gps_time_=utc;
    trusted_gps_until_=millis()+1500;
}
void T5RTCClock::expectCompanionTime(uint32_t utc){
    loadMetadata();
    if(time_mode_==MeshInkTimeMode::Manual)return;
    expected_companion_time_=utc;
    expected_companion_until_=millis()+1500;
}
bool T5RTCClock::setManualTime(uint32_t utc){
    loadMetadata();
    trusted_gps_time_=0;trusted_gps_until_=0;
    expected_companion_time_=0;expected_companion_until_=0;
    time_mode_=MeshInkTimeMode::Manual;
    return writeAcceptedTime(utc,MeshInkTimeSource::Manual);
}
bool T5RTCClock::setTimeMode(MeshInkTimeMode mode){
    loadMetadata();
    if(mode!=MeshInkTimeMode::Auto&&mode!=MeshInkTimeMode::Manual)return false;
    trusted_gps_time_=0;trusted_gps_until_=0;
    expected_companion_time_=0;expected_companion_until_=0;
    time_mode_=mode;
    if(mode==MeshInkTimeMode::Manual){
        last_gps_sync_utc_=0;
        time_source_=MeshInkTimeSource::Manual;
    }else if(time_source_==MeshInkTimeSource::Manual){
        time_source_=valid_?MeshInkTimeSource::HardwareRtc:MeshInkTimeSource::Unknown;
    }
    saveMetadata();
    return true;
}
MeshInkTimeMode T5RTCClock::timeMode(){loadMetadata();return time_mode_;}
MeshInkTimeSource T5RTCClock::timeSource(){
    loadMetadata();
    if(time_source_==MeshInkTimeSource::Unknown&&valid_)
        return MeshInkTimeSource::HardwareRtc;
    return time_source_;
}
bool T5RTCClock::gpsAuthoritative(){
    loadMetadata();
    if(!valid_||time_mode_==MeshInkTimeMode::Manual)return false;
    return gpsAuthorityActive(getCurrentTime());
}

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

static bool t5_release_held_radio_control_pins(const char* phase) {
    // gpio_get_level() returns 0 when the input path is disabled, even for a
    // correctly driven output. Use INPUT_OUTPUT here so the diagnostic reads
    // the actual pad level while still driving NSS/RESET HIGH.
    //
    // ESP-IDF requires the desired state to be configured before gpio_hold_dis()
    // after deep sleep. Program both pads HIGH first, verify the held physical
    // levels, then release the individual holds and verify them again.
    gpio_deep_sleep_hold_dis();

    const esp_err_t nss_dir=gpio_set_direction((gpio_num_t)P_LORA_NSS,GPIO_MODE_INPUT_OUTPUT);
    const esp_err_t reset_dir=gpio_set_direction((gpio_num_t)P_LORA_RESET,GPIO_MODE_INPUT_OUTPUT);
    const esp_err_t nss_high=gpio_set_level((gpio_num_t)P_LORA_NSS,1);
    const esp_err_t reset_high=gpio_set_level((gpio_num_t)P_LORA_RESET,1);
    delayMicroseconds(10);

    const int nss_before=gpio_get_level((gpio_num_t)P_LORA_NSS);
    const int reset_before=gpio_get_level((gpio_num_t)P_LORA_RESET);

    const esp_err_t nss_release=gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    const esp_err_t reset_release=gpio_hold_dis((gpio_num_t)P_LORA_RESET);
    delayMicroseconds(20);

    const int nss_after=gpio_get_level((gpio_num_t)P_LORA_NSS);
    const int reset_after=gpio_get_level((gpio_num_t)P_LORA_RESET);
    Serial.printf("[T5-DEEPSLEEP] radio control release phase=%s before=%d/%d after=%d/%d cfg=%d/%d/%d/%d release=%d/%d\n",
                  phase?phase:"unknown",nss_before,reset_before,nss_after,reset_after,
                  (int)nss_dir,(int)nss_high,(int)reset_dir,(int)reset_high,
                  (int)nss_release,(int)reset_release);

    return nss_dir==ESP_OK&&nss_high==ESP_OK&&reset_dir==ESP_OK&&reset_high==ESP_OK&&
           nss_release==ESP_OK&&reset_release==ESP_OK&&
           nss_before==HIGH&&reset_before==HIGH&&
           nss_after==HIGH&&reset_after==HIGH;
}

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
    T5_TRACE("radio: early rail start=%s at=%lums\n",
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
    T5_TRACE("radio: rail settle elapsed=%lums remaining=%lums\n",
        (unsigned long)elapsed,(unsigned long)remaining);
    if(remaining)delay(remaining);
    return true;
#endif
}

// The ESP-IDF GPIO ISR service is process-wide. MeshInk keeps the SX1262
// DIO1 callback under one explicit IDF lifetime in every runtime mode instead
// of relying on Arduino's hidden attachInterrupt bookkeeping.
static bool radio_gpio_irq_handler_active=false;
static bool gpio_isr_service_ready=false;

// EPDiy asks this hook at init and teardown. A true value means the global ISR
// service contains a live SX1262 handler and must be shared/preserved.
extern "C" bool meshink_epdiy_existing_gpio_isr_service() {
    return radio_gpio_irq_handler_active;
}

// EPDiy owns the service on cold UI startup, while the radio owns first creation
// on retained headless wake. Keep that process-wide fact explicit so the second
// subsystem never probes gpio_install_isr_service() just to discover it exists.
extern "C" void meshink_epdiy_note_gpio_isr_service(bool installed) {
    gpio_isr_service_ready=installed;
}

class T5RadioHal final : public ArduinoHal {
    static void (*callbacks_[GPIO_NUM_MAX])(void);
    static bool attached_[GPIO_NUM_MAX];
    static void irq_bridge(void* arg) {
        const uint32_t pin=(uint32_t)(uintptr_t)arg;
        if(pin<GPIO_NUM_MAX&&callbacks_[pin])callbacks_[pin]();
    }
public:
    explicit T5RadioHal(SPIClass& spi):ArduinoHal(spi) {}

    bool ensureIsrService(const char* phase=nullptr) {
        if(gpio_isr_service_ready)return true;
        const esp_err_t installed=gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
        if(installed==ESP_OK){
            gpio_isr_service_ready=true;
            if(phase)
                Serial.printf("[T5-DEEPSLEEP] GPIO ISR service created phase=%s\n",phase);
            return true;
        }
        if(installed==ESP_ERR_INVALID_STATE){
            // Compatibility fallback for an unexpected third-party owner. The
            // EPDiy V3 handshake prevents this probe on normal MeshInk paths.
            gpio_isr_service_ready=true;
            return true;
        }
        Serial.printf("[T5-ERROR] GPIO ISR service unavailable phase=%s err=%d\n",
                      phase?phase:"attach",(int)installed);
        return false;
    }

    void attachInterrupt(uint32_t interruptNum,void (*interruptCb)(void),
                         uint32_t mode) override {
        if(interruptNum==RADIOLIB_NC||interruptNum>=GPIO_NUM_MAX)return;
        const gpio_num_t pin=(gpio_num_t)interruptNum;
        gpio_int_type_t type=GPIO_INTR_ANYEDGE;
        if(mode==GpioInterruptRising)type=GPIO_INTR_POSEDGE;
        else if(mode==GpioInterruptFalling)type=GPIO_INTR_NEGEDGE;

        if(attached_[interruptNum])detachInterrupt(interruptNum);
        callbacks_[interruptNum]=interruptCb;
        attached_[interruptNum]=false;

        if(!ensureIsrService(nullptr)){
            callbacks_[interruptNum]=nullptr;
            return;
        }
        gpio_set_intr_type(pin,type);
        const esp_err_t added=gpio_isr_handler_add(
            pin,irq_bridge,(void*)(uintptr_t)interruptNum);

        if(added==ESP_OK){
            attached_[interruptNum]=true;
            if(interruptNum==P_LORA_DIO_1)radio_gpio_irq_handler_active=true;
            return;
        }

        callbacks_[interruptNum]=nullptr;
        Serial.printf("[T5-ERROR] radio DIO interrupt attach failed gpio=%lu err=%d\n",
                      (unsigned long)interruptNum,(int)added);
    }

    void detachInterrupt(uint32_t interruptNum) override {
        if(interruptNum==RADIOLIB_NC||interruptNum>=GPIO_NUM_MAX)return;
        if(!attached_[interruptNum])return;
        gpio_isr_handler_remove((gpio_num_t)interruptNum);
        gpio_set_intr_type((gpio_num_t)interruptNum,GPIO_INTR_DISABLE);
        attached_[interruptNum]=false;
        callbacks_[interruptNum]=nullptr;
        if(interruptNum==P_LORA_DIO_1)radio_gpio_irq_handler_active=false;
    }

    bool handlerActive(uint32_t interruptNum) const {
        return interruptNum<GPIO_NUM_MAX&&attached_[interruptNum]&&callbacks_[interruptNum];
    }

    bool dispatchInterrupt(uint32_t interruptNum) {
        if(!handlerActive(interruptNum))return false;
        callbacks_[interruptNum]();
        return true;
    }
};
void (*T5RadioHal::callbacks_[GPIO_NUM_MAX])(void)={};
bool T5RadioHal::attached_[GPIO_NUM_MAX]={};

static T5RadioHal radio_hal(radio_spi);
static CustomSX1262 radio = new Module(
    &radio_hal,P_LORA_NSS,P_LORA_DIO_1,P_LORA_RESET,P_LORA_BUSY);
MeshInkSX1262Wrapper radio_driver(radio, board);

PhysicalLayer* t5_radio_physical_layer() {
    return &radio;
}

bool t5_radio_set_lora_crc(uint8_t bytes) {
    return radio.setCRC(bytes)==RADIOLIB_ERR_NONE;
}

void MeshInkSX1262Wrapper::stageWakePacket(const uint8_t* data,uint16_t len,float rssi,float snr) {
    if(!data||!len){
        wake_packet_len_=0;
        wake_metrics_active_=false;
        return;
    }
    if(len>MAX_TRANS_UNIT)len=MAX_TRANS_UNIT;
    memcpy(wake_packet_,data,len);
    wake_packet_len_=len;
    wake_rssi_=rssi;
    wake_snr_=snr;
    wake_metrics_active_=false;
}

bool MeshInkSX1262Wrapper::captureRetainedWakePacket(float rssi,float snr) {
    uint8_t packet[MAX_TRANS_UNIT]{};
    wake_packet_len_=0;
    wake_metrics_active_=false;
    const int len=CustomSX1262Wrapper::recvRaw(packet,sizeof(packet));
    if(len<=0||len>(int)MAX_TRANS_UNIT)return false;
    stageWakePacket(packet,(uint16_t)len,rssi,snr);
    resetStats();
    return true;
}

int MeshInkSX1262Wrapper::recvRaw(uint8_t* bytes,int sz) {
    if(wake_packet_len_&&bytes&&sz>0){
        const int len=(wake_packet_len_<(uint16_t)sz)?(int)wake_packet_len_:sz;
        memcpy(bytes,wake_packet_,len);
        wake_packet_len_=0;
        wake_metrics_active_=true;
        n_recv++;
        uint8_t scratch=0;
        (void)CustomSX1262Wrapper::recvRaw(&scratch,1);
        Serial.printf("[T5-DEEPSLEEP] injected saved wake packet after clean radio reset len=%d rxmode=%u\n",
                      len,isInRecvMode()?1U:0U);
        return len;
    }
    wake_metrics_active_=false;
    return CustomSX1262Wrapper::recvRaw(bytes,sz);
}

float MeshInkSX1262Wrapper::getLastRSSI() const {
    return wake_metrics_active_?wake_rssi_:CustomSX1262Wrapper::getLastRSSI();
}

float MeshInkSX1262Wrapper::getLastSNR() const {
    return wake_metrics_active_?wake_snr_:CustomSX1262Wrapper::getLastSNR();
}


bool meshink_board_service_asserted_radio_irq() {
    pinMode(P_LORA_DIO_1,INPUT);
    if(digitalRead(P_LORA_DIO_1)!=HIGH)return false;

    // These reads do not clear the IRQ. Invoke the exact callback that the
    // missing GPIO edge would have invoked; MeshCore then performs its normal
    // readData()/finishTransmit() path and clears the radio IRQ itself.
    const uint16_t irq=(uint16_t)radio.getIrqFlags();
    const uint16_t packet_len=(uint16_t)radio.getPacketLength();
    const bool handler=radio_hal.handlerActive(P_LORA_DIO_1);
    const bool dispatched=radio_hal.dispatchInterrupt(P_LORA_DIO_1);

    static uint32_t last_report=0;
    const uint32_t now=millis();
    if(!last_report||now-last_report>=250UL){
        last_report=now;
        Serial.printf("[T5-DEEPSLEEP] DIO1 level recovery irq=0x%04x packet_len=%u handler=%u dispatched=%u\n",
                      (unsigned)irq,(unsigned)packet_len,handler?1U:0U,dispatched?1U:0U);
    }
    return dispatched;
}

static T5RTCClock& t5_rtc_clock(){
    static T5RTCClock clock;
    return clock;
}

mesh::RTCClock& meshink_rtc_meshcore(){return t5_rtc_clock();}
void meshink_rtc_begin(){t5_rtc_clock().begin();}
void meshink_rtc_tick(){t5_rtc_clock().tick();}
uint32_t meshink_rtc_current_time(){return t5_rtc_clock().getCurrentTime();}
bool meshink_rtc_valid(){return t5_rtc_clock().isValid();}
bool meshink_rtc_set_manual_time(uint32_t utc){return t5_rtc_clock().setManualTime(utc);}
bool meshink_rtc_set_time_mode(MeshInkTimeMode mode){return t5_rtc_clock().setTimeMode(mode);}
MeshInkTimeMode meshink_rtc_time_mode(){return t5_rtc_clock().timeMode();}
void meshink_rtc_expect_companion_time(uint32_t utc){t5_rtc_clock().expectCompanionTime(utc);}
MeshInkTimeSource meshink_rtc_time_source(){return t5_rtc_clock().timeSource();}
bool meshink_rtc_gps_authoritative(){return t5_rtc_clock().gpsAuthoritative();}
static uint32_t detected_gps_baud = 9600;
static bool gps_baud_locked = false;
enum class GpsModule : uint8_t { Unknown, L76K, MiaM10Q };
static GpsModule detected_gps_module = GpsModule::Unknown;
static MeshInkGpsError gps_error = MeshInkGpsError::None;

// GNSS identity is a hardware fact worth retaining across ESP32 deep sleep.
// It lets an interactive resume restore the correct UART/tuning immediately,
// while a short bounded NMEA verification still catches a receiver that failed
// to resume cleanly.
static constexpr uint32_t GPS_RETAINED_IDENTITY_MAGIC=0x47504931; // "GPI1"
RTC_DATA_ATTR uint32_t gps_retained_identity_magic=0;
RTC_DATA_ATTR uint8_t gps_retained_module=0;
RTC_DATA_ATTR uint32_t gps_retained_baud=0;
static bool gps_resume_verify_pending=false;
static uint32_t gps_resume_verify_deadline=0;
// Tracks the MeshCore provider's stopped state. GPS is NOT electrically
// switched off: LoRa and GPS share the same power rail.
static bool gps_command_sleeping = false;
static uint32_t gps_last_byte_at = 0;

// LoRa and GPS share VCC3V3. Constellation selection remains user-controlled;
// compact GGA+RMC NMEA output is always configured on the inferred L76K.
// Neither setting shuts down receiver power or changes the 1 Hz fix rate.
// The generic 1..7 GPS/BDS/GLO bitmask maps directly to documented PCAS04;
// generic mode 0 means disabled and is parked with the proven BeiDou-zero state.
#ifndef T5_GPS_FULL_NMEA_DIAGNOSTIC
#define T5_GPS_FULL_NMEA_DIAGNOSTIC 0
#endif
static MeshInkGpsConstellationMode gps_constellation_mode=MeshInkGpsConstellationMode::GpsBeiDou;
static bool gps_tuning_loaded=false;
static bool gps_deep_sleep_power_save=false; // default OFF: retain live tracking
static bool gps_persisted_deep_sleep_masks=false;
static bool gps_constellation_dirty=false;
// Defensive T5/L76K boot recovery: the receiver can retain PCAS15 satellite
// masks across an ESP reset while its shared rail remains powered.
static bool gps_receiver_masks_zeroed=false;
static bool gps_nmea_dirty=true;  // apply automatic compact output each boot

// T5/L76K zero-mask recovery state. PCAS15 has no documented readback/query
// form, and the receiver can retain configuration while V_BCKP remains alive.
// RTC state handles normal deep sleep; Preferences covers an ESP reboot/reset
// while the GNSS backup domain still remembers the masks.
static constexpr uint32_t GPS_DEEP_SLEEP_LOW_WORK_MAGIC=0x47505A31; // "GPZ1"
RTC_DATA_ATTR uint32_t gps_deep_sleep_low_work_magic=0;

static const char* gps_module_name();

static bool gps_supported_identity(GpsModule module,uint32_t baud){
    return (module==GpsModule::L76K&&baud==9600UL)||
           (module==GpsModule::MiaM10Q&&baud==38400UL);
}
static void gps_clear_retained_identity(){
    gps_retained_identity_magic=0;
    gps_retained_module=(uint8_t)GpsModule::Unknown;
    gps_retained_baud=0;
}
static void gps_retain_identity(){
    if(!gps_supported_identity(detected_gps_module,detected_gps_baud)){
        gps_clear_retained_identity();
        return;
    }
    gps_retained_module=(uint8_t)detected_gps_module;
    gps_retained_baud=detected_gps_baud;
    gps_retained_identity_magic=GPS_RETAINED_IDENTITY_MAGIC;
}
static bool gps_restore_retained_identity(){
    if(esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_UNDEFINED||
       gps_retained_identity_magic!=GPS_RETAINED_IDENTITY_MAGIC)return false;
    const GpsModule retained=(GpsModule)gps_retained_module;
    if(!gps_supported_identity(retained,gps_retained_baud)){
        gps_clear_retained_identity();
        return false;
    }
    detected_gps_module=retained;
    detected_gps_baud=gps_retained_baud;
    gps_baud_locked=true;
    gps_error=MeshInkGpsError::None;
    return true;
}

static void gps_load_tuning(){
    if(gps_tuning_loaded)return;
    gps_tuning_loaded=true;
    Preferences pref;
    if(pref.begin("t5-gnss",false)){
        const bool mode_v2=pref.getBool("mode_v2",false);
        gps_deep_sleep_power_save=pref.getBool("ds_power_save",false);
        gps_persisted_deep_sleep_masks=pref.getBool("ds_masks_zero",false);
        const auto stored=pref.getUChar(
            "constellation",(uint8_t)MeshInkGpsConstellationMode::GpsBeiDou);
        const auto mode=static_cast<MeshInkGpsConstellationMode>(stored);
        if(!mode_v2&&stored==0){
            // Pre-GPS-MODE firmware used zero to mean "unchanged". Migrate that
            // one ambiguous legacy value to the previous effective default.
            gps_constellation_mode=MeshInkGpsConstellationMode::GpsBeiDou;
            pref.putUChar("constellation",(uint8_t)gps_constellation_mode);
        }else if(meshink_gps_constellation_mode_valid(mode)){
            gps_constellation_mode=mode;
        }else{
            gps_constellation_mode=MeshInkGpsConstellationMode::GpsBeiDou;
            pref.putUChar("constellation",(uint8_t)gps_constellation_mode);
        }
        pref.putBool("mode_v2",true);
        pref.end();
    }
    // Ignore the pre-1.5.0 "compact" preference: full output is now a
    // developer-only diagnostic build option, not an on-device toggle.
    gps_constellation_dirty=true;
    gps_nmea_dirty=true;
}
static void gps_send_pcas(const char* payload) {
    uint8_t checksum=0;
    for(const char* p=payload;*p;++p)checksum^=(uint8_t)*p;
    Serial1.printf("$%s*%02X\r\n",payload,checksum);
    Serial1.flush();
    T5_GPS_TRACE("gps tuning: TX $%s*%02X (receiver acceptance not confirmed)\n",payload,checksum);
}
static void gps_set_deep_sleep_mask_marker(bool active){
    gps_deep_sleep_low_work_magic=active?GPS_DEEP_SLEEP_LOW_WORK_MAGIC:0;
    if(gps_persisted_deep_sleep_masks==active)return;
    Preferences pref;
    if(pref.begin("t5-gnss",false)){
        if(pref.putBool("ds_masks_zero",active)==1)
            gps_persisted_deep_sleep_masks=active;
        pref.end();
    }
}
static void gps_restore_full_bds_masks(){
    gps_send_pcas("PCAS15,2,FFFFFFFF");
    gps_send_pcas("PCAS15,3,FFFFFFFF");
    gps_receiver_masks_zeroed=false;
    gps_set_deep_sleep_mask_marker(false);
}
static void gps_apply_low_work(bool deep_sleep){
    // Mark first so an ESP reset between the marker write and the GNSS command
    // is recoverable. A false-positive restore is harmless; an unmarked
    // retained zero mask is not.
    if(deep_sleep)gps_set_deep_sleep_mask_marker(true);
    gps_send_pcas("PCAS04,2");
    gps_send_pcas("PCAS15,2,00000000");
    gps_send_pcas("PCAS15,3,00000000");
    gps_receiver_masks_zeroed=true;
}
static void gps_apply_tuning(){
    if(detected_gps_module!=GpsModule::L76K||!gps_baud_locked)return;

    if(gps_deep_sleep_low_work_magic==GPS_DEEP_SLEEP_LOW_WORK_MAGIC||
       gps_persisted_deep_sleep_masks){
        // Restore only when MeshInk's retained/persisted marker says it may
        // have applied the zero masks before deep sleep.
        gps_restore_full_bds_masks();
        gps_deep_sleep_low_work_magic=0;
        gps_constellation_dirty=true;
        Serial.println("[T5-GPS] startup GNSS satellite masks restored");
        meshink_board_wake_log_append("[T5-GPS] startup GNSS satellite masks restored");
    }

    if(gps_constellation_mode==MeshInkGpsConstellationMode::None){
        if(gps_constellation_dirty||!gps_receiver_masks_zeroed){
            gps_constellation_dirty=false;
            gps_apply_low_work(false);
            Serial.println("[T5-GPS] GPS mode disabled; receiver parked in BeiDou zero-mask state");
        }
    }else{
        if(gps_receiver_masks_zeroed)gps_restore_full_bds_masks();
        if(gps_constellation_dirty){
            gps_constellation_dirty=false;
            char payload[16];
            snprintf(payload,sizeof(payload),"PCAS04,%u",
                     (unsigned)static_cast<uint8_t>(gps_constellation_mode));
            gps_send_pcas(payload);
        }
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
    return "COMPACT NMEA IS AUTOMATIC FOR L76K. CONSTELLATION SELECTION IS APPLIED LIVE. GPS STAYS POWERED WHILE LORA IS ON.";
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
    if(saved)pref.putBool("mode_v2",true);
    pref.end();
    if(!saved)return false;
    gps_constellation_mode=mode;
    gps_constellation_dirty=true;
    gps_apply_tuning();
    return true;
}
bool meshink_gps_deep_sleep_power_save(){
    gps_load_tuning();
    return gps_deep_sleep_power_save;
}
bool meshink_gps_set_deep_sleep_power_save(bool enabled){
    gps_load_tuning();
    if(gps_deep_sleep_power_save==enabled)return true;
    Preferences pref;
    if(!pref.begin("t5-gnss",false))return false;
    const bool saved=pref.putBool("ds_power_save",enabled)==1;
    pref.end();
    if(!saved)return false;
    gps_deep_sleep_power_save=enabled;
    return true;
}

static bool t5_gps_prepare_deep_sleep_low_work(){
#if ENV_INCLUDE_GPS == 1
    // Preserve a previously identified receiver even though the ESP32 process
    // state will be rebuilt on the next deep-sleep wake.
    gps_retain_identity();
    gps_load_tuning();

    // Default behavior keeps GNSS tracking alive across ESP32 deep sleep.
    // The radio/GPS rail remains powered, so no receiver command is needed.
    if(!gps_deep_sleep_power_save){
        if(!gps_persisted_deep_sleep_masks)
            gps_deep_sleep_low_work_magic=0;
        if(gps_constellation_mode==MeshInkGpsConstellationMode::None)
            Serial.println("[T5-GPS] deep-sleep GNSS power save OFF; GPS mode already disabled");
        else
            Serial.println("[T5-GPS] deep-sleep GNSS power save OFF; receiver tracking retained");
        return true;
    }

    // The final handoff is allowed to be a no-op if this boot never identified
    // an L76K. Never send L76K-specific commands to an unknown receiver.
    if(!gps_baud_locked||detected_gps_module!=GpsModule::L76K){
        if(!gps_persisted_deep_sleep_masks)
            gps_deep_sleep_low_work_magic=0;
        Serial.printf("[T5-GPS] deep-sleep GNSS low-work skipped locked=%u module=%s\n",
                      gps_baud_locked?1U:0U,gps_module_name());
        return false;
    }
    gps_apply_low_work(true);
    Serial.println("[T5-GPS] deep-sleep GNSS power save ON: BeiDou zero mask");
    return true;
#else
    return false;
#endif
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
    bool sentence_valid = false;
    uint8_t checksum = 0;
    uint8_t expected = 0;
    uint8_t checksum_digits = 0;
    uint8_t payload_chars = 0;
    char sentence_header[5]{};

    static bool upperAlpha(char c) {
        return c >= 'A' && c <= 'Z';
    }
    bool hasStandardNmeaHeader() const {
        // Standard NMEA sentences use a two-character talker ID followed by a
        // three-character sentence type (for example GPGGA, GNRMC, BDGGA).
        // Proprietary messages begin with P and must not identify a receiver
        // merely because their checksum is valid.
        return payload_chars >= 5 && sentence_header[0] != 'P' &&
            upperAlpha(sentence_header[0]) && upperAlpha(sentence_header[1]) &&
            upperAlpha(sentence_header[2]) && upperAlpha(sentence_header[3]) &&
            upperAlpha(sentence_header[4]);
    }

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
            sentence_valid = false;
            checksum = expected = checksum_digits = payload_chars = 0;
            memset(sentence_header,0,sizeof(sentence_header));
            return;
        }
        if (!collecting) return;
        if (!after_star) {
            if (c == '*') { after_star = true; return; }
            if (c == '\r' || c == '\n' || c < 32 || c > 126) { collecting = false; return; }
            if(payload_chars<sizeof(sentence_header))sentence_header[payload_chars]=c;
            ++payload_chars;
            checksum ^= static_cast<uint8_t>(c);
            return;
        }
        const int nibble = hexValue(c);
        if (nibble < 0 || checksum_digits >= 2) { collecting = false; return; }
        expected = static_cast<uint8_t>((expected << 4) | nibble);
        if (++checksum_digits == 2) {
            sentence_valid = hasStandardNmeaHeader() && checksum == expected;
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
    void clearValidation() {
        sentence_valid=collecting=after_star=false;
        checksum=expected=checksum_digits=payload_chars=0;
        memset(sentence_header,0,sizeof(sentence_header));
    }
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
        // valid GPS time. Trust the first valid fix immediately when the RTC has
        // never had a GPS correction, or its persisted GPS authority is older
        // than 24 hours. Once GPS is fresh again, keep the normal hourly
        // correction cadence to avoid rewriting the RTC for every fix.
        static uint32_t last_gps_clock_sync_ms = 0;
        if (!last_gps_clock_sync_ms) {
            last_gps_clock_sync_ms = millis() ? millis() : 1;
        }
        if (isValid()) {
            const uint32_t now_ms = millis();
            const bool rtc_needs_time = !t5_rtc_clock().isValid();
            const bool gps_time_stale = !t5_rtc_clock().gpsAuthoritative();
            const bool hourly_correction_due = last_gps_clock_sync_ms == 0 ||
                now_ms - last_gps_clock_sync_ms >= 3600000UL;
            if (rtc_needs_time || gps_time_stale || hourly_correction_due) {
                t5_rtc_clock().expectGpsTime((uint32_t)getTimestamp());
                last_gps_clock_sync_ms = now_ms ? now_ms : 1;
                if(gps_time_stale)
                    T5_GPS_TRACE("gps: first fresh fix accepted because GPS clock authority was stale\n");
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
        if(active&&gps_resume_verify_pending){
            if(gps_stream.hasValidSentence()){
                gps_resume_verify_pending=false;
                gps_resume_verify_deadline=0;
                gps_error=MeshInkGpsError::None;
                gps_last_byte_at=millis();
                gps_retain_identity();
                Serial.printf("[T5-GPS] deep-sleep GNSS resume verified module=%s baud=%lu\n",
                              gps_module_name(),(unsigned long)detected_gps_baud);
                meshink_board_wake_log_appendf(
                    "[T5-GPS] deep-sleep GNSS resume verified module=%s baud=%lu",
                    gps_module_name(),(unsigned long)detected_gps_baud);
            }else if((int32_t)(millis()-gps_resume_verify_deadline)>=0){
                gps_resume_verify_pending=false;
                gps_resume_verify_deadline=0;
                gps_error=MeshInkGpsError::NmeaUnavailable;
                gps_baud_locked=false;
                next_baud_retry=millis();
                Serial.printf("[T5-ERROR] deep-sleep GNSS resume has no valid NMEA module=%s baud=%lu; reprobe active\n",
                              gps_module_name(),(unsigned long)Serial1.baudRate());
                meshink_board_wake_log_appendf(
                    "[T5-ERROR] deep-sleep GNSS resume has no valid NMEA module=%s baud=%lu; reprobe active",
                    gps_module_name(),(unsigned long)Serial1.baudRate());
            }
        }
        if (active && !gps_baud_locked && gps_stream.hasValidSentence()) {
            gps_baud_locked = true;
            detected_gps_baud = Serial1.baudRate();
            detected_gps_module = detected_gps_baud == 9600 ? GpsModule::L76K : GpsModule::MiaM10Q;
            gps_error = MeshInkGpsError::None;
            gps_retain_identity();
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
            gps_error=MeshInkGpsError::NmeaUnavailable;
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

// This board build enables only MeshCore's GPS environment provider. The
// board-level radio/GPS probe above has already selected the UART baud and,
// on a healthy receiver, confirmed checksum-valid NMEA before this manager is
// started. Upstream EnvironmentSensorManager::begin() performs another GPS
// detection cycle with a fixed 1000 ms delay plus an I2C sensor scan. Reuse
// the board probe instead while preserving the same public GPS setting and
// applyGpsPrefs() semantics.
bool T5EnvironmentSensorManager::begin() {
#if ENV_INCLUDE_GPS == 1
    // ENV_SKIP_GPS_DETECT was already part of this target, so GPS remains
    // exposed even if the first boot probe has not locked yet. T5GPS's
    // background baud retry then continues exactly as before.
    gps_detected=true;
    gps_active=false;
#endif
    return true;
}

static T5GPS gps;
T5EnvironmentSensorManager sensors(gps);

void meshink_gps_prepare_runtime(){
#if ENV_INCLUDE_GPS == 1
    // Retained radio wake skips T5Board::beginLocal(). Restore the receiver
    // identity first so UART/tuning are correct before MeshCore starts its GPS
    // provider. Verification is asynchronous so UI promotion is never blocked.
    const bool restored=gps_restore_retained_identity();
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    Serial1.begin(detected_gps_baud);
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
    gps_resume_verify_pending=restored;
    gps_resume_verify_deadline=restored?millis()+2500UL:0;
    if(restored){
        gps_load_tuning();
        const bool tracking_retained=
            !gps_deep_sleep_power_save&&
            gps_deep_sleep_low_work_magic!=GPS_DEEP_SLEEP_LOW_WORK_MAGIC&&
            !gps_persisted_deep_sleep_masks;
        if(tracking_retained){
            // The shared rail never went down and we deliberately left the
            // receiver tracking. Preserve that hot state: do not resend masks,
            // constellation selection, or NMEA configuration before verifying
            // the already-running stream.
            gps_constellation_dirty=false;
            gps_nmea_dirty=false;
            Serial.println("[T5-GPS] deep-sleep GNSS tracking retained; receiver reconfiguration skipped");
            meshink_board_wake_log_append(
                "[T5-GPS] deep-sleep GNSS tracking retained; receiver reconfiguration skipped");
        }
        gps_apply_tuning();
        Serial.printf("[T5-GPS] deep-sleep GNSS identity restored module=%s baud=%lu; NMEA verification armed\n",
                      gps_module_name(),(unsigned long)Serial1.baudRate());
        meshink_board_wake_log_appendf(
            "[T5-GPS] deep-sleep GNSS identity restored module=%s baud=%lu; NMEA verification armed",
            gps_module_name(),(unsigned long)Serial1.baudRate());
    }else{
        gps_error=MeshInkGpsError::ModuleNotIdentified;
        Serial.println("[T5-ERROR] deep-sleep GNSS identity unavailable; live NMEA identification required");
        meshink_board_wake_log_append(
            "[T5-ERROR] deep-sleep GNSS identity unavailable; live NMEA identification required");
    }
#endif
}

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
    status.error=location?gps_error:MeshInkGpsError::ProviderUnavailable;
    if(location){
        status.valid=location->isValid()&&status.error==MeshInkGpsError::None;
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
    if (meshink_power_read_battery_mv(voltage))cached_mv = voltage;
    return cached_mv;
}

void meshink_board_companion_exit_feedback_begin() {
    // Companion startup performs additional board/radio initialization after
    // the splash, so do not assume the early LEDC attachment is still intact.
    // Reassert the board frontlight backend before driving the visible exit
    // acknowledgement.
    meshink_power_frontlight_begin();
    meshink_power_frontlight_set(100);
    T5_TRACE("frontlight: companion exit acknowledgement brightness=100%%\n");
}

void meshink_board_companion_release_resources() {
    // Release the radio's own IRQ handler and shared SPI bus cleanly. The
    // global Arduino GPIO ISR service can remain until the imminent reset;
    // unlike test10, no same-boot EPDiy reinitialization needs that service.
    radio_hal.detachInterrupt(P_LORA_DIO_1);
    radio_spi.end();
    T5_TRACE("companion exit: radio IRQ/SPI resources released\n");
}

void meshink_board_begin_companion(){board.begin();}
void meshink_board_begin_local(){board.beginLocal();}
void meshink_board_begin_local_rx_wake(bool packet_wake){board.beginLocalRxWake(packet_wake);}
void meshink_board_boot_complete(){board.onBootComplete();}

bool meshink_board_woke_from_radio() {
    if(esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_EXT1)return false;
    return (esp_sleep_get_ext1_wakeup_status()&(1ULL<<P_LORA_DIO_1))!=0;
}

bool meshink_board_woke_from_primary_button() {
    return esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_EXT0;
}

bool meshink_board_woke_from_timer() {
    return esp_sleep_get_wakeup_cause()==ESP_SLEEP_WAKEUP_TIMER;
}

bool meshink_board_radio_irq_asserted() {
    pinMode(P_LORA_DIO_1,INPUT);
    return digitalRead(P_LORA_DIO_1)==HIGH;
}

void meshink_board_restore_deep_sleep_wake_pads() {
    // EXT0/EXT1 route their wake pads through RTC IO. Explicitly release any
    // per-pad RTC hold before returning DIO1/BOOT to normal digital GPIO.
    gpio_deep_sleep_hold_dis();
    const esp_err_t radio_hold=rtc_gpio_hold_dis((gpio_num_t)P_LORA_DIO_1);
    const esp_err_t button_hold=rtc_gpio_hold_dis((gpio_num_t)T5_PIN_BOOT_BUTTON);
    const esp_err_t radio_pad=rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1);
    const esp_err_t button_pad=rtc_gpio_deinit((gpio_num_t)T5_PIN_BOOT_BUTTON);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
    Serial.printf("[T5-DEEPSLEEP] wake pads restored dio1=%d boot=%d hold=%d/%d deinit=%d/%d\n",
                  digitalRead(P_LORA_DIO_1),digitalRead(T5_PIN_BOOT_BUTTON),
                  (int)radio_hold,(int)button_hold,(int)radio_pad,(int)button_pad);
    meshink_board_wake_log_appendf(
        "[T5-DEEPSLEEP] wake pads restored dio1=%d boot=%d hold=%d/%d deinit=%d/%d",
        digitalRead(P_LORA_DIO_1),digitalRead(T5_PIN_BOOT_BUTTON),
        (int)radio_hold,(int)button_hold,(int)radio_pad,(int)button_pad);
}

void meshink_board_prepare_retained_aux_wake() {
    // Release only the automatic digital-pad hold so I2C/EPD pins can be used.
    // Keep the explicit SX1262 NSS/RESET holds intact while a timer wake merely
    // checks battery state.
    gpio_deep_sleep_hold_dis();
}

void meshink_board_release_retained_radio_holds() {
    gpio_deep_sleep_hold_dis();
    gpio_hold_dis((gpio_num_t)P_LORA_NSS);
    gpio_hold_dis((gpio_num_t)P_LORA_RESET);
}

static constexpr uint64_t T5_DEEP_SLEEP_BATTERY_CHECK_US=
    60ULL*60ULL*1000000ULL;

static bool t5_enable_deep_sleep_wake_sources() {
    // deepsleep26 returns to the original repeatedly-tested wake assignment:
    // BOOT uses EXT0 LOW and SX1262 DIO1 uses EXT1 ANY_HIGH.
    const esp_err_t clear_wake=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t button_wake=esp_sleep_enable_ext0_wakeup(
        (gpio_num_t)T5_PIN_BOOT_BUTTON,0);
    const esp_err_t radio_wake=esp_sleep_enable_ext1_wakeup(
        1ULL<<P_LORA_DIO_1,ESP_EXT1_WAKEUP_ANY_HIGH);
    const esp_err_t timer_wake=esp_sleep_enable_timer_wakeup(
        T5_DEEP_SLEEP_BATTERY_CHECK_US);
    if(clear_wake!=ESP_OK||button_wake!=ESP_OK||radio_wake!=ESP_OK||timer_wake!=ESP_OK){
        Serial.printf("[T5-DEEPSLEEP] wake-source setup failed clear=%d button-ext0=%d radio-ext1=%d timer=%d\n",
                      (int)clear_wake,(int)button_wake,(int)radio_wake,(int)timer_wake);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        return false;
    }
    Serial.printf("[T5-DEEPSLEEP] wake armed button=EXT0 GPIO%d LOW radio=EXT1 GPIO%d ANY_HIGH timer=1h dio1=%d boot=%d\n",
                  T5_PIN_BOOT_BUTTON,P_LORA_DIO_1,
                  digitalRead(P_LORA_DIO_1),digitalRead(T5_PIN_BOOT_BUTTON));
    return true;
}

bool meshink_board_enter_deep_sleep_standby() {
    // The SX1262 stays powered and in continuous receive. Only the ESP32-S3
    // sleeps; DIO1 wakes through EXT1 ANY_HIGH and BOOT through EXT0 LOW.
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
    pinMode(P_LORA_DIO_1,INPUT);
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: BOOT is still held");
        return false;
    }
    if(digitalRead(P_LORA_DIO_1)==HIGH){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: SX1262 DIO1 already asserted");
        return false;
    }

    // Re-arm the physical SX1262 at the last possible point. This is the
    // pre-Heltec behavior that repeatedly woke correctly: it re-applies the
    // RX_DONE -> DIO1 mapping on every sleep interval. The ESP32 resets on
    // deep sleep anyway, so RadioLibWrapper bookkeeping after this point is
    // irrelevant.
    const int16_t rx_rearm=radio.startReceive();
    if(rx_rearm!=RADIOLIB_ERR_NONE){
        Serial.printf("[T5-DEEPSLEEP] sleep deferred: SX1262 RX re-arm failed code=%d\n",(int)rx_rearm);
        return false;
    }
    delayMicroseconds(200);
    Serial.printf("[T5-DEEPSLEEP] SX1262 RX re-armed before sleep dio1=%d busy=%d\n",
                  digitalRead(P_LORA_DIO_1),digitalRead(P_LORA_BUSY));
    if(digitalRead(P_LORA_BUSY)==HIGH){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: SX1262 BUSY asserted after RX re-arm");
        return false;
    }
    if(digitalRead(P_LORA_DIO_1)==HIGH){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: DIO1 asserted during RX re-arm");
        return false;
    }

    if(!t5_enable_deep_sleep_wake_sources())return false;

    // Keep the radio out of hardware reset while the ESP32 GPIO domain sleeps.
    // The H752-01's external PCA9535 keeps the shared LoRa/GPS 3V3 rail on.
    pinMode(P_LORA_NSS,OUTPUT);
    digitalWrite(P_LORA_NSS,HIGH);
    pinMode(P_LORA_RESET,OUTPUT);
    digitalWrite(P_LORA_RESET,HIGH);
    const esp_err_t nss_hold=gpio_hold_en((gpio_num_t)P_LORA_NSS);
    const esp_err_t reset_hold=gpio_hold_en((gpio_num_t)P_LORA_RESET);
    if(nss_hold!=ESP_OK||reset_hold!=ESP_OK){
        gpio_hold_dis((gpio_num_t)P_LORA_NSS);
        gpio_hold_dis((gpio_num_t)P_LORA_RESET);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        Serial.printf("[T5-DEEPSLEEP] SX1262 pin hold failed nss=%d reset=%d\n",
                      (int)nss_hold,(int)reset_hold);
        return false;
    }
    gpio_deep_sleep_hold_en();

    // Close the race immediately before sleep. DIO1 must remain LOW for
    // EXT1-ANY_HIGH and BOOT must remain HIGH for EXT0-LOW.
    const int dio1_now=digitalRead(P_LORA_DIO_1);
    const int boot_now=digitalRead(T5_PIN_BOOT_BUTTON);
    if(boot_now==LOW||dio1_now==HIGH){
        gpio_deep_sleep_hold_dis();
        gpio_hold_dis((gpio_num_t)P_LORA_NSS);
        gpio_hold_dis((gpio_num_t)P_LORA_RESET);
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        Serial.printf("[T5-DEEPSLEEP] sleep race avoided boot=%d dio1=%d\n",
                      boot_now,dio1_now);
        return false;
    }

    // All sleep-deferral/race checks have passed. Start a fresh RTC transcript
    // only when this deep-sleep interval is definitely going ahead. A deferred
    // attempt must not erase the previous wake evidence.
    t5_wake_log_reset_for_sleep();

    // Only now alter the GNSS receiver, so a failed/deferred sleep attempt
    // never changes the user's active constellation configuration.
    t5_gps_prepare_deep_sleep_low_work();

    Serial.printf("[T5-DEEPSLEEP] entering: DIO1 EXT1 GPIO%d=LOW BOOT EXT0 GPIO%d=HIGH NSS/RESET=held-high\n",
                  P_LORA_DIO_1,T5_PIN_BOOT_BUTTON);
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
    return true;
}

bool meshink_board_return_to_retained_deep_sleep() {
    // Timer and accidental short-BOOT wakes reset the ESP32 but leave the
    // retained SX1262 hardware listening. Do not issue RadioLib commands here:
    // its C++ object state was reset and no packet needs to be consumed.
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
    pinMode(P_LORA_DIO_1,INPUT);
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        Serial.println("[T5-DEEPSLEEP] retained re-sleep deferred: BOOT is held");
        return false;
    }
    if(digitalRead(P_LORA_DIO_1)==HIGH){
        Serial.println("[T5-DEEPSLEEP] retained re-sleep deferred: DIO1 is asserted");
        return false;
    }
    if(!t5_enable_deep_sleep_wake_sources())return false;

    // NSS and RESET were individually held when the original deep sleep began.
    // Keep the global automatic deep-sleep hold policy enabled for this next
    // interval without unholding or reconfiguring the retained radio.
    gpio_deep_sleep_hold_en();

    const int dio1_now=digitalRead(P_LORA_DIO_1);
    const int boot_now=digitalRead(T5_PIN_BOOT_BUTTON);
    if(boot_now==LOW||dio1_now==HIGH){
        gpio_deep_sleep_hold_dis();
        esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
        Serial.printf("[T5-DEEPSLEEP] retained re-sleep race avoided boot=%d dio1=%d\n",
                      boot_now,dio1_now);
        return false;
    }

    // This is a new deep-sleep interval; discard the prior wake transcript
    // only after every re-sleep race/deferral check has passed.
    t5_wake_log_reset_for_sleep();
    Serial.println("[T5-DEEPSLEEP] retained radio untouched; re-entering deep sleep (battery timer 1h)");
    Serial.flush();
    delay(20);
    esp_deep_sleep_start();
    return true;
}

void T5Board::begin() {
    // The application renders and tears down the companion splash before this
    // board lifecycle entry. EPDiy has released I2C/GPIO resources, so the
    // upstream ESP32 board setup can safely take ownership here.
    T5_TRACE("board: companion display released; MeshCore board/I2C begin\n");
    ESP32Board::begin();
    T5_TRACE("board: MeshCore I2C ready\n");
    t5_radio_shared_bus_idle(true);
    enableRadioGpsRail();
    meshink_power_prepare_board();
    const uint16_t startup_battery_mv=getBattMilliVolts();
    if(startup_battery_mv)Serial.printf("[T5-INIT] battery-gauge=OK voltage=%umV\n",(unsigned)startup_battery_mv);
    else Serial.println("[T5-ERROR] battery gauge unavailable during startup");
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
    // The local UI initialized EPDiy and I2C first. Reinstalling the legacy
    // I2C driver here would abort; only perform MeshCore's remaining board work.
    startup_reason = BD_STARTUP_NORMAL;
    // ui_setup() normally started this rail while preparing the splash. Only
    // wait for the remainder here; recover by starting it now if early start failed.
    t5_wait_local_radio_settle();
    // Unified/local mode calls beginLocal(), not begin(). Without this call
    // the 1500mAh factory-profile migration ran only in BLE companion mode.
    meshink_power_prepare_board();
    const uint16_t startup_battery_mv=getBattMilliVolts();
    if(startup_battery_mv)Serial.printf("[T5-INIT] battery-gauge=OK voltage=%umV\n",(unsigned)startup_battery_mv);
    else Serial.println("[T5-ERROR] battery gauge unavailable during startup");
#if ENV_INCLUDE_GPS == 1
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
#endif
    T5_TRACE("board: local UI handoff complete; shared I2C retained\n");
}

void T5Board::beginLocalRxWake(bool packet_wake) {
    startup_reason=packet_wake?BD_STARTUP_RX_PACKET:BD_STARTUP_NORMAL;
    Serial.printf("[T5-DEEPSLEEP] board wake capture mode=%s\n",
                  packet_wake?"RX_PACKET":"NORMAL");
}

void T5Board::finishLocalRxWakeCapture() {
    startup_reason=BD_STARTUP_NORMAL;
    Serial.println("[T5-DEEPSLEEP] retained wake packet secured; startup reason returned to NORMAL");
}

static bool t5_sx1262_raw_wait_busy(uint32_t timeout_ms=50) {
    const uint32_t started=millis();
    while(digitalRead(P_LORA_BUSY)==HIGH){
        if(millis()-started>=timeout_ms)return false;
        delayMicroseconds(100);
    }
    return true;
}

static bool t5_sx1262_raw_read(const uint8_t* command,size_t command_len,
                               uint8_t* data,size_t data_len,uint8_t* status_out=nullptr) {
    if(!command||!command_len)return false;
    if(!t5_sx1262_raw_wait_busy())return false;

    radio_spi.beginTransaction(SPISettings(1000000,MSBFIRST,SPI_MODE0));
    digitalWrite(P_LORA_NSS,LOW);
    delayMicroseconds(2);
    for(size_t i=0;i<command_len;++i)(void)radio_spi.transfer(command[i]);

    // SX126x read commands return one status byte between the command bytes
    // and the requested data. Use this raw framing only before RadioLib has
    // rebuilt its process-local SX126x transport state after deep sleep.
    const uint8_t status=radio_spi.transfer(0x00);
    for(size_t i=0;i<data_len;++i)data[i]=radio_spi.transfer(0x00);

    delayMicroseconds(2);
    digitalWrite(P_LORA_NSS,HIGH);
    radio_spi.endTransaction();
    if(status_out)*status_out=status;
    return t5_sx1262_raw_wait_busy();
}

static bool t5_sx1262_raw_capture_wake_packet(bool require_packet,uint16_t& captured_len) {
    captured_len=0;

    uint8_t irq_data[2]{};
    uint8_t rx_status[2]{};
    uint8_t packet_status[3]{};
    uint8_t command_status=0;
    const uint8_t get_irq[]={0x12};             // GetIrqStatus
    const uint8_t get_rx_buffer[]={0x13};       // GetRxBufferStatus
    const uint8_t get_packet_status[]={0x14};   // GetPacketStatus

    if(!t5_sx1262_raw_read(get_irq,sizeof(get_irq),irq_data,sizeof(irq_data),&command_status)||
       !t5_sx1262_raw_read(get_rx_buffer,sizeof(get_rx_buffer),rx_status,sizeof(rx_status))||
       !t5_sx1262_raw_read(get_packet_status,sizeof(get_packet_status),
                           packet_status,sizeof(packet_status))){
        Serial.println("[T5-DEEPSLEEP] raw wake capture failed: SX1262 status/FIFO metadata unavailable");
        return false;
    }

    const uint16_t irq=(uint16_t)(((uint16_t)irq_data[0]<<8)|irq_data[1]);
    const uint16_t packet_len=rx_status[0];
    const uint8_t offset=rx_status[1];
    constexpr uint16_t IRQ_RX_DONE=0x0002U;

    if(!(irq&IRQ_RX_DONE)||packet_len==0||packet_len>MAX_TRANS_UNIT){
        Serial.printf("[T5-DEEPSLEEP] raw wake capture has no valid RX packet status=0x%02x irq=0x%04x len=%u offset=%u dio1=%u\n",
                      (unsigned)command_status,(unsigned)irq,(unsigned)packet_len,
                      (unsigned)offset,digitalRead(P_LORA_DIO_1)==HIGH?1U:0U);
        return !require_packet;
    }

    uint8_t packet[MAX_TRANS_UNIT]{};
    const uint8_t read_buffer[]={0x1E,offset};   // ReadBuffer(offset)
    if(!t5_sx1262_raw_read(read_buffer,sizeof(read_buffer),packet,packet_len)){
        Serial.printf("[T5-DEEPSLEEP] raw wake FIFO read failed irq=0x%04x len=%u offset=%u\n",
                      (unsigned)irq,(unsigned)packet_len,(unsigned)offset);
        return false;
    }

    // RadioLib uses packet-status byte 2 for packet RSSI and byte 1 for SNR.
    const float wake_rssi=-((float)packet_status[2])/2.0f;
    const float wake_snr=((float)(int8_t)packet_status[1])/4.0f;
    radio_driver.stageWakePacket(packet,packet_len,wake_rssi,wake_snr);
    captured_len=packet_len;
    Serial.printf("[T5-DEEPSLEEP] raw wake packet captured BEFORE RadioLib init len=%u offset=%u irq=0x%04x status=0x%02x rssi=%d snr_x4=%d\n",
                  (unsigned)packet_len,(unsigned)offset,(unsigned)irq,
                  (unsigned)command_status,(int)wake_rssi,(int)(wake_snr*4.0f));
    return true;
}

static bool radio_apply_post_init_board_settings() {
    constexpr float LILYGO_TCXO_VOLTAGE=2.4f;
    const int16_t tcxo_state=radio.setTCXO(LILYGO_TCXO_VOLTAGE);
    const int16_t rf_switch_state=tcxo_state==RADIOLIB_ERR_NONE
        ?radio.setDio2AsRfSwitch(true):tcxo_state;
    if(tcxo_state!=RADIOLIB_ERR_NONE||rf_switch_state!=RADIOLIB_ERR_NONE){
        Serial.printf("[T5-DEEPSLEEP] SX1262 post-init board setup failed tcxo=%d rf-switch=%d\n",
                      (int)tcxo_state,(int)rf_switch_state);
        return false;
    }
    return true;
}

static bool radio_resume_retained(bool packet_wake) {
    if(!t5_release_held_radio_control_pins(packet_wake?"packet-wake":"button-wake")){
        Serial.println("[T5-DEEPSLEEP] retained radio restore failed: control-pin hold release");
        return false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_hal.detachInterrupt(P_LORA_DIO_1);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);

    uint16_t captured_len=0;
    if(packet_wake){
        // Critical ordering: copy the retained FIFO using raw SX1262 commands
        // before RadioLib touches the chip. The ESP reset loses RadioLib's
        // process-local Module/SX126x framing state while the radio hardware
        // and FIFO remain powered and intact.
        if(!t5_sx1262_raw_capture_wake_packet(true,captured_len)){
            Serial.println("[T5-DEEPSLEEP] raw retained wake packet capture failed");
            return false;
        }
    }

    // The packet is now safe in ESP RAM. Rebuild the radio from a completely
    // normal baseline, including RadioLib's SX126x-specific SPI framing.
    radio.resetOnStartup=true;
    if(!radio.std_init(&radio_spi)){
        Serial.println("[T5-DEEPSLEEP] clean SX1262 reset/reinit failed after raw wake capture");
        return false;
    }
    if(!radio_apply_post_init_board_settings())return false;

    Serial.printf("[T5-DEEPSLEEP] clean SX1262/RadioLib init complete saved-packet=%u len=%u\n",
                  radio_driver.hasWakePacket()?1U:0U,(unsigned)captured_len);
    return true;
}

bool radio_resume_rx_wake() {
    return radio_resume_retained(true);
}

bool radio_resume_retained_wake() {
    return radio_resume_retained(false);
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
            T5_TRACE("radio: recovery=spi-reset+sx1262-reset\n");
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
            T5_TRACE("radio: recovery=rail-cycle off=%u on=%u\n",off?1U:0U,on?1U:0U);
            if(!on)continue;
        }else{
            t5_radio_shared_bus_idle(true);
        }

        const uint32_t attempt_started=millis();
        T5_TRACE("radio: init attempt=%u/3 lora-cs=%d sd-cs=%d busy=%d reset=%d\n",
            attempt,digitalRead(T5_PIN_LORA_CS),digitalRead(T5_PIN_SD_CS),
            digitalRead(P_LORA_BUSY),digitalRead(P_LORA_RESET));
        // CustomSX1262::std_init prints RadioLib's numeric failure code on error.
        // With no MeshInk TCXO override it mirrors LilyGO's radio.begin() stage
        // by using RadioLib's 1.6 V default during initialization.
        ready=radio.std_init(&radio_spi);
        if(ready){
            // LilyGO H752-01 examples then switch the fitted TCXO to 2.4 V
            // before assigning DIO2 to the RF switch. These are board-electrical
            // settings; MeshCore's protocol parameters remain unchanged.
            constexpr float LILYGO_TCXO_VOLTAGE=2.4f;
            const int16_t tcxo_state=radio.setTCXO(LILYGO_TCXO_VOLTAGE);
            T5_TRACE("radio: post-init TCXO=%.1fV result=%d\n",
                (double)LILYGO_TCXO_VOLTAGE,(int)tcxo_state);
            if(tcxo_state!=RADIOLIB_ERR_NONE){
                Serial.printf("[T5-ERROR] SX1262 TCXO 2.4V setup failed code=%d\n",(int)tcxo_state);
                ready=false;
            }
            if(ready){
                const int16_t rf_switch_state=radio.setDio2AsRfSwitch(true);
                T5_TRACE("radio: post-init DIO2 RF-switch result=%d\n",
                    (int)rf_switch_state);
                if(rf_switch_state!=RADIOLIB_ERR_NONE){
                    Serial.printf("[T5-ERROR] SX1262 DIO2 RF-switch setup failed code=%d\n",(int)rf_switch_state);
                    ready=false;
                }
            }
        }
        T5_TRACE("radio: init attempt=%u result=%s elapsed=%lums busy=%d\n",
            attempt,ready?"OK":"FAILED",(unsigned long)(millis()-attempt_started),
            digitalRead(P_LORA_BUSY));
        if(!ready&&attempt<3)delay(500);
    }

    T5_TRACE("radio: SX1262 init=%d, heap=%u\n", ready, ESP.getFreeHeap());
    if(ready)Serial.println("[T5-INIT] radio=SX1262 OK");
    else Serial.println("[T5-ERROR] SX1262 radio initialization failed after recovery attempts");
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
                T5_GPS_TRACE("gps: probe pass=%u baud=%lu valid-NMEA=%d\n",
                              (unsigned)(pass+1),(unsigned long)baud,found);
                if (found) {
                    detected_gps_baud = baud;
                    gps_baud_locked = true;
                    detected_gps_module = baud == 9600 ? GpsModule::L76K : GpsModule::MiaM10Q;
                    gps_error = MeshInkGpsError::None;
                    gps_retain_identity();
                    gps_last_byte_at = millis();
                    break;
                }
            }
        }
        if (!found) {
            detected_gps_baud = 9600;
            gps_baud_locked = false;
            detected_gps_module = GpsModule::Unknown;
            gps_error = MeshInkGpsError::ModuleNotIdentified;
            gps_clear_retained_identity();
            Serial1.updateBaudRate(detected_gps_baud);
            gps_stream.clearValidation();
            Serial.println("[T5-ERROR] gps module not identified: no valid NMEA at supported baud; background retry active");
        } else {
            Serial.printf("[T5-INIT] gps=%s baud=%lu OK\n",
                gps_module_name(),(unsigned long)Serial1.baudRate());
            // Apply saved mode during the board probe, not only when MeshCore's
            // provider starts. This keeps explicit GPS MODE=DISABLED parked
            // correctly after a cold boot and performs defensive mask recovery.
            gps_load_tuning();
            gps_apply_tuning();
        }
        T5_GPS_TRACE("gps: module=%s baud=%lu%s\n",
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
        T5_TRACE("radio failure: PCA9535 absent; not classifying as H752-01 Lite\n");
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
    T5_TRACE("radio failure classification: H752-01=yes GPS-NMEA=%s\n",
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

// Native SX1262 pre-init: the same board preparation and chip-select
// deselection used by radio_init(), without initializing MeshCore's wrapper.
void t5_prepare_native_radio_spi(){
    meshink_rtc_begin();
    t5_radio_shared_bus_idle(true);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);
}
