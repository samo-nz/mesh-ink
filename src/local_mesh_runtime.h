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
bool local_mesh_save_tx_power(uint8_t dbm);
uint8_t local_mesh_tx_power();
void local_mesh_apply_name(const char* name);
void local_mesh_sync_gps_mode(MeshInkGpsConstellationMode mode);
uint32_t local_mesh_gps_interval();
bool local_mesh_gps_advert_location();
bool local_mesh_my_location(long& latitude, long& longitude);
void local_mesh_cycle_gps_interval();

void local_mesh_toggle_gps_advert_location();
const char* local_mesh_radio_summary();
bool local_mesh_radio_matches(float frequency_mhz,float bandwidth_khz,uint8_t spreading_factor,uint8_t coding_rate,uint8_t path_hash_bytes);
const char* local_mesh_privacy_value(uint8_t item);
void local_mesh_toggle_privacy(uint8_t item);
void local_mesh_cycle_path_hash();
uint8_t local_mesh_path_hash_mode();
void local_mesh_prepare_shutdown();

bool local_mesh_request_diagnostics();
bool local_mesh_diagnostics_busy();
const char* local_mesh_diagnostics_core();
const char* local_mesh_diagnostics_radio();
const char* local_mesh_diagnostics_packets();
