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
