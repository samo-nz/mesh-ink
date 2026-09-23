"""Validate the web flasher's safety-critical offsets and published assets.

Run before pushing Pages. In --build mode, also validate release SHA-256
checksums and generate the static latest.json manifest for the site.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
WEB = ROOT / "web"

def require(condition, label):
    if not condition:
        raise SystemExit("FAIL: " + label)

html = (WEB / "index.html").read_text(encoding="utf-8")
js = (WEB / "flasher.js").read_text(encoding="utf-8")
css = (WEB / "style.css").read_text(encoding="utf-8")
require('value="update" checked' in html, "safe update must be selected by default")
require('value="wipe"' in html and "Choose this to install MeshInk for the first time." in html, "first-time install option")
require('id="wipe-word"' not in html and "Type ERASE" not in html, "no unnecessary text-entry gate")
require('id="flash-button"' in html and "disabled" in html, "initially disabled flash button")
require('0x10000' in js and '0x0' in js, "update and full-wipe offsets")
require('eraseAll: wipe' in js, "no full-device erase in update mode")
require('address: wipe ? FULL_WIPE_ADDRESS : UPDATE_ADDRESS' in js, "mode-specific offset")
require('wipeWord' not in js, "no typed confirmation logic")
require('window.confirm(' in js, "one confirmation before installing over existing data")
require('/ESP32-S3/i.test(chip)' in js, "reject wrong chip before writing")
require('await sha256(bytes) !== item.sha256' in js, "checksum gate before USB flash")
require('new Uint8Array(await response.arrayBuffer())' in js, "typed binary data")
require('siteStatus.textContent' in js and 'aria-live' in html, "user-visible flash status")
require('background:' in css, "flasher stylesheet exists")

parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, default=None, help="Pages output directory")
parser.add_argument("--version", default=None, help="Version published from the release")
args = parser.parse_args()
if args.build is None:
    require(args.version is None, "version only applies to --build")
    print("PASS: simple update/install choices, safe update default, chip and SHA-256 gates")
else:
    require(args.version is not None and re.fullmatch(r"\d+\.\d+\.\d+", args.version),
            "invalid release version")
    build = args.build
    for name in ("index.html", "style.css", "flasher.js", "vendor/esptool-js.js"):
        require((build / name).is_file() and (build / name).stat().st_size > 0,
                f"missing Pages file: {name}")
    checksums = {}
    for line in (build / "assets" / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines():
        match = re.fullmatch(r"([0-9a-f]{64})\s+\*?([^/\\]+\.bin)", line)
        require(match is not None, "malformed release checksum line")
        checksums[match[2]] = match[1]
    manifest = {"version": args.version, "files": {}}
    for mode, suffix in (("update", "update"), ("wipe", "full-wipe")):
        name = f"meshink-{args.version}-{suffix}.bin"
        path = build / "assets" / name
        require(name in checksums and path.is_file(), f"missing {mode} release firmware")
        length = path.stat().st_size
        require(length == 16777216 if mode == "wipe" else 1024 < length < (16777216 - 65536),
                f"unexpected {mode} binary size")
        digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
        require(digest == checksums[name], f"{mode} release checksum mismatch")
        manifest["files"][mode] = {"name": name, "size": length, "sha256": digest}
    (build / "latest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("PASS: " + args.version + " firmware SHA-256 verified; Pages manifest generated")
