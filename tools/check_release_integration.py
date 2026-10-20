"""Fast release regression checks, before the long firmware build."""
from pathlib import Path
import subprocess
import sys

root = Path(__file__).resolve().parents[1]
subprocess.run([sys.executable, str(root / "tools/generate_core_versions.py")],
               cwd=root, check=True)
generated = (root / "include/core_build_versions.h").read_text()
assert 'MESHINK_BUILD_MESHCORE_VERSION "' in generated
assert 'MESHINK_BUILD_MESHTASTIC_VERSION "' in generated
assert generated.count("(") == 2 and generated.count(")") == 2
for name, marker in (
    ("src/protocol/meshcore_protocol.cpp", "MESHINK_BUILD_MESHCORE_VERSION"),
    ("src/protocol/meshtastic_protocol.cpp", "MESHINK_BUILD_MESHTASTIC_VERSION"),
    ("platformio.ini", "pre:tools/generate_core_versions.py"),
):
    assert marker in (root / name).read_text(), name
companion = (root / "src/protocol/meshtastic_companion.cpp").read_text()
timezone = companion.split("void sync_native_timezone_to_meshink(", 1)[1].split(
    "\nvoid snapshot_gps()", 1)[0]
setup = companion.split("void meshink_meshtastic_companion_setup()", 1)[1].split(
    "\nvoid meshink_meshtastic_companion_loop()", 1)[0]
assert "message_observer.observe(" not in timezone
assert "message_observer.observe(textMessageModule)" in setup
protocol = (root / "src/protocol/mesh_protocol.cpp").read_text().split(
    "bool mesh_protocol_restart_into(", 1)[1].split(
        "\nvoid mesh_protocol_restart_after_restore()", 1)[0]
assert "mesh_protocol_flush_now();" in protocol
assert "mesh_protocol_prepare_shutdown();" in protocol
assert "prepare_switch_shutdown()" in protocol
backend = (root / "src/protocol/meshtastic_protocol.cpp").read_text()
assert "value.flush_now=meshink_meshtastic_ui_flush;" in backend
assert "value.prepare_switch_shutdown=prepare_switch_shutdown;" in backend
print("PASS: source-derived version, companion logging and checked shutdown gates")
