#include "io_coordinator.h"
#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// Hold this spinlock only for state transitions, never during flash or EPDiy.
namespace {
portMUX_TYPE state_mux = portMUX_INITIALIZER_UNLOCKED;
TaskHandle_t flash_owner = nullptr;
uint16_t flash_depth = 0;
uint16_t display_requests = 0;
bool display_active = false;
uint32_t deferred_meshtastic_segments = 0;
}
void meshink_io_display_begin() {
    portENTER_CRITICAL(&state_mux);
    ++display_requests; // Prevent a new flash lease while UI is waiting.
    portEXIT_CRITICAL(&state_mux);
    const uint32_t started=millis();
    for (;;) {
        bool granted=false;
        portENTER_CRITICAL(&state_mux);
        if (!display_active && !flash_owner) {
            display_active=true;
            --display_requests;
            granted=true;
        }
        portEXIT_CRITICAL(&state_mux);
        if (granted) break;
        vTaskDelay(1);
    }
    const uint32_t waited=millis()-started;
    if (waited>=100)
        Serial.printf("[MeshInk/IO] display waited %lums for flash\n",(unsigned long)waited);
}
void meshink_io_display_end() {
    portENTER_CRITICAL(&state_mux);
    display_active=false;
    portEXIT_CRITICAL(&state_mux);
}
bool meshink_io_flash_try_begin() {
    const TaskHandle_t self=xTaskGetCurrentTaskHandle();
    bool granted=false;
    portENTER_CRITICAL(&state_mux);
    if (flash_owner==self && flash_depth) {
        ++flash_depth; // SafeFile nested inside NodeDB save.
        granted=true;
    } else if (!display_active && !display_requests && !flash_owner) {
        flash_owner=self;
        flash_depth=1;
        granted=true;
    }
    portEXIT_CRITICAL(&state_mux);
    return granted;
}
bool meshink_io_flash_begin_wait(uint32_t timeout_ms) {
    const uint32_t started=millis();
    do {
        if (meshink_io_flash_try_begin()) return true;
        vTaskDelay(1);
    } while ((uint32_t)(millis()-started)<timeout_ms);
    Serial.println("[MeshInk/IO] ERROR: physical flash lease timed out");
    return false;
}
void meshink_io_flash_end() {
    const TaskHandle_t self=xTaskGetCurrentTaskHandle();
    portENTER_CRITICAL(&state_mux);
    if (flash_owner==self && flash_depth)
        if (--flash_depth==0) flash_owner=nullptr;
    portEXIT_CRITICAL(&state_mux);
}
void meshink_io_defer_meshtastic_save(int segments) {
    bool first=false;
    portENTER_CRITICAL(&state_mux);
    first=deferred_meshtastic_segments==0;
    deferred_meshtastic_segments|=(uint32_t)segments;
    portEXIT_CRITICAL(&state_mux);
    if(first) Serial.println("[MeshInk/IO] Meshtastic config staged in RAM until EPD idle");
}
int meshink_io_take_deferred_meshtastic_save() {
    uint32_t mask=0;
    portENTER_CRITICAL(&state_mux);
    if (!display_active && !display_requests && !flash_owner) {
        mask=deferred_meshtastic_segments;
        deferred_meshtastic_segments=0;
    }
    portEXIT_CRITICAL(&state_mux);
    return (int)mask;
}
