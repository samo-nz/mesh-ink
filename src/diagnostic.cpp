#include <Arduino.h>

// App-only boot probe. Use it at 0x10000 with the companion partition table
// already installed. A one-second heartbeat catches missed USB CDC startup.
void setup() {
    Serial.begin(115200);
    delay(750);
    Serial.printf("[T5-BOOT] setup, heap=%u, psram=%u\n",
                  ESP.getFreeHeap(), ESP.getPsramSize());
}

void loop() {
    Serial.printf("[T5-BOOT] alive, uptime=%lu ms, heap=%u\n",
                  static_cast<unsigned long>(millis()), ESP.getFreeHeap());
    delay(1000);
}
