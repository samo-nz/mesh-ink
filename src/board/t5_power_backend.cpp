#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include "board_profile.h"
#include "t5_power_backend.h"
#include "../t5_logging.h"

namespace {

static constexpr uint8_t BQ27220_ADDR = 0x55;
static constexpr uint8_t BQ25896_PRIMARY_ADDR = 0x6B;
static constexpr uint8_t BQ25896_ALT_ADDR = 0x6A;
static constexpr uint8_t FRONTLIGHT_PWM_CHANNEL = 6;

bool read_bytes(uint8_t device,uint8_t reg,uint8_t* data,size_t len,uint32_t timeout_ms=20) {
    return i2c_master_write_read_device(
        I2C_NUM_0,device,&reg,1,data,len,pdMS_TO_TICKS(timeout_ms))==ESP_OK;
}

bool write_byte(uint8_t device,uint8_t reg,uint8_t value) {
    const uint8_t data[2]={reg,value};
    return i2c_master_write_to_device(
        I2C_NUM_0,device,data,sizeof(data),pdMS_TO_TICKS(50))==ESP_OK;
}

bool gauge_word(uint8_t command,uint16_t& result) {
    uint8_t data[2]{};
    if(!read_bytes(BQ27220_ADDR,command,data,sizeof(data)))return false;
    result=(uint16_t)(data[0]|((uint16_t)data[1]<<8));
    // TI SLUUBD4A requires >=66 us bus-free time at 400 kHz.
    delayMicroseconds(70);
    return true;
}

bool primary_charger_byte(uint8_t reg,uint8_t& value) {
    return read_bytes(BQ25896_PRIMARY_ADDR,reg,&value,1);
}

bool find_charger(uint8_t& address,uint8_t& reg09) {
    for(const uint8_t candidate:{BQ25896_PRIMARY_ADDR,BQ25896_ALT_ADDR}) {
        if(read_bytes(candidate,0x09,&reg09,1)) {
            address=candidate;
            return true;
        }
    }
    address=0;
    return false;
}

[[noreturn]] void deep_sleep_fallback(const char* reason) {
    Serial.printf("[T5-SHUTDOWN] entering deep-sleep fallback reason=%s wake=BOOT/GPIO0\n",reason);
    Serial.flush();
    esp_sleep_enable_ext0_wakeup((gpio_num_t)T5_PIN_BOOT_BUTTON,0);
    delay(50);
    esp_deep_sleep_start();
    while(true) delay(1000);
}

} // namespace

void meshink_power_frontlight_begin() {
    pinMode(T5_PIN_FRONTLIGHT,OUTPUT);
    digitalWrite(T5_PIN_FRONTLIGHT,LOW);
    ledcSetup(FRONTLIGHT_PWM_CHANNEL,5000,8);
    ledcAttachPin(T5_PIN_FRONTLIGHT,FRONTLIGHT_PWM_CHANNEL);
    ledcWrite(FRONTLIGHT_PWM_CHANNEL,0);
}

void meshink_power_frontlight_set(uint8_t percent) {
    if(percent>100)percent=100;
    const uint8_t duty=percent?(uint8_t)max(1,((int)percent*255)/100):0;
    ledcWrite(FRONTLIGHT_PWM_CHANNEL,duty);
}

bool meshink_power_read_battery_mv(uint16_t& millivolts) {
    uint16_t value=0;
    if(!gauge_word(0x08,value)||value<2500||value>5000)return false;
    millivolts=value;
    return true;
}

bool meshink_power_read_battery_percent(uint8_t& percent) {
    uint16_t value=0;
    if(!gauge_word(0x2C,value)||value>100)return false;
    percent=(uint8_t)value;
    return true;
}

bool meshink_power_read_charge_state(uint8_t& state) {
    uint8_t reg0b=0;
    if(!primary_charger_byte(0x0B,reg0b))return false;
    state=(reg0b>>3)&0x03;
    return true;
}

bool meshink_power_external_present() {
    uint8_t reg0b=0;
    return primary_charger_byte(0x0B,reg0b)&&(reg0b&(1U<<2));
}

bool meshink_power_read_status(MeshInkPowerStatus& status) {
    status=MeshInkPowerStatus{};
    status.battery_voltage_valid=meshink_power_read_battery_mv(status.battery_mv);
    status.battery_percent_valid=meshink_power_read_battery_percent(status.battery_percent);
    status.charger_valid=meshink_power_read_charge_state(status.charge_state);
    status.external_power=meshink_power_external_present();
    return status.battery_voltage_valid||status.battery_percent_valid||status.charger_valid;
}

void meshink_power_recover_boot_path() {
    constexpr uint8_t BATFET_DIS=1u<<5;
    constexpr uint8_t BATFET_RST_EN=1u<<2;
    uint8_t address=0,reg09=0;
    if(!find_charger(address,reg09)) {
        Serial.println("[T5-POWER] boot PMIC recovery skipped: charger not detected");
        return;
    }
    if(!(reg09&BATFET_DIS)) {
        T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] boot PMIC address=0x%02X REG09=0x%02X battery path ready\n",
                  address,reg09);
        return;
    }
    const uint8_t restored=(uint8_t)((reg09&~BATFET_DIS)|BATFET_RST_EN);
    const bool ok=write_byte(address,0x09,restored);
    Serial.printf("[T5-POWER] boot PMIC recovery address=0x%02X REG09 0x%02X->0x%02X result=%s\n",
                  address,reg09,restored,ok?"OK":"FAILED");
    delay(150);
}

[[noreturn]] void meshink_power_enter_ship_mode(MeshInkPowerOffReason reason) {
    constexpr uint8_t BATFET_DIS=1u<<5;
    constexpr uint8_t BATFET_DLY=1u<<3;
    constexpr uint8_t BATFET_RST_EN=1u<<2;
    const bool low_battery=reason==MeshInkPowerOffReason::LowBattery;
    uint8_t address=0,reg09=0;

    if(!find_charger(address,reg09)) {
        if(low_battery)
            Serial.println("[T5-POWER] low-battery PMIC unavailable; deep-sleep fallback");
        else
            Serial.println("[T5-SHUTDOWN] ERROR: BQ25896 not detected at 0x6B or 0x6A");
        deep_sleep_fallback(low_battery?"LOW_BATTERY_PMIC_NOT_FOUND":"PMIC_NOT_FOUND");
    }

    const uint8_t requested=(uint8_t)((reg09|BATFET_DIS|BATFET_RST_EN)&~BATFET_DLY);
    if(low_battery) {
        Serial.printf("[T5-POWER] low-battery ship mode PMIC=0x%02X REG09=0x%02X->0x%02X\n",
                      address,reg09,requested);
    } else {
        T5_DEBUGF(T5_LOG_POWER,"[T5-SHUTDOWN] PMIC detected address=0x%02X REG09 before=0x%02X\n",
                  address,reg09);
        T5_DEBUGF(T5_LOG_POWER,"[T5-SHUTDOWN] preserving wake reset; REG09 request=0x%02X BATFET_DIS=1\n",
                  requested);
    }

    Serial.flush();
    if(!write_byte(address,0x09,requested)) {
        if(!low_battery)Serial.println("[T5-SHUTDOWN] ERROR: REG09 write failed");
        deep_sleep_fallback(low_battery?"LOW_BATTERY_PMIC_WRITE_FAILED":"PMIC_WRITE_FAILED");
    }

    // On battery alone BATFET_DIS removes SYS power and execution stops here.
    // If execution continues, VBUS is present; sleep rather than resume the UI.
    delay(750);
    if(!low_battery)
        Serial.println("[T5-SHUTDOWN] PMIC command returned; USB/VBUS is probably present, using deep sleep until power is removed");
    deep_sleep_fallback(low_battery?"LOW_BATTERY_VBUS_PRESENT":"VBUS_STILL_POWERED");
}
