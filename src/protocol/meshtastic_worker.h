#pragma once
#include <stddef.h>
#include <stdint.h>

// The Meshtastic worker exclusively runs the official router and PhoneAPI.
// The common MeshInk UI only exchanges serialized packets via RTOS queues.
bool meshink_meshtastic_worker_start();
bool meshink_meshtastic_worker_submit(const uint8_t* bytes,size_t size,uint32_t packet_id=0);
// After an accepted channel Admin command, replay the authoritative channel
// snapshot back into the MeshInk UI on the owning network task.
void meshink_meshtastic_worker_refresh_channels();
size_t meshink_meshtastic_worker_receive(uint8_t* bytes,size_t capacity);
bool meshink_meshtastic_worker_tx_failed(uint32_t& packet_id);
// Actual retry count changes observed in the pinned native ReliableRouter.
// The native owner posts these on core 0; the UI drains on core 1.
// Total denotes number of retries, not total transmissions.
bool meshink_meshtastic_worker_note_retry(uint32_t packet_id,uint8_t retry,uint8_t total);
bool meshink_meshtastic_worker_retry_status(uint32_t& packet_id,uint8_t& retry,uint8_t& total);
bool meshink_meshtastic_worker_stop();
