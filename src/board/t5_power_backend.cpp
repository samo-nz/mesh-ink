#include <Arduino.h>
#include <driver/i2c.h>
#include <esp_sleep.h>

#include "board_profile.h"
#include "t5_power_backend.h"
#include "../t5_logging.h"

#define T5_TRACE(...) T5_DEBUGF(T5_LOG_BOARD, "[T5] " __VA_ARGS__)
#define T5_POWER_TRACE(...) T5_DEBUGF(T5_LOG_POWER, "[T5] " __VA_ARGS__)

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
       !gauge_word(0x00,control)) {
        T5_POWER_TRACE("gauge profile: telemetry unavailable; no changes made\n");return;
    }
    if(design==1500) {
        T5_POWER_TRACE("gauge profile: design=1500mAh; no configuration write needed\n");return;
    }
    // Only migrate the board's known factory-default 3000-mAh mismatch.
    // Do not overwrite an unknown/replacement battery or unexpected gauge state.
    if(design!=3000||voltage<3000||voltage>4250||!(battery_status&0x0008)||
       !(operation&0x0020)||(operation&0x0400)||(control&0x0007)) {
        T5_POWER_TRACE("gauge profile: SKIP unexpected state design=%u voltage=%u battery=0x%04X operation=0x%04X control=0x%04X\n",
                 design,voltage,battery_status,operation,control);return;
    }
    T5_POWER_TRACE("gauge profile: factory 1500mAh migration requested (reported=%umAh)\n",design);
    uint8_t sec=(operation>>1)&3;bool cfg=(operation&0x0400)!=0;
    bool unlocked=false,config_entered=false,profile_ok=false,rollback_ok=true;
    uint16_t originals[T5_PROFILE_COUNT]{};
    bool modified[T5_PROFILE_COUNT]{};
    size_t changed=0;
    do {
        if(sec==3) {
            if(!gauge_control_command(0x0414)||!gauge_control_command(0x3672)||
               !gauge_wait_state(2,0,500)) {
                T5_POWER_TRACE("gauge profile: UNSEAL failed\n");break;
            }
        } else if(sec!=2&&sec!=1) {
            T5_POWER_TRACE("gauge profile: unexpected security state=%u\n",sec);break;
        }
        unlocked=true;
        if(sec!=1) {
            if(!gauge_control_command(0xFFFF)||!gauge_control_command(0xFFFF)||
               !gauge_wait_state(1,0,500)) {
                T5_POWER_TRACE("gauge profile: FULL ACCESS failed\n");break;
            }
        }
        for(size_t i=0;i<T5_PROFILE_COUNT;++i) {
            if(!gauge_profile_read(T5_FACTORY_GAUGE_PROFILE[i],originals[i])) {
                T5_POWER_TRACE("gauge profile: preflight read failed at 0x%04X; NO WRITES\n",
                         T5_FACTORY_GAUGE_PROFILE[i].address);break;
            }
            ++changed;
        }
        if(changed!=T5_PROFILE_COUNT)break;
        if(originals[4]!=3000) {
            T5_POWER_TRACE("gauge profile: preflight design RAM=%u differs from live design=3000; aborting\n",originals[4]);
            break;
        }
        changed=0;
        for(size_t i=0;i<T5_PROFILE_COUNT;++i)
            if(originals[i]!=T5_FACTORY_GAUGE_PROFILE[i].value)++changed;
        T5_POWER_TRACE("gauge profile: verified %u fields, %u differ from LILYGO profile\n",
                 (unsigned)T5_PROFILE_COUNT,(unsigned)changed);
        if(!gauge_control_command(0x0090)||!gauge_wait_state(1,1,1200)) {
            T5_POWER_TRACE("gauge profile: CONFIG UPDATE entry failed\n");break;
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
                T5_POWER_TRACE("gauge profile: WRITE OR VERIFY FAILED address=0x%04X\n",field.address);
                write_ok=false;break;
            }
        }
        if(!write_ok) {
            // Best-effort rollback of fields already changed, while still in
            // CFGUPDATE. A failed rollback is reported loudly, never hidden.
            for(size_t i=0;i<T5_PROFILE_COUNT;++i)if(modified[i])
                if(!gauge_profile_write(T5_FACTORY_GAUGE_PROFILE[i],originals[i]))rollback_ok=false;
            T5_POWER_TRACE("gauge profile: update failed; rollback=%s\n",rollback_ok?"OK":"FAILED");
            if(!rollback_ok)Serial.println("[T5-ERROR] battery gauge profile rollback failed");
            break;
        }
        if(!gauge_control_command(0x0091)) {
            T5_POWER_TRACE("gauge profile: EXIT/REINIT command failed\n");break;
        }
        config_entered=false;
        delay(2000);
        uint8_t post_sec=0;bool post_cfg=true;
        uint16_t result=0,fcc=0,soc=0;
        if(gauge_security_state(post_sec,post_cfg)&&!post_cfg&&
           gauge_word(0x3C,result)&&gauge_word(0x12,fcc)&&gauge_word(0x2C,soc)&&
           result==1500&&fcc<=1500&&soc<=100) {
            profile_ok=true;
            T5_POWER_TRACE("gauge profile: SUCCESS design=%umAh FCC=%umAh SOC=%u%%\n",result,fcc,soc);
        } else T5_POWER_TRACE("gauge profile: POST-UPDATE VALIDATION FAILED design=%u FCC=%u SOC=%u cfg=%u\n",
                       result,fcc,soc,post_cfg);
    }while(false);
    if(config_entered) {
        // Whether update or rollback failed, leave the gauge out of CFGUPDATE.
        if(!gauge_control_command(0x0092))Serial.println("[T5-ERROR] battery gauge emergency config exit failed");
        delay(300);
    }
    if(unlocked) {
        if(!gauge_control_command(0x0030)||!gauge_wait_state(3,0,1000))
            Serial.println("[T5-ERROR] battery gauge reseal failed; inspect before reboot");
    } else {
        // An interrupted unseal attempt may have succeeded; seal defensively.
        gauge_control_command(0x0030);
    }
    if(!profile_ok)Serial.println("[T5-ERROR] battery gauge profile not verified; do not trust battery percentage");
}

#if T5_DIAGNOSTICS
static bool gauge_dm_read_word(uint16_t address,uint16_t& result) {
    // TI SLUUBD4A section 6.1: select the RAM address at 0x3E/0x3F,
    // then read its big-endian value from the MAC data window at 0x40.
    // The address selector is LOW byte first (for 0x929F: 0x9F,0x92).
    // Merely selecting an address does NOT commit a change to data memory.
    const uint8_t pointer[2]={(uint8_t)address,(uint8_t)(address>>8)};
    uint8_t bytes[2]{};
    if(!write_bytes(BQ27220_ADDR,0x3E,pointer,sizeof(pointer)))return false;
    delay(2);
    if(!read_bytes(BQ27220_ADDR,0x40,bytes,sizeof(bytes)))return false;
    result=((uint16_t)bytes[0]<<8)|bytes[1];
    delayMicroseconds(70);
    return true;
}
#endif

static bool pmic_byte(uint8_t reg, uint8_t& result) {
    constexpr uint8_t BQ25896_ADDR = 0x6B;
    return read_bytes(BQ25896_ADDR,reg,&result,1);
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

void meshink_power_diagnostics_report(const char* reason) {
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
        T5_POWER_TRACE("power snapshot reason=%s uptime=%lums\n",reason,(unsigned long)millis());
        T5_POWER_TRACE("charger: VBUS=%umV good=%u source=%u state=%s(%u) IINLIM=%umA ICHG_TARGET=%umA ICHG_ADC=%umA BAT_ADC=%umV TS=0x%02X fault=0x%02X\n",
            adc_vbus,(reg11>>7)&1,(reg0b>>5)&7,charge_names[charge],charge,input_limit,target_current,
            adc_charge,adc_battery,reg10,reg0c);
        last_charger_state=charge;
    }else T5_POWER_TRACE("charger: BQ25896 diagnostic read failed\n");

    GaugeDiagnosticSnapshot s{};
    if(gauge_diagnostic_snapshot(s)){
        const int current=(int16_t)s.current_raw;
        const int temp_c10=(int)s.temperature-2731;
        T5_POWER_TRACE("gauge: voltage=%umV current=%dmA SOC=%u%% SOH=%u%% RM=%umAh FCC=%umAh Design=%umAh cycles=%u temp=%d.%dC\n",
            s.voltage,current,s.soc,s.soh,s.remaining,s.full,s.design_capacity,s.cycle_count,
            temp_c10/10,abs(temp_c10%10));
        T5_POWER_TRACE("gauge request: charging_voltage=%umV charging_current=%umA\n",
            s.charging_voltage,s.charging_current);
        if(s.design_capacity!=1500)
            T5_POWER_TRACE("gauge: CAPACITY MISMATCH fitted=1500mAh reported=%umAh; SOC and FCC not yet trustworthy\n",s.design_capacity);
        if(!strcmp(reason,"early-boot")) {
            const uint8_t security=(s.operation_status>>1)&3;
            if(security==3) {
                T5_POWER_TRACE("gauge profile audit: SEALED (SEC=3), data-memory values NOT readable; previous 0 readings were invalid, not settings\n");
            } else {
                uint16_t full=0,design=0,nominal=0,charge_current=0,charge_voltage=0,taper=0;
                const bool profile_ok=gauge_dm_read_word(0x929D,full)&&
                    gauge_dm_read_word(0x929F,design)&&gauge_dm_read_word(0x92A3,nominal);
                const bool charge_ok=gauge_dm_read_word(0x91FB,charge_current)&&
                    gauge_dm_read_word(0x91FD,charge_voltage)&&gauge_dm_read_word(0x9201,taper);
                if(profile_ok&&design>=100&&design<=32000&&nominal>=2500&&nominal<=5000)
                    T5_POWER_TRACE("gauge profile1 RAM: initial_FCC=%umAh design=%umAh nominal=%umV\n",full,design,nominal);
                else T5_POWER_TRACE("gauge profile1 RAM: invalid or unavailable; do not infer values\n");
                if(charge_ok&&charge_voltage>=2500&&charge_voltage<=4600)
                    T5_POWER_TRACE("gauge profile RAM: requested_charge=%umA charge_voltage=%umV taper=%umA\n",
                        charge_current,charge_voltage,taper);
                else T5_POWER_TRACE("gauge profile RAM: invalid or unavailable; do not infer values\n");
            }
            T5_POWER_TRACE("gauge profile audit: no unseal, config change or calibration performed\n");
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
    }else T5_POWER_TRACE("gauge: BQ27220 diagnostic read failed\n");
#else
    (void)reason;
#endif
}

static void t5_power_diagnostics_tick_impl() {
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
        meshink_power_diagnostics_report(reason);
        reported_at=now?now:1;
    }else{
        last_charger_state=state;
    }
#endif
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
    meshink_power_diagnostics_report("early-boot");
}

void meshink_power_diagnostics_tick() {
    t5_power_diagnostics_tick_impl();
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
    T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] low battery sample %u/%u: %umV\n",
              (unsigned)low_samples,(unsigned)T5_CRITICAL_SAMPLES,
              (unsigned)millivolts);
    if(low_samples<T5_CRITICAL_SAMPLES)return false;

    state.critical=true;
    return true;
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
