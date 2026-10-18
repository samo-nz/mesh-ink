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

# SPIFFS cannot replace an existing destination via rename as LittleFS can.
# Preserve the original preference as a sidecar during the already-verified
# SafeFile .tmp commit; recover interrupted saves before loading NodeDB.
# This operates ONLY on Meshtastic /prefs files on MeshInk's existing SPIFFS.
fs_atomic_path = upstream / "src" / "FSCommon.cpp"
fs_atomic = fs_atomic_path.read_text(encoding="utf-8")
old_rename = """    bool result = FSCom.rename(pathFrom, pathTo);
    spiLock->unlock();
    return result;"""
new_rename = """    bool result = FSCom.rename(pathFrom, pathTo);
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    if (!result && FSCom.exists(pathFrom) && FSCom.exists(pathTo)) {
        // SafeFile already verified the complete temporary file by readback.
        String backup(pathTo); backup += ".mbak";
        if (FSCom.exists(backup.c_str()) && !FSCom.remove(backup.c_str())) {
            LOG_ERROR("Cannot remove obsolete preference backup");
        } else if (FSCom.rename(pathTo, backup.c_str())) {
            if (FSCom.rename(pathFrom, pathTo)) {
                if (!FSCom.remove(backup.c_str()))
                    LOG_WARN("Old preference backup remains; recover on boot");
                result = true;
            } else if (!FSCom.rename(backup.c_str(), pathTo)) {
                LOG_ERROR("Preference rename/rollback failed; boot recovery needed");
            }
        }
    }
#endif
    spiLock->unlock();
    return result;"""
if new_rename not in fs_atomic:
    if fs_atomic.count(old_rename) != 1:
        raise RuntimeError("Official renameFile changed; inspect shared SPIFFS safety")
    fs_atomic = fs_atomic.replace(old_rename, new_rename, 1)

old_after_mount = """        // assert(0); This auto-formats the partition, so no need to fail here.
    }
#if defined(ARCH_ESP32)"""
new_after_mount = """        // assert(0); This auto-formats the partition, so no need to fail here.
    }
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    const char* const meshinkPrefs[] = {
        "/prefs/nodes.proto", "/prefs/device.proto", "/prefs/config.proto",
        "/prefs/module.proto", "/prefs/channels.proto", "/prefs/uiconfig.proto"
    };
    for (const char* primary : meshinkPrefs) {
        String backup(primary); backup += ".mbak";
        if (!FSCom.exists(backup.c_str())) continue;
        if (!FSCom.exists(primary)) {
            if (FSCom.rename(backup.c_str(), primary))
                LOG_WARN("Recovered interrupted preference save: %s", primary);
            else LOG_ERROR("Cannot restore preference backup: %s", primary);
        } else if (!FSCom.remove(backup.c_str())) {
            LOG_WARN("Could not clear obsolete preference backup: %s", primary);
        }
    }
#endif
#if defined(ARCH_ESP32)"""
if new_after_mount not in fs_atomic:
    if fs_atomic.count(old_after_mount) != 1:
        raise RuntimeError("Official fsInit mount changed; inspect backup recovery")
    fs_atomic = fs_atomic.replace(old_after_mount, new_after_mount, 1)
fs_atomic_path.write_text(fs_atomic, encoding="utf-8")
print("[MeshInk] Safe /prefs SPIFFS replace with rollback and startup recovery")

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
# The upstream PositionModule checks for an official physical I2C RTC,
# rtc_found, which MeshInk intentionally never probes or instantiates. Our
# bridge already seeds Meshtastic's logical clock from the tested MeshInk RTC.
# Preserve upstream RTC-quality-based packet policy without a second I2C scan.
rtc_expression = "rtc_found.address != ScanI2C::ADDRESS_NONE.address"
rtc_replacement = "getRTCQuality() >= RTCQualityDevice"
if position_data.count(rtc_expression) == 3:
    position_data = position_data.replace(rtc_expression, rtc_replacement)
elif position_data.count(rtc_expression) != 0 or position_data.count(rtc_replacement) != 3:
    raise RuntimeError("Official PositionModule RTC checks changed; review hardware ownership")
# The warnings-audit build runs after the production build in the same
# workspace. This rewrite must be idempotent when sources are already patched.
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

# test.33: build-only flash/display arbitration for the pinned upstream source.
# SafeFile has a single physical commit transaction; NodeDB config stays in
# RAM when an e-paper update is busy; TransmitHistory already has a dirty cache.
io_include = '#include "hardware/io_coordinator.h"\n'
node_path = upstream / "src" / "mesh" / "NodeDB.cpp"
node_data = node_path.read_text(encoding="utf-8")
node_old = '''bool NodeDB::saveToDisk(int saveWhat)
{
    LOG_DEBUG("Save to disk %d", saveWhat);
'''
node_new = '''bool NodeDB::saveToDisk(int saveWhat)
{
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    if (!meshink_io_flash_try_begin()) {
        meshink_io_defer_meshtastic_save(saveWhat);
        return true; // Accepted into native NodeDB RAM; disk commit is pending.
    }
    MeshInkFlashGuard meshink_flash_guard(true);
#endif
    LOG_DEBUG("Save to disk %d", saveWhat);
'''
if node_new not in node_data:
    if node_data.count(node_old) != 1:
        raise RuntimeError("NodeDB save signature changed, review arbitration")
    node_data = io_include + node_data.replace(node_old, node_new, 1)
    node_path.write_text(node_data, encoding="utf-8")

tx_path = upstream / "src" / "mesh" / "TransmitHistory.cpp"
tx_data = tx_path.read_text(encoding="utf-8")
tx_old = '''bool TransmitHistory::saveToDisk()
{
    if (!dirty) {
        return true;
    }
'''
tx_new = '''bool TransmitHistory::saveToDisk()
{
    if (!dirty) {
        return true;
    }
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    if (!meshink_io_flash_try_begin()) return false;
    MeshInkFlashGuard meshink_flash_guard(true);
#endif
'''
if tx_new not in tx_data:
    if tx_data.count(tx_old) != 1:
        raise RuntimeError("TransmitHistory save entry changed")
    tx_data = io_include + tx_data.replace(tx_old, tx_new, 1)
    tx_path.write_text(tx_data, encoding="utf-8")

# Other upstream writes (e.g. MessageStore) pass through SafeFile. Reserve
# the whole open/write/verify/rename lifetime rather than a single write().
safe_h = upstream / "src" / "SafeFile.h"
safe_h_data = safe_h.read_text(encoding="utf-8")
safe_h_old = '    explicit SafeFile(char const *filepath, bool fullAtomic = false);\n'
safe_h_new = '''    explicit SafeFile(char const *filepath, bool fullAtomic = false);
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    ~SafeFile();
#endif
'''
safe_m_old = '    bool fullAtomic;\n'
safe_m_new = '''    bool fullAtomic;
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    bool meshink_flash_owned = false;
#endif
'''
if safe_h_new not in safe_h_data:
    if safe_h_data.count(safe_h_old) != 1 or safe_h_data.count(safe_m_old) != 1:
        raise RuntimeError("SafeFile header changed")
    safe_h_data = safe_h_data.replace(safe_h_old, safe_h_new, 1)
    safe_h_data = safe_h_data.replace(safe_m_old, safe_m_new, 1)
    safe_h.write_text(safe_h_data, encoding="utf-8")

safe_cpp = upstream / "src" / "SafeFile.cpp"
safe_data = safe_cpp.read_text(encoding="utf-8")
safe_old = '''SafeFile::SafeFile(const char *_filename, bool fullAtomic)
    : filename(_filename), f(openFile(_filename, fullAtomic)), fullAtomic(fullAtomic)
{
}
'''
safe_new = '''SafeFile::SafeFile(const char *_filename, bool fullAtomic)
    : filename(_filename), f(), fullAtomic(fullAtomic)
{
#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
    meshink_flash_owned = meshink_io_flash_begin_wait(3000);
    if (!meshink_flash_owned) return;
#endif
    f = openFile(_filename, fullAtomic);
}

#if defined(MESHINK_MESHTASTIC_EXTERNAL_UI)
SafeFile::~SafeFile()
{
    if (meshink_flash_owned) {
        if (f) f.close();
        meshink_io_flash_end();
    }
}
#endif
'''
if safe_new not in safe_data:
    if safe_data.count(safe_old) != 1:
        raise RuntimeError("SafeFile constructor changed")
    safe_data = io_include + safe_data.replace(safe_old, safe_new, 1)
    safe_cpp.write_text(safe_data, encoding="utf-8")

# MeshInk serial monitor is a human text log, not an ANSI terminal. The
# official logger turns color on unconditionally for embedded targets. Keep
# upstream source pinned and change only its ephemeral build checkout, in BOTH
# logging paths; serial output content and severity remain unchanged.
redirectable_path = upstream / "src" / "RedirectablePrint.cpp"
redirectable = redirectable_path.read_text(encoding="utf-8")
uncolored_original = "#else\n    bool color = true;\n#endif"
uncolored_new = (
    "#elif defined(MESHINK_MESHTASTIC_PLAIN_LOGS)\n"
    "    bool color = false;\n"
    "#else\n"
    "    bool color = true;\n"
    "#endif"
)
if redirectable.count(uncolored_new) != 2:
    if redirectable.count(uncolored_original) != 2:
        raise RuntimeError("Official Meshtastic ANSI logger changed; review before patching")
    redirectable_path.write_text(
        redirectable.replace(uncolored_original, uncolored_new), encoding="utf-8"
    )
print("[MeshInk] Native Meshtastic serial logs use plain text without ANSI colors")

# Test.61: Multi-task serial interleaving fix. The upstream logger currently
# emits one line through several printf()/Print::write() calls (some one byte
# at a time), while MeshInk emits its own T5-/MT- records on the same HWCDC
# port. Use one bounded buffer and ONE dest->write(buffer,length) instead.
# Arduino HWCDC serializes each buffer write with tx_lock; it cannot protect
# a log record split into independent writes. This is build-only and must be
# idempotent for CI's warnings-visible second build.
atomic_path = upstream / "src" / "RedirectablePrint.cpp"
atomic_text = atomic_path.read_text(encoding="utf-8")
atomic_include = '#include "protocol/meshtastic_atomic_log.h"\n'
atomic_anchor = 'void RedirectablePrint::log_to_serial(const char *logLevel, const char *format, va_list arg)\n{\n'
atomic_bridge = """#if defined(MESHINK_MESHTASTIC_ATOMIC_LOGS) && MESHINK_MESHTASTIC_ATOMIC_LOGS
    { // isolate local names from upstream fallback code in this function
    char body[160]{};
    va_list copy;
    va_copy(copy, arg);
    const int wanted = vsnprintf(body, sizeof(body), format, copy);
    va_end(copy);
    if (wanted < 0) return;
    size_t body_len = (size_t)wanted < sizeof(body) ? (size_t)wanted : sizeof(body) - 1;
    if ((size_t)wanted >= sizeof(body)) body[sizeof(body)-2] = '\\n';
    for (size_t i = 0; i < body_len; ++i) {
        if (!std::isprint((unsigned char)body[i]) && body[i] != '\\n')
            body[i] = '#';
    }
    const uint32_t local_time = getValidTime(RTCQuality::RTCQualityDevice, true);
    auto thread = concurrency::OSThread::currentThread;
    char line[meshink_atomic_log::CAPACITY]{};
    const size_t bytes = meshink_atomic_log::compose(
        line, sizeof(line), logLevel, local_time, millis()/1000U,
        thread ? thread->ThreadName.c_str() : nullptr, body, body_len);
    const bool serial_enabled = config.has_security ?
        config.security.serial_enabled : config.device.serial_enabled;
    if (bytes && (!config.has_lora || serial_enabled)) {
        // Exactly one underlying HWCDC write; no prefix/body split.
        dest->write((const uint8_t*)line, bytes);
    }
    return;
    } // end isolated atomic logger scope
#endif
"""
if atomic_bridge not in atomic_text:
    if atomic_text.count(atomic_anchor) != 1:
        raise RuntimeError("Official Meshtastic log-to-serial entry changed; abort atomic patch")
    atomic_text = atomic_text.replace(atomic_anchor, atomic_anchor + atomic_bridge, 1)
    if atomic_include not in atomic_text:
        atomic_text = atomic_include + atomic_text
    atomic_path.write_text(atomic_text, encoding="utf-8")
print("[MeshInk] Meshtastic serial records sent as one atomic HWCDC buffer write")
