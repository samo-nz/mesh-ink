#!/usr/bin/env python3
"""Prepare an isolated official Meshtastic v2.8.1 H752-01 headless build.

Only writes new board files into a temporary upstream source checkout.
NEVER flash its standalone 16MB partition image over MeshInk.
"""
from pathlib import Path
import argparse

UPSTREAM_TAG = "v2.8.1.8e6a88d"
ENV = "meshink-h752-headless"

PLATFORMIO = """[env:meshink-h752-headless]
extends = esp32s3_base
board = t5-epaper-s3
board_level = testing
custom_meshtastic_hw_model = 123
custom_meshtastic_hw_model_slug = T5_S3_EPAPER_PRO
custom_meshtastic_architecture = esp32-s3
custom_meshtastic_display_name = MeshInk H752-01 headless experimental
custom_meshtastic_partition_scheme = 16MB
board_build.partitions = default_16MB.csv
upload_protocol = esptool
build_flags =
    ${esp32s3_base.build_flags}
    -I variants/esp32s3/meshink_h752_headless
    -DMESHINK_OFFICIAL_HEADLESS=1
    -DMESHTASTIC_EXCLUDE_SCREEN=1
    -DMESHTASTIC_EXCLUDE_INPUTBROKER=1
    -DMESHTASTIC_EXCLUDE_BLUETOOTH=1
    -DMESHTASTIC_EXCLUDE_WIFI=1
    -DMESHTASTIC_EXCLUDE_MQTT=1
    -DMESHTASTIC_EXCLUDE_WEBSERVER=1
    -DMESHTASTIC_EXCLUDE_AUDIO=1
    -DMESHTASTIC_EXCLUDE_CANNEDMESSAGES=1
    -DMESHTASTIC_EXCLUDE_PAXCOUNTER=1
    -DMESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR=1
    -DMESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR=1
    -DMESHTASTIC_EXCLUDE_DETECTIONSENSOR=1
    -DMESHTASTIC_EXCLUDE_EXTERNALNOTIFICATION=1
    -DMESHTASTIC_EXCLUDE_HEALTH_TELEMETRY=1
    -DMESHTASTIC_EXCLUDE_POWERSTRESS=1
    -DMESHTASTIC_EXCLUDE_REMOTEHARDWARE=1
    -DMESHTASTIC_EXCLUDE_RANGETEST=1
    -DBOARD_HAS_PSRAM
custom_sdkconfig =
    ${esp32s3_base.custom_sdkconfig}
    CONFIG_SPIRAM_SPEED_40M=y
    CONFIG_SPIRAM_SPEED=40
build_src_filter =
    ${esp32s3_base.build_src_filter}
    +<../variants/esp32s3/meshink_h752_headless>
lib_deps =
    ${esp32s3_base.lib_deps}
    https://github.com/meshtastic/PCF8xRTC/archive/efe1d9c92f79d8413f8ac4e9bdce0c48410600de.zip
    https://github.com/mverch67/BQ27220/archive/07d92be846abd8a0258a50c23198dac0858b22ed.zip
"""

PINS = """#ifndef Pins_Arduino_h
#define Pins_Arduino_h
#include <stdint.h>
#define USB_VID 0x303a
#define USB_PID 0x1001
static const uint8_t SDA = 39;
static const uint8_t SCL = 40;
static const uint8_t SS = 46;
static const uint8_t MOSI = 13;
static const uint8_t MISO = 21;
static const uint8_t SCK = 14;
#define SPI_MOSI 13
#define SPI_SCK 14
#define SPI_MISO 21
#define SPI_CS 12
#endif // Pins_Arduino_h
"""

BOARD = """#pragma once
#include "pins_arduino.h"
#define I2C_SDA SDA
#define I2C_SCL SCL
#define PCF8563_RTC 0x51
#define HAS_RTC 1
#define PCF8563_INT 2
#define BUTTON_PIN 0
#define GPS_RX_PIN 44
#define GPS_TX_PIN 43
#define HAS_PPM 1
#define XPOWERS_CHIP_BQ25896
#define HAS_BQ27220 1
#define BQ27220_I2C_SDA SDA
#define BQ27220_I2C_SCL SCL
#define BQ27220_DESIGN_CAPACITY 1500
#define USE_SX1262
#define LORA_SCK SCK
#define LORA_MISO MISO
#define LORA_MOSI MOSI
#define LORA_CS 46
#define LORA_RESET 1
#define LORA_DIO1 10
#define LORA_DIO2 47
#define SX126X_CS LORA_CS
#define SX126X_DIO1 LORA_DIO1
#define SX126X_BUSY LORA_DIO2
#define SX126X_RESET LORA_RESET
#define SX126X_DIO2_AS_RF_SWITCH
#define SX126X_DIO3_TCXO_VOLTAGE 2.4
"""

EARLY = """#include "variant.h"
#include "Arduino.h"
// Upstream T5 InkHUD and touch startup are intentionally excluded.
void earlyInitVariant() {
    pinMode(LORA_CS, OUTPUT);
    digitalWrite(LORA_CS, HIGH);
    pinMode(SPI_CS, OUTPUT);
    digitalWrite(SPI_CS, HIGH);
}
void lateInitVariant() {}
"""

CLIENT_HEADER = """#pragma once
#include <stddef.h>
#include <stdint.h>
// In-process variant of the same official ToRadio/FromRadio client protocol
// used by Meshtastic phone apps. No BLE, UART or extra ESP32 is required.
// Call begin ONLY after the official MeshService and NodeDB have initialized.
extern "C" bool meshink_phoneapi_begin(uint32_t config_nonce);
extern "C" bool meshink_phoneapi_send(const uint8_t *data, size_t length);
extern "C" size_t meshink_phoneapi_receive(uint8_t *buffer, size_t capacity);
extern "C" bool meshink_phoneapi_pending();
"""

CLIENT_CPP = """#include "meshink_phoneapi.h"
#include "mesh/PhoneAPI.h"
#include <pb_encode.h>
#include <new>

// Use official API state machine and packet types rather than reimplementing
// NodeDB synchronization, config handling, RF queues and message events.
class MeshInkLocalPhoneAPI final : public PhoneAPI {
  protected:
    bool checkIsConnected() override { return true; }
  public:
    MeshInkLocalPhoneAPI() { api_type=TYPE_PACKET; }
};

static MeshInkLocalPhoneAPI* local_api=nullptr;

extern "C" bool meshink_phoneapi_begin(uint32_t nonce) {
    if(!local_api){
        local_api=new(std::nothrow) MeshInkLocalPhoneAPI();
        if(!local_api)return false;
    }
    meshtastic_ToRadio request=meshtastic_ToRadio_init_zero;
    request.which_payload_variant=meshtastic_ToRadio_want_config_id_tag;
    request.want_config_id=nonce;
    uint8_t encoded[meshtastic_ToRadio_size];
    pb_ostream_t stream=pb_ostream_from_buffer(encoded,sizeof(encoded));
    if(!pb_encode(&stream,meshtastic_ToRadio_fields,&request))return false;
    // PhoneAPI::handleToRadio returns "packet queued", not "request handled".
    // A successful configuration handshake normally queues no RF packet.
    local_api->handleToRadio(encoded,stream.bytes_written);
    return local_api->isConnected();
}
extern "C" bool meshink_phoneapi_send(const uint8_t* data,size_t length) {
    return local_api&&data&&length&&length<=MAX_TO_FROM_RADIO_SIZE&&
           local_api->handleToRadio(data,length);
}
extern "C" size_t meshink_phoneapi_receive(uint8_t* buffer,size_t capacity) {
    if(!local_api||!buffer||capacity<MAX_TO_FROM_RADIO_SIZE||!local_api->available())
        return 0;
    return local_api->getFromRadio(buffer);
}
extern "C" bool meshink_phoneapi_pending() {
    return local_api&&local_api->available();
}
"""

# In upstream v2.8.1, MAX17048Sensor inherits both TelemetrySensor and
# VoltageSensor for power telemetry. Both base-class headers accidentally
# exclude their declarations when environmental sensors are disabled.
# Restore just the missing power-telemetry branch in the temporary checkout;
# do not drop the H752's battery telemetry to work around those declarations.
UPSTREAM_GUARD_PATCHES = (
    (
        "src/modules/Telemetry/Sensor/TelemetrySensor.h",
        "#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR",
        "#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_AIR_QUALITY_SENSOR "
        "|| !MESHTASTIC_EXCLUDE_POWER_TELEMETRY",
    ),
    (
        "src/modules/Telemetry/Sensor/VoltageSensor.h",
        "#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR",
        "#if !MESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR || !MESHTASTIC_EXCLUDE_POWER_TELEMETRY",
    ),
)

FILES = {
    "platformio.ini": PLATFORMIO,
    "pins_arduino.h": PINS,
    "variant.h": BOARD,
    "variant.cpp": EARLY,
    "meshink_phoneapi.h": CLIENT_HEADER,
    "meshink_phoneapi.cpp": CLIENT_CPP,
}

def prepare(root: Path):
    if not (root / "src/mesh/PhoneAPI.cpp").is_file():
        raise ValueError("Not an official Meshtastic firmware source checkout")
    if not (root / "variants/esp32s3/t5s3_epaper/variant.h").is_file():
        raise ValueError("Missing official H752-01 board source")
    target = root / "variants/esp32s3/meshink_h752_headless"
    target.mkdir(parents=True, exist_ok=True)
    for name, body in FILES.items():
        path = target / name
        if path.exists() and path.read_text() != body:
            raise ValueError("Refusing to overwrite modified upstream checkout: " + str(path))
        path.write_text(body)

    for relative, old_guard, new_guard in UPSTREAM_GUARD_PATCHES:
        sensor_header = root / relative
        source = sensor_header.read_text()
        if new_guard in source:
            continue  # Already patched in this temporary checkout.
        if source.count(old_guard) != 1:
            raise ValueError(f"Unexpected upstream guard in {relative}; review before patching")
        sensor_header.write_text(source.replace(old_guard, new_guard, 1))
        print(f"Patched upstream {relative} declaration guard for power telemetry")
    print(f"Created {ENV} based on {UPSTREAM_TAG}: build only, never flash over MeshInk.")

if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("upstream_checkout", type=Path)
    args = parser.parse_args()
    prepare(args.upstream_checkout.resolve())
