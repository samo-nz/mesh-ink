#include <Arduino.h>
#include <Preferences.h>
#include "ui_onboarding.h"
#include "companion_runtime.h"
#include "t5_timing.h"
#include "map_tiles.h"
#include "hardware/wireless.h"
#include "hardware/buttons.h"

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

static bool companion_mode = false;
static bool cache64_psram_blocked = false;

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

void request_companion_mode() {
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
    Serial.begin(115200);
    meshink_buttons_begin();
    companion_mode = consume_companion_request();
#if defined(CONFIG_ESP32S3_DATA_CACHE_LINE_SIZE)
#endif
#if T5_CACHE64_EXPERIMENT
    const bool psram_ok=psramFound();
    if(!psram_ok){
        cache64_psram_blocked=true;
        Serial.println("[T5-ERROR] cache64 PSRAM unavailable; UI start blocked");
        return;
    }
#endif
    if (companion_mode) {
        // Wi-Fi is never used, even in Bluetooth Companion Mode. Keep its
        // driver deinitialized while allowing the BLE controller/host to start.
        const MeshInkWirelessState before=meshink_wireless_force_wifi_off();
        if(!before.wifi_off)
            Serial.println("[T5-ERROR] companion startup could not disable Wi-Fi");
        companion_setup();
        check_companion_wireless_state("companion-ready",meshink_wireless_read_state());
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
        check_local_wireless_state("local-post-mesh",
            meshink_wireless_force_local_radios_off());
        map_tiles_warm_storage(); // hide SD/map inventory work behind splash
        ui_finish_startup();  // only now show a tappable setup/home screen
    }
}

void loop() {
    if(cache64_psram_blocked){delay(1000);return;}
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
    }
}
