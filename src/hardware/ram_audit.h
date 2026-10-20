#pragma once
// RAM accounting for diagnostic builds only. No allocations or heap tracing.
#include <Arduino.h>
#include <esp_heap_caps.h>
#ifndef MESHINK_RAM_AUDIT
#define MESHINK_RAM_AUDIT 0
#endif
#if MESHINK_RAM_AUDIT
inline void meshink_ram_snapshot(const char* stage) {
    const uint32_t internal = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    const uint32_t external = MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT;
    multi_heap_info_t i{}, p{};
    heap_caps_get_info(&i, internal);
    heap_caps_get_info(&p, external);
    Serial.printf("[RAM-AUDIT] %s internal total=%u free=%u used=%u min_free=%u largest=%u allocated_blocks=%u free_blocks=%u psram_total=%u psram_free=%u psram_largest=%u esp_heap=%u\n",
       stage, (unsigned)heap_caps_get_total_size(internal),
       (unsigned)i.total_free_bytes,
       (unsigned)(heap_caps_get_total_size(internal)-i.total_free_bytes),
       (unsigned)heap_caps_get_minimum_free_size(internal),
       (unsigned)i.largest_free_block, (unsigned)i.allocated_blocks,
       (unsigned)i.free_blocks, (unsigned)heap_caps_get_total_size(external),
       (unsigned)p.total_free_bytes, (unsigned)p.largest_free_block,
       (unsigned)ESP.getFreeHeap());
}
inline void meshink_ram_audit_periodic() {
    static uint32_t last=0;
    const uint32_t now=millis();
    if(now-last>=60000UL){last=now;meshink_ram_snapshot("periodic");}
}
#else
inline void meshink_ram_snapshot(const char*) {}
inline void meshink_ram_audit_periodic() {}
#endif
