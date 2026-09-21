#pragma once
#include <stdint.h>
class UiDataProvider;

void ui_setup();
void ui_loop();
void ui_status_set_unread(uint16_t count);
void ui_status_set_channel_unread(uint16_t count);
void ui_status_set_gps(bool enabled, bool has_fix, int satellites, long latitude, long longitude, uint32_t timestamp);
void ui_notify_message_received(bool channel);
void ui_notify_advert_result(bool flood, bool ok);
void ui_request_data_refresh(const char* reason);
void ui_mesh_ready();
void ui_use_data_provider(UiDataProvider* provider);
bool ui_is_standby();
void ui_show_radio_failure();
