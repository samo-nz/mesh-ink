#include <Arduino.h>
#include <Preferences.h>
#include <esp32-hal-psram.h>
#include "ui_onboarding.h"
#include "companion_runtime.h"
#include "t5_timing.h"
#include "map_tiles.h"
#include "hardware/wireless.h"
#include "hardware/buttons.h"
#include "hardware/power.h"

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

static bool companion_mode = false;
static bool cache64_psram_blocked = false;

namespace {
constexpr uint32_t POWER_SAMPLE_INTERVAL_MS=10000UL;
constexpr size_t STANDBY_POWER_SAMPLE_CAPACITY=8192; // ~22.7 h at 10 s/sample

struct CachedPowerSample {
    uint32_t elapsed_seconds=0;
    uint16_t battery_mv=0;
    int16_t current_ma=0;
    uint16_t remaining_mah=0;
    uint16_t full_mah=0;
    uint8_t battery_percent=0;
    uint8_t external_power=0;
};

struct StandbyPowerSession {
    CachedPowerSample* samples=nullptr;
    size_t capacity=0;
    size_t head=0;
    size_t count=0;
    uint32_t total_samples=0;
    uint32_t overwritten_samples=0;
    uint32_t telemetry_failures=0;
    uint32_t started_at=0;
    uint32_t ended_at=0;
    uint32_t next_sample_at=0;
    int64_t current_sum_ma=0;
    uint32_t battery_only_samples=0;
    int64_t battery_only_current_sum_ma=0;
    int16_t min_current_ma=32767;
    int16_t max_current_ma=-32768;
    CachedPowerSample first{};
    CachedPowerSample last{};
    MeshInkLightSleepStats sleep{};
    bool active=false;
    bool complete=false;
    uint8_t reports_remaining=0;
    uint32_t next_report_at=0;
};

static CachedPowerSample standby_fallback_samples[256];
static StandbyPowerSession standby_power{};
static uint32_t awake_power_sample_at=0;
static bool cached_report_usb_armed=false;

static bool capture_power_sample(CachedPowerSample& out,uint32_t elapsed_seconds) {
    MeshInkPowerTelemetry telemetry{};
    if(!meshink_power_read_telemetry(telemetry))return false;
    out.elapsed_seconds=elapsed_seconds;
    out.battery_mv=telemetry.battery_mv;
    out.current_ma=telemetry.current_ma;
    out.remaining_mah=telemetry.remaining_mah;
    out.full_mah=telemetry.full_mah;
    out.battery_percent=telemetry.battery_percent;
    out.external_power=meshink_power_external_present()?1U:0U;
    return true;
}

static void standby_power_append(const CachedPowerSample& sample) {
    auto& s=standby_power;
    if(!s.samples||!s.capacity)return;
    size_t slot=0;
    if(s.count<s.capacity){
        slot=(s.head+s.count)%s.capacity;
        ++s.count;
    }else{
        slot=s.head;
        s.head=(s.head+1)%s.capacity;
        ++s.overwritten_samples;
    }
    s.samples[slot]=sample;
    ++s.total_samples;
    s.current_sum_ma+=sample.current_ma;
    if(!sample.external_power){
        ++s.battery_only_samples;
        s.battery_only_current_sum_ma+=sample.current_ma;
    }
    if(sample.current_ma<s.min_current_ma)s.min_current_ma=sample.current_ma;
    if(sample.current_ma>s.max_current_ma)s.max_current_ma=sample.current_ma;
    if(s.total_samples==1)s.first=sample;
    s.last=sample;
}

static void standby_power_capture_now(uint32_t now) {
    auto& s=standby_power;
    CachedPowerSample sample{};
    if(capture_power_sample(sample,(now-s.started_at)/1000UL))standby_power_append(sample);
    else ++s.telemetry_failures;
}

static void standby_power_begin(uint32_t now) {
    auto& s=standby_power;
    if(!s.samples){
        s.samples=(CachedPowerSample*)ps_malloc(sizeof(CachedPowerSample)*STANDBY_POWER_SAMPLE_CAPACITY);
        s.capacity=s.samples?STANDBY_POWER_SAMPLE_CAPACITY:
            sizeof(standby_fallback_samples)/sizeof(standby_fallback_samples[0]);
        if(!s.samples)s.samples=standby_fallback_samples;
    }
    CachedPowerSample* buffer=s.samples;
    const size_t capacity=s.capacity;
    s=StandbyPowerSession{};
    s.samples=buffer;
    s.capacity=capacity;
    s.active=true;
    s.started_at=now?now:1;
    s.next_sample_at=now+POWER_SAMPLE_INTERVAL_MS;
    meshink_power_light_sleep_stats_reset();
    standby_power_capture_now(now);
    Serial.printf("[T5-POWER-CACHE] standby measurement started capacity=%u sample_interval=%lus\n",
                  (unsigned)s.capacity,(unsigned long)(POWER_SAMPLE_INTERVAL_MS/1000UL));
    Serial.flush();
}

static void standby_power_finish(uint32_t now) {
    auto& s=standby_power;
    if(!s.active)return;
    // Freeze a final battery-only measurement before the user has time to attach
    // USB. Everything printed later comes from this cached session.
    standby_power_capture_now(now);
    s.ended_at=now;
    s.sleep=meshink_power_light_sleep_stats();
    s.active=false;
    s.complete=true;
    s.reports_remaining=0;
    s.next_report_at=0;
    Serial.printf("[T5-POWER-CACHE] standby measurement frozen elapsed=%lus samples=%lu; connect USB to print cached report\n",
                  (unsigned long)((s.ended_at-s.started_at)/1000UL),
                  (unsigned long)s.total_samples);
    Serial.flush();
}

static void print_hms(uint32_t seconds,char out[16]) {
    const uint32_t h=seconds/3600UL;
    const uint32_t m=(seconds/60UL)%60UL;
    const uint32_t sec=seconds%60UL;
    snprintf(out,16,"%02lu:%02lu:%02lu",(unsigned long)h,(unsigned long)m,(unsigned long)sec);
}

static void print_cached_standby_report() {
    auto& s=standby_power;
    if(!s.complete)return;
    const uint32_t elapsed_ms=s.ended_at-s.started_at;
    char elapsed[16]{},sleep_time[16]{};
    print_hms(elapsed_ms/1000UL,elapsed);
    print_hms((uint32_t)(s.sleep.total_us/1000000ULL),sleep_time);
    const int avg_all=s.total_samples?(int)(s.current_sum_ma/(int64_t)s.total_samples):0;
    const int avg_battery=s.battery_only_samples?
        (int)(s.battery_only_current_sum_ma/(int64_t)s.battery_only_samples):0;
    Serial.println("[T5-POWER-CACHE] ===== FROZEN PRE-USB STANDBY REPORT =====");
    Serial.printf("[T5-POWER-CACHE] firmware=%s elapsed=%s samples=%lu stored=%u overwritten=%lu failures=%lu\n",
                  T5_FIRMWARE_VERSION,elapsed,(unsigned long)s.total_samples,(unsigned)s.count,
                  (unsigned long)s.overwritten_samples,(unsigned long)s.telemetry_failures);
    Serial.printf("[T5-POWER-CACHE] light-sleep calls=%llu time=%s (%llu us)\n",
                  (unsigned long long)s.sleep.calls,sleep_time,
                  (unsigned long long)s.sleep.total_us);
    Serial.printf("[T5-POWER-CACHE] battery-current avg_all=%dmA avg_external_off=%dmA min=%dmA max=%dmA external_samples=%lu\n",
                  avg_all,avg_battery,(int)(s.total_samples?s.min_current_ma:0),
                  (int)(s.total_samples?s.max_current_ma:0),
                  (unsigned long)(s.total_samples-s.battery_only_samples));
    if(s.total_samples){
        Serial.printf("[T5-POWER-CACHE] first voltage=%umV current=%dmA soc=%u%% remaining=%umAh full=%umAh external=%u\n",
                      (unsigned)s.first.battery_mv,(int)s.first.current_ma,
                      (unsigned)s.first.battery_percent,(unsigned)s.first.remaining_mah,
                      (unsigned)s.first.full_mah,(unsigned)s.first.external_power);
        Serial.printf("[T5-POWER-CACHE] last  voltage=%umV current=%dmA soc=%u%% remaining=%umAh full=%umAh external=%u\n",
                      (unsigned)s.last.battery_mv,(int)s.last.current_ma,
                      (unsigned)s.last.battery_percent,(unsigned)s.last.remaining_mah,
                      (unsigned)s.last.full_mah,(unsigned)s.last.external_power);
    }
    for(size_t i=0;i<s.count;++i){
        const auto& sample=s.samples[(s.head+i)%s.capacity];
        Serial.printf("[T5-POWER-CACHE] sample=%u t=%lus voltage=%umV current=%dmA soc=%u%% remaining=%umAh full=%umAh external=%u\n",
                      (unsigned)(i+1),(unsigned long)sample.elapsed_seconds,
                      (unsigned)sample.battery_mv,(int)sample.current_ma,
                      (unsigned)sample.battery_percent,(unsigned)sample.remaining_mah,
                      (unsigned)sample.full_mah,(unsigned)sample.external_power);
    }
    Serial.println("[T5-POWER-CACHE] ===== END FROZEN STANDBY REPORT =====");
    Serial.flush();
}

static void power_logging_tick() {
    const uint32_t now=millis();
    const bool standby=!companion_mode&&ui_is_standby();

    // The measurement session is the source of truth. Do not rely on a
    // separate edge latch: USB/light-sleep can make observation timing awkward.
    if(standby&&!standby_power.active)standby_power_begin(now);
    else if(!standby&&standby_power.active)standby_power_finish(now);

    if(standby){
        if((int32_t)(now-standby_power.next_sample_at)>=0){
            standby_power_capture_now(now);
            standby_power.next_sample_at=now+POWER_SAMPLE_INTERVAL_MS;
        }
        // USB CDC is intentionally silent during light-sleep standby. Samples
        // are printed only after BOOT wake, from the frozen pre-USB cache.
        return;
    }

    const bool external=meshink_power_external_present();
    if(standby_power.complete&&!cached_report_usb_armed&&external){
        cached_report_usb_armed=true;
        standby_power.reports_remaining=3;
        standby_power.next_report_at=now+3000UL;
    }
    if(!external&&standby_power.complete)cached_report_usb_armed=false;

    if(standby_power.complete&&standby_power.reports_remaining&&
       (int32_t)(now-standby_power.next_report_at)>=0){
        print_cached_standby_report();
        --standby_power.reports_remaining;
        standby_power.next_report_at=millis()+5000UL;
    }

    if(!awake_power_sample_at||now-awake_power_sample_at>=POWER_SAMPLE_INTERVAL_MS){
        awake_power_sample_at=now?now:1;
        MeshInkPowerTelemetry sample{};
        if(!meshink_power_read_telemetry(sample)){
            Serial.printf("[T5-POWER] sample mode=%s standby=0 result=UNAVAILABLE\n",
                          companion_mode?"companion":"local");
        }else{
            Serial.printf("[T5-POWER] sample mode=%s standby=0 cpu=%luMHz voltage=%umV current=%dmA soc=%u%% remaining=%umAh full=%umAh external=%u\n",
                          companion_mode?"companion":"local",(unsigned long)getCpuFrequencyMhz(),
                          (unsigned)sample.battery_mv,(int)sample.current_ma,
                          (unsigned)sample.battery_percent,(unsigned)sample.remaining_mah,
                          (unsigned)sample.full_mah,external?1U:0U);
        }
    }
}
} // namespace

static void report_local_wireless_state(const char* phase,const MeshInkWirelessState& state) {
    const bool ok=meshink_wireless_local_radios_off(state);
    Serial.printf("[T5-POWER] wireless %s wifi=%s bt-controller=%s bt-host=%s result=%s\n",
                  phase,
                  state.wifi_off?"off":"ON",
                  state.bluetooth_controller_off?"off":"ON",
                  state.bluetooth_host_off?"off":"ON",
                  ok?"OK":"ERROR");
}

static void report_companion_wireless_state(const char* phase,const MeshInkWirelessState& state) {
    const bool ok=meshink_wireless_companion_radios_ready(state);
    Serial.printf("[T5-POWER] wireless %s wifi=%s bt-controller=%s bt-host=%s result=%s\n",
                  phase,
                  state.wifi_off?"off":"ON",
                  state.bluetooth_controller_off?"off":"ON",
                  state.bluetooth_host_off?"off":"ON",
                  ok?"OK":"ERROR");
}

void request_companion_mode() {
    Preferences mode;
    if (mode.begin("t5-boot", false)) {
        mode.putBool("companion_once", true);
        mode.end();
    }
    Serial.println("[T5-BOOT] one-shot companion mode saved; restarting");
    delay(150);
    ESP.restart();
}

static bool consume_companion_request() {
    Preferences mode;
    if (!mode.begin("t5-boot", false)) return false;
    const bool requested = mode.getBool("companion_once", false);
    if (requested) mode.remove("companion_once");
    mode.end();
    return requested;
}

static void companion_exit_button() {
    static uint32_t pressed_at = 0;
    const bool pressed = meshink_primary_button_pressed();
    if (pressed && pressed_at == 0) pressed_at = millis();
    if (pressed && pressed_at != 0 && millis() - pressed_at >= 2000) {
        Serial.println("[T5-BOOT] companion exit requested; returning to local UI now");
        companion_prepare_exit();
        delay(50);
        ESP.restart();
    }
    if (!pressed) pressed_at = 0;
}

void setup() {
    Serial.begin(115200);
    meshink_buttons_begin();
    companion_mode = consume_companion_request();
    Serial.printf("[T5-BOOT] firmware=%s mode=%s\n", T5_FIRMWARE_VERSION,
                  companion_mode ? "BT companion" : "local UI");
#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE)
    Serial.printf("[T5-BOOT] data-cache-line=%dB cache64-experiment=%d\n",
                  CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE, T5_CACHE64_EXPERIMENT);
#endif
#if T5_CACHE64_EXPERIMENT
    const bool psram_ok=psramFound();
    Serial.printf("[T5-BOOT] psram-found=%d size=%lu free=%lu\n",
                  psram_ok?1:0,(unsigned long)ESP.getPsramSize(),
                  (unsigned long)ESP.getFreePsram());
    if(!psram_ok){
        cache64_psram_blocked=true;
        Serial.println("[T5-BOOT] FATAL cache64 PSRAM unavailable; UI start blocked to prevent EPDiy reboot loop");
        return;
    }
#endif
    if (companion_mode) {
        // Wi-Fi is never used, even in Bluetooth Companion Mode. Keep its
        // driver deinitialized while allowing the BLE controller/host to start.
        const MeshInkWirelessState before=meshink_wireless_force_wifi_off();
        Serial.printf("[T5-POWER] wireless companion-pre wifi=%s result=%s\n",
                      before.wifi_off?"off":"ON",before.wifi_off?"OK":"ERROR");
        companion_setup();
        report_companion_wireless_state("companion-ready",meshink_wireless_read_state());
    } else {
        // Standalone UI never uses the ESP32-S3 2.4 GHz radios. Explicitly
        // stop/deinitialize both stacks before local startup, then enforce and
        // verify the policy again after MeshCore setup in case a dependency
        // changes in a future build. Returning from companion mode always
        // reboots through this same path.
        report_local_wireless_state("local-pre",
            meshink_wireless_force_local_radios_off());
        ui_setup();           // show boot logo while storage/radio initialize
        local_mesh_setup();   // includes first-boot SPIFFS mount / format
        report_local_wireless_state("local-post-mesh",
            meshink_wireless_force_local_radios_off());
        map_tiles_warm_storage(); // hide SD/map inventory work behind splash
        ui_finish_startup();  // only now show a tappable setup/home screen
    }
}

void loop() {
    if(cache64_psram_blocked){delay(1000);return;}
    power_logging_tick();
    if (companion_mode) {
        companion_loop();
        companion_exit_button();
    } else {
        const uint32_t cycle_started=t5_timing_cycle_begin();
        if(local_mesh_is_running()){
            const uint32_t mesh_started=t5_timing_section_begin(T5TimingSection::Mesh);
            local_mesh_loop();
            t5_timing_section_end(T5TimingSection::Mesh,mesh_started);
        }
        const uint32_t ui_started=t5_timing_section_begin(T5TimingSection::Ui);
        ui_loop();
        t5_timing_section_end(T5TimingSection::Ui,ui_started);
        t5_timing_cycle_end(cycle_started);
        t5_timing_service();
        // Catch standby transitions caused inside ui_loop() immediately. This
        // freezes BOOT-wake telemetry before the user reconnects USB.
        power_logging_tick();
    }
}
