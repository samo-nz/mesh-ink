#pragma once
#include <stddef.h>
#include <stdint.h>

void companion_setup();
void companion_loop();
void companion_prepare_exit();
void local_mesh_setup();
void local_mesh_loop();
bool local_mesh_is_running();
bool local_mesh_enqueue_command(const uint8_t* frame, size_t len);
void local_mesh_schedule_contacts_save();
void local_mesh_flush_contacts_save_if_due();
void local_mesh_flush_contacts_save_now();
