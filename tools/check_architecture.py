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

if errors:
    raise AssertionError("\n".join(errors))

print("PASS: MeshInk hardware and MeshCore adapter boundaries are intact.")
