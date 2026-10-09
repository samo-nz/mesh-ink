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
