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
serial_html = (WEB / "serial.html").read_text(encoding="utf-8")
serial_js = (WEB / "serial.js").read_text(encoding="utf-8")
require('value="update" checked' in html, "safe update must be selected by default")
require('value="wipe"' in html and "Choose this to install MeshInk for the first time." in html, "first-time install option")
require('id="firmware-source-select"' in html and '<option value="release" selected>Latest release</option>' in html,
        "published release remains the default firmware source")
require('<option value="custom">Custom BIN file</option>' in html and 'id="custom-file"' in html,
        "custom local BIN source")
require('id="wipe-word"' not in html and "Type ERASE" not in html, "no unnecessary text-entry gate")
require('id="flash-button"' in html and "disabled" in html, "initially disabled flash button")
require('0x10000' in js and '0x0' in js, "update and full-wipe offsets")
require('import { serial as webUsbSerial } from "./vendor/web-serial-polyfill.js";' in js,
        "Android WebUSB serial polyfill is imported locally")
require('isAndroidPlatform()' in js and 'usesWebUsbSerial()' in js and
        'return usesWebUsbSerial() ? webUsbSerial : navigator.serial;' in js,
        "Android uses WebUSB serial while desktop keeps native Web Serial")
require('const UPDATE_MAX_SIZE = 0x600000' in js, "update image is bounded by the app0 partition")
require('file.size === FULL_WIPE_SIZE' in js, "custom full-wipe image must be exactly 16 MB")
require('file.size >= 1024 && file.size <= UPDATE_MAX_SIZE' in js, "custom update image must fit the app partition")
require('new Uint8Array(await localFile.arrayBuffer())' in js, "custom BIN is read locally without upload")
require('sourceSelect.addEventListener("change"' in js and 'customFileInput.click()' in js,
        "choosing Custom BIN immediately opens the native file picker")
require('customOption.textContent = file.name' in js,
        "selected custom filename replaces the dropdown label")
require('customFileInput.addEventListener("cancel"' in js and
        'sourceSelect.value = "release"' in js,
        "cancelling file selection safely returns to Latest release")
flash_body = js[js.index("async function flash()"):js.index('button.addEventListener("click", flash)')]
require('const port = await serialApi().requestPort();' in flash_body,
        "Flash requests serial permission")
require(flash_body.index('const port = await serialApi().requestPort();') <
        flash_body.index('await localFile.arrayBuffer()'),
        "serial permission is requested before custom file I/O so Android keeps the user gesture")
require(flash_body.index('const port = await serialApi().requestPort();') <
        flash_body.index('await fetch('),
        "serial permission is requested before release download so Android keeps the user gesture")
require('Custom firmware SHA-256:' in js, "custom BIN digest is shown before flashing")
require('selectedSource() === "custom"' in js, "custom source can flash without a release manifest")
require('eraseAll: wipe' in js, "no full-device erase in update mode")
require('address: wipe ? FULL_WIPE_ADDRESS : UPDATE_ADDRESS' in js, "mode-specific offset")
require('wipeWord' not in js, "no typed confirmation logic")
require('window.confirm(' in js, "one confirmation before installing over existing data")
require('/ESP32-S3/i.test(chip)' in js, "reject wrong chip before writing")
require('await sha256(bytes) !== item.sha256' in js, "checksum gate before USB flash")
require('new Uint8Array(await response.arrayBuffer())' in js, "typed binary data")
require('siteStatus.textContent' in js and 'aria-live' in html, "user-visible flash status")
require('background:' in css, "flasher stylesheet exists")
require('id="restart-button"' in html, "manual retry restart button present")
require(html.index('id="log"') < html.index('id="prepare-heading"'),
        "live flashing log appears above preparation guide")
require('async function pulseReset(transport)' in js, "explicit EN reset pulse helper")
require('await transport.setDTR(false)' in js, "release BOOT before restart")
require('await transport.setRTS(true)' in js and 'await transport.setRTS(false)' in js,
        "explicit reset assert and release")
require('async function resetWithRetry(transport)' in js and 'attempt <= 2' in js,
        "automatic reset attempt is retryable")
require('restartButton.addEventListener("click", restartBoard)' in js,
        "manual restart button is wired")
require('flashingCompleted = true;' in js, "manual restart only appears after firmware written")
require('progress.removeAttribute("value")' in js, "visible indeterminate progress during download")
require('let writeStarted = false;' in js and 'writeStarted = true;' in js,
        "explicitly track whether flash writing has started")
require('const connectionFailure = !cancelled && !writeStarted;' in js,
        "pre-write serial failures enter bootloader retry state")
require('progress.hidden = true;' in js and 'connectionRetry = connectionFailure;' in js,
        "connection failure stops progress and enables retry guidance")
require('setActivity("Checking firmware checksum…")' in js, "checksum progress is visible")

require('class="serial-console-button" href="./serial.html">Serial Console</a>' in html,
        "web flasher places Serial Console beside firmware source controls")
require(html.index('id="firmware-source-select"') < html.index('class="serial-console-button"') <
        html.index('id="flash-heading"'),
        "Serial Console button is adjacent to the firmware selector rather than a separate promo row")
require('id="connect-button"' in serial_html and 'id="disconnect-button"' in serial_html and
        'id="reconnect-button"' in serial_html, "serial monitor exposes connect/disconnect/reconnect controls")
require('id="copy-button"' in serial_html and "Copy all" in serial_html,
        "serial monitor exposes copy-all control")
require('id="clear-button"' in serial_html and "Clear window" in serial_html,
        "serial monitor exposes clear-window control")
require('id="serial-log"' in serial_html and 'id="autoscroll"' in serial_html and
        'id="serial-stats"' in serial_html,
        "serial monitor has persistent log window, autoscroll and receive statistics")
require('import { serial as webUsbSerial } from "./vendor/web-serial-polyfill.js";' in serial_js,
        "serial monitor reuses local Android WebUSB serial polyfill")
require('if (isAndroidPlatform()) {' in serial_js and
        'return navigator.usb && webUsbSerial ? { api: webUsbSerial, name: "Android WebUSB" } : null;' in serial_js and
        'return navigator.serial ? { api: navigator.serial, name: "Web Serial" } : null;' in serial_js,
        "serial console always uses WebUSB polyfill on Android and native Web Serial on desktop")
require('await findRememberedGrantedPort(backend.api)' in serial_js and
        'port = await backend.api.requestPort();' in serial_js,
        "Connect reuses authorized ports before opening a picker")
require('async function reconnect()' in serial_js and
        'Previous device is not currently authorized. Press Connect to select it again.' in serial_js,
        "Reconnect is a distinct no-picker path")
require('await openPort(port, backend.name);' in serial_js and
        'const stream = port.readable;' in serial_js and 'localReader = stream.getReader();' in serial_js,
        "serial monitor opens and continuously reads the chosen serial port")
require('const SERIAL_BUFFER_SIZE = 65536;' in serial_js and
        'await port.open({ baudRate, bufferSize: SERIAL_BUFFER_SIZE });' in serial_js,
        "serial console uses a 64 KiB receive buffer for bursty debug output")
require('const DISPLAY_FLUSH_MS = 50;' in serial_js and
        'const VISIBLE_LOG_MAX_CHARS = 512 * 1024;' in serial_js and
        'pendingDisplayChunks' in serial_js and 'window.setTimeout(flushDisplay, DISPLAY_FLUSH_MS)' in serial_js,
        "serial capture is decoupled from DOM rendering with a bounded visible tail")
require('capturedChunks' in serial_js and 'capturedLength' in serial_js and
        'const text = capturedChunks.join("");' in serial_js,
        "copy-all uses the complete in-memory capture rather than the bounded terminal DOM")
require('receivedBytes' in serial_js and 'readChunks' in serial_js and
        'streamRecoveries' in serial_js and 'read retries' in serial_js and
        'updateReceiveStats()' in serial_js,
        "serial console exposes receive counters and labels transient failures as read retries")
require('while (token === generation && !userDisconnecting)' in serial_js and
        'if (!port.readable) break;' in serial_js and
        'localReader.releaseLock()' in serial_js,
        "serial console reacquires readers after recoverable stream errors")
require('Serial stream hiccup' in serial_js and 'recovering without closing the USB port' in serial_js and
        'connectedStatusText + " · stream recovered"' in serial_js,
        "recoverable serial read errors do not close the USB device and successful reads clear stale recovery status")
require('new TextDecoder()' in serial_js and 'captureDeviceText' in serial_js and
        'visibleTextNode.appendData(text)' in serial_js,
        "serial monitor decodes into immediate capture while rendering batches into one text node")
require('reconnectButton.addEventListener("click", reconnect)' in serial_js,
        "serial monitor reconnect button is wired to the no-picker reconnect path")
require('await navigator.clipboard.writeText(text)' in serial_js and
        'document.execCommand("copy")' in serial_js,
        "copy-all supports secure clipboard plus fallback")
require('capturedChunks = [];' in serial_js and 'visibleTextNode.data = "";' in serial_js,
        "clear-window control clears both complete capture state and the visible terminal")
require('Keep requestPort() directly in the user-triggered Connect path' in serial_js,
        "serial Connect keeps Android WebUSB permission selection in the user gesture path")
require('.serial-monitor-log{min-height:360px;max-height:62vh;white-space:pre;overflow-wrap:normal;overflow:auto}' in css,
        "serial terminal avoids expensive line wrapping during long captures")


parser = argparse.ArgumentParser()
parser.add_argument("--build", type=Path, default=None, help="Pages output directory")
parser.add_argument("--version", default=None, help="Version published from the release")
args = parser.parse_args()
if args.build is None:
    require(args.version is None, "version only applies to --build")
    print("PASS: simple update/install choices, safe update default, chip and SHA-256 gates")
else:
    require(args.version is not None and re.fullmatch(r"\d+\.\d+\.\d+(?:-rc\.\d+)?", args.version),
            "invalid release version")
    build = args.build
    for name in ("index.html", "style.css", "flasher.js", "serial.html", "serial.js",
                 "vendor/esptool-js.js", "vendor/web-serial-polyfill.js",
                 "vendor/web-serial-polyfill-LICENSE.txt"):
        require((build / name).is_file() and (build / name).stat().st_size > 0,
                f"missing Pages file: {name}")
    polyfill = (build / "vendor" / "web-serial-polyfill.js").read_text(encoding="utf-8")
    require("MeshInk patch: serialize WebUSB transferIn via ReadableStream backpressure" in polyfill,
            "Android WebUSB polyfill must include the serialized transferIn backpressure patch")
    require(re.search(r"pull\(controller\)\s*\{[^{}]{0,300}return\s*\(async\s*\(\)\s*=>", polyfill, re.S) is not None,
            "patched WebUSB pull() must return its asynchronous transfer promise")
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
        require(length == 16777216 if mode == "wipe" else 1024 < length <= 0x600000,
                f"unexpected {mode} binary size")
        digest = hashlib.file_digest(path.open("rb"), "sha256").hexdigest()
        require(digest == checksums[name], f"{mode} release checksum mismatch")
        manifest["files"][mode] = {"name": name, "size": length, "sha256": digest}
    (build / "latest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("PASS: " + args.version + " firmware SHA-256 verified; Pages manifest generated")

for forbidden in ["GPS power-test workflow","GPS Power Saving","power experiment","Replay Last Log"]:
    require(forbidden not in serial_html, f"serial console must stay generic: {forbidden}")
