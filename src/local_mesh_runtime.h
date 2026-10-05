#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ui_data.h"
#include "hardware/gps_types.h"


void local_mesh_setup();
void local_mesh_runtime_begin();
void local_mesh_loop();
UiDataProvider* local_mesh_provider();
void local_mesh_refresh_ui_data();
void local_mesh_receive_channel_from_core(uint8_t channel, uint32_t timestamp, const char* text, bool has_rf, int8_t snr_q4, uint8_t path_len);
bool local_mesh_send_direct(size_t contact_index, const char* text);
bool local_mesh_send_channel(size_t channel_index, const char* text);
bool local_mesh_send_active(const char* text);
bool local_mesh_send_advert(bool flood);
bool local_mesh_apply_radio(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t path_hash_mode);
void local_mesh_apply_name(const char* name);
void local_mesh_apply_gps(bool enabled);
bool local_mesh_gps_enabled();
bool local_mesh_gps_fix();
uint32_t local_mesh_gps_interval();
bool local_mesh_gps_advert_location();
bool local_mesh_my_location(long& latitude, long& longitude);
void local_mesh_cycle_gps_interval();
MeshInkGpsConstellationMode local_mesh_gps_constellation_mode();
bool local_mesh_gps_set_constellation_mode(MeshInkGpsConstellationMode mode);
const char* local_mesh_gps_tuning_note();

void local_mesh_toggle_gps_advert_location();
uint32_t local_mesh_current_time();
bool local_mesh_time_valid();
const char* local_mesh_node_name();
const char* local_mesh_radio_summary();
const char* local_mesh_privacy_value(uint8_t item);
void local_mesh_toggle_privacy(uint8_t item);
void local_mesh_cycle_path_hash();
uint8_t local_mesh_path_hash_mode();
void local_mesh_prepare_shutdown();
uint16_t local_mesh_direct_unread_total();
uint16_t local_mesh_channel_unread_total();

bool local_mesh_request_diagnostics();
bool local_mesh_diagnostics_busy();
const char* local_mesh_diagnostics_core();
const char* local_mesh_diagnostics_radio();
const char* local_mesh_diagnostics_packets();
