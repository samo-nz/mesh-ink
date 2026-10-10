"""Build-only official Meshtastic core selection.

Keep the pinned upstream repository and its Git commit unchanged. PlatformIO
otherwise auto-compiles *all* firmware .cpp files in lib/Meshtastic, including
Power.cpp, E-Ink, Bluetooth and its second application startup.

This PRE script creates only local, uncommitted library.json metadata in the
checked-out submodule before PlatformIO discovers libraries. The manifest,
maintained by MeshInk, selects actual official protocol source files.
"""
from pathlib import Path

Import("env")

project = Path(env.subst("$PROJECT_DIR"))
upstream = project / "lib" / "Meshtastic"
source = upstream / "src" / "mesh" / "PhoneAPI.h"
if not source.is_file():
    raise RuntimeError("Official Meshtastic source submodule not checked out")
template = project / "tools" / "official_meshtastic_library.json"
import json
definition = json.loads(template.read_text(encoding="utf-8"))
filters = definition["build"]["srcFilter"]
if "-<*>" not in filters or any("Power.cpp" in s for s in filters):
    raise RuntimeError("Meshtastic core selection incorrectly includes full power firmware")
library_meta = upstream / "library.json"
target = json.dumps(definition, indent=2) + "\n"
if not library_meta.is_file() or library_meta.read_text(encoding="utf-8") != target:
    library_meta.write_text(target, encoding="utf-8")
print(f"[MeshInk] Official Meshtastic networking library filter: {len(filters) - 1} patterns")

# Official Meshtastic's Power.h includes ESP-IDF 5 ADC headers unconditionally
# for ARCH_ESP32, even if the file is included only for power-status types.
# MeshInk owns the physical power driver; Power.cpp is absent from the source
# filter. Keep a tiny, fail-closed guard change in the temporary checkout,
# never in the committed upstream submodule or a fork.
power_header = upstream / "src" / "Power.h"
power_contents = power_header.read_text(encoding="utf-8")
original = "#ifdef ARCH_ESP32\n// #include <driver/adc.h>\n#include <esp_adc/adc_cali.h>"
replacement = "#if defined(ARCH_ESP32) && __has_include(<esp_adc/adc_cali.h>)\n// #include <driver/adc.h>\n#include <esp_adc/adc_cali.h>"
if replacement not in power_contents:
    if power_contents.count(original) != 1:
        raise RuntimeError("Upstream Power.h ADC include changed; review compatibility guard")
    power_header.write_text(power_contents.replace(original, replacement, 1), encoding="utf-8")
    print("[MeshInk] Applied build-only header guard for unavailable IDF5 ADC includes")


# Meshtastic's official TextMessageModule combines network message observers
# with display wake/buzzer rendering. Retain the upstream packet handler and
# observer notifications, but let MeshInk's own UI decide how to notify/wake.
# Apply this narrowly to the ephemeral submodule checkout, fail closed if the
# reviewed upstream source changes, and do not create a protocol fork.
text_module = upstream / "src" / "modules" / "TextMessageModule.cpp"
text_source = text_module.read_text(encoding="utf-8")
orig_headers = (
    '#include "PowerFSM.h"\n'
    '#include "buzz.h"\n'
    '#include "configuration.h"\n'
    '#include "graphics/Screen.h"\n'
    '#include "graphics/SharedUIDisplay.h"\n'
    '#include "graphics/draw/MessageRenderer.h"\n'
    '#include "main.h"'
)
new_headers = (
    '#include "configuration.h"\n'
    '#include "main.h"\n'
    '#if !defined(MESHINK_MESHTASTIC_EXTERNAL_UI)\n'
    '#include "PowerFSM.h"\n'
    '#include "buzz.h"\n'
    '#include "graphics/Screen.h"\n'
    '#include "graphics/SharedUIDisplay.h"\n'
    '#include "graphics/draw/MessageRenderer.h"\n'
    '#endif'
)
start_line = '    IF_SCREEN(\n'
end_line = '    // Notify any observers (e.g. external modules that care about packets)'
marker = '#endif // MESHINK_MESHTASTIC_EXTERNAL_UI\n'
if marker not in text_source:
    if text_source.count(orig_headers) != 1 or text_source.count(start_line) != 1 or text_source.count(end_line) != 1:
        raise RuntimeError("Upstream text notification block changed; review MeshInk integration")
    text_source = text_source.replace(orig_headers, new_headers, 1)
    text_source = text_source.replace(start_line,
                                      '#if !defined(MESHINK_MESHTASTIC_EXTERNAL_UI)\n' + start_line, 1)
    text_source = text_source.replace(end_line,
                                      marker + end_line, 1)
    text_module.write_text(text_source, encoding="utf-8")
    print("[MeshInk] Kept official text routing/observers, delegated display/buzzer wake to MeshInk")

# Both protocols own network state, but MeshInk exclusively mounts the
# physical flash. Mount Meshtastic preferences on that *same* SPIFFS instance
# without LittleFS auto-formatting any partition (and without a second FS).
fs_h = upstream / "src" / "FSCommon.h"
fs_content = fs_h.read_text(encoding="utf-8")
old_fs = (
    '#if defined(ARCH_ESP32)\n'
    '// ESP32 version\n'
    '#include "LittleFS.h"\n'
    '#define FSCom LittleFS\n'
    '#define FSBegin() FSCom.begin(true) // format on failure'
)
new_fs = (
    '#if defined(ARCH_ESP32)\n'
    '// ESP32 version: MeshInk shared FS, no partition autoformat\n'
    '#include "SPIFFS.h"\n'
    '#define FSCom SPIFFS\n'
    '#define FSBegin() FSCom.begin(false)'
)
if new_fs not in fs_content:
    if fs_content.count(old_fs) != 1:
        raise RuntimeError("Official FSCommon.h changed; review shared storage adapter")
    fs_h.write_text(fs_content.replace(old_fs, new_fs, 1), encoding="utf-8")
    print("[MeshInk] Meshtastic filesystem delegated to MeshInk SPIFFS without autoformat")

# MeshInk owns the serial port: skip optional serial configuration if the
# genuine Meshtastic SerialModule is excluded. Channel/LoRa admin is unchanged.
admin_path = upstream / "src" / "modules" / "AdminModule.cpp"
admin_source = admin_path.read_text(encoding="utf-8")
serial_old = "    case meshtastic_ModuleConfig_serial_tag:\n        LOG_INFO(\"Set module config: Serial\");"
serial_new = "    case meshtastic_ModuleConfig_serial_tag:\n#if MESHTASTIC_EXCLUDE_SERIAL\n        LOG_WARN(\"Serial peripheral is not hosted by MeshInk Meshtastic\");\n        return false;\n#else\n        LOG_INFO(\"Set module config: Serial\");"
serial_end = "        moduleConfig.serial = c.payload_variant.serial;\n        break;\n"
if serial_new not in admin_source:
    if admin_source.count(serial_old) != 1 or admin_source.count(serial_end) != 1:
        raise RuntimeError("Upstream serial admin source changed; review guarded setting")
    admin_source = admin_source.replace(serial_old, serial_new, 1)
    admin_source = admin_source.replace(serial_end, serial_end + "#endif\n", 1)
    admin_path.write_text(admin_source, encoding="utf-8")
    print("[MeshInk] Guarded optional serial admin setting")

# The stock Meshtastic factory formatter must never erase MeshInk's entire
# shared SPIFFS partition (UI journal and settings live alongside /prefs).
fs_cpp = upstream / "src" / "FSCommon.cpp"
fs_source = fs_cpp.read_text(encoding="utf-8")
unsafe_format = "    return FSCom.format();"
safe_format = ("#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)\n"
               "    LOG_WARN(\"Refusing partition-wide format of shared SPIFFS\");\n"
               "    return false;\n"
               "#else\n"
               "    return FSCom.format();\n"
               "#endif")
if safe_format not in fs_source:
    if fs_source.count(unsafe_format) != 1:
        raise RuntimeError("Upstream filesystem format path changed")
    fs_cpp.write_text(fs_source.replace(unsafe_format, safe_format, 1), encoding="utf-8")
    print("[MeshInk] Blocked partition-wide format of shared SPIFFS")

# Stock Meshtastic LOG_* macros always dereference its SerialConsole.
# MeshInk owns USB CDC and does not construct a competing PhoneAPI/console.
# Keep genuine RedirectablePrint with a MeshInk-owned Serial destination.
debug_path = upstream / "src" / "DebugConfiguration.h"
debug_source = debug_path.read_text(encoding="utf-8")
debug_original = "#define DEBUG_PORT (*console) // Serial debug port"
debug_meshink = (
    "#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)\n"
    "RedirectablePrint &meshink_meshtastic_debug_port();\n"
    "#define DEBUG_PORT meshink_meshtastic_debug_port()\n"
    "#else\n"
    "#define DEBUG_PORT (*console) // Serial debug port\n"
    "#endif"
)
if debug_meshink not in debug_source:
    if debug_source.count(debug_original) != 1:
        raise RuntimeError("Upstream debug sink declaration changed")
    debug_path.write_text(debug_source.replace(debug_original, debug_meshink, 1), encoding="utf-8")
    print("[MeshInk] Routed native Meshtastic diagnostics through existing MeshInk Serial")

# Meshtastic's built-in GPS receiver is explicitly disabled: one physical
# UART/receiver, powered and parsed only by MeshInk's tested board backend.
# Retain upstream PositionModule routing, privacy and rate-limiting independently
# of native receiver startup, using reviewed build-only source guard changes.
modules_file = upstream / "src" / "modules" / "Modules.cpp"
modules_data = modules_file.read_text(encoding="utf-8")
guard = "#if !MESHTASTIC_EXCLUDE_GPS || defined(MESHINK_MESHTASTIC_EXTERNAL_POSITION)"
for before in (
    '#if !MESHTASTIC_EXCLUDE_GPS\n#include "modules/PositionModule.h"',
    '#if !MESHTASTIC_EXCLUDE_GPS\n    positionModule = new PositionModule();',
):
    after = before.replace("#if !MESHTASTIC_EXCLUDE_GPS", guard, 1)
    if after not in modules_data:
        if modules_data.count(before) != 1:
            raise RuntimeError("Meshtastic position registration changed upstream")
        modules_data = modules_data.replace(before, after, 1)
# No official status LED task may manipulate MeshInk-controlled lights/pins.
# In headless mode it is not needed for packet processing.
led_before = "    statusLEDModule = new StatusLEDModule();"
led_after = ('#if !defined(MESHINK_MESHTASTIC_EXTERNAL_UI)\n'
             + led_before + '\n'
             '#endif')
if led_after not in modules_data:
    if modules_data.count(led_before) != 1:
        raise RuntimeError("Official status LED startup moved; review hardware ownership")
    modules_data = modules_data.replace(led_before, led_after, 1)
modules_file.write_text(modules_data, encoding="utf-8")

position_file = upstream / "src" / "modules" / "PositionModule.cpp"
position_data = position_file.read_text(encoding="utf-8")
before = '#if !MESHTASTIC_EXCLUDE_GPS\n#include "PositionModule.h"'
after = before.replace("#if !MESHTASTIC_EXCLUDE_GPS", guard, 1)
if after not in position_data:
    if position_data.count(before) != 1:
        raise RuntimeError("Meshtastic PositionModule entry guard changed upstream")
    position_data = position_data.replace(before, after, 1)
# The stock module includes the hardware GPS receiver header unconditionally,
# despite only using it in the !EXCLUDE_GPS branch. Suppress that include in
# MeshInk's receiver-free networking build; leave the upstream tree untouched.
gps_include = '#include "GPS.h"'
gps_guarded = '#if !MESHTASTIC_EXCLUDE_GPS\n#include "GPS.h"\n#endif'
if gps_guarded not in position_data:
    if position_data.count(gps_include) != 1:
        raise RuntimeError("Meshtastic PositionModule GPS include changed upstream")
    position_data = position_data.replace(gps_include, gps_guarded, 1)
# MeshInk must remain the exclusive arbiter of standby/deep-sleep (including
# for Meshtastic TRACKER roles with power-saving enabled).
sleep_before = "        doDeepSleep(nightyNightMs, false, false);"
sleep_after = ('#if defined(MESHINK_MESHTASTIC_EXTERNAL_POSITION)\n'
               '        LOG_DEBUG("MeshInk controls standby; native tracker sleep suppressed");\n'
               '        (void)nightyNightMs;\n'
               '#else\n'
               + sleep_before + '\n'
               '#endif')
if sleep_after not in position_data:
    if position_data.count(sleep_before) != 1:
        raise RuntimeError("Meshtastic position sleep policy changed upstream")
    position_data = position_data.replace(sleep_before, sleep_after, 1)
position_file.write_text(position_data, encoding="utf-8")
print("[MeshInk] Official PositionModule enabled; physical GPS and sleep remain MeshInk-owned")
