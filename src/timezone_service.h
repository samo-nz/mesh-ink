#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdio>
#include <cstring>

// One protocol-independent POSIX timezone store. MeshInk, MeshCore and
// Meshtastic use this same rule for local display; the physical RTC and
// mesh packet timestamps remain UTC. Meshtastic's native tzdef is an
// interoperable *view* of this rule, not a competing preference.
//
// Legacy "t5-ui" selection fields are retained for UI compatibility, and
// upgraded lazily when an explicit timezone is saved. No settings reset.
namespace meshink_timezone {

constexpr uint8_t kAuto = 0;
constexpr uint8_t kCustomOffset = 8;
constexpr uint8_t kImported = 9;
constexpr size_t kMaxRuleBytes = 64; // official Meshtastic device.tzdef[65]

static constexpr const char* kPresetRule[] = {
    nullptr,
    "NZST-12NZDT,M9.5.0,M4.1.0/3",
    "UTC0",
    "AEST-10AEDT,M10.1.0,M4.1.0/3",
    "GMT0BST,M3.5.0/1,M10.5.0",
    "CET-1CEST,M3.5.0,M10.5.0/3",
    "PST8PDT,M3.2.0,M11.1.0",
    "EST5EDT,M3.2.0,M11.1.0"
};
constexpr uint8_t kPresetCount = sizeof(kPresetRule)/sizeof(kPresetRule[0]);

inline bool valid_rule(const char* rule) {
    if (!rule || !rule[0]) return false;
    size_t n = 0;
    for (const unsigned char* c = (const unsigned char*)rule; *c; ++c) {
        if (*c < 33 || *c > 126 || ++n > kMaxRuleBytes) return false;
    }
    return true;
}

inline bool resolve_legacy(Preferences& prefs, char (&out)[65]) {
    const bool v2 = prefs.getBool("tz_v2", false);
    unsigned index = prefs.getUChar("timezone", 0);
    if (!v2) index = index < 7 ? index + 1 : kCustomOffset;
    if (index == kAuto || index == kImported) {
        const String value = prefs.getString(index == kAuto ? "auto_tz_rule" : "mt_tz_rule", "UTC0");
        if (!valid_rule(value.c_str())) return false;
        snprintf(out, sizeof(out), "%s", value.c_str());
        return true;
    }
    if (index < kPresetCount) {
        snprintf(out, sizeof(out), "%s", kPresetRule[index]);
        return true;
    }
    if (index != kCustomOffset) return false;
    const int east = constrain(prefs.getInt("custom_tz_min", 0), -12*60, 14*60);
    if (east == 0) snprintf(out, sizeof(out), "UTC0");
    else snprintf(out, sizeof(out), "UTC%c%d:%02d",
                  east > 0 ? '-' : '+', abs(east)/60, abs(east)%60);
    return true;
}

inline bool read(char (&out)[65]) {
    Preferences prefs;
    if (!prefs.begin("t5-ui", true)) return false;
    const String canonical = prefs.getString("tz_rule", "");
    bool result = false;
    if (valid_rule(canonical.c_str())) {
        snprintf(out, sizeof(out), "%s", canonical.c_str());
        result = true;
    } else result = resolve_legacy(prefs, out);
    prefs.end();
    return result;
}

inline bool save(const char* rule, uint8_t selection) {
    if (!valid_rule(rule) ||
        (selection >= kPresetCount && selection != kCustomOffset && selection != kImported))
        return false;
    Preferences prefs;
    if (!prefs.begin("t5-ui", false)) return false;
    // Write the canonical rule first; older readers still understand the
    // legacy selection keys, but no other protocol owns a second TZ store.
    const size_t len = strlen(rule);
    const bool ok_rule = prefs.putString("tz_rule", rule) == len;
    const bool ok_external = selection != kImported ||
        prefs.putString("mt_tz_rule", rule) == len;
    const bool ok_index = prefs.putUChar("timezone", selection) == 1;
    const bool ok_version = prefs.putBool("tz_v2", true) == 1;
    prefs.end();
    return ok_rule && ok_external && ok_index && ok_version;
}

inline uint8_t imported_index(const char* rule) {
    for (uint8_t i = 1; i < kPresetCount; ++i)
        if (!strcmp(rule, kPresetRule[i])) return i;
    return kImported;  // preserve arbitrary TZ and DST formulas verbatim
}

inline bool import_from_meshtastic(const char* rule) {
    if (!valid_rule(rule)) return false;
    char current[65]{};
    if (read(current) && !strcmp(current, rule)) return true;
    return save(rule, imported_index(rule));
}

} // namespace meshink_timezone
