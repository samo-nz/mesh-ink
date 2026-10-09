#pragma once
#include <stddef.h>
#include <stdint.h>
#include "mesh_protocol_backend.h"
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"

// Native Meshtastic ToRadio/FromRadio -> MeshInk protocol-neutral UI.
// No board pins, SPI/LoRa drivers, power management, or MeshCore types here.
void meshink_meshtastic_ui_begin();
UiDataProvider* meshink_meshtastic_ui_provider();
void meshink_meshtastic_ui_receive(const meshtastic_FromRadio& response);
bool meshink_meshtastic_ui_destination(uint32_t& node, uint8_t& channel);
void meshink_meshtastic_ui_sent(uint32_t packet_id, const char* text, bool success);
const char* meshink_meshtastic_ui_radio_summary();
size_t meshink_meshtastic_ui_settings_count();
bool meshink_meshtastic_ui_settings_item(size_t index, MeshInkProtocolSettingItem& item);
