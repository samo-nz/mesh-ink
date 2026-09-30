#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include "board_profile.h"
#include "t5_power_backend.h"
namespace {

static constexpr uint8_t BQ27220_ADDR = 0x55;
static constexpr uint8_t BQ25896_PRIMARY_ADDR = 0x6B;
static constexpr uint8_t BQ25896_ALT_ADDR = 0x6A;
static constexpr uint8_t FRONTLIGHT_PWM_CHANNEL = 6;

// H752-01 uses a single-cell Li-ion pack and the field-tested protection
// policy from pre-abstraction builds. These values are deliberately private
// to this board backend; other boards may use different chemistry, cell count,
// fuel-gauge flags or PMIC policy without changing MeshInk application code.
static constexpr uint16_t T5_CRITICAL_BATTERY_MV = 3300;
static constexpr uint32_t T5_CRITICAL_POLL_MS = 5000;
static constexpr uint8_t T5_CRITICAL_SAMPLES = 3;

bool read_bytes(uint8_t device,uint8_t reg,uint8_t* data,size_t len,uint32_t timeout_ms=20) {
    return i2c_master_write_read_device(
        I2C_NUM_0,device,&reg,1,data,len,pdMS_TO_TICKS(timeout_ms))==ESP_OK;
}

bool write_byte(uint8_t device,uint8_t reg,uint8_t value) {
    const uint8_t data[2]={reg,value};
    return i2c_master_write_to_device(
        I2C_NUM_0,device,data,sizeof(data),pdMS_TO_TICKS(50))==ESP_OK;
}

bool write_bytes(uint8_t device,uint8_t reg,const uint8_t* data,size_t len) {
    uint8_t buffer[9];
    if(len>sizeof(buffer)-1)return false;
    buffer[0]=reg;
    memcpy(buffer+1,data,len);
    return i2c_master_write_to_device(
        I2C_NUM_0,device,buffer,len+1,pdMS_TO_TICKS(50))==ESP_OK;
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
    if(!write_bytes(BQ27220_ADDR,0x00,bytes,sizeof(bytes)))return false;
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
    if(!write_bytes(BQ27220_ADDR,0x3E,address,sizeof(address)))return false;
    delay(2);
    if(!read_bytes(BQ27220_ADDR,0x40,bytes,field.bytes))return false;
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
    if(!write_bytes(BQ27220_ADDR,0x3E,packet,field.bytes+2))return false;
    delayMicroseconds(300);
    if(!write_bytes(BQ27220_ADDR,0x60,commit,sizeof(commit)))return false;
    delay(12);
    uint16_t readback=0;
    return gauge_profile_read(field,readback)&&readback==value;
}
static void gauge_apply_factory_profile_if_needed() {
    uint16_t design=0,voltage=0,battery_status=0,operation=0,control=0;
    if(!gauge_word(0x3C,design)||!gauge_word(0x08,voltage)||
       !gauge_word(0x0A,battery_status)||!gauge_word(0x3A,operation)||
       !gauge_word(0x00,control))return;
    if(design==1500)return;

    // Only migrate the board's known factory-default 3000-mAh mismatch.
    // Do not overwrite an unknown/replacement battery or unexpected gauge state.
    if(design!=3000||voltage<3000||voltage>4250||!(battery_status&0x0008)||
       !(operation&0x0020)||(operation&0x0400)||(control&0x0007))return;

    const uint8_t sec=(operation>>1)&3;
    bool unlocked=false,config_entered=false,profile_ok=false,rollback_ok=true;
    uint16_t originals[T5_PROFILE_COUNT]{};
    bool modified[T5_PROFILE_COUNT]{};
    size_t changed=0;
    do {
        if(sec==3) {
            if(!gauge_control_command(0x0414)||!gauge_control_command(0x3672)||
               !gauge_wait_state(2,0,500))break;
        } else if(sec!=2&&sec!=1) {
            break;
        }
        unlocked=true;

        if(sec!=1) {
            if(!gauge_control_command(0xFFFF)||!gauge_control_command(0xFFFF)||
               !gauge_wait_state(1,0,500))break;
        }

        for(size_t i=0;i<T5_PROFILE_COUNT;++i) {
            if(!gauge_profile_read(T5_FACTORY_GAUGE_PROFILE[i],originals[i]))break;
            ++changed;
        }
        if(changed!=T5_PROFILE_COUNT)break;
        if(originals[4]!=3000)break;

        if(!gauge_control_command(0x0090)||!gauge_wait_state(1,1,1200))break;
        config_entered=true;

        bool write_ok=true;
        for(size_t i=0;i<T5_PROFILE_COUNT;++i) {
            const auto& field=T5_FACTORY_GAUGE_PROFILE[i];
            if(originals[i]==field.value)continue;
            // A failed verify can occur AFTER the hardware committed data.
            // Mark it first so even that field is included in rollback.
            modified[i]=true;
            if(!gauge_profile_write(field,field.value)) {
                write_ok=false;
                break;
            }
        }
        if(!write_ok) {
            // Best-effort rollback of fields already changed, while still in
            // CFGUPDATE. A failed rollback is reported loudly, never hidden.
            for(size_t i=0;i<T5_PROFILE_COUNT;++i)if(modified[i])
                if(!gauge_profile_write(T5_FACTORY_GAUGE_PROFILE[i],originals[i]))rollback_ok=false;
            if(!rollback_ok)Serial.println("[T5-ERROR] battery gauge profile rollback failed");
            break;
        }

        if(!gauge_control_command(0x0091))break;
        config_entered=false;
        delay(2000);

        uint8_t post_sec=0;bool post_cfg=true;
        uint16_t result=0,fcc=0,soc=0;
        if(gauge_security_state(post_sec,post_cfg)&&!post_cfg&&
           gauge_word(0x3C,result)&&gauge_word(0x12,fcc)&&gauge_word(0x2C,soc)&&
           result==1500&&fcc<=1500&&soc<=100)profile_ok=true;
    }while(false);

    if(config_entered) {
        // Whether update or rollback failed, leave the gauge out of CFGUPDATE.
        if(!gauge_control_command(0x0092))
            Serial.println("[T5-ERROR] battery gauge emergency config exit failed");
        delay(300);
    }
    if(unlocked) {
        if(!gauge_control_command(0x0030)||!gauge_wait_state(3,0,1000))
            Serial.println("[T5-ERROR] battery gauge reseal failed; inspect before reboot");
    } else {
        // An interrupted unseal attempt may have succeeded; seal defensively.
        gauge_control_command(0x0030);
    }
    if(!profile_ok)
        Serial.println("[T5-ERROR] battery gauge profile not verified; do not trust battery percentage");
}

static MeshInkPowerWakeInfo make_t5_wake_info() {
    MeshInkPowerWakeInfo info;
    info.confirm_battery="PRESS PWR TO START AGAIN";
    info.confirm_external="ON USB: HOLD BOOT TO WAKE";
    info.off_battery_line1="PRESS PWR BUTTON";
    info.off_battery_line2="TO POWER ON";
    info.off_external_line1="IF STILL POWERED BY USB";
    info.off_external_line2="HOLD BOOT TO WAKE";
    return info;
}

static const MeshInkPowerWakeInfo T5_WAKE_INFO=make_t5_wake_info();

} // namespace

const MeshInkPowerWakeInfo& meshink_power_wake_info() {
    return T5_WAKE_INFO;
}

void meshink_power_prepare_board() {
    gauge_apply_factory_profile_if_needed();
}

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

bool meshink_power_read_charge_state(MeshInkChargeState& state) {
    uint8_t reg0b=0;
    if(!primary_charger_byte(0x0B,reg0b)) {
        state=MeshInkChargeState::Unknown;
        return false;
    }
    switch((reg0b>>3)&0x03) {
        case 0:state=MeshInkChargeState::Idle;break;
        case 1:
        case 2:state=MeshInkChargeState::Charging;break;
        case 3:state=MeshInkChargeState::Full;break;
        default:state=MeshInkChargeState::Unknown;break;
    }
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
    const bool charge_valid=meshink_power_read_charge_state(status.charge_state);
    status.external_power=meshink_power_external_present();
    return status.battery_voltage_valid||status.battery_percent_valid||charge_valid;
}

bool meshink_power_boot_critical(MeshInkPowerCriticalState& state) {
    state=MeshInkPowerCriticalState{};
    if(meshink_power_external_present())return false;

    uint16_t first=0,second=0;
    if(!meshink_power_read_battery_mv(first)||first>=T5_CRITICAL_BATTERY_MV)return false;
    delay(80);
    if(meshink_power_external_present())return false;
    if(!meshink_power_read_battery_mv(second)||second>=T5_CRITICAL_BATTERY_MV)return false;

    state.battery_mv_valid=true;
    state.battery_mv=(uint16_t)(((uint32_t)first+second)/2U);
    state.critical=state.battery_mv<T5_CRITICAL_BATTERY_MV;
    return state.critical;
}

bool meshink_power_poll_critical(MeshInkPowerCriticalState& state) {
    state=MeshInkPowerCriticalState{};
    static uint32_t sampled_at=0;
    static uint8_t low_samples=0;
    const uint32_t now=millis();
    if(sampled_at&&now-sampled_at<T5_CRITICAL_POLL_MS)return false;
    sampled_at=now?now:1;

    if(meshink_power_external_present()) {
        low_samples=0;
        return false;
    }

    uint16_t millivolts=0;
    if(!meshink_power_read_battery_mv(millivolts)) {
        low_samples=0;
        return false;
    }
    state.battery_mv_valid=true;
    state.battery_mv=millivolts;

    if(millivolts>=T5_CRITICAL_BATTERY_MV) {
        low_samples=0;
        return false;
    }
    if(low_samples<T5_CRITICAL_SAMPLES)++low_samples;
    if(low_samples<T5_CRITICAL_SAMPLES)return false;

    state.critical=true;
    return true;
}

void meshink_power_recover_boot_path() {
    constexpr uint8_t BATFET_DIS=1u<<5;
    constexpr uint8_t BATFET_RST_EN=1u<<2;
    uint8_t address=0,reg09=0;
    if(!find_charger(address,reg09)) {
        Serial.println("[T5-ERROR] boot PMIC recovery skipped: charger not detected");
        return;
    }
    if(!(reg09&BATFET_DIS)) {
        return;
    }
    const uint8_t restored=(uint8_t)((reg09&~BATFET_DIS)|BATFET_RST_EN);
    const bool ok=write_byte(address,0x09,restored);
    Serial.printf("[T5-BOOT] PMIC battery path recovery address=0x%02X REG09 0x%02X->0x%02X result=%s\n",
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
