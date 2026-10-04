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
    uint8_t r[7]{};
    if(!idf_read(0x51,0x02,r,sizeof(r))){
        valid_=false;
        Serial.println("[T5-WARN] rtc=PCF8563 unavailable; system/GPS fallback active");
        return;
    }
    const bool voltage_low=(r[0]&0x80)!=0;
    const uint8_t second=from_bcd(r[0]&0x7F),minute=from_bcd(r[1]&0x7F),hour=from_bcd(r[2]&0x3F);
    const uint8_t day=from_bcd(r[3]&0x3F),month=from_bcd(r[5]&0x1F),year=from_bcd(r[6]);
    valid_=!voltage_low&&second<60&&minute<60&&hour<24&&day>=1&&day<=31&&month>=1&&month<=12;
    if(valid_){
        const uint32_t utc=DateTime(2000+year,month,day,hour,minute,second).unixtime();
        timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
        Serial.println("[T5-INIT] rtc=PCF8563 OK");
    }else{
        Serial.println("[T5-WARN] rtc=PCF8563 invalid; system/GPS fallback active");
    }
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
    const DateTime dt(utc);const uint8_t r[7]={to_bcd(dt.second()),to_bcd(dt.minute()),to_bcd(dt.hour()),
        to_bcd(dt.day()),to_bcd(dt.dayOfTheWeek()),to_bcd(dt.month()),to_bcd((uint8_t)(dt.year()-2000))};
    valid_=idf_write(0x51,0x02,r,sizeof(r));
    timeval tv{(time_t)utc,0};settimeofday(&tv,nullptr);
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

// EPDiy asks this hook at init and teardown. A true value means the global ISR
// service contains a live SX1262 handler and must be shared/preserved.
extern "C" bool meshink_epdiy_existing_gpio_isr_service() {
    return radio_gpio_irq_handler_active;
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
        const esp_err_t installed=gpio_install_isr_service(ESP_INTR_FLAG_EDGE);
        if(installed==ESP_OK||installed==ESP_ERR_INVALID_STATE){
            if(phase)
                Serial.printf("[T5-DEEPSLEEP] GPIO ISR service ready phase=%s result=%d\n",
                              phase,(int)installed);
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

bool meshink_board_diag_gps_activity() {
#if ENV_INCLUDE_GPS == 1
    // GPS and SX1262 share the H752-01 switched 3V3 rail. Check GPS first,
    // before touching SPI/radio state, so UART activity is an independent rail
    // witness. This does not send any GPS command or alter the shared rail.
    Serial1.end();
    Serial1.setPins(PIN_GPS_TX,PIN_GPS_RX);
    Serial1.begin(9600);
    gps_stream.clearValidation();

    uint32_t bytes=0;
    uint32_t sentence_starts=0;
    uint32_t newlines=0;
    const uint32_t started=millis();
    while(millis()-started<3000UL){
        while(gps_stream.available()>0){
            const int c=gps_stream.read();
            if(c<0)break;
            ++bytes;
            if(c=='    // Restore only the ESP-side transport after deep-sleep reset. The SX1262
    // itself was never reset or powered down, so its modem configuration stays
    // live across the BOOT-only sleep cycle.
    if(!t5_release_held_radio_control_pins("diag-radio")){
        Serial.println("[T5-DIAG-RADIO] control-pin hold release FAILED");
        return false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);

    // No MeshCore ISR exists on the post-deep-sleep diagnostic boot. Keep DIO1
    // raw and explicitly re-arm continuous receive for the next packet.
    radio_hal.detachInterrupt(P_LORA_DIO_1);
    const int16_t armed=radio.startReceive();
    delayMicroseconds(250);
    Serial.printf("[T5-DIAG-RADIO] begin startReceive=%d dio1=%d busy=%d irq=0x%04x\n",
                  (int)armed,digitalRead(P_LORA_DIO_1),digitalRead(P_LORA_BUSY),
                  (unsigned)((uint16_t)radio.getIrqFlags()));
    return armed==RADIOLIB_ERR_NONE&&digitalRead(P_LORA_DIO_1)==LOW;
}

bool meshink_board_diag_radio_poll(uint32_t sequence) {
    pinMode(P_LORA_DIO_1,INPUT);
    if(digitalRead(P_LORA_DIO_1)!=HIGH)return false;

    const uint16_t irq=(uint16_t)radio.getIrqFlags();
    uint8_t offset=0;
    const size_t packet_len=radio.getPacketLength(true,&offset);
    const uint8_t status=radio.getStatus();
    Serial.printf("[T5-DIAG-RADIO] event=%lu DIO1=HIGH irq=0x%04x len=%u offset=%u status=0x%02x busy=%d\n",
                  (unsigned long)sequence,(unsigned)irq,(unsigned)packet_len,
                  (unsigned)offset,(unsigned)status,digitalRead(P_LORA_BUSY));

    uint8_t packet[MAX_TRANS_UNIT]{};
    int16_t consume=RADIOLIB_ERR_NONE;
    if(packet_len){
        const size_t read_len=packet_len>MAX_TRANS_UNIT?MAX_TRANS_UNIT:packet_len;
        consume=radio.readData(packet,read_len);
    }else{
        consume=radio.clearIrqFlags(0xFFFFU);
    }
    delayMicroseconds(250);
    const int after_clear=digitalRead(P_LORA_DIO_1);

    const int16_t rearm=radio.startReceive();
    delayMicroseconds(250);
    const uint16_t irq_after=(uint16_t)radio.getIrqFlags();
    const int after_rearm=digitalRead(P_LORA_DIO_1);
    Serial.printf("[T5-DIAG-RADIO] event=%lu consume=%d DIO1-after-clear=%d rearm=%d DIO1-after-rearm=%d irq-after=0x%04x\n",
                  (unsigned long)sequence,(int)consume,after_clear,(int)rearm,
                  after_rearm,(unsigned)irq_after);
    return true;
}

void meshink_board_diag_restore_button_wake() {
    // The ESP-only half of the diagnostic uses BOOT/EXT0 exclusively. Release
    // only the automatic deep-sleep hold and BOOT's RTC-IO routing. Leave the
    // explicit SX1262 NSS/RESET holds alone so the radio/GPS rail and radio
    // hardware remain undisturbed while the CPU exercises repeated sleep.
    gpio_deep_sleep_hold_dis();
    rtc_gpio_hold_dis((gpio_num_t)T5_PIN_BOOT_BUTTON);
    rtc_gpio_deinit((gpio_num_t)T5_PIN_BOOT_BUTTON);
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
}

bool meshink_board_diag_enter_button_only_deep_sleep(bool first_entry) {
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        Serial.println("[T5-DIAG-ESP] sleep deferred: BOOT still LOW");
        return false;
    }

    // Every post-wake radio test releases NSS/RESET so SPI can talk to the
    // live SX1262. Re-apply those holds before every deep-sleep entry.
    pinMode(P_LORA_NSS,OUTPUT);digitalWrite(P_LORA_NSS,HIGH);
    pinMode(P_LORA_RESET,OUTPUT);digitalWrite(P_LORA_RESET,HIGH);
    const esp_err_t nss_hold=gpio_hold_en((gpio_num_t)P_LORA_NSS);
    const esp_err_t reset_hold=gpio_hold_en((gpio_num_t)P_LORA_RESET);
    if(nss_hold!=ESP_OK||reset_hold!=ESP_OK){
        Serial.printf("[T5-DIAG-ESP] radio pin hold failed nss=%d reset=%d\n",
                      (int)nss_hold,(int)reset_hold);
        return false;
    }

    const esp_err_t clear=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t button=esp_sleep_enable_ext0_wakeup(
        (gpio_num_t)T5_PIN_BOOT_BUTTON,0);
    if(clear!=ESP_OK||button!=ESP_OK){
        Serial.printf("[T5-DIAG-ESP] BOOT-only wake setup failed clear=%d button=%d\n",
                      (int)clear,(int)button);
        return false;
    }

    gpio_deep_sleep_hold_en();
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        gpio_deep_sleep_hold_dis();
        Serial.println("[T5-DIAG-ESP] sleep race avoided: BOOT went LOW");
        return false;
    }

    Serial.printf("[T5-DIAG-ESP] entering deep sleep: ONLY BOOT EXT0 LOW armed; first=%u\n",
                  first_entry?1U:0U);
    Serial.flush();
    delay(100);
    esp_deep_sleep_start();
    return true;
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
    // sleeps; DIO1 wakes through EXT0 HIGH and BOOT through EXT1 ANY_LOW.
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

static bool t5_probe_deep_sleep_radio(MeshInkDeepSleepRadioProbe& probe) {
    probe={};
    // Recreate only the ESP32-side SPI/GPIO transport. Do NOT call std_init(),
    // toggle RESET, change the shared rail, clear IRQs, read FIFO contents or
    // ask the radio for RNG entropy. This is deliberately non-destructive.
    if(!t5_release_held_radio_control_pins("warm-probe")){
        Serial.println("[T5-DEEPSLEEP] warm radio probe failed: control-pin hold release");
        return false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);

    probe.valid=true;
    probe.dio1=(uint8_t)digitalRead(P_LORA_DIO_1);
    probe.busy=(uint8_t)digitalRead(P_LORA_BUSY);

    const uint32_t busy_started=millis();
    while(digitalRead(P_LORA_BUSY)==HIGH&&millis()-busy_started<50)delayMicroseconds(100);
    probe.busy=(uint8_t)digitalRead(P_LORA_BUSY);
    if(probe.busy==HIGH){
        Serial.println("[T5-DEEPSLEEP] warm radio probe failed: BUSY stayed high for 50ms");
        return false;
    }

    probe.irq=(uint16_t)radio.getIrqFlags();
    probe.packet_len=(uint16_t)radio.getPacketLength();
    probe.status=radio.getStatus();
    probe.dio1=(uint8_t)digitalRead(P_LORA_DIO_1);
    probe.transport_ok=true;
    return true;
}

bool meshink_board_probe_deep_sleep_radio(MeshInkDeepSleepRadioProbe& probe) {
    const bool ok=t5_probe_deep_sleep_radio(probe);
    Serial.printf("[T5-DEEPSLEEP] retained-radio probe ok=%u dio1=%u busy=%u irq=0x%04x packet_len=%u status=0x%02x\n",
                  ok?1U:0U,(unsigned)probe.dio1,(unsigned)probe.busy,
                  (unsigned)probe.irq,(unsigned)probe.packet_len,(unsigned)probe.status);
    return ok;
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

    if(packet_wake){
        radio.resetOnStartup=false;
        const bool retained_ready=radio.std_init(&radio_spi);
        radio.resetOnStartup=true;
        if(!retained_ready){
            Serial.println("[T5-DEEPSLEEP] retained FIFO transport init failed");
            return false;
        }
        if(!radio_apply_post_init_board_settings())return false;

        const uint16_t irq=(uint16_t)radio.getIrqFlags();
        const float wake_rssi=radio.getRSSI();
        const float wake_snr=radio.getSNR();
        if(!radio_hal.ensureIsrService("retained-capture"))return false;
        radio_driver.begin();
        if(!radio_driver.captureRetainedWakePacket(wake_rssi,wake_snr)){
            Serial.printf("[T5-DEEPSLEEP] retained wake capture failed irq=0x%04x dio1=%u\n",
                          (unsigned)irq,digitalRead(P_LORA_DIO_1)==HIGH?1U:0U);
            return false;
        }
        Serial.printf("[T5-DEEPSLEEP] retained wake packet captured via normal readData irq=0x%04x saved=%u\n",
                      (unsigned)irq,radio_driver.hasWakePacket()?1U:0U);

        radio_hal.detachInterrupt(P_LORA_DIO_1);
        radio.resetOnStartup=true;
        if(!radio.std_init(&radio_spi)){
            Serial.println("[T5-DEEPSLEEP] clean SX1262 reset/reinit failed after wake capture");
            return false;
        }
        if(!radio_apply_post_init_board_settings())return false;
        board.finishLocalRxWakeCapture();
        Serial.println("[T5-DEEPSLEEP] clean SX1262 reset/reinit complete; saved wake packet pending");
        return true;
    }

    radio.resetOnStartup=true;
    if(!radio.std_init(&radio_spi)){
        Serial.println("[T5-DEEPSLEEP] retained button-wake radio init failed");
        return false;
    }
    if(!radio_apply_post_init_board_settings())return false;
    Serial.println("[T5-DEEPSLEEP] button-wake SX1262 clean init complete");
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
)++sentence_starts;
            if(c=='\n')++newlines;
        }
        delay(2);
    }
    const bool valid=gps_stream.hasValidSentence();
    Serial.printf("[T5-DIAG-GPS] post-sleep UART bytes=%lu starts=%lu lines=%lu valid-nmea=%u baud=%lu\n",
                  (unsigned long)bytes,(unsigned long)sentence_starts,
                  (unsigned long)newlines,valid?1U:0U,
                  (unsigned long)Serial1.baudRate());
    Serial1.end();
    return valid||bytes>0;
#else
    Serial.println("[T5-DIAG-GPS] GPS not compiled in");
    return false;
#endif
}

bool meshink_board_diag_radio_begin() {
    // Restore only the ESP-side transport after deep-sleep reset. The SX1262
    // itself was never reset or powered down, so its modem configuration stays
    // live across the BOOT-only sleep cycle.
    if(!t5_release_held_radio_control_pins("diag-radio")){
        Serial.println("[T5-DIAG-RADIO] control-pin hold release FAILED");
        return false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);

    // No MeshCore ISR exists on the post-deep-sleep diagnostic boot. Keep DIO1
    // raw and explicitly re-arm continuous receive for the next packet.
    radio_hal.detachInterrupt(P_LORA_DIO_1);
    const int16_t armed=radio.startReceive();
    delayMicroseconds(250);
    Serial.printf("[T5-DIAG-RADIO] begin startReceive=%d dio1=%d busy=%d irq=0x%04x\n",
                  (int)armed,digitalRead(P_LORA_DIO_1),digitalRead(P_LORA_BUSY),
                  (unsigned)((uint16_t)radio.getIrqFlags()));
    return armed==RADIOLIB_ERR_NONE&&digitalRead(P_LORA_DIO_1)==LOW;
}

bool meshink_board_diag_radio_poll(uint32_t sequence) {
    pinMode(P_LORA_DIO_1,INPUT);
    if(digitalRead(P_LORA_DIO_1)!=HIGH)return false;

    const uint16_t irq=(uint16_t)radio.getIrqFlags();
    uint8_t offset=0;
    const size_t packet_len=radio.getPacketLength(true,&offset);
    const uint8_t status=radio.getStatus();
    Serial.printf("[T5-DIAG-RADIO] event=%lu DIO1=HIGH irq=0x%04x len=%u offset=%u status=0x%02x busy=%d\n",
                  (unsigned long)sequence,(unsigned)irq,(unsigned)packet_len,
                  (unsigned)offset,(unsigned)status,digitalRead(P_LORA_BUSY));

    uint8_t packet[MAX_TRANS_UNIT]{};
    int16_t consume=RADIOLIB_ERR_NONE;
    if(packet_len){
        const size_t read_len=packet_len>MAX_TRANS_UNIT?MAX_TRANS_UNIT:packet_len;
        consume=radio.readData(packet,read_len);
    }else{
        consume=radio.clearIrqFlags(0xFFFFU);
    }
    delayMicroseconds(250);
    const int after_clear=digitalRead(P_LORA_DIO_1);

    const int16_t rearm=radio.startReceive();
    delayMicroseconds(250);
    const uint16_t irq_after=(uint16_t)radio.getIrqFlags();
    const int after_rearm=digitalRead(P_LORA_DIO_1);
    Serial.printf("[T5-DIAG-RADIO] event=%lu consume=%d DIO1-after-clear=%d rearm=%d DIO1-after-rearm=%d irq-after=0x%04x\n",
                  (unsigned long)sequence,(int)consume,after_clear,(int)rearm,
                  after_rearm,(unsigned)irq_after);
    return true;
}

void meshink_board_diag_restore_button_wake() {
    // The ESP-only half of the diagnostic uses BOOT/EXT0 exclusively. Release
    // only the automatic deep-sleep hold and BOOT's RTC-IO routing. Leave the
    // explicit SX1262 NSS/RESET holds alone so the radio/GPS rail and radio
    // hardware remain undisturbed while the CPU exercises repeated sleep.
    gpio_deep_sleep_hold_dis();
    rtc_gpio_hold_dis((gpio_num_t)T5_PIN_BOOT_BUTTON);
    rtc_gpio_deinit((gpio_num_t)T5_PIN_BOOT_BUTTON);
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
}

bool meshink_board_diag_enter_button_only_deep_sleep(bool first_entry) {
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        Serial.println("[T5-DIAG-ESP] sleep deferred: BOOT still LOW");
        return false;
    }

    // Every post-wake radio test releases NSS/RESET so SPI can talk to the
    // live SX1262. Re-apply those holds before every deep-sleep entry.
    pinMode(P_LORA_NSS,OUTPUT);digitalWrite(P_LORA_NSS,HIGH);
    pinMode(P_LORA_RESET,OUTPUT);digitalWrite(P_LORA_RESET,HIGH);
    const esp_err_t nss_hold=gpio_hold_en((gpio_num_t)P_LORA_NSS);
    const esp_err_t reset_hold=gpio_hold_en((gpio_num_t)P_LORA_RESET);
    if(nss_hold!=ESP_OK||reset_hold!=ESP_OK){
        Serial.printf("[T5-DIAG-ESP] radio pin hold failed nss=%d reset=%d\n",
                      (int)nss_hold,(int)reset_hold);
        return false;
    }

    const esp_err_t clear=esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
    const esp_err_t button=esp_sleep_enable_ext0_wakeup(
        (gpio_num_t)T5_PIN_BOOT_BUTTON,0);
    if(clear!=ESP_OK||button!=ESP_OK){
        Serial.printf("[T5-DIAG-ESP] BOOT-only wake setup failed clear=%d button=%d\n",
                      (int)clear,(int)button);
        return false;
    }

    gpio_deep_sleep_hold_en();
    if(digitalRead(T5_PIN_BOOT_BUTTON)==LOW){
        gpio_deep_sleep_hold_dis();
        Serial.println("[T5-DIAG-ESP] sleep race avoided: BOOT went LOW");
        return false;
    }

    Serial.printf("[T5-DIAG-ESP] entering deep sleep: ONLY BOOT EXT0 LOW armed; first=%u\n",
                  first_entry?1U:0U);
    Serial.flush();
    delay(100);
    esp_deep_sleep_start();
    return true;
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
    // sleeps; DIO1 wakes through EXT0 HIGH and BOOT through EXT1 ANY_LOW.
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

static bool t5_probe_deep_sleep_radio(MeshInkDeepSleepRadioProbe& probe) {
    probe={};
    // Recreate only the ESP32-side SPI/GPIO transport. Do NOT call std_init(),
    // toggle RESET, change the shared rail, clear IRQs, read FIFO contents or
    // ask the radio for RNG entropy. This is deliberately non-destructive.
    if(!t5_release_held_radio_control_pins("warm-probe")){
        Serial.println("[T5-DEEPSLEEP] warm radio probe failed: control-pin hold release");
        return false;
    }
    pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);
    pinMode(P_LORA_DIO_1,INPUT);
    pinMode(P_LORA_BUSY,INPUT);
    radio_spi.begin(P_LORA_SCLK,P_LORA_MISO,P_LORA_MOSI);

    probe.valid=true;
    probe.dio1=(uint8_t)digitalRead(P_LORA_DIO_1);
    probe.busy=(uint8_t)digitalRead(P_LORA_BUSY);

    const uint32_t busy_started=millis();
    while(digitalRead(P_LORA_BUSY)==HIGH&&millis()-busy_started<50)delayMicroseconds(100);
    probe.busy=(uint8_t)digitalRead(P_LORA_BUSY);
    if(probe.busy==HIGH){
        Serial.println("[T5-DEEPSLEEP] warm radio probe failed: BUSY stayed high for 50ms");
        return false;
    }

    probe.irq=(uint16_t)radio.getIrqFlags();
    probe.packet_len=(uint16_t)radio.getPacketLength();
    probe.status=radio.getStatus();
    probe.dio1=(uint8_t)digitalRead(P_LORA_DIO_1);
    probe.transport_ok=true;
    return true;
}

bool meshink_board_probe_deep_sleep_radio(MeshInkDeepSleepRadioProbe& probe) {
    const bool ok=t5_probe_deep_sleep_radio(probe);
    Serial.printf("[T5-DEEPSLEEP] retained-radio probe ok=%u dio1=%u busy=%u irq=0x%04x packet_len=%u status=0x%02x\n",
                  ok?1U:0U,(unsigned)probe.dio1,(unsigned)probe.busy,
                  (unsigned)probe.irq,(unsigned)probe.packet_len,(unsigned)probe.status);
    return ok;
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

    if(packet_wake){
        radio.resetOnStartup=false;
        const bool retained_ready=radio.std_init(&radio_spi);
        radio.resetOnStartup=true;
        if(!retained_ready){
            Serial.println("[T5-DEEPSLEEP] retained FIFO transport init failed");
            return false;
        }
        if(!radio_apply_post_init_board_settings())return false;

        const uint16_t irq=(uint16_t)radio.getIrqFlags();
        const float wake_rssi=radio.getRSSI();
        const float wake_snr=radio.getSNR();
        if(!radio_hal.ensureIsrService("retained-capture"))return false;
        radio_driver.begin();
        if(!radio_driver.captureRetainedWakePacket(wake_rssi,wake_snr)){
            Serial.printf("[T5-DEEPSLEEP] retained wake capture failed irq=0x%04x dio1=%u\n",
                          (unsigned)irq,digitalRead(P_LORA_DIO_1)==HIGH?1U:0U);
            return false;
        }
        Serial.printf("[T5-DEEPSLEEP] retained wake packet captured via normal readData irq=0x%04x saved=%u\n",
                      (unsigned)irq,radio_driver.hasWakePacket()?1U:0U);

        radio_hal.detachInterrupt(P_LORA_DIO_1);
        radio.resetOnStartup=true;
        if(!radio.std_init(&radio_spi)){
            Serial.println("[T5-DEEPSLEEP] clean SX1262 reset/reinit failed after wake capture");
            return false;
        }
        if(!radio_apply_post_init_board_settings())return false;
        board.finishLocalRxWakeCapture();
        Serial.println("[T5-DEEPSLEEP] clean SX1262 reset/reinit complete; saved wake packet pending");
        return true;
    }

    radio.resetOnStartup=true;
    if(!radio.std_init(&radio_spi)){
        Serial.println("[T5-DEEPSLEEP] retained button-wake radio init failed");
        return false;
    }
    if(!radio_apply_post_init_board_settings())return false;
    Serial.println("[T5-DEEPSLEEP] button-wake SX1262 clean init complete");
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
