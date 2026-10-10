#pragma once
#include <stddef.h>
#include <stdint.h>

// The Meshtastic worker exclusively runs the official router and PhoneAPI.
// The common MeshInk UI only exchanges serialized packets via RTOS queues.
bool meshink_meshtastic_worker_start();
bool meshink_meshtastic_worker_submit(const uint8_t* bytes,size_t size,uint32_t packet_id=0);
size_t meshink_meshtastic_worker_receive(uint8_t* bytes,size_t capacity);
bool meshink_meshtastic_worker_tx_failed(uint32_t& packet_id);
bool meshink_meshtastic_worker_stop();
