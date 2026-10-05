"""Verify MeshInk is building the declared pristine upstream MeshCore pin."""

from pathlib import Path
import re
import subprocess

ROOT=Path(__file__).resolve().parents[1]
SUBMODULE=ROOT/"lib"/"MeshCore"
HEADER=(ROOT/"include"/"meshcore_version.h").read_text(encoding="utf-8")
GITMODULES=(ROOT/".gitmodules").read_text(encoding="utf-8")

EXPECTED_URL="https://github.com/meshcore-dev/MeshCore.git"

def run(*args: str) -> str:
    return subprocess.check_output(args,cwd=ROOT,text=True).strip()

release_match=re.search(r'#define\s+MESHCORE_RELEASE\s+"([^"]+)"',HEADER)
revision_match=re.search(r'#define\s+MESHCORE_REVISION\s+"([0-9a-fA-F]+)"',HEADER)
assert release_match, "MESHCORE_RELEASE missing from include/meshcore_version.h"
assert revision_match, "MESHCORE_REVISION missing from include/meshcore_version.h"

release=release_match.group(1)
declared_revision=revision_match.group(1).lower()
assert len(declared_revision)>=7, "MESHCORE_REVISION must identify at least seven hex digits"

assert "path = lib/MeshCore" in GITMODULES, "MeshCore submodule path changed unexpectedly"
assert f"url = {EXPECTED_URL}" in GITMODULES, "MeshCore submodule must point at official upstream"

actual_revision=run("git","-C",str(SUBMODULE),"rev-parse","HEAD").lower()
assert actual_revision.startswith(declared_revision), (
    f"MeshCore pin mismatch: header={declared_revision} submodule={actual_revision}"
)

origin=run("git","-C",str(SUBMODULE),"remote","get-url","origin")
assert origin==EXPECTED_URL, f"MeshCore working-copy origin is not official upstream: {origin}"

dirty=run("git","-C",str(SUBMODULE),"status","--porcelain","--untracked-files=no")
assert not dirty, "lib/MeshCore contains local modifications; upstream submodule must stay pristine"

mymesh=(SUBMODULE/"examples"/"companion_radio"/"MyMesh.h").read_text(encoding="utf-8")
upstream_version=re.search(r'#define\s+FIRMWARE_VERSION\s+"v([^"]+)"',mymesh)
assert upstream_version, "unable to read upstream companion firmware version"
assert upstream_version.group(1)==release, (
    f"declared MeshCore release {release} does not match upstream MyMesh version "
    f"{upstream_version.group(1)} at {actual_revision[:7]}"
)

print(
    f"PASS: pristine official MeshCore {release} pin "
    f"{actual_revision[:7]} matches include/meshcore_version.h"
)
