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

meshtastic_helper=(SRC/"protocol/meshtastic_protocol.cpp").read_text(encoding="utf-8")
if "meshink_protocol_backend_slot_2" not in meshtastic_helper:
    errors.append("src/protocol/meshtastic_protocol.cpp: Meshtastic helper is not registered in backend slot 2")
for token in ("P_LORA_", "CustomSX1262", "T5RadioHal", "radio_spi"):
    if token in meshtastic_helper:
        errors.append(
            "src/protocol/meshtastic_protocol.cpp: board-specific radio detail "
            f"{token!r} bypasses hardware/radio.h"
        )

for rel in (
    "src/ui_onboarding.cpp",
    "src/message_store.cpp",
    "src/companion_runtime.cpp",
    "src/unified_main.cpp",
):
    text=(ROOT/rel).read_text(encoding="utf-8")
    if "hardware/performance.h" not in text:
        errors.append(f"{rel}: CPU policy must use generic hardware/performance.h")

platformio=(ROOT/"platformio.ini").read_text(encoding="utf-8")
warning_env=platformio[platformio.index("[env:t5-unified-cache64-warnings]"):platformio.index("; Generic portability",platformio.index("[env:t5-unified-cache64-warnings]"))]
if "build_src_flags =" not in warning_env or "-Wall" not in warning_env or "-Wextra" not in warning_env:
    errors.append("platformio.ini: MeshInk-source warnings-visible RC audit environment is missing")
if "build_unflags =" in warning_env:
    errors.append("platformio.ini: warning audit must not alter dependency/framework production flags")
unified_env=platformio[platformio.index("[env:t5-unified]"):platformio.index("[env:t5-unified-cache64]")]
if "-DMESHINK_DEVICE_SERVICES_ENABLED=1" not in unified_env:
    errors.append("platformio.ini: full firmware must enable MeshInk-owned GPS/RTC device services")
if "-DMESHINK_PROTOCOL_SLOT_2_ENABLED=1" not in unified_env:
    errors.append("platformio.ini: full firmware must register the Meshtastic backend")
leaf_pin="https://github.com/Meshtastic-Solutions/libmeshtastic-leaf.git#bd542d6e77aa7b9507dcc8d153f6dad0c35a8c17"
if leaf_pin not in unified_env:
    errors.append("platformio.ini: libmeshtastic-leaf must stay pinned to reviewed 1.0.0 commit")
crypto_section=platformio[platformio.index("[meshink-crypto]"):platformio.index("[meshink-crypto-meshcore]")]
meshtastic_crypto_pin="Crypto=https://github.com/meshtastic/Crypto/archive/591ff9a690e8168ccb7a36abde8d7783e448d395.zip"
if meshtastic_crypto_pin not in crypto_section:
    errors.append("platformio.ini: active shared Crypto provider must stay pinned to Leaf's reviewed Meshtastic fork")
fallback_section=platformio[platformio.index("[meshink-crypto-meshcore]"):platformio.index("[env:t5-pro]")]
if "rweather/Crypto @ ^0.4.0" not in fallback_section:
    errors.append("platformio.ini: MeshCore Crypto recovery provider is missing")
if "${meshink-crypto.lib_deps}" not in unified_env:
    errors.append("platformio.ini: unified firmware must consume Crypto only through [meshink-crypto]")
if meshtastic_crypto_pin in unified_env or "rweather/Crypto" in unified_env:
    errors.append("platformio.ini: protocol build hard-codes a Crypto provider instead of the shared selector")

if errors:
    raise AssertionError("\n".join(errors))

print("PASS: MeshInk hardware and MeshCore adapter boundaries are intact.")
