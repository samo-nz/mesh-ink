#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ui_data.h"

void local_mesh_setup();
void local_mesh_runtime_begin();
void local_mesh_loop();
UiDataProvider* local_mesh_provider();
bool local_mesh_send_direct(size_t contact_index, const char* text);
bool local_mesh_send_channel(size_t channel_index, const char* text);
bool local_mesh_send_active(const char* text);
bool local_mesh_send_advert(bool flood);
void local_mesh_apply_name(const char* name);
void local_mesh_apply_gps(bool enabled);
bool local_mesh_gps_enabled();
bool local_mesh_gps_fix();
uint32_t local_mesh_gps_interval();
bool local_mesh_gps_advert_location();
void local_mesh_cycle_gps_interval();
void local_mesh_toggle_gps_advert_location();
const char* local_mesh_node_name();
const char* local_mesh_radio_summary();
const char* local_mesh_privacy_value(uint8_t item);
void local_mesh_toggle_privacy(uint8_t item);
void local_mesh_cycle_path_hash();
