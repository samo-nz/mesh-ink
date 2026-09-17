#include <Arduino.h>
#include <Preferences.h>
#include "ui_onboarding.h"

// Compile the current upstream companion application unchanged, but rename its
// Arduino entry points so our device-owned boot selector can choose the mode.
#include "../lib/MeshCore/examples/companion_radio/MyMesh.cpp"
#define setup meshcore_companion_setup
#define loop meshcore_companion_loop
#include "../lib/MeshCore/examples/companion_radio/main.cpp"
#undef setup
#undef loop

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
    if (!pressed && pressed_at != 0) {
        const uint32_t held = millis() - pressed_at;
        pressed_at = 0;
        if (held >= 2000) {
            Serial.println("[T5-BOOT] companion exit requested; returning to local UI");
            delay(100);
            ESP.restart();
        }
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(BOOT_BUTTON, INPUT_PULLUP);
    companion_mode = consume_companion_request();
    Serial.printf("[T5-BOOT] firmware=%s mode=%s\n", T5_FIRMWARE_VERSION,
                  companion_mode ? "BT companion" : "local UI");
    if (companion_mode) meshcore_companion_setup();
    else ui_setup();
}

void loop() {
    if (companion_mode) {
        meshcore_companion_loop();
        companion_exit_button();
    } else {
        ui_loop();
    }
}
