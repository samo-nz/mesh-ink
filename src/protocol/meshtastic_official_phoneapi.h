#pragma once
#include <stddef.h>
#include <stdint.h>

// Official Meshtastic PhoneAPI, in-process without BLE/serial.
// Caller must initialize upstream NodeDB and MeshService FIRST.
// This adapter is NOT enabled in flashable MeshInk until an official
// router, radio, storage and lifecycle are connected and tested.
extern "C" bool meshink_official_phoneapi_open(uint32_t config_nonce);
// Returns upstream "radio packet queued", not generic request validity.
extern "C" bool meshink_official_phoneapi_submit(const uint8_t* data, size_t len);
extern "C" bool meshink_official_phoneapi_has_data();
extern "C" size_t meshink_official_phoneapi_receive(uint8_t* out, size_t capacity);
extern "C" void meshink_official_phoneapi_close();
