#pragma once
#include <stddef.h>
#include <stdint.h>

void companion_setup();
void companion_loop();
void companion_prepare_exit();
void local_mesh_setup();
bool local_mesh_setup_rx_wake();
bool local_mesh_setup_button_wake();
void local_mesh_loop();
void local_mesh_service_startup();
void local_mesh_rx_wake_loop();
bool local_mesh_rx_wake_promoted();
bool local_mesh_enter_deep_sleep_standby();
bool local_mesh_is_running();
bool local_mesh_enqueue_command(const uint8_t* frame, size_t len);
void local_mesh_schedule_contacts_save();
void local_mesh_flush_contacts_save_if_due();
void local_mesh_flush_contacts_save_now();
