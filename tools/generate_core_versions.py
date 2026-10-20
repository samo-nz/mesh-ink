"""Build-time About-screen versions from the actual checked-out upstream cores."""
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parent.parent if "__file__" in globals() else Path.cwd()
OUTPUT = Path("include/core_build_versions.h")


def revision(root: Path, name: str) -> str:
    directory = root / "lib" / name
    if not directory.is_dir():
        raise RuntimeError(f"Missing {name} submodule; run git submodule update --init --recursive")
    sha = subprocess.check_output(
        ["git", "-C", str(directory), "rev-parse", "HEAD"], text=True
    ).strip().lower()
    if not re.fullmatch(r"[0-9a-f]{40}", sha):
        raise RuntimeError(f"Invalid {name} Git revision: {sha!r}")
    return sha[:7]


def versions(root: Path) -> tuple[str, str]:
    core = (root / "lib/MeshCore/examples/companion_radio/MyMesh.h").read_text(encoding="utf-8")
    mc = re.search(r'(?m)^\s*#define\s+FIRMWARE_VERSION\s+"v(\d+(?:\.\d+)+)"', core)
    if not mc:
        raise RuntimeError("Cannot find the actual upstream MeshCore FIRMWARE_VERSION")
    props = (root / "lib/Meshtastic/version.properties").read_text(encoding="utf-8")
    matches = re.findall(r"(?m)^\s*(major|minor|build)\s*=\s*(\d+)\s*$", props)
    values = dict(matches)
    if len(matches) != 3 or set(values) != {"major", "minor", "build"}:
        raise RuntimeError("Cannot read official Meshtastic version.properties")
    mt_version = ".".join(values[key] for key in ("major", "minor", "build"))
    return f"{mc.group(1)} ({revision(root, 'MeshCore')})", \
           f"{mt_version} ({revision(root, 'Meshtastic')})"


def generate(root: Path) -> None:
    mc, mt = versions(root)
    value = (
        "#pragma once\n"
        "// Generated from checked-out upstream source versions and Git commits.\n"
        f'#define MESHINK_BUILD_MESHCORE_VERSION "{mc}"\n'
        f'#define MESHINK_BUILD_MESHTASTIC_VERSION "{mt}"\n'
    )
    output = root / OUTPUT
    if not output.exists() or output.read_text(encoding="utf-8") != value:
        output.write_text(value, encoding="utf-8")
    print(f"[MeshInk] Upstream About-screen versions: MeshCore {mc}; Meshtastic {mt}")


try:
    Import("env")
except NameError:
    if __name__ == "__main__":
        generate(ROOT)
else:
    generate(Path(env.subst("$PROJECT_DIR")))
