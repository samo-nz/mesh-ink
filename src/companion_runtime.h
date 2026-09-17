#pragma once
#include <stddef.h>
#include <stdint.h>

void companion_setup();
void companion_loop();
void local_mesh_setup();
void local_mesh_loop();
bool local_mesh_is_running();
bool local_mesh_enqueue_command(const uint8_t* frame, size_t len);
