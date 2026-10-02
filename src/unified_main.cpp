#include <Arduino.h>
#include <Preferences.h>
#include <string.h>
#include "ui_onboarding.h"
#include "companion_runtime.h"
#include "t5_timing.h"
#include "map_tiles.h"
#include "hardware/wireless.h"
#include "hardware/buttons.h"
#include "hardware/board.h"

#ifndef T5_CACHE64_EXPERIMENT
#define T5_CACHE64_EXPERIMENT 0
#endif

static bool companion_mode = false;
static bool cache64_psram_blocked = false;

static char terminal_line[48]{};
static uint8_t terminal_length=0;

static void service_local_terminal() {
    while(Serial.available()>0) {
        const int raw=Serial.read();
        if(raw<0)break;
        const char ch=(char)raw;
        if(ch=='\r'||ch=='\n') {
            if(!terminal_length)continue;
            terminal_line[terminal_length]=0;
            if(!strcmp(terminal_line,"screenshot")||!strcmp(terminal_line,"shot")) {
                char path[20]{};
                if(ui_save_screenshot(path,sizeof(path)))
                    Serial.printf("[T5-CMD] screenshot saved: %s\n",path);
                else
                    Serial.println("[T5-CMD] screenshot failed (SD card available?)");
            } else if(!strcmp(terminal_line,"help")) {
                Serial.println("[T5-CMD] commands: screenshot | shot | help");
            } else {
                Serial.printf("[T5-CMD] unknown command: %s (try 'help')\n",terminal_line);
            }
            terminal_length=0;
            terminal_line[0]=0;
        } else if(ch=='\b'||ch==0x7F) {
            if(terminal_length)terminal_line[--terminal_length]=0;
        } else if(ch>=32&&ch<127&&terminal_length+1<sizeof(terminal_line)) {
            terminal_line[terminal_length++]=ch;
            terminal_line[terminal_length]=0;
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
    Serial.begin(115200);
    meshink_buttons_begin();
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
    if(cache64_psram_blocked){delay(1000);return;}
    if (companion_mode) {
        companion_loop();
        companion_exit_button();
    } else {
        service_local_terminal();
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
