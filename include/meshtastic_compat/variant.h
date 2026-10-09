#pragma once
// MeshInk integration variant for the upstream Meshtastic core.
//
// This file deliberately contains NO GPIOs, SPI ports, TCXO settings, GPS UART
// pins or device-specific screen options. Those are defined only by MeshInk's
// hardware/{radio,gps,rtc,power,board} interfaces and per-board implementations.
//
// This is included by the pinned upstream Meshtastic configuration.h; it is
// not a Seeed/XIAO/T5 board variant or a second application entry point.
#define HAS_SCREEN 0
#define HAS_BUTTON 0
#define HAS_SENSOR 0
#define HAS_GPS 0
#define HAS_BLUETOOTH 0
#define HAS_WIFI 0
#define HAS_RADIO 1
// MeshInk supplies hardware power telemetry; keep native network telemetry parsing.
#define HAS_TELEMETRY 0
#define USE_SX1262 1

// Official Meshtastic's native SX1262Interface reads these names from
// variant.h. Bind them to the runtime board descriptor rather than copying
// H752/T5 GPIO constants into this protocol-level configuration.
#include "hardware/radio.h"
#define SX126X_CS (meshink_radio_native_module_config().chip_select)
#define SX126X_DIO1 (meshink_radio_native_module_config().dio1)
#define SX126X_RESET (meshink_radio_native_module_config().reset)
#define SX126X_BUSY (meshink_radio_native_module_config().busy)
#define SX126X_DIO3_TCXO_VOLTAGE (meshink_radio_native_module_config().tcxo_voltage)
