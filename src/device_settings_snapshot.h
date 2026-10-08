#pragma once
#include <stddef.h>
#include <stdint.h>

// Portable, explicitly whitelisted device-wide preferences. No protocol
// identities, radio profiles, channel secrets, live GPS position or RTC time.
namespace meshink_device_settings {
constexpr uint32_t MAGIC=0x31565344U; // DSV1
constexpr uint16_t VERSION=1;
struct __attribute__((packed)) Snapshot {
    uint32_t magic=0;
    uint16_t version=0;
    uint16_t bytes=0;
    uint32_t present=0;
    uint8_t light_mode=0,light_timeout=0,light_level=0,standby_timeout=0;
    uint8_t deep_standby=0;
    uint16_t night_start=0,night_end=0;
    uint8_t timezone=0,tz_v2=0;
    int32_t custom_tz_min=0;
    char auto_tz_label[28]{},auto_tz_rule[80]{};
    uint8_t map_imperial=0;
    uint8_t gps_constellation=0,gps_mode_v2=0,gps_deep_sleep=0;
    uint8_t rtc_source=0,rtc_mode=0;
};
static_assert(sizeof(Snapshot)<512,"Device settings snapshot unexpectedly large");
bool valid(const Snapshot& value);
bool capture(Snapshot& value);
bool apply(const Snapshot& value);
} // namespace meshink_device_settings
