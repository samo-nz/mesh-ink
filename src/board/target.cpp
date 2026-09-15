#include <Arduino.h>
#include <SPI.h>
#include "target.h"
#include <helpers/sensors/MicroNMEALocationProvider.h>

T5Board board;

// Board mapping only. The upstream wrapper controls radio parameters and
// transmit/receive/power state through MeshCore.
static SPIClass radio_spi(FSPI);
static CustomSX1262 radio = new Module(
    P_LORA_NSS, P_LORA_DIO_1, P_LORA_RESET, P_LORA_BUSY, radio_spi);
CustomSX1262Wrapper radio_driver(radio, board);

static ESP32RTCClock fallback_clock;
AutoDiscoverRTCClock rtc_clock(fallback_clock);
static MicroNMEALocationProvider gps(Serial1, &rtc_clock);
EnvironmentSensorManager sensors(gps);

void T5Board::begin() {
    ESP32Board::begin();
    pinMode(9, OUTPUT);
    digitalWrite(9, LOW);  // GT911 disabled in companion mode
    pinMode(11, OUTPUT);
    digitalWrite(11, LOW); // frontlight disabled
    Serial1.setPins(PIN_GPS_TX, PIN_GPS_RX);
    Serial1.begin(9600);
    Serial.printf("T5: companion boot, internal heap=%u\n", ESP.getFreeHeap());
}

bool radio_init() {
    fallback_clock.begin();
    rtc_clock.begin(Wire);
    return radio.std_init(&radio_spi);
}

mesh::LocalIdentity radio_new_identity() {
    RadioNoiseListener rng(radio);
    return mesh::LocalIdentity(&rng);
}
