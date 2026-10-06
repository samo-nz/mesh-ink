"""Patch web-serial-polyfill 1.0.15 for correct ReadableStream backpressure.

Upstream's UsbEndpointUnderlyingSource.pull() launches transferIn() inside a
floating async IIFE and returns undefined. For an asynchronous pull source that
allows the stream to request another chunk before the previous USB transfer has
settled, creating overlapping transferIn() calls on Android WebUSB.

Return the IIFE promise so ReadableStream waits for each pull to settle before
asking for another one. Keep this as a small deterministic deployment patch so
the original Apache-2.0 package and license remain self-hosted unchanged apart
from this behavioral fix.
"""
from pathlib import Path
import re
import sys

MARKER = "MeshInk patch: serialize WebUSB transferIn via ReadableStream backpressure"

def main():
    if len(sys.argv) != 2:
        raise SystemExit("usage: patch_web_serial_polyfill.py <serial.js>")
    path = Path(sys.argv[1])
    source = path.read_text(encoding="utf-8")
    if MARKER in source:
        return

    # web-serial-polyfill 1.0.15 compiled output contains:
    #   pull(controller) {
    #       (async () => {
    # Return that promise instead of abandoning it.
    pattern = re.compile(r"(\bpull\(controller\)\s*\{\s*)\(async\s*\(\)\s*=>\s*\{")
    source, count = pattern.subn(
        r"\1/* " + MARKER + r" */\n        return (async () => {",
        source,
        count=1,
    )
    if count != 1:
        raise SystemExit(f"expected exactly one WebUSB pull() patch point, found {count}")

    path.write_text(source, encoding="utf-8")

if __name__ == "__main__":
    main()
