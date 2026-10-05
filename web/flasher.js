import { ESPLoader, Transport } from "./vendor/esptool-js.js";

const UPDATE_ADDRESS = 0x10000;
const FULL_WIPE_ADDRESS = 0x0;
const FULL_WIPE_SIZE = 16 * 1024 * 1024;
const UPDATE_MAX_SIZE = 0x600000; // app0 partition size from partitions.csv
const $ = (id) => document.getElementById(id);
const button = $("flash-button");
const restartButton = $("restart-button");
const detail = $("flash-detail");
const progress = $("progress");
const progressLabel = $("progress-label");
const logArea = $("log");
const siteStatus = $("site-status");
const modeInputs = [...document.querySelectorAll('input[name="mode"]')];
const sourceInputs = [...document.querySelectorAll('input[name="source"]')];
const customFileRow = $("custom-file-row");
const customFileInput = $("custom-file");
const customFileHelp = $("custom-file-help");
let manifest = null;
let busy = false;
let flashingCompleted = false;
let connectionRetry = false;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

function log(message) {
  logArea.textContent += "\n" + message;
  logArea.scrollTop = logArea.scrollHeight;
}
function selectedMode() {
  return document.querySelector('input[name="mode"]:checked').value;
}
function selectedSource() {
  return document.querySelector('input[name="source"]:checked').value;
}
function customFile() {
  return customFileInput.files?.[0] || null;
}
function customFileFitsMode(file, mode) {
  if (!file) return false;
  return mode === "wipe" ?
    file.size === FULL_WIPE_SIZE :
    file.size >= 1024 && file.size <= UPDATE_MAX_SIZE;
}
function ready() {
  if (busy || !navigator.serial || !window.isSecureContext) return false;
  if (selectedSource() === "custom") return customFileFitsMode(customFile(), selectedMode());
  return !!manifest;
}
function setActivity(message) {
  siteStatus.textContent = message;
  progressLabel.textContent = message;
  log(message);
}
function updateControls() {
  const wipe = selectedMode() === "wipe";
  const custom = selectedSource() === "custom";
  const file = customFile();
  button.classList.toggle("wipe", wipe);
  button.textContent = busy ? "Flashing — do not disconnect" :
    connectionRetry ? "Retry connection" :
    custom ? (wipe ? "Flash custom full wipe" : "Flash custom update") :
    !manifest ? "Loading firmware…" :
    wipe ? "Install MeshInk" : "Update MeshInk";
  button.disabled = !ready();
  restartButton.hidden = !flashingCompleted;
  restartButton.disabled = busy || !navigator.serial || !window.isSecureContext;
  for (const input of [...modeInputs, ...sourceInputs]) input.disabled = busy;
  customFileInput.disabled = busy;
  customFileRow.hidden = !custom;
  customFileHelp.textContent = wipe ?
    "Full-wipe images must be exactly 16 MB and are written at 0x0 after erasing the whole flash." :
    "Update images must fit the 6 MB app partition and are written at 0x10000 without erasing stored data.";
  detail.textContent = !navigator.serial || !window.isSecureContext ?
    "Desktop Chrome or Edge with Web Serial over HTTPS is required." :
    connectionRetry ? "Hold BOOT, press RST, then release both buttons. Then click Retry connection." :
    custom && file && !customFileFitsMode(file, selectedMode()) ?
      (wipe ? "That file is not a 16 MB full-wipe image." : "That file is too large or too small for the 6 MB update partition.") :
    custom && file ? `Selected: ${file.name} · ${(file.size / 1024 / 1024).toFixed(2)} MB` :
    custom ? "Choose a local .bin file for the selected flash mode." :
    wipe ? "For a new device or a fresh start." :
           "For a device that already has MeshInk installed.";
}
for (const input of modeInputs) input.addEventListener("change", () => {
  progress.hidden = true;
  progressLabel.textContent = "";
  updateControls();
});
for (const input of sourceInputs) input.addEventListener("change", () => {
  progress.hidden = true;
  progressLabel.textContent = "";
  updateControls();
});
customFileInput.addEventListener("change", () => {
  progress.hidden = true;
  progressLabel.textContent = "";
  updateControls();
});

async function sha256(bytes) {
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(hash)].map((x) => x.toString(16).padStart(2,"0")).join("");
}
function checkManifest(data) {
  if (!data || !/^\d+\.\d+\.\d+(?:-rc\.\d+)?$/.test(data.version || "")) throw new Error("Invalid release version.");
  for (const mode of ["update","wipe"]) {
    const entry = data.files?.[mode];
    const expectedName = `meshink-${data.version}-${mode === "wipe" ? "full-wipe" : "update"}.bin`;
    if (!entry || entry.name !== expectedName || !/^[a-f0-9]{64}$/.test(entry.sha256) ||
        !Number.isSafeInteger(entry.size) || entry.size < 1024 ||
        (mode === "wipe" && entry.size !== FULL_WIPE_SIZE) ||
        (mode === "update" && entry.size > UPDATE_MAX_SIZE)) {
      throw new Error(`Invalid ${mode} firmware metadata.`);
    }
  }
  return data;
}
async function loadLatest() {
  if (!navigator.serial || !window.isSecureContext) {
    siteStatus.textContent = "Unsupported browser · use desktop Chrome/Edge over HTTPS";
    updateControls();
    log("Web Serial is not available. Try Chrome or Edge on a desktop computer.");
    return;
  }
  try {
    const response = await fetch("./latest.json", { cache: "no-store" });
    if (!response.ok) throw new Error(`Firmware manifest unavailable (HTTP ${response.status}).`);
    manifest = checkManifest(await response.json());
    siteStatus.textContent = `Ready · MeshInk v${manifest.version}`;
    logArea.textContent = `Ready to flash MeshInk v${manifest.version}.\nChoose Update for a newer version, or Install for the first time.`;
  } catch (error) {
    siteStatus.textContent = "Latest release unavailable · custom BIN flashing is still available";
    log(`ERROR: ${error.message}`);
    log("You can still choose Custom BIN file and flash a local image.");
  }
  updateControls();
}

// esptool-js 0.6.0's after("hard_reset") only releases RTS.
 // An explicit LOW/HIGH EN pulse provides a real hardware reset request.
async function pulseReset(transport) {
  await transport.setDTR(false); // release BOOT/IO0 before starting firmware
  await transport.setRTS(false);
  await sleep(70);
  await transport.setRTS(true);  // assert EN reset
  await sleep(160);
  await transport.setRTS(false); // release EN reset
  await sleep(200);
}
async function resetWithRetry(transport) {
  for (let attempt = 1; attempt <= 2; attempt++) {
    try {
      log(`Sending reset pulse ${attempt}/2…`);
      await pulseReset(transport);
      log("Reset pulse sent. The board should restart shortly.");
      return true;
    } catch (error) {
      log(`Reset attempt ${attempt} could not finish: ${error.message || String(error)}`);
      // Native USB may disconnect when the board reboots. Never treat
      // loss of its serial port as a firmware-flashing failure.
      if (attempt === 1) await sleep(300);
    }
  }
  return false;
}

async function restartBoard() {
  if (busy || !flashingCompleted) return;
  busy = true;
  updateControls();
  let resetTransport = null;
  try {
    setActivity("Choose your T5 USB port to retry the restart…");
    const port = await navigator.serial.requestPort();
    resetTransport = new Transport(port, false);
    await resetTransport.connect(115200);
    const sent = await resetWithRetry(resetTransport);
    if (sent) setActivity("Restart pulse sent. If needed, press RESET on the T5.");
    else setActivity("Automatic restart unavailable. Press the RESET button on the T5.");
  } catch (error) {
    setActivity(`Restart not available: ${error.message || String(error)}. Press RESET on the T5.`);
  } finally {
    if (resetTransport) {
      try { await resetTransport.disconnect(); } catch { /* USB can detach when rebooting */ }
    }
    busy = false;
    updateControls();
  }
}

async function flash() {
  if (!ready()) return;
  const mode = selectedMode();
  const wipe = mode === "wipe";
  const source = selectedSource();
  const activeManifest = manifest;
  const localFile = customFile();
  if (wipe && !window.confirm(
    source === "custom" ?
      "Flash this 16 MB full-wipe BIN? This erases the entire flash, including settings and stored data." :
      "Install MeshInk for the first time? This will reset any existing data on the device."
  )) return;
  busy = true;
  flashingCompleted = false;
  connectionRetry = false;
  progress.hidden = false;
  progress.removeAttribute("value"); // show indeterminate activity while downloading/checking
  setActivity("Downloading firmware…");
  updateControls();
  let transport = null;
  let completed = false;
  let writeStarted = false;
  try {
    let bytes;
    let firmwareLabel;
    if (source === "custom") {
      if (!localFile || !customFileFitsMode(localFile, mode))
        throw new Error(wipe ? "Choose an exact 16 MB full-wipe BIN." : "Choose an update BIN that fits the 6 MB app partition.");
      setActivity(`Reading custom firmware ${localFile.name}…`);
      bytes = new Uint8Array(await localFile.arrayBuffer());
      const digest = await sha256(bytes);
      firmwareLabel = localFile.name;
      log(`Custom firmware SHA-256: ${digest}`);
      setActivity("Custom firmware validated. Choose the T5 USB serial port…");
    } else {
      setActivity(`Preparing MeshInk v${activeManifest.version} ${wipe ? "install" : "update"}…`);
      const item = activeManifest.files[mode];
      const response = await fetch(`./assets/${item.name}`, { cache: "no-store" });
      if (!response.ok) throw new Error(`Firmware download failed (HTTP ${response.status}).`);
      bytes = new Uint8Array(await response.arrayBuffer());
      if (bytes.length !== item.size) throw new Error("Firmware size mismatch; nothing was flashed.");
      setActivity("Checking firmware checksum…");
      if (await sha256(bytes) !== item.sha256) throw new Error("Firmware checksum mismatch; nothing was flashed.");
      firmwareLabel = `MeshInk v${activeManifest.version}`;
      setActivity("Firmware verified. Choose the T5 USB serial port…");
    }
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport, baudrate: 115200, debugLogging: false,
      terminal: { clean() {}, write(message) { log(message); }, writeLine(message) { log(message); } }
    });
    const chip = String(await loader.main());
    connectionRetry = false;
    setActivity(`Connected to ${chip}. Starting flash…`);
    if (!/ESP32-S3/i.test(chip)) throw new Error("This image is only for ESP32-S3. No flash was written.");
    progress.value = 0;
    progressLabel.textContent = "Writing firmware: 0%";
    log(wipe ? "Erasing entire flash, then writing 16 MB image at 0x0…" :
               "Writing application at 0x10000 with eraseAll=false; NVS and message storage are left alone.");
    writeStarted = true;
    await loader.writeFlash({
      fileArray: [{ data: bytes, address: wipe ? FULL_WIPE_ADDRESS : UPDATE_ADDRESS }],
      flashMode: "dio",
      flashFreq: "40m",
      flashSize: "16MB",
      eraseAll: wipe,
      compress: true,
      reportProgress: (_index, written, total) => {
        const percent = total ? Math.min(100, Math.floor(written * 100 / total)) : 0;
        progress.value = percent;
        progressLabel.textContent = `Writing firmware: ${percent}%`;
      }
    });
    completed = true;
    flashingCompleted = true;
    progress.value = 100;
    setActivity("Firmware written successfully. Restarting the T5…");
    const sent = await resetWithRetry(transport);
    siteStatus.textContent = sent ?
      `${firmwareLabel} flashed · reset pulse sent` :
      `${firmwareLabel} flashed · press RESET to restart`;
    progressLabel.textContent = "100% · firmware flashed";
    log(sent ?
      "If the T5 does not restart, click Restart device above or press RESET on the T5." :
      "Automatic restart was not confirmed. Click Restart device above or press RESET on the T5.");
  } catch (error) {
    if (!completed) {
      const message = error.message || String(error);
      const cancelled = error.name === "NotFoundError";
      const connectionFailure = !cancelled && !writeStarted;
      progress.removeAttribute("value");
      progress.hidden = true;
      connectionRetry = connectionFailure;
      siteStatus.textContent = cancelled ? "No serial port selected" :
        connectionFailure ? "T5 not connected" :
        "Flashing did not complete";
      progressLabel.textContent = cancelled ? "No serial port selected" :
        connectionFailure ? "Connection failed" :
        "Flashing did not complete";
      log(`ERROR: ${message}`);
      if (cancelled) log("No serial port selected; no flash operation started.");
      else if (connectionFailure) log("Could not connect to the T5. Hold BOOT, press RST, then release both buttons and click Retry connection.");
      else log("If an update was interrupted during writing, do not assume the firmware is bootable. Reconnect and retry.");
    }
  } finally {
    if (transport) {
      try { await transport.disconnect(); }
      catch (error) { log(`Serial port disconnect: ${error.message}`); }
    }
    busy = false;
    updateControls();
  }
}

button.addEventListener("click", flash);
restartButton.addEventListener("click", restartBoard);
updateControls();
loadLatest();
