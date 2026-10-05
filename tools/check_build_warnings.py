"""Fail CI if the warnings-visible firmware build reports MeshInk-owned warnings."""

from pathlib import Path
import sys

if len(sys.argv)!=2:
    raise SystemExit("usage: check_build_warnings.py <build-log>")

log=Path(sys.argv[1]).read_text(encoding="utf-8",errors="replace").splitlines()
owned=[]
for line in log:
    if ": warning:" not in line:
        continue
    normalized=line.replace("\\","/")
    prefix=normalized.split(": warning:",1)[0]
    # Dependencies may be included from an src/ translation unit but keep
    # their own path in the diagnostic. They are intentionally reported in the
    # log but are not MeshInk code and must remain upstream-unmodified.
    if "/lib/MeshCore/" in prefix or "src/../lib/MeshCore/" in prefix:
        continue
    if "/.pio/" in prefix or ".pio/" in prefix:
        continue
    if "/framework-" in prefix or "/packages/" in prefix:
        continue

    meshink_source=(
        " src/" in prefix or prefix.startswith("src/") or "/src/" in prefix or
        " include/" in prefix or prefix.startswith("include/") or "/include/" in prefix
    )
    if meshink_source:
        owned.append(line)

if owned:
    print("MeshInk-owned compiler warnings detected:")
    for line in owned:
        print(line)
    raise SystemExit(1)

print("PASS: warnings-visible build has no MeshInk-owned compiler warnings.")
