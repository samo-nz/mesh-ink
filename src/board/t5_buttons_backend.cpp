#include <Arduino.h>

#include "board_profile.h"
#include "t5_buttons_backend.h"

void meshink_buttons_begin() {
    pinMode(T5_PIN_BOOT_BUTTON,INPUT_PULLUP);
}

bool meshink_primary_button_pressed() {
    return digitalRead(T5_PIN_BOOT_BUTTON)==LOW;
}

const char* meshink_primary_button_name() {
    return "BOOT";
}
