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
// Channel editing is sent to the local official Meshtastic AdminModule.
// It cannot access pins, hardware settings, or MeshCore transports.
bool meshink_meshtastic_submit_channel(const meshtastic_Channel& channel);
uint32_t meshink_meshtastic_ui_own_node();

// Region/preset data is supplied by the upstream Meshtastic config handshake,
// not by MeshCore's frequency table or a board-specific UI.
size_t meshink_meshtastic_ui_region_count();
const char* meshink_meshtastic_ui_region_name(size_t index);
size_t meshink_meshtastic_ui_preset_count(size_t region);
const char* meshink_meshtastic_ui_preset_name(size_t region,size_t preset);
int meshink_meshtastic_ui_preset_index(size_t region,size_t preset);
bool meshink_meshtastic_ui_commit_radio(size_t region,size_t preset,uint8_t hops);
bool meshink_meshtastic_submit_lora_config(const meshtastic_Config_LoRaConfig& lora);
