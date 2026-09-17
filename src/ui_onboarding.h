#pragma once
#include <stdint.h>
class UiDataProvider;

void ui_setup();
void ui_loop();
void ui_status_set_unread(uint16_t count);
void ui_status_set_channel_unread(uint16_t count);
void ui_status_set_gps(bool enabled, bool has_fix);
void ui_use_data_provider(UiDataProvider* provider);
