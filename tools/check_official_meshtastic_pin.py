#!/usr/bin/env python3
"""Offline guard: pinned official Meshtastic and shared crypto, independent of Leaf."""
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parent.parent
UPSTREAM = "8e6a88d06f44cad26f1e8d7cd402939ccacb9b4c"
TAG = "v2.8.1.8e6a88d"

def check():
    actual = subprocess.check_output(
        ["git", "ls-tree", "HEAD", "lib/Meshtastic"], cwd=ROOT, text=True
    ).strip()
    expected = f"160000 commit {UPSTREAM}\tlib/Meshtastic"
    if actual != expected:
        raise SystemExit(f"Meshtastic gitlink mismatch: {actual!r}")
    submodules = (ROOT / ".gitmodules").read_text()
    if '[submodule "lib/Meshtastic"]' not in submodules or (
        "url = https://github.com/meshtastic/firmware.git" not in submodules
    ):
        raise SystemExit("Official Meshtastic submodule URL changed")
    versions = (ROOT / "include/meshtastic_official_version.h").read_text()
    if f'"{UPSTREAM}"' not in versions or f'"{TAG}"' not in versions:
        raise SystemExit("Official Meshtastic version header mismatch")
    ini = (ROOT / "platformio.ini").read_text()
    if "Crypto=https://github.com/meshtastic/Crypto/archive/" not in ini:
        raise SystemExit("The Meshtastic Crypto fork is required for XEdDSA/PKI")
    if "${meshink-crypto-meshtastic.lib_deps}" not in ini:
        raise SystemExit("Shared crypto is not Meshtastic's selected provider")
    if "libmeshtastic-leaf.git" in ini:
        raise SystemExit("Obsolete Leaf dependency must be removed")
    official = ROOT / "lib/Meshtastic/src/mesh/PhoneAPI.h"
    if official.is_file() and "class PhoneAPI" not in official.read_text():
        raise SystemExit("Official PhoneAPI header missing")
    print(f"Official Meshtastic {TAG} / {UPSTREAM} pinned; shared crypto preserved")

if __name__ == "__main__":
    check()
