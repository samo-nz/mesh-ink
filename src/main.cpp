#include <Arduino.h>
#include <Mesh.h>
#include <esp_wifi.h>

// First build gate: compile the selected upstream MeshCore dependency on the
// T5 Pro's ESP32-S3 toolchain. Radio, BLE and display are added in separate
// device-owned modules after this gate succeeds.
void setup() {
    Serial.begin(115200);
    esp_wifi_stop();
    esp_wifi_deinit();
    Serial.println("T5 Pro upstream MeshCore build gate");
    Serial.println("No radio or display initialized in this gate");
}

void loop() {
    delay(1000);
}
