#pragma once
#include <stdint.h>
#include <stddef.h>
#include "hardware/radio_types.h"
class UiDataProvider;

void ui_setup();
void ui_prepare_headless_rx_wake();
void ui_show_storage_initializing(); // update boot splash before formatting
void ui_finish_startup(); // reveal interactive UI after storage/mesh initialization
void ui_loop();
void ui_status_set_unread(uint16_t count);
void ui_status_set_channel_unread(uint16_t count);
void ui_status_set_gps(bool enabled, bool has_fix, int satellites, long latitude, long longitude, uint32_t timestamp);
void ui_notify_message_received(bool channel);
bool ui_restore_failed_compose(const char* text);
void ui_notify_advert_result(bool flood, bool ok);
void ui_notify_node_position_unavailable();
void ui_request_data_refresh(const char* reason);
void ui_apply_initial_radio_preset(); // sync first-time setup radio before showing UI
void ui_mesh_ready();
void ui_use_data_provider(UiDataProvider* provider);
bool ui_is_standby();
bool ui_chat_is_visible(bool channel); // true only for the currently displayed chat type
void ui_show_radio_failure(MeshInkRadioFailureClass failure);
bool ui_save_screenshot(char* path_out,size_t path_len);
