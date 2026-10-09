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
#define HAS_TELEMETRY 1
#define USE_SX1262 1
