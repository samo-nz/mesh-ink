"""Static architecture gates for hardware and MeshCore integration boundaries."""

from pathlib import Path
import re

ROOT=Path(__file__).resolve().parents[1]
SRC=ROOT/"src"

# Board/platform electrical APIs belong under src/board. Generic src/hardware
# selectors may choose a backend but must not implement the physical details.
APP_FILES=[
    p for p in SRC.rglob("*")
    if p.suffix in {".cpp",".h"}
    and "board" not in p.parts
    and "hardware" not in p.parts
    and "fonts" not in p.parts
]

BANNED={
    r"\bsetCpuFrequencyMhz\b": "direct ESP32 CPU clock setter",
    r"\bgetCpuFrequencyMhz\b": "direct ESP32 CPU clock getter",
    r"#include\s*<esp32-hal-cpu\.h>": "ESP32 CPU clock header",
    r"\bpinMode\s*\(": "direct GPIO pinMode",
    r"\bdigitalWrite\s*\(": "direct GPIO digitalWrite",
    r"\bdigitalRead\s*\(": "direct GPIO digitalRead",
    r"\bgpio_[A-Za-z0-9_]+\s*\(": "direct ESP-IDF GPIO call",
    r"\bi2c_[A-Za-z0-9_]+\s*\(": "direct ESP-IDF I2C call",
    r"\bGT911\b": "touch-controller implementation detail",
    r"\bT5_PIN_[A-Za-z0-9_]+\b": "T5 electrical pin macro",
    r"\bP_LORA_[A-Za-z0-9_]+\b": "LoRa electrical pin macro",
    r"#include\s*<SPI\.h>": "direct SPI implementation header",
    r"#include\s*<Wire\.h>": "direct I2C implementation header",
    r"#include\s*<SD\.h>": "direct SD implementation header",
}

errors=[]
for path in APP_FILES:
    text=path.read_text(encoding="utf-8")
    rel=path.relative_to(ROOT)
    for pattern,label in BANNED.items():
        match=re.search(pattern,text)
        if match:
            line=text.count("\n",0,match.start())+1
            errors.append(f"{rel}:{line}: {label} leaked outside src/board")

# Upstream companion example internals are deliberately confined to the
# composition root and one adapter include. Other MeshInk code consumes the
# adapter rather than reaching into lib/MeshCore/examples directly.
allowed_example_refs={
    Path("src/companion_runtime.cpp"),
    Path("src/meshcore_adapter.h"),
}
example_ref="lib/MeshCore/examples/companion_radio"
seen=set()
for path in SRC.rglob("*"):
    if path.suffix not in {".cpp",".h"}:
        continue
    rel=path.relative_to(ROOT)
    if example_ref in path.read_text(encoding="utf-8"):
        seen.add(rel)
        if rel not in allowed_example_refs:
            errors.append(f"{rel}: direct MeshCore companion-example dependency bypasses meshcore_adapter.h")

if seen!=allowed_example_refs:
    errors.append(
        "MeshCore adapter boundary changed unexpectedly: "
        f"expected {sorted(map(str,allowed_example_refs))}, got {sorted(map(str,seen))}"
    )

local=(SRC/"local_mesh_runtime.cpp").read_text(encoding="utf-8")
assert '#include "meshcore_adapter.h"' in local, "local runtime must enter MeshCore through meshcore_adapter.h"
assert "t5_mesh()" not in local, "legacy board-named MeshCore accessor must not return"

# Shared product/boot code must depend only on the protocol facade. Upstream
# protocol names, helpers and legacy local_mesh entry points stay behind it.
for rel in ("src/ui_onboarding.cpp","src/unified_main.cpp","src/ui_data.h"):
    text=(ROOT/rel).read_text(encoding="utf-8")
    if rel!="src/ui_data.h" and '"protocol/mesh_protocol.h"' not in text:
        errors.append(f"{rel}: shared code must include protocol/mesh_protocol.h")
    for token in ("MeshCore","MESHCORE","local_mesh_","meshcore_adapter.h","companion_runtime.h"):
        if token in text:
            errors.append(f"{rel}: protocol-specific token {token!r} leaked into shared code")

for rel in (
    "src/protocol/mesh_protocol.h",
    "src/protocol/mesh_protocol.cpp",
    "src/protocol/mesh_protocol_backend.h",
):
    text=(ROOT/rel).read_text(encoding="utf-8")
    for token in ("MeshCore","MESHCORE","local_mesh_","meshcore_adapter.h","companion_runtime.h"):
        if token in text:
            errors.append(f"{rel}: generic protocol layer contains backend-specific token {token!r}")

backend_contract=(SRC/"protocol/mesh_protocol_backend.h").read_text(encoding="utf-8")
for token in (
    "(*current_time)",
    "(*time_valid)",
    "(*set_manual_time)",
    "(*gps_fix)",
    "(*gps_constellation_mode)",
    "(*gps_deep_sleep_power_save)",
    "(*node_name)",
    "(*direct_unread_total)",
    "(*channel_unread_total)",
):
    if token in backend_contract:
        errors.append(
            "src/protocol/mesh_protocol_backend.h: shared MeshInk service leaked back "
            f"into backend contract via {token}"
        )

meshcore_helper=(SRC/"protocol/meshcore_protocol.cpp").read_text(encoding="utf-8")
if "meshink_protocol_backend_slot_1" not in meshcore_helper:
    errors.append("src/protocol/meshcore_protocol.cpp: MeshCore helper is not registered in backend slot 1")


# Native SX126x module settings belong exclusively to the board abstraction.
native_config = (SRC/"hardware/radio_types.h").read_text(encoding="utf-8")
native_backend = (SRC/"board/t5_radio_backend.cpp").read_text(encoding="utf-8")
native_public = (SRC/"board/t5_radio_backend.h").read_text(encoding="utf-8")
if "struct MeshInkSX126xModuleConfig" not in native_config:
    errors.append("hardware/radio_types.h: generic native SX126x board contract missing")
if "meshink_radio_native_module_config()" not in native_public or "meshink_radio_native_spi_bus()" not in native_public:
    errors.append("board radio backend: protocol-native SPI/module descriptor missing")
if "P_LORA_NSS,P_LORA_DIO_1,P_LORA_RESET,P_LORA_BUSY" not in native_backend:
    errors.append("board radio backend: H752 native radio pins must be board-owned")
if any(token in native_config for token in ("P_LORA_", "T5_", "t5_")):
    errors.append("hardware/radio_types.h: board-specific macros leaked into generic module contract")

meshtastic_helper=(SRC/"protocol/meshtastic_protocol.cpp").read_text(encoding="utf-8")
if "if(ready)ui_mesh_ready()" not in meshtastic_helper:
    errors.append("Meshtastic startup must notify MeshInk that the clock/UI is ready")
if "phone_rx=psram_scratch<PhoneRxScratch>();" not in meshtastic_helper:
    errors.append("Native PhoneAPI RX buffers must be PSRAM-backed to protect loop stack")
for token in ("psram_scratch<PhoneTxScratch>()","phone_rx->wire","scratch->wire"):
    if token not in meshtastic_helper:
        errors.append(f"Native PhoneAPI TX/RX stack-safety hook missing: {token}")
ui_meshtastic=(SRC/"protocol/meshtastic_ui_data.cpp").read_text(encoding="utf-8")
if '"MESHTASTIC"};' in ui_meshtastic:
    errors.append("Meshtastic message badge must not mask native status (SENT/FAILED/DELIVERED)")
if "meshink_protocol_backend_slot_2" not in meshtastic_helper:
    errors.append("src/protocol/meshtastic_protocol.cpp: Meshtastic helper is not registered in backend slot 2")
if "libmeshtastic_leaf" in meshtastic_helper:
    errors.append("src/protocol/meshtastic_protocol.cpp: removed Leaf code remains")
for token in ("P_LORA_", "CustomSX1262", "T5RadioHal", "radio_spi"):
    if token in meshtastic_helper:
        errors.append(f"src/protocol/meshtastic_protocol.cpp: board-specific {token!r} bypasses hardware/radio.h")

for rel in (
    "src/ui_onboarding.cpp",
    "src/message_store.cpp",
    "src/companion_runtime.cpp",
    "src/unified_main.cpp",
):
    text=(ROOT/rel).read_text(encoding="utf-8")
    if "hardware/performance.h" not in text:
        errors.append(f"{rel}: CPU policy must use generic hardware/performance.h")

# Incompatible legacy Meshtastic preferences may reset the native LoRa
# region without resetting MeshInk's first-run flag. Reconfiguration must
# remain available directly in the shared protocol-settings UI.
ui_source=(SRC/"ui_onboarding.cpp").read_text(encoding="utf-8")
for token in ("ProtocolSettingsRowKind::RadioSetup", 'title="CONFIGURE LORA"',
              "setup_enter(Screen::SetupRegion);"):
    if token not in ui_source:
        errors.append(f"Native region reconfiguration path missing: {token}")

platformio=(ROOT/"platformio.ini").read_text(encoding="utf-8")
warning_env=platformio[platformio.index("[env:t5-unified-cache64-warnings]"):platformio.index("; Generic portability",platformio.index("[env:t5-unified-cache64-warnings]"))]
if "build_src_flags =" not in warning_env or "-Wall" not in warning_env or "-Wextra" not in warning_env:
    errors.append("platformio.ini: MeshInk-source warnings-visible RC audit environment is missing")
if "build_unflags =" in warning_env:
    errors.append("platformio.ini: warning audit must not alter dependency/framework production flags")
unified_env=platformio[platformio.index("[env:t5-unified]"):platformio.index("[env:t5-unified-cache64]")]
if "-DMESHINK_DEVICE_SERVICES_ENABLED=1" not in unified_env:
    errors.append("platformio.ini: full firmware must enable MeshInk-owned GPS/RTC device services")
# Enforce hardware-ownership boundaries in every future Meshtastic build.
# The official PositionModule is a network feature; a second GNSS receiver,
# GPS UART, sleep engine, screen, or physical radio pin mapping is forbidden.
required_headless_flags=(
    "-DMESHTASTIC_EXCLUDE_GPS=1",
    "-DMESHINK_MESHTASTIC_EXTERNAL_POSITION=1",
    "-DMESHTASTIC_EXCLUDE_POWER_FSM=1",
    "-DMESHTASTIC_EXCLUDE_BLUETOOTH=1",
    "-DMESHTASTIC_EXCLUDE_WIFI=1",
    "-DMESHTASTIC_EXCLUDE_I2C=1",
    "-DMESHTASTIC_EXCLUDE_INPUTBROKER=1",
)
for flag in required_headless_flags:
    if flag not in unified_env:
        errors.append(f"Meshtastic native subsystem boundary missing {flag}")
native_library=(ROOT/"tools/official_meshtastic_library.json").read_text(encoding="utf-8")
for prohibited in ('+<gps/GPS.cpp>', '+<main.cpp>', '+<Power.cpp>', '+<graphics/', '+<mesh/Bluetooth', '+<mesh/wifi/'):
    if prohibited in native_library:
        errors.append(f"Official source filter must not compile hardware/application source {prohibited}")
for required in ('+<modules/PositionModule.cpp>', '+<gps/GeoCoord.cpp>'):
    if required not in native_library:
        errors.append(f"Official position networking dependency missing {required}")
shared_position_patch=(ROOT/"tools/prepare_meshtastic_library.py").read_text(encoding="utf-8")
for phrase in ("Safe /prefs SPIFFS replace", "Recovered interrupted preference save",
               "Preference rename/rollback failed", 'backup += ".mbak"'):
    if phrase not in shared_position_patch:
        errors.append(f"Meshtastic SPIFFS safe preference save or recovery missing: {phrase}")


for token in ("MESHINK_MESHTASTIC_EXTERNAL_POSITION", "native tracker sleep suppressed", "positionModule = new PositionModule"):
    if token not in shared_position_patch:
        errors.append(f"Build-only position adapter missing {token}")
# MeshInk owns the physical GPS/RTC services. The native position bridge
# runs on the lower-priority Meshtastic worker, not on the UI loop.
for token in ("meshink_gps_service_loop()", "meshink_gps_background_tick()",
              "meshink_rtc_tick()"):
    if token not in meshtastic_helper:
        errors.append(f"Meshtastic mode bypasses existing MeshInk GPS/RTC service: {token}")
native_worker=(SRC/"protocol/meshtastic_worker.cpp").read_text(encoding="utf-8")
for token in ("xTaskCreatePinnedToCore(network_task", "meshink_meshtastic_native_loop()",
              "meshink_meshtastic_native_gps_update()",
              "meshink_official_phoneapi_submit(", "meshink_official_phoneapi_receive("):
    if token not in native_worker:
        errors.append(f"Meshtastic worker missing engine/PhoneAPI operation: {token}")
for token in ("meshink_meshtastic_native_loop()", "meshink_meshtastic_native_gps_update()",
              "meshink_official_phoneapi_submit("):
    if token in meshtastic_helper:
        errors.append(f"Meshtastic UI thread directly runs native engine operation: {token}")
if "+<protocol/meshtastic_worker.cpp>" not in platformio:
    errors.append("Meshtastic worker missing from firmware source filter")
cmake=(SRC/"CMakeLists.txt").read_text(encoding="utf-8")
if \'"protocol/meshtastic_worker.cpp"\' not in cmake:
    errors.append("Meshtastic worker missing from the ESP-IDF/CMake production source list")
native_runtime=(SRC/"protocol/meshtastic_runtime.cpp").read_text(encoding="utf-8")
if "nodeDB->updatePosition(nodeDB->getNodeNum(),p,RX_SRC_LOCAL)" not in native_runtime:
    errors.append("Shared GPS bridge must submit to upstream NodeDB, not a second GPS engine")
if "if(had_fix && !config.position.fixed_position)" not in native_runtime or "nodeDB->clearLocalPosition();" not in native_runtime:
    errors.append("Shared GPS bridge must clear stale live GNSS position after fix loss/off, preserving fixed-position mode")

# Meshtastic wire coordinates use 1e7 while the established MeshInk map and
# node-detail UI contracts use 1e6. Protect against a tenfold map displacement.
native_ui = (SRC/"protocol/meshtastic_ui_data.cpp").read_text(encoding="utf-8")
for token in ("out.latitude=n.latitude/10;out.longitude=n.longitude/10;",
              "details_.latitude=n->latitude/10;details_.longitude=n->longitude/10;"):
    if token not in native_ui:
        errors.append("Meshtastic position UI coordinates must convert native 1e7 to MeshInk 1e6: " + token)


if "-DMESHINK_PROTOCOL_SLOT_2_ENABLED=1" not in unified_env:
    errors.append("platformio.ini: full firmware must register the Meshtastic backend")
meshtastic_crypto_pin="Crypto=https://github.com/meshtastic/Crypto/archive/591ff9a690e8168ccb7a36abde8d7783e448d395.zip"
if "libmeshtastic-leaf.git" in platformio:
    errors.append("Leaf dependency must not be linked")
if meshtastic_crypto_pin not in platformio:
    errors.append("platformio.ini: pinned Meshtastic Crypto provider is missing")
if "rweather/Crypto @ ^0.4.0" not in platformio:
    errors.append("platformio.ini: MeshCore Crypto recovery provider is missing")
selector_match=re.search(
    r"\[meshink-crypto\]\s+lib_deps\s*=\s*\n\s*\$\{meshink-crypto-meshtastic\.lib_deps\}",
    platformio,
)
if not selector_match:
    errors.append("platformio.ini: active Crypto selector must explicitly choose the Meshtastic provider")
if "${meshink-crypto.lib_deps}" not in unified_env:
    errors.append("platformio.ini: unified firmware must consume Crypto only through [meshink-crypto]")
if meshtastic_crypto_pin in unified_env or "rweather/Crypto" in unified_env:
    errors.append("platformio.ini: protocol build hard-codes a Crypto provider instead of the shared selector")
if "pre:tools/select_crypto_provider.py" not in unified_env:
    errors.append("platformio.ini: unified firmware must resolve duplicate transitive Crypto providers before LDF")
crypto_selector=(ROOT/"tools/select_crypto_provider.py").read_text(encoding="utf-8")
for token in ("XEdDSA.h", "rweather/Crypto", "meshtastic/Crypto", "shutil.rmtree"):
    if token not in crypto_selector:
        errors.append(f"tools/select_crypto_provider.py: Crypto provider selector missing {token!r}")

if errors:
    raise AssertionError("\n".join(errors))

print("PASS: MeshInk hardware and MeshCore adapter boundaries are intact.")
