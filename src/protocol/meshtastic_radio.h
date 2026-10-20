#pragma once
#include <memory>

class RadioInterface;
class Router;

// Native radio factory: MeshInk supplies the electrical hardware and SPI,
// official Meshtastic supplies SX1262Interface, ISR, RX/TX, CAD and air-time.
// The returned radio is protocol-owned, never shared with MeshCore.
std::unique_ptr<RadioInterface> meshink_meshtastic_create_radio();
// Diagnostics for temporary first-hardware-test serial logging.
void meshink_meshtastic_radio_report();
// Network-worker-only tracking of a specific quick-settings packet through the
// official SX1262 TX-completion/release callback. No heuristic UI timer can
// label a merely queued packet as transmitted.
bool meshink_meshtastic_radio_quick_begin(uint8_t kind); // 1=position, 2=NodeInfo
void meshink_meshtastic_radio_quick_finish(bool native_accepted);
bool meshink_meshtastic_radio_quick_result(uint8_t& kind,bool& transmitted);

// Only call after official NodeDB and radio concurrency services initialize.
// On success Router owns the SX1262 for the rest of the Meshtastic boot.
bool meshink_meshtastic_attach_radio(Router& native_router);
