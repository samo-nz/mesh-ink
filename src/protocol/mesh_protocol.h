#pragma once

#include <stddef.h>
#include <stdint.h>

#include "mesh_protocol_backend.h"
#include "../hardware/rtc_types.h"

const MeshInkProtocolDescriptor& mesh_protocol_descriptor();
const char* mesh_protocol_name();
const char* mesh_protocol_core_name();
const char* mesh_protocol_core_version();
bool mesh_protocol_has(uint32_t capability);

size_t mesh_protocol_available_count();
const MeshInkProtocolDescriptor* mesh_protocol_available(size_t index);
bool mesh_protocol_select_for_next_boot(uint8_t protocol_id);
bool mesh_protocol_restart_into(uint8_t protocol_id);

void mesh_protocol_setup();
bool mesh_protocol_setup_rx_wake();
bool mesh_protocol_setup_button_wake();
void mesh_protocol_loop();
void mesh_protocol_service_startup();
void mesh_protocol_rx_wake_loop();
bool mesh_protocol_rx_wake_promoted();
void mesh_protocol_prepare_interactive_services();
bool mesh_protocol_promote_to_ui(const char* source);
bool mesh_protocol_enter_deep_sleep_standby();
bool mesh_protocol_is_running();
void mesh_protocol_flush_now();
void mesh_protocol_prepare_shutdown();

void mesh_protocol_request_companion_mode();
bool mesh_protocol_consume_companion_request();
void mesh_protocol_companion_setup();
void mesh_protocol_companion_loop();
void mesh_protocol_companion_prepare_exit();

UiDataProvider* mesh_protocol_provider();
void mesh_protocol_refresh_ui_data();
bool mesh_protocol_send_active(const char* text);
bool mesh_protocol_send_advert(bool flood);

bool mesh_protocol_apply_radio(float freq, float bw, uint8_t sf, uint8_t cr, uint8_t path_hash_mode);
void mesh_protocol_apply_name(const char* name);
bool mesh_protocol_name_character_allowed(char c);
size_t mesh_protocol_node_name_max_length();
const char* mesh_protocol_radio_summary();
bool mesh_protocol_radio_matches(float frequency_mhz, float bandwidth_khz, uint8_t spreading_factor,
                                 uint8_t coding_rate, uint8_t path_hash_bytes);
void mesh_protocol_cycle_path_hash();
uint8_t mesh_protocol_path_hash_mode();

void mesh_protocol_apply_gps(bool enabled);
bool mesh_protocol_gps_enabled();
bool mesh_protocol_gps_fix();
uint32_t mesh_protocol_gps_interval();
bool mesh_protocol_gps_advert_location();
bool mesh_protocol_my_location(long& latitude, long& longitude);
void mesh_protocol_cycle_gps_interval();
MeshInkGpsConstellationMode mesh_protocol_gps_constellation_mode();
bool mesh_protocol_gps_set_constellation_mode(MeshInkGpsConstellationMode mode);
bool mesh_protocol_gps_deep_sleep_power_save();
bool mesh_protocol_gps_set_deep_sleep_power_save(bool enabled);
const char* mesh_protocol_gps_tuning_note();
void mesh_protocol_toggle_gps_advert_location();

uint32_t mesh_protocol_current_time();
bool mesh_protocol_time_valid();
bool mesh_protocol_set_manual_time(uint32_t utc);
bool mesh_protocol_set_time_mode(MeshInkTimeMode mode);
MeshInkTimeMode mesh_protocol_time_mode();
MeshInkTimeSource mesh_protocol_time_source();
bool mesh_protocol_gps_time_authoritative();

const char* mesh_protocol_privacy_value(uint8_t item);
void mesh_protocol_toggle_privacy(uint8_t item);

bool mesh_protocol_request_diagnostics();
bool mesh_protocol_diagnostics_busy();
const char* mesh_protocol_diagnostics_core();
const char* mesh_protocol_diagnostics_radio();
const char* mesh_protocol_diagnostics_packets();

uint16_t mesh_protocol_direct_unread_total();
uint16_t mesh_protocol_channel_unread_total();
