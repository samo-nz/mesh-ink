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
    // begin() is only reached once the shared I2C lifecycle is active.
    // Retained headless wakes intentionally skip it.
    i2c_ready_=true;
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
            Serial.println("[T5-INIT] rtc=PCF8563 OK");
            return;
        }
    }else{
        valid_=false;
    }

    // MeshCore may already have bootstrapped software time from saved contact
    // timestamps while this retained wake was headless. Only use that deferred
    // value when the hardware RTC itself is unavailable/invalid; a valid RTC
    // above always remains authoritative.
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
    timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
    if(!i2c_ready_){
        // MeshCore bootstraps a plausible clock from contact timestamps during
        // retained radio-first startup. Keep that as system time only: the
        // headless path deliberately has no shared I2C driver yet.
        deferred_hardware_time_=utc;
        T5_TRACE("rtc: deferred hardware write UTC=%lu; I2C lifecycle not initialized\n",
            (unsigned long)utc);
        return;
    }
    const DateTime dt(utc);const uint8_t r[7]={to_bcd(dt.second()),to_bcd(dt.minute()),to_bcd(dt.hour()),
        to_bcd(dt.day()),to_bcd(dt.dayOfTheWeek()),to_bcd(dt.month()),to_bcd((uint8_t)(dt.year()-2000))};
    valid_=idf_write(0x51,0x02,r,sizeof(r));
    if(!valid_)Serial.println("[T5-WARN] rtc hardware write failed; system time remains active");
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
static uint32_t detected_gps_baud = 9600;
static bool gps_baud_locked = false;
enum class GpsModule : uint8_t { Unknown, L76K, MiaM10Q };
static GpsModule detected_gps_module = GpsModule::Unknown;
// Tracks the MeshCore provider's stopped state. GPS is NOT electrically
// switched off: LoRa and GPS share the same power rail.
static bool gps_command_sleeping = false;
static uint32_t gps_last_byte_at = 0;

// gps-powersave branch: deliberately intrusive receiver experiments are kept
// board-local. Stable application code never sees UARTs or receiver commands.
static bool gps_power_test_running = false;
static bool gps_standby_forced_single = false;
static constexpr uint32_t GPS_STANDBY_MAGIC = 0x47505331; // "GPS1"
RTC_DATA_ATTR uint32_t gps_standby_magic = 0;

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

    // If deep sleep preserved the powered GNSS in our forced GPS-only standby
    // state, restore the user's interactive constellation choice as soon as
    // the receiver UART is identified again. A stale RTC marker on an ordinary
    // reset is cleared without touching receiver configuration.
    if(gps_standby_magic==GPS_STANDBY_MAGIC){
        const bool legitimate_restore=gps_standby_forced_single||
            esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_UNDEFINED;
        if(legitimate_restore){
            const uint8_t restore=gps_constellation_mode==MeshInkGpsConstellationMode::Unchanged
                ? 3U:(uint8_t)gps_constellation_mode;
            char payload[16];
            snprintf(payload,sizeof(payload),"PCAS04,%u",(unsigned)restore);
            gps_send_pcas(payload);
            gps_constellation_dirty=false;
            gps_standby_forced_single=false;
            gps_standby_magic=0;
            Serial.printf("[T5-GPS-POWER] deep/standby wake restored constellation mode=%u%s\n",
                          (unsigned)restore,
                          gps_constellation_mode==MeshInkGpsConstellationMode::Unchanged
                            ?" (GPS+BeiDou fallback for UNCHANGED preference)":"");
        }else{
            gps_standby_magic=0;
            Serial.println("[T5-GPS-POWER] cleared stale standby constellation marker on ordinary boot");
        }
    }

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
            checksum = expected = checksum_digits = payload_chars = 0;
            return;
        }
        if (!collecting) return;
        if (!after_star) {
            if (c == '*') { after_star = true; return; }
            if (c == '\r' || c == '\n' || c < 32 || c > 126) { collecting = false; return; }
            ++payload_chars;
            checksum ^= static_cast<uint8_t>(c);
            return;
        }
        const int nibble = hexValue(c);
        if (nibble < 0 || checksum_digits >= 2) { collecting = false; return; }
        expected = static_cast<uint8_t>((expected << 4) | nibble);
        if (++checksum_digits == 2) {
            // Baud/protocol lock is deliberately receiver-agnostic: accept
            // any printable NMEA sentence with a valid checksum, regardless
            // of talker ID (GP/GN/BD/GL/GA/proprietary/etc.).
            sentence_valid = payload_chars > 0 && checksum == expected;
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
    void clearValidation() { sentence_valid = collecting = after_star = false; checksum = expected = checksum_digits = payload_chars = 0; }
    bool hasValidSentence() const { return sentence_valid; }
};

static NMEAProbeStream gps_stream(Serial1);
class T5GPS : public MicroNMEALocationProvider {
    bool active = false;
    uint32_t next_baud_retry = 0;
public:
    T5GPS() : MicroNMEALocationProvider(gps_stream, &t5_rtc_clock()) {}
    bool isActive() const { return active; }
    void rearmBaudProbe() { next_baud_retry = millis() + 6000; }
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
        if (!gps_power_test_running) {
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
    // Retained radio wake skips T5Board::beginLocal(), so restore the GNSS UART
    // only. The running SX1262 and its shared rail remain untouched.
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    Serial1.begin(detected_gps_baud);
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
    T5_GPS_TRACE("gps: retained UI promotion UART ready baud=%lu\n",
                 (unsigned long)Serial1.baudRate());
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

namespace {
struct GpsPowerLoggedSample {
    bool post = false;
    uint16_t elapsed_s = 0;
    MeshInkPowerMeasurement power{};
};

static constexpr uint32_t GPS_POWER_SETTLE_MS = 5000;
static constexpr uint32_t GPS_POWER_BASELINE_MS = 30000;
static constexpr uint32_t GPS_POWER_POST_MS = 60000;
static constexpr uint32_t GPS_POWER_SAMPLE_MS = 1000;
static constexpr uint32_t GPS_SERIAL_REPLAY_DELAY_MS = 60;
static constexpr size_t GPS_POWER_LOG_CAPACITY = 96;
static GpsPowerLoggedSample gps_power_log[GPS_POWER_LOG_CAPACITY]{};
static size_t gps_power_log_count = 0;
static bool gps_power_log_valid = false;
static bool gps_power_post_phase = false;
static bool gps_power_measurement_started = false;
static uint32_t gps_power_settle_until = 0;
static uint32_t gps_power_phase_started = 0;
static uint32_t gps_power_next_sample = 0;
static MeshInkGpsPowerExperiment gps_power_experiment = MeshInkGpsPowerExperiment::GpsOnly;
static MeshInkGpsConstellationMode gps_power_saved_constellation = MeshInkGpsConstellationMode::Unchanged;
static bool gps_power_uart_suspended = false;
static constexpr uint32_t GPS_POWER_LOG_MAGIC = 0x47504C31; // "GPL1"

enum class GpsMatrixSatState : uint8_t { Off=0, Full=1, Zero=2 };
struct GpsMatrixResult {
    uint8_t code=0;
    int16_t mean_current_ma=0;
    int16_t min_current_ma=0;
    int16_t max_current_ma=0;
    uint16_t mean_voltage_mv=0;
    uint8_t samples=0;
    uint8_t flags=0; // B0 external power seen; B1 experimental binary mask used
};
static constexpr uint32_t GPS_MATRIX_MAGIC=0x47504D31; // "GPM1"
static constexpr uint32_t GPS_MATRIX_SETTLE_MS=5000;
static constexpr uint32_t GPS_MATRIX_MEASURE_MS=10000;
static constexpr size_t GPS_MATRIX_MAX_RESULTS=26;
static GpsMatrixResult gps_matrix_results[GPS_MATRIX_MAX_RESULTS]{};
static size_t gps_matrix_result_count=0;
static uint8_t gps_matrix_current_code=0;
static uint8_t gps_matrix_winner_code=0;
static bool gps_matrix_binary_satmask_supported=false;
static bool gps_matrix_log_valid=false;
static bool gps_matrix_sampling=false;
static uint32_t gps_matrix_settle_until=0;
static uint32_t gps_matrix_sample_started=0;
static uint32_t gps_matrix_next_sample=0;
static int32_t gps_matrix_current_sum=0;
static uint32_t gps_matrix_voltage_sum=0;
static int16_t gps_matrix_current_min=32767;
static int16_t gps_matrix_current_max=-32768;
static uint16_t gps_matrix_sample_count=0;
static uint16_t gps_matrix_voltage_count=0;
static bool gps_matrix_external_seen=false;

static void gps_serial_replay_pause() {
    Serial.flush();
    delay(GPS_SERIAL_REPLAY_DELAY_MS);
    yield();
}

static int32_t gps_discharge_ma(int16_t value) {
    return value < 0 ? -(int32_t)value : 0;
}
static int32_t gps_discharge_mw(int16_t value) {
    return value < 0 ? -(int32_t)value : 0;
}

static void gps_power_uart_resume() {
    if(!gps_power_uart_suspended)return;
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    Serial1.begin(detected_gps_baud);
    gps_power_uart_suspended=false;
    gps_last_byte_at=millis();
    Serial.printf("[T5-GPS-POWER] host UART resumed baud=%lu pins=%d/%d\n",
                  (unsigned long)detected_gps_baud,(int)PIN_GPS_TX,(int)PIN_GPS_RX);
}

static void gps_power_send(const char* payload,const char* reason) {
    gps_power_uart_resume();
    Serial.printf("[T5-GPS-POWER] receiver command reason='%s' payload='$%s*<xor>'\n",
                  reason?reason:"experiment",payload?payload:"");
    gps_send_pcas(payload);
}

static void gps_power_send_casic_binary(uint8_t cls,uint8_t id,
                                        const uint8_t* payload,uint16_t len,
                                        const char* reason) {
    gps_power_uart_resume();
    if((len&3U)!=0U||(!payload&&len!=0U)){
        Serial.printf("[T5-GPS-POWER] binary command rejected locally class=0x%02X id=0x%02X len=%u reason='%s'\n",
                      (unsigned)cls,(unsigned)id,(unsigned)len,reason?reason:"experiment");
        return;
    }
    uint32_t checksum=((uint32_t)id<<24)|((uint32_t)cls<<16)|(uint32_t)len;
    for(uint16_t offset=0;offset<len;offset+=4){
        const uint32_t word=(uint32_t)payload[offset]|
                            ((uint32_t)payload[offset+1]<<8)|
                            ((uint32_t)payload[offset+2]<<16)|
                            ((uint32_t)payload[offset+3]<<24);
        checksum+=word;
    }
    const uint8_t header[6]={0xBA,0xCE,(uint8_t)(len&0xFFU),(uint8_t)(len>>8),cls,id};
    const uint8_t trailer[4]={
        (uint8_t)(checksum&0xFFU),(uint8_t)((checksum>>8)&0xFFU),
        (uint8_t)((checksum>>16)&0xFFU),(uint8_t)((checksum>>24)&0xFFU)
    };
    Serial.printf("[T5-GPS-POWER] CASIC binary reason='%s' class=0x%02X id=0x%02X len=%u checksum=0x%08lX\n",
                  reason?reason:"experiment",(unsigned)cls,(unsigned)id,(unsigned)len,
                  (unsigned long)checksum);
    Serial1.write(header,sizeof(header));
    if(len)Serial1.write(payload,len);
    Serial1.write(trailer,sizeof(trailer));
    Serial1.flush();
}

static void gps_matrix_send_pcas(const char* payload) {
    gps_power_uart_resume();
    uint8_t checksum=0;
    for(const char* p=payload;*p;++p)checksum^=(uint8_t)*p;
    Serial1.printf("$%s*%02X\r\n",payload,checksum);
    Serial1.flush();
}

static void gps_matrix_send_binary(uint8_t cls,uint8_t id,
                                   const uint8_t* payload,uint16_t len) {
    gps_power_uart_resume();
    if((len&3U)!=0U||(!payload&&len!=0U))return;
    uint32_t checksum=((uint32_t)id<<24)|((uint32_t)cls<<16)|(uint32_t)len;
    for(uint16_t offset=0;offset<len;offset+=4){
        const uint32_t word=(uint32_t)payload[offset]|
                            ((uint32_t)payload[offset+1]<<8)|
                            ((uint32_t)payload[offset+2]<<16)|
                            ((uint32_t)payload[offset+3]<<24);
        checksum+=word;
    }
    const uint8_t header[6]={0xBA,0xCE,(uint8_t)(len&0xFFU),(uint8_t)(len>>8),cls,id};
    const uint8_t trailer[4]={
        (uint8_t)(checksum&0xFFU),(uint8_t)((checksum>>8)&0xFFU),
        (uint8_t)((checksum>>16)&0xFFU),(uint8_t)((checksum>>24)&0xFFU)
    };
    Serial1.write(header,sizeof(header));
    if(len)Serial1.write(payload,len);
    Serial1.write(trailer,sizeof(trailer));
    Serial1.flush();
}

static GpsMatrixSatState gps_matrix_state(uint8_t code,uint8_t position) {
    for(uint8_t i=0;i<position;++i)code=(uint8_t)(code/3U);
    return (GpsMatrixSatState)(code%3U);
}

static const char* gps_matrix_state_name(GpsMatrixSatState state) {
    switch(state){
        case GpsMatrixSatState::Off:return "OFF";
        case GpsMatrixSatState::Full:return "FULL";
        case GpsMatrixSatState::Zero:return "ZERO";
        default:return "?";
    }
}

static void gps_matrix_format_label(uint8_t code,char* out,size_t out_len) {
    snprintf(out,out_len,"GPS=%s BDS=%s GLO=%s",
             gps_matrix_state_name(gps_matrix_state(code,0)),
             gps_matrix_state_name(gps_matrix_state(code,1)),
             gps_matrix_state_name(gps_matrix_state(code,2)));
}

static bool gps_matrix_requires_binary_mask(uint8_t code) {
    return gps_matrix_state(code,0)==GpsMatrixSatState::Zero||
           gps_matrix_state(code,2)==GpsMatrixSatState::Zero;
}

static bool gps_matrix_config_supported(uint8_t code) {
    if(code==0||code>26)return false;
    return !gps_matrix_requires_binary_mask(code)||gps_matrix_binary_satmask_supported;
}

static uint8_t gps_matrix_next_supported_code(uint8_t after) {
    for(uint8_t code=(uint8_t)(after+1U);code<=26U;++code)
        if(gps_matrix_config_supported(code))return code;
    return 0;
}

static bool gps_matrix_probe_binary_satmask() {
    // Newer/reverse-engineered CASBIN material defines CFG-SATMASK (06/21)
    // as seven 64-bit satellite masks. It is not documented for this L76K,
    // so only use it when a read-only poll actually returns the expected
    // 56-byte CFG-SATMASK response header.
    while(Serial1.available()>0)Serial1.read();
    gps_matrix_send_binary(0x06,0x21,nullptr,0);
    uint8_t window[6]{};
    size_t have=0;
    const uint32_t until=millis()+800;
    while((int32_t)(millis()-until)<0){
        while(Serial1.available()>0){
            const uint8_t ch=(uint8_t)Serial1.read();
            if(have<sizeof(window))window[have++]=ch;
            else{
                memmove(window,window+1,sizeof(window)-1);
                window[sizeof(window)-1]=ch;
            }
            if(have==sizeof(window)&&
               window[0]==0xBA&&window[1]==0xCE&&
               window[2]==0x38&&window[3]==0x00&&
               window[4]==0x06&&window[5]==0x21){
                gps_last_byte_at=millis();
                return true;
            }
        }
        yield();
        delay(1);
    }
    gps_last_byte_at=millis();
    return false;
}

static void gps_matrix_send_binary_satmask(bool gps_zero,bool glo_zero) {
    uint8_t payload[56];
    memset(payload,0xFF,sizeof(payload));
    if(gps_zero)memset(payload+0,0x00,8);
    if(glo_zero)memset(payload+16,0x00,8);
    gps_matrix_send_binary(0x06,0x21,payload,sizeof(payload));
}

static void gps_matrix_restore_masks() {
    if(gps_matrix_binary_satmask_supported){
        uint8_t payload[56];
        memset(payload,0xFF,sizeof(payload));
        gps_matrix_send_binary(0x06,0x21,payload,sizeof(payload));
        delay(40);
    }
    gps_matrix_send_pcas("PCAS15,2,FFFFFFFF");
    delay(40);
    gps_matrix_send_pcas("PCAS15,3,FFFFFFFF");
    delay(40);
}

static void gps_matrix_apply_config(uint8_t code) {
    const auto gps=gps_matrix_state(code,0);
    const auto bds=gps_matrix_state(code,1);
    const auto glo=gps_matrix_state(code,2);
    const uint8_t mode=(gps==GpsMatrixSatState::Off?0U:1U)|
                       (bds==GpsMatrixSatState::Off?0U:2U)|
                       (glo==GpsMatrixSatState::Off?0U:4U);
    gps_matrix_restore_masks();
    char constellation[16];
    snprintf(constellation,sizeof(constellation),"PCAS04,%u",(unsigned)mode);
    gps_matrix_send_pcas(constellation);
    delay(80);
    if(gps_matrix_binary_satmask_supported&&
       (gps==GpsMatrixSatState::Zero||glo==GpsMatrixSatState::Zero)){
        gps_matrix_send_binary_satmask(gps==GpsMatrixSatState::Zero,
                                       glo==GpsMatrixSatState::Zero);
        delay(80);
    }
    if(bds==GpsMatrixSatState::Zero){
        gps_matrix_send_pcas("PCAS15,2,00000000");
        delay(40);
        gps_matrix_send_pcas("PCAS15,3,00000000");
        delay(80);
    }
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
}

static void gps_diag_set_uart(uint32_t baud) {
    while(Serial1.available()>0)Serial1.read();
    Serial1.end();
    delay(10);
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    Serial1.begin(baud);
    gps_power_uart_suspended=false;
    Serial.printf("[T5-GPS-DIAG] host UART=%lu baud\n",(unsigned long)baud);
    gps_serial_replay_pause();
}

static int gps_diag_hex_nibble(uint8_t ch) {
    if(ch>='0'&&ch<='9')return ch-'0';
    if(ch>='A'&&ch<='F')return 10+ch-'A';
    if(ch>='a'&&ch<='f')return 10+ch-'a';
    return -1;
}

static void gps_diag_capture(const char* phase,uint32_t baud,uint32_t duration_ms) {
    uint8_t first[64]{};
    size_t first_count=0,total=0,printable=0,dollar=0,crlf=0,casic_sync=0,valid_nmea=0,bad_nmea=0;
    uint8_t prev=0;
    bool nmea=false,nmea_star=false;
    uint8_t nmea_xor=0;
    int checksum_hi=-1;
    const uint32_t started=millis();
    while(millis()-started<duration_ms){
        while(Serial1.available()>0){
            const uint8_t ch=(uint8_t)Serial1.read();
            ++total;
            if(first_count<sizeof(first))first[first_count++]=ch;
            if(ch>=0x20&&ch<=0x7E)++printable;
            if(ch=='$'){
                ++dollar;
                nmea=true;
                nmea_star=false;
                nmea_xor=0;
                checksum_hi=-1;
            }else if(nmea){
                if(!nmea_star){
                    if(ch=='*')nmea_star=true;
                    else if(ch!='\r'&&ch!='\n')nmea_xor^=ch;
                }else if(checksum_hi<0){
                    checksum_hi=gps_diag_hex_nibble(ch);
                    if(checksum_hi<0)nmea=false;
                }else{
                    const int lo=gps_diag_hex_nibble(ch);
                    if(lo>=0){
                        const uint8_t expected=(uint8_t)((checksum_hi<<4)|lo);
                        if(expected==nmea_xor)++valid_nmea;
                        else ++bad_nmea;
                    }
                    nmea=false;
                }
            }
            if(ch=='\n'||ch=='\r')++crlf;
            if(prev==0xBA&&ch==0xCE)++casic_sync;
            prev=ch;
        }
        yield();
        delay(1);
    }
    Serial.printf("[T5-GPS-DIAG] capture phase='%s' baud=%lu window=%lums bytes=%u printable=%u '$'=%u CRLF=%u valid-NMEA=%u bad-NMEA=%u CASIC-BA-CE=%u\n",
                  phase?phase:"capture",(unsigned long)baud,(unsigned long)duration_ms,
                  (unsigned)total,(unsigned)printable,(unsigned)dollar,(unsigned)crlf,
                  (unsigned)valid_nmea,(unsigned)bad_nmea,(unsigned)casic_sync);
    gps_serial_replay_pause();
    Serial.print("[T5-GPS-DIAG] first-bytes hex=");
    if(!first_count)Serial.print("<none>");
    for(size_t i=0;i<first_count;++i)Serial.printf("%02X%s",(unsigned)first[i],i+1==first_count?"":" ");
    Serial.println();
    gps_serial_replay_pause();
    Serial.print("[T5-GPS-DIAG] first-bytes ascii='");
    for(size_t i=0;i<first_count;++i){
        const uint8_t ch=first[i];
        Serial.write((ch>=0x20&&ch<=0x7E)?ch:'.');
    }
    Serial.println("'");
    gps_serial_replay_pause();
}

static void gps_diag_send_pcas(const char* payload,const char* label) {
    Serial.printf("[T5-GPS-DIAG] TX PCAS label='%s' payload='$%s*<xor>'\n",
                  label?label:"query",payload?payload:"");
    gps_serial_replay_pause();
    gps_send_pcas(payload);
    Serial1.flush();
}

static void gps_diag_send_binary_poll(uint8_t cls,uint8_t id,const char* label) {
    Serial.printf("[T5-GPS-DIAG] TX CASIC poll label='%s' class=0x%02X id=0x%02X\n",
                  label?label:"poll",(unsigned)cls,(unsigned)id);
    gps_serial_replay_pause();
    gps_power_send_casic_binary(cls,id,nullptr,0,label);
}

static void gps_diag_return_to_probe() {
    gps_diag_set_uart(9600);
    gps_stream.clearValidation();
    gps_baud_locked=false;
    detected_gps_baud=9600;
    detected_gps_module=GpsModule::Unknown;
    gps_last_byte_at=millis();
    gps.rearmBaudProbe();
    Serial.println("[T5-GPS-DIAG] returned host to 9600; normal checksum/NMEA background detection re-armed");
    gps_serial_replay_pause();
}

static void gps_diag_passive_scan() {
    static const uint32_t bauds[]={4800,9600,19200,38400,57600,115200,230400,256000};
    Serial.println("[T5-GPS-DIAG] ===== PASSIVE UART SCAN: NO BYTES TRANSMITTED TO GNSS =====");
    gps_serial_replay_pause();
    for(uint32_t baud:bauds){
        gps_diag_set_uart(baud);
        gps_diag_capture("passive",baud,1500);
    }
    gps_diag_return_to_probe();
}

static void gps_diag_identify_all_bauds() {
    static const uint32_t bauds[]={4800,9600,19200,38400,57600,115200};
    static const struct {const char* payload;const char* label;} pcas_queries[]={
        {"PCAS06,0","firmware version"},
        {"PCAS06,1","hardware model / serial"},
        {"PCAS06,2","working mode / enabled systems"},
        {"PCAS06,3","legacy customer ID query"},
        {"PCAS06,4","available GNSS signals"},
        {"PCAS06,5","legacy upgrade-code query"},
        {"PCAS06,6","chip information"}
    };
    static const struct {uint8_t cls;uint8_t id;const char* label;} binary_queries[]={
        {0x0A,0x04,"MON-VER"},
        {0x0A,0x09,"MON-HW"},
        {0x06,0x00,"CFG-PRT"},
        {0x06,0x04,"CFG-RATE"},
        {0x06,0x07,"CFG-NAVX"}
    };
    Serial.println("[T5-GPS-DIAG] ===== IDENTIFY / STATE QUERY AT EVERY DOCUMENTED PCAS BAUD =====");
    gps_serial_replay_pause();
    for(uint32_t baud:bauds){
        gps_diag_set_uart(baud);
        gps_diag_capture("pre-query passive",baud,250);
        for(const auto& q:pcas_queries){
            gps_diag_send_pcas(q.payload,q.label);
            gps_diag_capture(q.label,baud,400);
        }
        for(const auto& q:binary_queries){
            gps_diag_send_binary_poll(q.cls,q.id,q.label);
            gps_diag_capture(q.label,baud,500);
        }
    }
    gps_diag_return_to_probe();
}

static void gps_diag_force_9600_nmea() {
    static const uint32_t bauds[]={115200,57600,38400,19200,4800,9600};
    Serial.println("[T5-GPS-DIAG] ===== FORCE 9600 + NMEA RECOVERY =====");
    gps_serial_replay_pause();
    Serial.println("[T5-GPS-DIAG] PCAS01,1 sent at each documented input baud; receiver FLASH is not saved");
    gps_serial_replay_pause();
    for(uint32_t baud:bauds){
        gps_diag_set_uart(baud);
        for(uint8_t repeat=0;repeat<3;++repeat){
            gps_diag_send_pcas("PCAS01,1","force receiver UART to 9600");
            delay(80);
        }
    }
    gps_diag_set_uart(9600);
    gps_diag_send_pcas("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0","restore compact GGA+RMC output");
    delay(120);
    gps_diag_send_pcas("PCAS02,1000","restore 1 Hz update interval");
    delay(120);
    gps_diag_send_pcas("PCAS04,3","restore GPS+BeiDou mode");
    delay(250);
    gps_diag_capture("force-9600 recovery",9600,5000);
    gps_diag_return_to_probe();
}

static void gps_diag_factory_start_sweep() {
    static const uint32_t bauds[]={115200,57600,38400,19200,4800,9600};
    Serial.println("[T5-GPS-DIAG] ===== FACTORY START SWEEP =====");
    gps_serial_replay_pause();
    Serial.println("[T5-GPS-DIAG] WARNING: factory-start clears receiver backup data/configuration and restores defaults");
    gps_serial_replay_pause();
    for(uint32_t baud:bauds){
        gps_diag_set_uart(baud);
        gps_diag_send_pcas("PCAS10,3","factory start / clear backup data and configuration");
        delay(450);
    }
    delay(2000);
    gps_diag_set_uart(9600);
    gps_diag_send_pcas("PCAS01,1","reassert receiver UART 9600 after factory start");
    delay(120);
    gps_diag_send_pcas("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0","enable compact GGA+RMC");
    delay(120);
    gps_diag_send_pcas("PCAS02,1000","restore 1 Hz");
    delay(120);
    gps_diag_send_pcas("PCAS04,3","restore GPS+BeiDou");
    delay(300);
    gps_diag_capture("factory-start recovery",9600,6000);
    gps_diag_return_to_probe();
}

static uint8_t gps_restore_constellation_value() {
    return gps_power_saved_constellation==MeshInkGpsConstellationMode::Unchanged
        ? 3U : (uint8_t)gps_power_saved_constellation;
}

static void gps_power_prepare_baseline() {
    gps_power_uart_resume();
    gps_power_send("PCAS02,1000","normalize power-test baseline to documented 1 Hz");
#if T5_GPS_FULL_NMEA_DIAGNOSTIC
    gps_power_send("PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0","normalize baseline NMEA output");
#else
    gps_power_send("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0","normalize baseline to compact GGA+RMC");
#endif
    gps_power_send("PCAS04,3","normalize baseline to GPS+BeiDou dual-system mode");
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
    Serial.println("[T5-GPS-POWER] baseline normalized: GPS+BeiDou, 1 Hz, compact NMEA; no settings saved to receiver flash");
}

static void gps_power_restore_receiver(bool cancelled) {
    if(gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState){
        Serial.println("[T5-GPS-POWER] CURRENT STATE measurement: receiver deliberately left untouched; no restore commands sent");
        return;
    }
    Serial.printf("[T5-GPS-POWER] restore begin cancelled=%u experiment='%s'\n",
                  cancelled?1U:0U,meshink_gps_power_experiment_name(gps_power_experiment));
    gps_power_uart_resume();

    if(gps_power_experiment==MeshInkGpsPowerExperiment::BeiDouZeroSatelliteMask){
        gps_power_send("PCAS15,2,FFFFFFFF","restore BeiDou satellites 1-32 after zero-mask experiment");
        delay(50);
        gps_power_send("PCAS15,3,FFFFFFFF","restore BeiDou satellites 33-64 after zero-mask experiment");
        delay(50);
    }
    if(gps_power_experiment==MeshInkGpsPowerExperiment::WatchdogPowerOffReset||
       gps_power_experiment==MeshInkGpsPowerExperiment::OnlineUpgradeWait)
        Serial.println("[T5-GPS-POWER] NOTE: this experiment may intentionally leave GNSS unresponsive; normal restore is attempted, but a full device power cycle may be required");

    gps_power_send("PCAS02,1000","restore documented 1 Hz fix interval");
#if T5_GPS_FULL_NMEA_DIAGNOSTIC
    gps_power_send("PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0","restore diagnostic NMEA output");
#else
    gps_power_send("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0","restore compact GGA+RMC output");
#endif
    char constellation[16];
    snprintf(constellation,sizeof(constellation),"PCAS04,%u",(unsigned)gps_restore_constellation_value());
    gps_power_send(constellation,
        gps_power_saved_constellation==MeshInkGpsConstellationMode::Unchanged
            ?"restore known default GPS+BeiDou because preference was UNCHANGED"
            :"restore saved constellation preference");
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
    Serial.println("[T5-GPS-POWER] restore attempt complete; receiver settings were not saved with PCAS00");
}

static void gps_power_apply_experiment() {
    Serial.printf("[T5-GPS-POWER] ===== APPLY '%s' =====\n",
                  meshink_gps_power_experiment_name(gps_power_experiment));
    switch(gps_power_experiment) {
        case MeshInkGpsPowerExperiment::GpsOnly:
            gps_power_send("PCAS04,1","documented single-system GPS-only mode");
            break;
        case MeshInkGpsPowerExperiment::BeiDouOnly:
            gps_power_send("PCAS04,2","documented single-system BeiDou-only mode");
            break;
        case MeshInkGpsPowerExperiment::GlonassOnly:
            gps_power_send("PCAS04,4","documented single-system GLONASS-only mode");
            break;
        case MeshInkGpsPowerExperiment::BeiDouZeroSatelliteMask:
            gps_power_send("PCAS04,2","select BeiDou-only before removing all BeiDou satellite channels");
            delay(50);
            gps_power_send("PCAS15,2,00000000","disable BeiDou satellites 1-32 without saving configuration");
            delay(50);
            gps_power_send("PCAS15,3,00000000","disable BeiDou satellites 33-64 without saving configuration");
            break;
        case MeshInkGpsPowerExperiment::NavSystemZero: {
            uint8_t payload[44]{};
            payload[1]=0x01;
            payload[13]=0x00;
            gps_power_send_casic_binary(0x06,0x07,payload,sizeof(payload),
                                        "CFG-NAVX mask B8 with navSystem=0");
            break;
        }
        case MeshInkGpsPowerExperiment::NavRate65535: {
            const uint8_t payload[4]={0xFF,0xFF,0x00,0x00};
            gps_power_send_casic_binary(0x06,0x04,payload,sizeof(payload),
                                        "CFG-RATE interval=65535 ms");
            break;
        }
        case MeshInkGpsPowerExperiment::WatchdogPowerOffReset: {
            const uint8_t payload[4]={0x00,0x00,0x04,0x00};
            gps_power_send_casic_binary(0x06,0x02,payload,sizeof(payload),
                                        "CFG-RST resetMode=4 watchdog power-off path");
            break;
        }
        case MeshInkGpsPowerExperiment::OnlineUpgradeWait:
            gps_power_send("PCAS20",
                           "enter CASIC online-upgrade mode and measure loader/wait state");
            break;
        case MeshInkGpsPowerExperiment::CurrentState:
            Serial.println("[T5-GPS-POWER] CURRENT STATE sends no GNSS command");
            break;
        case MeshInkGpsPowerExperiment::AutoMatrixSweep:
        case MeshInkGpsPowerExperiment::VerifyMatrixWinner:
            // These modes use the dedicated quiet matrix state machine.
            break;
    }
    Serial.println("[T5-GPS-POWER] post-change measurement window is 60 seconds; one gauge sample per second");
}

struct GpsPowerSummary {
    int32_t current_sum=0,avg_current_sum=0,avg_power_sum=0;
    uint16_t current_count=0,avg_current_count=0,avg_power_count=0;
    bool external_seen=false;
};

static GpsPowerSummary gps_power_summary(bool post) {
    GpsPowerSummary result{};
    for(size_t i=0;i<gps_power_log_count;++i) {
        const auto& sample=gps_power_log[i];
        if(sample.post!=post)continue;
        result.external_seen=result.external_seen||sample.power.external_power;
        if(sample.power.current_valid){
            result.current_sum+=gps_discharge_ma(sample.power.current_ma);
            ++result.current_count;
        }
        if(sample.power.average_current_valid){
            result.avg_current_sum+=gps_discharge_ma(sample.power.average_current_ma);
            ++result.avg_current_count;
        }
        if(sample.power.average_power_valid){
            result.avg_power_sum+=gps_discharge_mw(sample.power.average_power_mw);
            ++result.avg_power_count;
        }
    }
    return result;
}

static int32_t gps_power_mean(int32_t sum,uint16_t count){
    return count?sum/(int32_t)count:0;
}

static int32_t gps_matrix_power_mw(const GpsMatrixResult& result) {
    return ((int32_t)result.mean_voltage_mv*(int32_t)result.mean_current_ma)/1000;
}

static bool gps_matrix_persist_results() {
    if(!gps_matrix_result_count||!gps_matrix_winner_code)return false;
    Preferences pref;
    if(!pref.begin("gps-matrix",false))return false;
    const size_t bytes=gps_matrix_result_count*sizeof(GpsMatrixResult);
    const bool ok=
        pref.putUInt("magic",GPS_MATRIX_MAGIC)==sizeof(uint32_t)&&
        pref.putUChar("count",(uint8_t)gps_matrix_result_count)==sizeof(uint8_t)&&
        pref.putUChar("winner",gps_matrix_winner_code)==sizeof(uint8_t)&&
        pref.putBool("binmask",gps_matrix_binary_satmask_supported)==sizeof(bool)&&
        pref.putBytes("results",gps_matrix_results,bytes)==bytes;
    pref.end();
    return ok;
}

static bool gps_matrix_load_results() {
    Preferences pref;
    if(!pref.begin("gps-matrix",true))return false;
    const uint32_t magic=pref.getUInt("magic",0);
    const uint8_t count=pref.getUChar("count",0);
    const uint8_t winner=pref.getUChar("winner",0);
    const size_t bytes=pref.getBytesLength("results");
    bool ok=magic==GPS_MATRIX_MAGIC&&count>0&&count<=GPS_MATRIX_MAX_RESULTS&&
            winner>0&&winner<=26&&bytes==(size_t)count*sizeof(GpsMatrixResult);
    if(ok){
        ok=pref.getBytes("results",gps_matrix_results,bytes)==bytes;
        if(ok){
            gps_matrix_result_count=count;
            gps_matrix_winner_code=winner;
            gps_matrix_binary_satmask_supported=pref.getBool("binmask",false);
            gps_matrix_log_valid=true;
        }
    }
    pref.end();
    return ok;
}

static void gps_matrix_print_summary(bool paced) {
    if((!gps_matrix_log_valid||!gps_matrix_result_count)&&!gps_matrix_load_results()){
        Serial.println("[T5-GPS-MATRIX] no completed matrix result is retained");
        if(paced)gps_serial_replay_pause();
        return;
    }
    Serial.printf("[T5-GPS-MATRIX] ===== SUMMARY %u CONFIGURATIONS binary-satmask=%s =====\n",
                  (unsigned)gps_matrix_result_count,
                  gps_matrix_binary_satmask_supported?"SUPPORTED/EXPERIMENTAL":"UNSUPPORTED/SKIPPED");
    if(paced)gps_serial_replay_pause();
    for(size_t i=0;i<gps_matrix_result_count;++i){
        const auto& r=gps_matrix_results[i];
        char label[64];
        gps_matrix_format_label(r.code,label,sizeof(label));
        Serial.printf("[T5-GPS-MATRIX] %02u %-34s mean=%dmA range=%d-%dmA voltage=%umV power=%ldmW samples=%u%s%s\n",
                      (unsigned)(i+1),label,(int)r.mean_current_ma,(int)r.min_current_ma,
                      (int)r.max_current_ma,(unsigned)r.mean_voltage_mv,
                      (long)gps_matrix_power_mw(r),(unsigned)r.samples,
                      (r.flags&0x01)?" EXTERNAL-POWER":"",
                      (r.flags&0x02)?" EXPERIMENTAL-SATMASK":"");
        if(paced)gps_serial_replay_pause();
    }
    char winner[64];
    gps_matrix_format_label(gps_matrix_winner_code,winner,sizeof(winner));
    const GpsMatrixResult* best=nullptr;
    for(size_t i=0;i<gps_matrix_result_count;++i)
        if(gps_matrix_results[i].code==gps_matrix_winner_code){best=&gps_matrix_results[i];break;}
    if(best)
        Serial.printf("[T5-GPS-MATRIX] WINNER code=%u %s mean=%dmA voltage=%umV power=%ldmW\n",
                      (unsigned)gps_matrix_winner_code,winner,(int)best->mean_current_ma,
                      (unsigned)best->mean_voltage_mv,(long)gps_matrix_power_mw(*best));
    else
        Serial.printf("[T5-GPS-MATRIX] WINNER code=%u %s\n",
                      (unsigned)gps_matrix_winner_code,winner);
    if(paced)gps_serial_replay_pause();
    Serial.println("[T5-GPS-MATRIX] power is derived from measured battery voltage x measured current; positive values are battery draw");
    if(paced)gps_serial_replay_pause();
}

static void gps_matrix_reset_accumulator() {
    gps_matrix_current_sum=0;
    gps_matrix_voltage_sum=0;
    gps_matrix_current_min=32767;
    gps_matrix_current_max=-32768;
    gps_matrix_sample_count=0;
    gps_matrix_voltage_count=0;
    gps_matrix_external_seen=false;
}

static void gps_matrix_sample_quiet() {
    MeshInkPowerMeasurement measurement{};
    if(!meshink_power_read_measurement(measurement))return;
    if(measurement.current_valid){
        const int32_t load=gps_discharge_ma(measurement.current_ma);
        gps_matrix_current_sum+=load;
        gps_matrix_current_min=min(gps_matrix_current_min,(int16_t)load);
        gps_matrix_current_max=max(gps_matrix_current_max,(int16_t)load);
        ++gps_matrix_sample_count;
    }
    if(measurement.voltage_valid){
        gps_matrix_voltage_sum+=measurement.voltage_mv;
        ++gps_matrix_voltage_count;
    }
    gps_matrix_external_seen=gps_matrix_external_seen||measurement.external_power;
}

static GpsMatrixResult gps_matrix_make_result(uint8_t code) {
    GpsMatrixResult result{};
    result.code=code;
    result.samples=(uint8_t)min((uint16_t)255,gps_matrix_sample_count);
    result.mean_current_ma=gps_matrix_sample_count
        ?(int16_t)(gps_matrix_current_sum/(int32_t)gps_matrix_sample_count):0;
    result.min_current_ma=gps_matrix_sample_count?gps_matrix_current_min:0;
    result.max_current_ma=gps_matrix_sample_count?gps_matrix_current_max:0;
    result.mean_voltage_mv=gps_matrix_voltage_count
        ?(uint16_t)(gps_matrix_voltage_sum/gps_matrix_voltage_count):0;
    if(gps_matrix_external_seen)result.flags|=0x01;
    if(gps_matrix_requires_binary_mask(code))result.flags|=0x02;
    return result;
}

static void gps_matrix_restore_receiver_after_run() {
    gps_matrix_restore_masks();
    gps_matrix_send_pcas("PCAS02,1000");
#if T5_GPS_FULL_NMEA_DIAGNOSTIC
    gps_matrix_send_pcas("PCAS03,1,1,1,1,1,1,1,1,0,0,,,0,0");
#else
    gps_matrix_send_pcas("PCAS03,1,0,0,0,1,0,0,0,0,0,,,0,0");
#endif
    char constellation[16];
    snprintf(constellation,sizeof(constellation),"PCAS04,%u",
             (unsigned)gps_restore_constellation_value());
    gps_matrix_send_pcas(constellation);
    gps_stream.clearValidation();
    gps_last_byte_at=millis();
}

static void gps_matrix_finish_sweep(bool cancelled) {
    if(cancelled){
        gps_matrix_restore_receiver_after_run();
        gps_power_test_running=false;
        Serial.println("[T5-GPS-MATRIX] sweep cancelled; receiver restore attempted");
        return;
    }
    gps_matrix_log_valid=gps_matrix_result_count>0&&gps_matrix_winner_code>0;
    const bool saved=gps_matrix_log_valid&&gps_matrix_persist_results();
    gps_matrix_restore_receiver_after_run();
    gps_power_test_running=false;
    Serial.printf("[T5-GPS-MATRIX] sweep complete; retained=%u winner=%u NVS=%s\n",
                  (unsigned)gps_matrix_result_count,(unsigned)gps_matrix_winner_code,
                  saved?"OK":"FAILED");
    gps_matrix_print_summary(false);
}

static void gps_matrix_finish_verify(const GpsMatrixResult& result,bool cancelled) {
    gps_matrix_restore_receiver_after_run();
    gps_power_test_running=false;
    if(cancelled){
        Serial.println("[T5-GPS-MATRIX] winner verification cancelled; receiver restore attempted");
        return;
    }
    char label[64];
    gps_matrix_format_label(result.code,label,sizeof(label));
    Serial.printf("[T5-GPS-MATRIX] VERIFY WINNER %s mean=%dmA range=%d-%dmA voltage=%umV power=%ldmW samples=%u%s%s\n",
                  label,(int)result.mean_current_ma,(int)result.min_current_ma,
                  (int)result.max_current_ma,(unsigned)result.mean_voltage_mv,
                  (long)gps_matrix_power_mw(result),(unsigned)result.samples,
                  (result.flags&0x01)?" EXTERNAL-POWER":"",
                  (result.flags&0x02)?" EXPERIMENTAL-SATMASK":"");
}

static void gps_matrix_tick() {
    const uint32_t now=millis();
    if(!gps_matrix_sampling){
        if((int32_t)(now-gps_matrix_settle_until)<0)return;
        gps_matrix_reset_accumulator();
        gps_matrix_sampling=true;
        gps_matrix_sample_started=now;
        gps_matrix_next_sample=now+GPS_POWER_SAMPLE_MS;
        return;
    }
    while((int32_t)(now-gps_matrix_next_sample)>=0){
        gps_matrix_sample_quiet();
        gps_matrix_next_sample+=GPS_POWER_SAMPLE_MS;
        if(now-gps_matrix_sample_started>=
           (gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner
                ?GPS_POWER_POST_MS:GPS_MATRIX_MEASURE_MS))break;
    }
    const uint32_t duration=gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner
        ?GPS_POWER_POST_MS:GPS_MATRIX_MEASURE_MS;
    if(now-gps_matrix_sample_started<duration)return;

    const GpsMatrixResult result=gps_matrix_make_result(gps_matrix_current_code);
    gps_matrix_sampling=false;

    if(gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner){
        gps_matrix_finish_verify(result,false);
        return;
    }

    if(gps_matrix_result_count<GPS_MATRIX_MAX_RESULTS)
        gps_matrix_results[gps_matrix_result_count++]=result;
    if(result.samples&&(!gps_matrix_winner_code||
       result.mean_current_ma<
         [&](){
             int16_t current=32767;
             for(size_t i=0;i+1<gps_matrix_result_count;++i)
                 if(gps_matrix_results[i].code==gps_matrix_winner_code)
                     current=gps_matrix_results[i].mean_current_ma;
             return current;
         }()))
        gps_matrix_winner_code=result.code;

    char label[64];
    gps_matrix_format_label(result.code,label,sizeof(label));
    Serial.printf("[T5-GPS-MATRIX] DONE %02u/%02u %-34s mean=%dmA range=%d-%dmA voltage=%umV power=%ldmW%s%s\n",
                  (unsigned)gps_matrix_result_count,
                  (unsigned)(gps_matrix_binary_satmask_supported?26:11),
                  label,(int)result.mean_current_ma,(int)result.min_current_ma,
                  (int)result.max_current_ma,(unsigned)result.mean_voltage_mv,
                  (long)gps_matrix_power_mw(result),
                  (result.flags&0x01)?" EXTERNAL-POWER":"",
                  (result.flags&0x02)?" EXPERIMENTAL-SATMASK":"");

    const uint8_t next=gps_matrix_next_supported_code(gps_matrix_current_code);
    if(!next){
        gps_matrix_finish_sweep(false);
        return;
    }
    gps_matrix_current_code=next;
    gps_matrix_apply_config(gps_matrix_current_code);
    gps_matrix_settle_until=millis()+GPS_MATRIX_SETTLE_MS;
}

static bool gps_power_persist_log() {
    if(!gps_power_log_count)return false;
    Preferences pref;
    if(!pref.begin("gps-pwrlog",false)){
        Serial.println("[T5-GPS-POWER] WARNING: could not open NVS namespace for completed test log");
        return false;
    }
    const size_t bytes=gps_power_log_count*sizeof(GpsPowerLoggedSample);
    const bool ok=
        pref.putUInt("magic",GPS_POWER_LOG_MAGIC)==sizeof(uint32_t)&&
        pref.putUChar("experiment",(uint8_t)gps_power_experiment)==sizeof(uint8_t)&&
        pref.putUInt("count",(uint32_t)gps_power_log_count)==sizeof(uint32_t)&&
        pref.putBytes("samples",gps_power_log,bytes)==bytes;
    pref.end();
    Serial.printf("[T5-GPS-POWER] completed log NVS save=%s samples=%u bytes=%u; write occurs AFTER measurement window\n",
                  ok?"OK":"FAILED",(unsigned)gps_power_log_count,(unsigned)bytes);
    return ok;
}

static bool gps_power_load_persisted_log() {
    Preferences pref;
    if(!pref.begin("gps-pwrlog",true))return false;
    const uint32_t magic=pref.getUInt("magic",0);
    const uint32_t count=pref.getUInt("count",0);
    const uint8_t experiment=pref.getUChar("experiment",0xFF);
    const size_t bytes=pref.getBytesLength("samples");
    bool ok=magic==GPS_POWER_LOG_MAGIC&&count>0&&count<=GPS_POWER_LOG_CAPACITY&&
            experiment<=(uint8_t)MeshInkGpsPowerExperiment::CurrentState&&
            bytes==count*sizeof(GpsPowerLoggedSample);
    if(ok){
        ok=pref.getBytes("samples",gps_power_log,bytes)==bytes;
        if(ok){
            gps_power_log_count=(size_t)count;
            gps_power_experiment=(MeshInkGpsPowerExperiment)experiment;
            gps_power_log_valid=true;
        }
    }
    pref.end();
    if(ok)Serial.printf("[T5-GPS-POWER] loaded persisted test log experiment='%s' samples=%u\n",
                        meshink_gps_power_experiment_name(gps_power_experiment),
                        (unsigned)gps_power_log_count);
    return ok;
}

static void gps_power_print_summary(bool paced=false) {
    const auto base=gps_power_summary(false);
    const auto post=gps_power_summary(true);
    const int32_t base_i=gps_power_mean(base.current_sum,base.current_count);
    const int32_t post_i=gps_power_mean(post.current_sum,post.current_count);
    const int32_t base_ai=gps_power_mean(base.avg_current_sum,base.avg_current_count);
    const int32_t post_ai=gps_power_mean(post.avg_current_sum,post.avg_current_count);
    const int32_t base_p=gps_power_mean(base.avg_power_sum,base.avg_power_count);
    const int32_t post_p=gps_power_mean(post.avg_power_sum,post.avg_power_count);

    Serial.printf("[T5-GPS-POWER] ===== RESULT '%s' =====\n",
                  meshink_gps_power_experiment_name(gps_power_experiment));
    if(paced)gps_serial_replay_pause();

    if(gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState){
        Serial.printf("[T5-GPS-POWER] untouched-state mean load: Current=%ldmA AverageCurrent=%ldmA AveragePower=%ldmW samples=%u/%u/%u\n",
                      (long)post_i,(long)post_ai,(long)post_p,
                      (unsigned)post.current_count,(unsigned)post.avg_current_count,(unsigned)post.avg_power_count);
        if(paced)gps_serial_replay_pause();
        if(post.external_seen)
            Serial.println("[T5-GPS-POWER] WARNING: external/USB power was detected during untouched-state measurement");
        else
            Serial.println("[T5-GPS-POWER] untouched-state measurement remained battery-only according to charger power-good");
        if(paced)gps_serial_replay_pause();
        return;
    }

    Serial.printf("[T5-GPS-POWER] baseline mean load: Current=%ldmA AverageCurrent=%ldmA AveragePower=%ldmW samples=%u/%u/%u\n",
                  (long)base_i,(long)base_ai,(long)base_p,
                  (unsigned)base.current_count,(unsigned)base.avg_current_count,(unsigned)base.avg_power_count);
    if(paced)gps_serial_replay_pause();
    Serial.printf("[T5-GPS-POWER] post mean load:     Current=%ldmA AverageCurrent=%ldmA AveragePower=%ldmW samples=%u/%u/%u\n",
                  (long)post_i,(long)post_ai,(long)post_p,
                  (unsigned)post.current_count,(unsigned)post.avg_current_count,(unsigned)post.avg_power_count);
    if(paced)gps_serial_replay_pause();
    Serial.printf("[T5-GPS-POWER] apparent saving:    Current=%ldmA AverageCurrent=%ldmA AveragePower=%ldmW (positive = less battery draw)\n",
                  (long)(base_i-post_i),(long)(base_ai-post_ai),(long)(base_p-post_p));
    if(paced)gps_serial_replay_pause();
    if(base.external_seen||post.external_seen)
        Serial.println("[T5-GPS-POWER] WARNING: external/USB power was detected during measurement; charger behaviour can invalidate the comparison");
    else
        Serial.println("[T5-GPS-POWER] measurement remained battery-only according to charger power-good");
    if(paced)gps_serial_replay_pause();
}

static void gps_power_sample_now() {
    MeshInkPowerMeasurement measurement{};
    const uint32_t now=millis();
    const uint32_t elapsed_seconds=(now-gps_power_phase_started)/1000U;
    const uint16_t elapsed=(uint16_t)(elapsed_seconds>65535U?65535U:elapsed_seconds);
    const bool ok=meshink_power_read_measurement(measurement);
    if(gps_power_log_count<GPS_POWER_LOG_CAPACITY){
        GpsPowerLoggedSample& entry=gps_power_log[gps_power_log_count++];
        entry.post=gps_power_post_phase;
        entry.elapsed_s=elapsed;
        entry.power=measurement;
    }
    const int32_t load_i=measurement.current_valid?gps_discharge_ma(measurement.current_ma):0;
    const int32_t load_ai=measurement.average_current_valid?gps_discharge_ma(measurement.average_current_ma):0;
    const int32_t load_p=measurement.average_power_valid?gps_discharge_mw(measurement.average_power_mw):0;
    const char* phase=gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState
        ?"STATE":(gps_power_post_phase?"POST":"BASE");
    Serial.printf("[T5-GPS-POWER] sample phase=%s t=%us read=%u valid[V/I/AI/AP/SOC]=%u/%u/%u/%u/%u V=%umV I=%dmA load=%ldmA AI=%dmA avg-load=%ldmA AP=%dmW avg-load=%ldmW SOC=%u%% ext=%u\n",
                  phase,(unsigned)elapsed,ok?1U:0U,
                  measurement.voltage_valid?1U:0U,measurement.current_valid?1U:0U,
                  measurement.average_current_valid?1U:0U,measurement.average_power_valid?1U:0U,
                  measurement.battery_percent_valid?1U:0U,
                  (unsigned)measurement.voltage_mv,(int)measurement.current_ma,(long)load_i,
                  (int)measurement.average_current_ma,(long)load_ai,
                  (int)measurement.average_power_mw,(long)load_p,
                  (unsigned)measurement.battery_percent,measurement.external_power?1U:0U);
}

static void gps_power_finish(bool cancelled) {
    if(!gps_power_test_running)return;
    if(gps_power_experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep){
        gps_matrix_finish_sweep(cancelled);
        return;
    }
    if(gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner){
        if(cancelled){
            gps_matrix_restore_receiver_after_run();
            gps_power_test_running=false;
            Serial.println("[T5-GPS-MATRIX] winner verification cancelled; receiver restore attempted");
        }
        return;
    }
    if(!cancelled)gps_power_print_summary(false);
    else Serial.println("[T5-GPS-POWER] experiment cancelled by standby transition");
    gps_power_restore_receiver(cancelled);
    gps_power_test_running=false;
    gps_power_log_valid=gps_power_log_count>0;
    if(gps_power_log_valid)gps_power_persist_log();
    Serial.printf("[T5-GPS-POWER] test idle; retained %u samples for REPLAY LAST LOG (RAM + NVS when save succeeded)\n",
                  (unsigned)gps_power_log_count);
}
} // namespace

bool meshink_gps_power_test_start(MeshInkGpsPowerExperiment experiment) {
#if ENV_INCLUDE_GPS == 1
    if(gps_power_test_running){
        Serial.println("[T5-GPS-POWER] start rejected: another experiment is already running");
        return false;
    }
    const bool untouched=experiment==MeshInkGpsPowerExperiment::CurrentState;
    const bool matrix=experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep||
                      experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner;
    if(!untouched&&(!gps_baud_locked||detected_gps_module!=GpsModule::L76K)){
        Serial.printf("[T5-GPS-POWER] start rejected: experiment requires checksum-locked L76K; locked=%u module=%s baud=%lu\n",
                      gps_baud_locked?1U:0U,gps_module_name(),(unsigned long)detected_gps_baud);
        return false;
    }

    gps_load_tuning();
    gps_power_experiment=experiment;
    gps_power_saved_constellation=gps_constellation_mode;

    if(matrix){
        gps_power_log_count=0;
        gps_power_log_valid=false;
        gps_power_post_phase=false;
        gps_power_measurement_started=false;

        if(experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep){
            gps_matrix_result_count=0;
            gps_matrix_winner_code=0;
            gps_matrix_log_valid=false;
            gps_matrix_binary_satmask_supported=gps_matrix_probe_binary_satmask();
            gps_matrix_current_code=gps_matrix_next_supported_code(0);
        }else{
            if((!gps_matrix_log_valid||!gps_matrix_winner_code)&&!gps_matrix_load_results()){
                Serial.println("[T5-GPS-MATRIX] VERIFY rejected: run AUTO MATRIX SWEEP first");
                return false;
            }
            const uint8_t winner=gps_matrix_winner_code;
            const bool needs_binary=gps_matrix_requires_binary_mask(winner);
            const bool binary_now=gps_matrix_probe_binary_satmask();
            if(needs_binary&&!binary_now){
                Serial.println("[T5-GPS-MATRIX] VERIFY rejected: saved winner requires experimental CFG-SATMASK but receiver did not answer its poll");
                return false;
            }
            gps_matrix_binary_satmask_supported=binary_now;
            gps_matrix_current_code=winner;
        }

        if(!gps_matrix_current_code){
            Serial.println("[T5-GPS-MATRIX] start rejected: no supported configuration");
            return false;
        }

        gps_power_test_running=true;
        gps_matrix_sampling=false;
        gps_matrix_apply_config(gps_matrix_current_code);
        gps_matrix_settle_until=millis()+GPS_MATRIX_SETTLE_MS;

        char label[64];
        gps_matrix_format_label(gps_matrix_current_code,label,sizeof(label));
        const bool external=meshink_power_external_present();
        Serial.println("[T5-GPS-MATRIX] ============================================================");
        Serial.printf("[T5-GPS-MATRIX] START '%s' first=%s binary-satmask=%s\n",
                      meshink_gps_power_experiment_name(experiment),label,
                      gps_matrix_binary_satmask_supported?"SUPPORTED/EXPERIMENTAL":"UNSUPPORTED/SKIPPED");
        Serial.printf("[T5-GPS-MATRIX] strict isolation=ON settle=%lus sample=%lus per configuration; UART output is silent during each measured window\n",
                      (unsigned long)(GPS_MATRIX_SETTLE_MS/1000U),
                      (unsigned long)((experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner
                          ?GPS_POWER_POST_MS:GPS_MATRIX_MEASURE_MS)/1000U));
        if(experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep)
            Serial.printf("[T5-GPS-MATRIX] configurations=%u (all non-empty OFF/FULL/ZERO combinations supported by this receiver)\n",
                          gps_matrix_binary_satmask_supported?26U:11U);
        Serial.printf("[T5-GPS-MATRIX] external-power-at-start=%u; battery-only is required for trustworthy ranking\n",
                      external?1U:0U);
        return true;
    }

    if(untouched)
        Serial.println("[T5-GPS-POWER] CURRENT STATE mode: no UART changes, no GNSS commands, no baseline normalization");
    else
        gps_power_prepare_baseline();

    gps_power_log_count=0;
    gps_power_log_valid=false;
    gps_power_post_phase=untouched;
    gps_power_measurement_started=false;
    gps_power_test_running=true;
    const uint32_t start_now=millis();
    gps_power_settle_until=start_now+GPS_POWER_SETTLE_MS;
    gps_power_phase_started=0;
    gps_power_next_sample=0;
    const auto status=meshink_gps_read_status();
    const bool external=meshink_power_external_present();

    Serial.println("[T5-GPS-POWER] ============================================================");
    Serial.printf("[T5-GPS-POWER] START experiment='%s' settle=5s %s sample=1Hz\n",
                  meshink_gps_power_experiment_name(experiment),
                  untouched?"untouched-state=60s":"baseline=30s post=60s");
    Serial.printf("[T5-GPS-POWER] receiver module=%s baud=%lu fix=%u sats=%ld constellation-pref=%u\n",
                  gps_module_name(),(unsigned long)detected_gps_baud,status.valid?1U:0U,
                  (long)status.satellites,(unsigned)gps_power_saved_constellation);
    Serial.printf("[T5-GPS-POWER] external-power-at-start=%u; battery-only is strongly preferred\n",external?1U:0U);
    if(external)Serial.println("[T5-GPS-POWER] WARNING: disconnect USB/charger and restart the test for trustworthy battery-current data");
    Serial.println("[T5-GPS-POWER] SETTLE: 5 seconds for display/frontlight transients to finish; samples are not counted");
    if(untouched)
        Serial.println("[T5-GPS-POWER] STATE then measures exactly the present receiver condition for 60 seconds; no GNSS traffic is generated");
    else
        Serial.println("[T5-GPS-POWER] BASELINE then measures the normalized receiver state for a full 30 seconds");
    return true;
#else
    (void)experiment;
    return false;
#endif
}

void meshink_gps_power_test_tick() {
#if ENV_INCLUDE_GPS == 1
    if(!gps_power_test_running)return;
    if(gps_power_experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep||
       gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner){
        gps_matrix_tick();
        return;
    }
    const uint32_t now=millis();
    if(!gps_power_measurement_started){
        if((int32_t)(now-gps_power_settle_until)<0)return;
        gps_power_measurement_started=true;
        gps_power_phase_started=now;
        gps_power_next_sample=now+GPS_POWER_SAMPLE_MS;
        if(gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState)
            Serial.println("[T5-GPS-POWER] UNTOUCHED STATE MEASUREMENT START: 60 seconds, one BQ27220 sample per second; no GNSS traffic generated");
        else
            Serial.println("[T5-GPS-POWER] BASELINE MEASUREMENT START: 30 seconds, one BQ27220 sample per second");
        return;
    }

    while((int32_t)(now-gps_power_next_sample)>=0) {
        gps_power_sample_now();
        gps_power_next_sample+=GPS_POWER_SAMPLE_MS;
        if((uint32_t)(now-gps_power_phase_started) >=
           (gps_power_post_phase?GPS_POWER_POST_MS:GPS_POWER_BASELINE_MS))break;
    }

    if(!gps_power_post_phase&&now-gps_power_phase_started>=GPS_POWER_BASELINE_MS) {
        const auto base=gps_power_summary(false);
        Serial.printf("[T5-GPS-POWER] BASELINE COMPLETE samples=%u mean-current-load=%ldmA mean-average-current-load=%ldmA mean-average-power-load=%ldmW\n",
                      (unsigned)gps_power_log_count,
                      (long)gps_power_mean(base.current_sum,base.current_count),
                      (long)gps_power_mean(base.avg_current_sum,base.avg_current_count),
                      (long)gps_power_mean(base.avg_power_sum,base.avg_power_count));
        gps_power_apply_experiment();
        gps_power_post_phase=true;
        gps_power_phase_started=millis();
        gps_power_next_sample=gps_power_phase_started+GPS_POWER_SAMPLE_MS;
        return;
    }

    if(gps_power_post_phase&&now-gps_power_phase_started>=GPS_POWER_POST_MS)
        gps_power_finish(false);
#endif
}

bool meshink_gps_power_test_busy(){return gps_power_test_running;}

bool meshink_gps_power_test_preserves_receiver_state(){
    return gps_power_test_running&&gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState;
}

bool meshink_gps_power_test_isolation_active(){
    return gps_power_test_running&&
        (gps_power_experiment==MeshInkGpsPowerExperiment::AutoMatrixSweep||
         gps_power_experiment==MeshInkGpsPowerExperiment::VerifyMatrixWinner);
}

bool meshink_gps_power_test_replay_last() {
    if((!gps_power_log_valid||gps_power_log_count==0)&&!gps_power_load_persisted_log()){
        Serial.println("[T5-GPS-POWER] REPLAY requested but no completed/cancelled measurement log is retained");
        return false;
    }

    // Pace every line, not only the sample body. On Android/WebUSB the first
    // unpaced header/summary burst was the part most likely to be corrupted.
    Serial.printf("[T5-GPS-POWER] ===== REPLAY '%s' %u samples =====\n",
                  meshink_gps_power_experiment_name(gps_power_experiment),(unsigned)gps_power_log_count);
    gps_serial_replay_pause();
    gps_power_print_summary(true);
    Serial.println("[T5-GPS-POWER] detailed samples follow at fully throttled USB-safe rate");
    gps_serial_replay_pause();

    for(size_t i=0;i<gps_power_log_count;++i){
        const auto& sample=gps_power_log[i];
        const auto& m=sample.power;
        const char* replay_phase=gps_power_experiment==MeshInkGpsPowerExperiment::CurrentState
            ?"STATE":(sample.post?"POST":"BASE");
        Serial.printf("[T5-GPS-POWER] replay phase=%s t=%us valid[V/I/AI/AP/SOC]=%u/%u/%u/%u/%u V=%umV I=%dmA load=%ldmA AI=%dmA avg-load=%ldmA AP=%dmW avg-load=%ldmW SOC=%u%% ext=%u\n",
                      replay_phase,(unsigned)sample.elapsed_s,
                      m.voltage_valid?1U:0U,m.current_valid?1U:0U,m.average_current_valid?1U:0U,
                      m.average_power_valid?1U:0U,m.battery_percent_valid?1U:0U,
                      (unsigned)m.voltage_mv,(int)m.current_ma,(long)(m.current_valid?gps_discharge_ma(m.current_ma):0),
                      (int)m.average_current_ma,(long)(m.average_current_valid?gps_discharge_ma(m.average_current_ma):0),
                      (int)m.average_power_mw,(long)(m.average_power_valid?gps_discharge_mw(m.average_power_mw):0),
                      (unsigned)m.battery_percent,m.external_power?1U:0U);
        gps_serial_replay_pause();
    }

    Serial.println("[T5-GPS-POWER] ===== REPLAY COMPLETE =====");
    gps_serial_replay_pause();
    return true;
}

bool meshink_gps_power_matrix_replay_last(){
#if ENV_INCLUDE_GPS == 1
    if(gps_power_test_running){
        Serial.println("[T5-GPS-MATRIX] replay rejected: measurement is running");
        return false;
    }
    if((!gps_matrix_log_valid||!gps_matrix_result_count)&&!gps_matrix_load_results())
        return false;
    gps_matrix_print_summary(true);
    return true;
#else
    return false;
#endif
}

bool meshink_gps_diagnostic_run(MeshInkGpsDiagnosticAction action) {
#if ENV_INCLUDE_GPS == 1
    if(gps_power_test_running){
        Serial.println("[T5-GPS-DIAG] diagnostic rejected: power measurement is running");
        return false;
    }
    Serial.println("[T5-GPS-DIAG] ============================================================");
    gps_serial_replay_pause();
    Serial.printf("[T5-GPS-DIAG] START action='%s' locked=%u module=%s detected-baud=%lu\n",
                  meshink_gps_diagnostic_action_name(action),gps_baud_locked?1U:0U,
                  gps_module_name(),(unsigned long)detected_gps_baud);
    gps_serial_replay_pause();

    switch(action){
        case MeshInkGpsDiagnosticAction::PassiveUartScan:
            gps_diag_passive_scan();
            break;
        case MeshInkGpsDiagnosticAction::IdentifyAllBauds:
            gps_diag_identify_all_bauds();
            break;
        case MeshInkGpsDiagnosticAction::Force9600Nmea:
            gps_diag_force_9600_nmea();
            break;
        case MeshInkGpsDiagnosticAction::FactoryStartSweep:
            gps_diag_factory_start_sweep();
            break;
        case MeshInkGpsDiagnosticAction::FullRescue:
            Serial.println("[T5-GPS-DIAG] FULL RESCUE stage 1/2: force UART/NMEA");
            gps_serial_replay_pause();
            gps_diag_force_9600_nmea();
            Serial.println("[T5-GPS-DIAG] FULL RESCUE stage 2/2: factory-start sweep");
            gps_serial_replay_pause();
            gps_diag_factory_start_sweep();
            break;
        default:
            return false;
    }

    Serial.printf("[T5-GPS-DIAG] COMPLETE action='%s'\n",
                  meshink_gps_diagnostic_action_name(action));
    gps_serial_replay_pause();
    Serial.println("[T5-GPS-DIAG] ============================================================");
    gps_serial_replay_pause();
    return true;
#else
    (void)action;
    return false;
#endif
}

void meshink_gps_enter_standby_power_mode() {
#if ENV_INCLUDE_GPS == 1
    if(gps_power_test_running)gps_power_finish(true);
    if(!gps_baud_locked||detected_gps_module!=GpsModule::L76K){
        Serial.printf("[T5-GPS-POWER] standby single-system skipped locked=%u module=%s\n",
                      gps_baud_locked?1U:0U,gps_module_name());
        return;
    }
    gps_power_send("PCAS04,1","standby policy: force single-system GPS to reduce GNSS load");
    gps_standby_forced_single=true;
    gps_standby_magic=GPS_STANDBY_MAGIC;
    Serial.println("[T5-GPS-POWER] standby policy active: GPS-only; shared LoRa/GNSS rail remains ON");
#endif
}

void meshink_gps_leave_standby_power_mode() {
#if ENV_INCLUDE_GPS == 1
    if(gps_standby_magic!=GPS_STANDBY_MAGIC&&!gps_standby_forced_single)return;
    if(!gps_baud_locked||detected_gps_module!=GpsModule::L76K){
        Serial.printf("[T5-GPS-POWER] standby restore deferred until L76K baud lock; locked=%u module=%s\n",
                      gps_baud_locked?1U:0U,gps_module_name());
        return;
    }
    gps_load_tuning();
    const uint8_t restore=gps_constellation_mode==MeshInkGpsConstellationMode::Unchanged
        ? 3U:(uint8_t)gps_constellation_mode;
    char payload[16];snprintf(payload,sizeof(payload),"PCAS04,%u",(unsigned)restore);
    gps_power_send(payload,gps_constellation_mode==MeshInkGpsConstellationMode::Unchanged
        ?"wake policy: restore GPS+BeiDou default"
        :"wake policy: restore saved constellation preference");
    gps_standby_forced_single=false;
    gps_standby_magic=0;
    Serial.println("[T5-GPS-POWER] standby single-system policy cleared on interactive wake");
#endif
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
                    gps_last_byte_at = millis();
                    break;
                }
            }
        }
        if (!found) {
            detected_gps_baud = 9600;
            Serial1.updateBaudRate(detected_gps_baud);
            gps_stream.clearValidation();
            Serial.println("[T5-WARN] gps=NMEA not confirmed; background retry active");
        } else {
            Serial.printf("[T5-INIT] gps=%s baud=%lu OK\n",
                gps_module_name(),(unsigned long)Serial1.baudRate());
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
