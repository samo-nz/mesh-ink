#include <Arduino.h>
#include <Preferences.h>
#include "ui_onboarding.h"
#include "companion_runtime.h"
#include "t5_timing.h"

static bool companion_mode = false;
static constexpr uint8_t BOOT_BUTTON = 0;

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
    const bool pressed = digitalRead(BOOT_BUTTON) == LOW;
    if (pressed && pressed_at == 0) pressed_at = millis();
    if (pressed && pressed_at != 0 && millis() - pressed_at >= 2000) {
        Serial.println("[T5-BOOT] companion exit requested; returning to local UI now");
        delay(100);
        ESP.restart();
    }
    if (!pressed) pressed_at = 0;
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    companion_mode = consume_companion_request();
    Serial.printf("[T5-BOOT] firmware=%s mode=%s\n", T5_FIRMWARE_VERSION,
                  companion_mode ? "BT companion" : "local UI");
    if (companion_mode) companion_setup();
    else {
        ui_setup();           // show boot logo with INITIALISING STORAGE...
        local_mesh_setup();   // includes first-boot SPIFFS mount / format
        ui_finish_startup();  // only now show a tappable setup/home screen
    }
}

void loop() {
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
