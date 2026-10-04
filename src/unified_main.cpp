#include <Arduino.h>
#include <esp32-hal-cpu.h>
#include <Preferences.h>
#include <string.h>
#include "ui_onboarding.h"
#include "companion_runtime.h"
#include "map_tiles.h"
#include "hardware/wireless.h"
#include "hardware/buttons.h"
#include "hardware/board.h"
#include "hardware/power.h"

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif
#ifndef T5_SPLIT_SLEEP_DIAG
#define T5_SPLIT_SLEEP_DIAG 0
#endif

static bool companion_mode = false;
static bool deep_sleep_rx_mode = false;
static bool cache64_psram_blocked = false;

#if T5_SPLIT_SLEEP_DIAG
static constexpr uint32_t SPLIT_DIAG_MAGIC=0x534C5038UL; // "SLP8"
RTC_DATA_ATTR static uint32_t split_diag_magic=0;
RTC_DATA_ATTR static uint32_t split_diag_phase=0;
RTC_DATA_ATTR static uint32_t split_diag_boot_wakes=0;
static bool split_diag_radio_active=false;
static uint32_t split_diag_radio_events=0;
static uint32_t split_diag_boot_hold_started=0;

static void split_diag_wait_boot_release() {
    while(meshink_primary_button_pressed())delay(20);
}

[[noreturn]] static void split_diag_button_wake_cycle() {
    const esp_sleep_wakeup_cause_t cause=esp_sleep_get_wakeup_cause();
    meshink_board_diag_restore_button_wake();
    if(cause==ESP_SLEEP_WAKEUP_EXT0)++split_diag_boot_wakes;

    Serial.printf("[T5-DIAG-ESP] WAKE #%lu cause=%d; release BOOT, 15s USB reconnect, then 30s radio test\n",
                  (unsigned long)split_diag_boot_wakes,(int)cause);
    split_diag_wait_boot_release();

    // Give native USB time to enumerate before touching the retained SX1262.
    for(int seconds=15;seconds>0;--seconds){
        Serial.printf("[T5-DIAG-ESP] USB settle before radio test: %ds\n",seconds);
        Serial.flush();
        delay(1000);
    }

    const bool radio_ready=meshink_board_diag_radio_begin();
    Serial.printf("[T5-DIAG-ESP] wake #%lu retained radio test ready=%u; send LoRa packets now\n",
                  (unsigned long)split_diag_boot_wakes,radio_ready?1U:0U);

    uint32_t window_events=0;
    const uint32_t window_started=millis();
    int last_reported=-1;
    while(millis()-window_started<30000UL){
        if(meshink_board_diag_radio_poll(window_events+1))
            ++window_events;

        const uint32_t elapsed=millis()-window_started;
        const int remaining=30-(int)(elapsed/1000UL);
        if(remaining!=last_reported&&
           (remaining==30||remaining==20||remaining==10||remaining<=5)){
            last_reported=remaining;
            Serial.printf("[T5-DIAG-ESP] wake #%lu radio-events=%lu sleeping again in %ds\n",
                          (unsigned long)split_diag_boot_wakes,
                          (unsigned long)window_events,remaining);
            Serial.flush();
        }
        delay(5);
    }

    // Put the live SX1262 back into a known continuous-RX/DIO1-low state
    // immediately before the next CPU deep-sleep interval.
    const bool radio_rearmed=meshink_board_diag_radio_begin();
    Serial.printf("[T5-DIAG-ESP] wake #%lu radio window complete events=%lu final-rearm=%u; sleeping\n",
                  (unsigned long)split_diag_boot_wakes,
                  (unsigned long)window_events,radio_rearmed?1U:0U);

    split_diag_wait_boot_release();
    Serial.flush();
    while(!meshink_board_diag_enter_button_only_deep_sleep(false)){
        split_diag_wait_boot_release();
        delay(250);
    }
    while(true)delay(1000);
}

static bool split_diag_resume_if_needed() {
    if(split_diag_magic!=SPLIT_DIAG_MAGIC||split_diag_phase!=2)return false;
    if(esp_reset_reason()!=ESP_RST_DEEPSLEEP){
        split_diag_magic=0;
        split_diag_phase=0;
        split_diag_boot_wakes=0;
        return false;
    }
    split_diag_button_wake_cycle();
    return true;
}
#endif

static char terminal_line[48]{};
static uint8_t terminal_length=0;
static bool screenshot_capture_mode=false;
static bool terminal_last_was_cr=false;

static void terminal_screenshot_prompt() {
    Serial.println("[T5-CMD] Press Enter to save a screenshot");
}

static void terminal_save_screenshot() {
    char path[20]{};
    if(ui_save_screenshot(path,sizeof(path)))
        Serial.printf("[T5-CMD] Saved %s\n",path);
    else
        Serial.println("[T5-CMD] Screenshot failed (SD card available?)");
    terminal_screenshot_prompt();
}

static void service_local_terminal() {
    // Native USB CDC reports false once the host closes/disconnects the port.
    // Capture mode is deliberately session-scoped, so a reconnect starts clean.
    if(screenshot_capture_mode&&!Serial) {
        screenshot_capture_mode=false;
        terminal_length=0;
        terminal_line[0]=0;
        terminal_last_was_cr=false;
        return;
    }

    while(Serial.available()>0) {
        const int raw=Serial.read();
        if(raw<0)break;
        const char ch=(char)raw;

        // Most serial terminals send CRLF for one Enter. Treat it as one line
        // ending so capture mode produces exactly one screenshot per keypress.
        if(ch=='\n'&&terminal_last_was_cr) {
            terminal_last_was_cr=false;
            continue;
        }

        if(ch=='\r'||ch=='\n') {
            terminal_last_was_cr=(ch=='\r');

            if(!terminal_length) {
                if(screenshot_capture_mode)terminal_save_screenshot();
                continue;
            }

            terminal_line[terminal_length]=0;
            if(screenshot_capture_mode) {
                Serial.println("[T5-CMD] Screenshot mode active; press Enter to capture");
                terminal_screenshot_prompt();
            } else if(!strcmp(terminal_line,"screenshot")||!strcmp(terminal_line,"shot")) {
                screenshot_capture_mode=true;
                Serial.println("[T5-CMD] Screenshot mode armed");
                terminal_screenshot_prompt();
                Serial.println("[T5-CMD] Disconnect serial to exit screenshot mode");
            } else if(!strcmp(terminal_line,"help")) {
                Serial.println("[T5-CMD] commands: screenshot | shot | help");
            } else {
                Serial.printf("[T5-CMD] unknown command: %s (try 'help')\n",terminal_line);
            }

            terminal_length=0;
            terminal_line[0]=0;
        } else {
            terminal_last_was_cr=false;
            if(ch=='\b'||ch==0x7F) {
                if(terminal_length)terminal_line[--terminal_length]=0;
            } else if(ch>=32&&ch<127&&terminal_length+1<sizeof(terminal_line)) {
                terminal_line[terminal_length++]=ch;
                terminal_line[terminal_length]=0;
            }
        }
    }
}

static void check_local_wireless_state(const char* phase,const MeshInkWirelessState& state) {
    if(meshink_wireless_local_radios_off(state))return;
    Serial.printf("[T5-ERROR] local wireless shutdown failed phase=%s wifi=%s bt-controller=%s bt-host=%s\n",
                  phase,
                  state.wifi_off?"off":"ON",
                  state.bluetooth_controller_off?"off":"ON",
                  state.bluetooth_host_off?"off":"ON");
}

static void check_companion_wireless_state(const char* phase,const MeshInkWirelessState& state) {
    if(meshink_wireless_companion_radios_ready(state))return;
    Serial.printf("[T5-ERROR] companion wireless state invalid phase=%s wifi=%s bt-controller=%s bt-host=%s\n",
                  phase,
                  state.wifi_off?"off":"ON",
                  state.bluetooth_controller_off?"off":"ON",
                  state.bluetooth_host_off?"off":"ON");
}

static const char* battery_check_name(MeshInkPowerSleepCheck result) {
    switch(result){
        case MeshInkPowerSleepCheck::Safe:return "safe";
        case MeshInkPowerSleepCheck::Critical:return "critical";
        case MeshInkPowerSleepCheck::ExternalPower:return "external";
        default:return "unavailable";
    }
}

static MeshInkPowerSleepCheck minimal_battery_check(
        const char* phase,MeshInkPowerCriticalState& state,bool recover_path=false) {
    state=MeshInkPowerCriticalState{};
    if(!meshink_power_begin_minimal_bus()){
        Serial.printf("[T5-POWER] %s battery guard: I2C unavailable\n",
                      phase?phase:"unknown");
        return MeshInkPowerSleepCheck::Unavailable;
    }
    if(recover_path)meshink_power_recover_boot_path();
    const MeshInkPowerSleepCheck result=meshink_power_deep_sleep_check(state);
    meshink_power_end_minimal_bus();
    Serial.printf("[T5-POWER] %s battery guard result=%s voltage=%s%umV\n",
                  phase?phase:"unknown",battery_check_name(result),
                  state.battery_mv_valid?"":"unavailable/",
                  state.battery_mv_valid?(unsigned)state.battery_mv:0U);
    return result;
}

void request_companion_mode() {
    if(local_mesh_is_running())local_mesh_flush_contacts_save_now();
    Preferences mode;
    if (mode.begin("t5-boot", false)) {
        mode.putBool("companion_once", true);
        mode.end();
    }
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
        companion_prepare_exit();
        delay(50);
        ESP.restart();
    }
    if (!pressed) pressed_at = 0;
}

void setup() {
    // Boot is a race-to-idle phase: run the ESP32-S3 at its maximum clock
    // until the local UI becomes interactive (or companion setup completes).
    // The steady-state policies then drop back to their validated 80 MHz cruise.
    setCpuFrequencyMhz(240);
    Serial.begin(115200);
    meshink_buttons_begin();

#if T5_SPLIT_SLEEP_DIAG
    if(split_diag_resume_if_needed())return;
#endif

    bool radio_wake=meshink_board_woke_from_radio();
    const bool button_wake=meshink_board_woke_from_primary_button();
    const bool timer_wake=meshink_board_woke_from_timer();

    // EXT0/EXT1 route their wake pads through RTC IO. Capture the wake cause
    // first, then restore DIO1/BOOT to digital GPIO before RadioLib ISR setup
    // or another deep-sleep cycle.
    if(radio_wake||button_wake||timer_wake)
        meshink_board_restore_deep_sleep_wake_pads();

    // A timer can win the ESP32 wake-cause race just as DIO1 asserts. Release
    // only the automatic pad hold, then let a pending radio packet take priority
    // over the periodic battery-only wake path.
    if(timer_wake){
        meshink_board_prepare_retained_aux_wake();
        if(meshink_board_radio_irq_asserted()){
            radio_wake=true;
            Serial.println("[T5-DEEPSLEEP] timer wake coincided with DIO1; radio packet takes priority");
        }
    }

    if(radio_wake||button_wake||timer_wake)
        Serial.printf("[T5-DEEPSLEEP] reset wake radio=%u button=%u timer=%u\n",
                      radio_wake?1U:0U,button_wake?1U:0U,timer_wake?1U:0U);

    if(timer_wake&&!radio_wake){
        MeshInkPowerCriticalState timer_power{};
        const MeshInkPowerSleepCheck timer_result=
            minimal_battery_check("deep-timer",timer_power,false);
        if(timer_result==MeshInkPowerSleepCheck::Critical){
            Serial.println("[T5-DEEPSLEEP] timer wake found critical battery; rendering minimal shutdown notice");
            meshink_board_release_retained_radio_holds();
            ui_minimal_low_battery_shutdown(timer_power,"deep-timer");
        }

        Serial.printf("[T5-DEEPSLEEP] timer battery check=%s; returning to retained deep sleep\n",
                      battery_check_name(timer_result));
        if(meshink_board_return_to_retained_deep_sleep())return;

        // A packet may have arrived during the battery check. Preserve it
        // rather than falling through to a cold boot that would reset the FIFO.
        if(meshink_board_radio_irq_asserted()){
            radio_wake=true;
            Serial.println("[T5-DEEPSLEEP] DIO1 asserted during timer check; switching to RX-wake path");
        }else{
            meshink_board_release_retained_radio_holds();
            Serial.println("[T5-DEEPSLEEP] timer re-sleep refused without radio IRQ; falling back to normal boot");
        }
    }

    if(button_wake){
        const uint32_t hold_started=millis();
        while(meshink_primary_button_pressed()&&millis()-hold_started<2000UL)delay(10);
        const bool long_hold=meshink_primary_button_pressed()&&millis()-hold_started>=2000UL;
        if(long_hold){
            // Give the user immediate confirmation as soon as the long hold is
            // accepted. Retained MeshCore/radio restoration may take a moment.
            meshink_power_frontlight_begin();
            meshink_power_frontlight_set(100);
            Serial.println("[T5-DEEPSLEEP] BOOT wake confirmed by 2s hold; frontlight=100%; restoring retained radio/MeshCore before UI");
            deep_sleep_rx_mode=true;
            companion_mode=false;
            check_local_wireless_state("deep-button-pre",
                meshink_wireless_force_local_radios_off());
            if(!local_mesh_setup_button_wake()){
                Serial.println("[T5-DEEPSLEEP] retained BOOT startup failed; restarting into normal recovery boot");
                Serial.flush();delay(100);ESP.restart();return;
            }
            if(local_mesh_promote_to_ui("deep-button-wake")){
                deep_sleep_rx_mode=false;
                Serial.println("[T5-DEEPSLEEP] BOOT wake interactive UI ready; retained radio runtime preserved");
                return;
            }
            Serial.println("[T5-DEEPSLEEP] BOOT wake UI promotion failed; remaining in headless MeshCore mode");
            return;
        }else{
            Serial.printf("[T5-DEEPSLEEP] BOOT released after %lums; treating as accidental/short wake and re-sleeping\n",
                          (unsigned long)(millis()-hold_started));
            Serial.flush();
            if(meshink_board_return_to_retained_deep_sleep())return;
            meshink_board_release_retained_radio_holds();
            Serial.println("[T5-DEEPSLEEP] short-wake retained re-sleep was refused; falling back to normal full boot");
        }
    }

    if(radio_wake){
        deep_sleep_rx_mode=true;
        companion_mode=false;
        Serial.printf("[T5-BOOT] MeshInk %s board=%s mode=deep-rx-headless\n",
                      T5_FIRMWARE_VERSION,meshink_board_name());
        check_local_wireless_state("deep-rx-pre",
            meshink_wireless_force_local_radios_off());
        if(!local_mesh_setup_rx_wake()){
            Serial.println("[T5-DEEPSLEEP] FATAL: minimal RX-wake startup failed; restarting into normal recovery boot");
            Serial.flush();delay(100);ESP.restart();return;
        }
        Serial.println("[T5-DEEPSLEEP] startup=RX-WAKE-READY; UI intentionally not initialized");
        return;
    }

    // Validate battery state before display/touch/MeshCore startup. This is
    // intentionally independent of the later UI runtime guard so battery-only
    // cold boots exercise a real early cutoff path.
    MeshInkPowerCriticalState boot_power{};
    const MeshInkPowerSleepCheck boot_check=
        minimal_battery_check("cold-boot",boot_power,true);
    if(boot_check==MeshInkPowerSleepCheck::Critical)
        ui_minimal_low_battery_shutdown(boot_power,"cold-boot");

    companion_mode = consume_companion_request();
    Serial.printf("[T5-BOOT] MeshInk %s board=%s mode=%s\n",
                  T5_FIRMWARE_VERSION,meshink_board_name(),
                  companion_mode?"companion":"local");
#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE)
    Serial.printf("[T5-BOOT] cache-line=%dB\n",CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE);
#endif
#if T5_CACHE64_EXPERIMENT
    const bool psram_ok=psramFound();
    if(!psram_ok){
        cache64_psram_blocked=true;
        Serial.println("[T5-ERROR] cache64 PSRAM unavailable; UI start blocked");
        return;
    }
    Serial.println("[T5-INIT] psram=OK");
#endif
    if (companion_mode) {
        // Wi-Fi is never used, even in Bluetooth Companion Mode. Keep its
        // driver deinitialized while allowing the BLE controller/host to start.
        const MeshInkWirelessState before=meshink_wireless_force_wifi_off();
        if(!before.wifi_off)
            Serial.println("[T5-ERROR] companion startup could not disable Wi-Fi");
        companion_setup();
        const MeshInkWirelessState companion_ready=meshink_wireless_read_state();
        check_companion_wireless_state("companion-ready",companion_ready);
        if(meshink_wireless_companion_radios_ready(companion_ready))
            Serial.println("[T5-INIT] wifi-bt=OK wifi=off bt=ready");
        Serial.println("[T5-INIT] companion=READY");
    } else {
        // Standalone UI never uses the ESP32-S3 2.4 GHz radios. Explicitly
        // stop/deinitialize both stacks before local startup, then enforce and
        // verify the policy again after MeshCore setup in case a dependency
        // changes in a future build. Returning from companion mode always
        // reboots through this same path.
        check_local_wireless_state("local-pre",
            meshink_wireless_force_local_radios_off());

        ui_setup();           // show boot logo while storage/radio initialize

        local_mesh_setup();   // includes first-boot SPIFFS mount / format

#if T5_SPLIT_SLEEP_DIAG
        const bool split_diag_radio_armed=meshink_board_diag_radio_begin();
        split_diag_radio_active=true;
        ui_show_split_sleep_diag("SX1262 DIO1 TEST",
                                 split_diag_radio_armed?"WAITING FOR PACKETS":"RADIO ARM FAILED",
                                 "SEND MULTIPLE LORA MESSAGES",
                                 "HOLD BOOT 2S FOR ESP TEST");
        Serial.println("[T5-DIAG-RADIO] diagnostic ready: send repeated packets; DIO1 HIGH/LOW will be logged");
        Serial.println("[T5-DIAG-RADIO] hold BOOT for 2 seconds to switch to BOOT-only deep-sleep test");
        return;
#endif

        const MeshInkWirelessState local_ready=meshink_wireless_force_local_radios_off();
        check_local_wireless_state("local-post-mesh",local_ready);
        if(meshink_wireless_local_radios_off(local_ready))
            Serial.println("[T5-INIT] wifi-bt=OK wifi=off bt=off");

        map_tiles_warm_storage(); // hide SD/map inventory work behind splash

        ui_finish_startup();  // only now show a tappable setup/home screen

        if(local_mesh_is_running())Serial.println("[T5-INIT] startup=READY");
    }


}

void loop() {
#if T5_SPLIT_SLEEP_DIAG
    if(split_diag_radio_active){
        if(meshink_board_diag_radio_poll(split_diag_radio_events+1))
            ++split_diag_radio_events;

        const bool pressed=meshink_primary_button_pressed();
        if(pressed&&!split_diag_boot_hold_started)
            split_diag_boot_hold_started=millis();
        if(!pressed)
            split_diag_boot_hold_started=0;

        if(pressed&&split_diag_boot_hold_started&&
           millis()-split_diag_boot_hold_started>=2000UL){
            split_diag_radio_active=false;
            split_diag_magic=SPLIT_DIAG_MAGIC;
            split_diag_phase=2;
            split_diag_boot_wakes=0;
            Serial.printf("[T5-DIAG] radio phase complete events=%lu; switching to BOOT-only wake test\n",
                          (unsigned long)split_diag_radio_events);
            split_diag_wait_boot_release();
            for(int seconds=3;seconds>0;--seconds){
                Serial.printf("[T5-DIAG-ESP] initial sleep in %ds\n",seconds);
                Serial.flush();
                delay(1000);
            }
            while(!meshink_board_diag_enter_button_only_deep_sleep(true)){
                split_diag_wait_boot_release();
                delay(250);
            }
            return;
        }
        delay(5);
        return;
    }
#endif
    if(cache64_psram_blocked){delay(1000);return;}
    if(deep_sleep_rx_mode){
        local_mesh_rx_wake_loop();
        if(local_mesh_rx_wake_promoted())deep_sleep_rx_mode=false;
        return;
    }
    if (companion_mode) {
        companion_loop();
        companion_exit_button();
    } else {
        service_local_terminal();
        if(local_mesh_is_running())local_mesh_loop();
        ui_loop();
    }
}
