import { ESPLoader, Transport } from "./vendor/esptool-js.js";

const UPDATE_ADDRESS = 0x10000;
const FULL_WIPE_ADDRESS = 0x0;
const FULL_WIPE_SIZE = 16 * 1024 * 1024;
const $ = (id) => document.getElementById(id);
const button = $("flash-button");
const detail = $("flash-detail");
const progress = $("progress");
const progressLabel = $("progress-label");
const logArea = $("log");
const siteStatus = $("site-status");
const wipeConfirm = $("wipe-confirm");
const wipeWord = $("wipe-word");
const modeInputs = [...document.querySelectorAll('input[name="mode"]')];
let manifest = null;
let busy = false;

function log(message) {
  logArea.textContent += "\n" + message;
  logArea.scrollTop = logArea.scrollHeight;
}
function selectedMode() {
  return document.querySelector('input[name="mode"]:checked').value;
}
function ready() {
  if (!manifest || busy || !navigator.serial || !window.isSecureContext) return false;
  return selectedMode() === "update" || wipeWord.value.trim() === "ERASE";
}
function updateControls() {
  const wipe = selectedMode() === "wipe";
  wipeConfirm.hidden = !wipe;
  button.classList.toggle("wipe", wipe);
  button.textContent = busy ? "Flashing — do not disconnect" :
    !manifest ? "Loading firmware…" : wipe ? "Erase device and install MeshInk" : "Update MeshInk (keep settings)";
  button.disabled = !ready();
  wipeWord.disabled = busy;
  for (const input of modeInputs) input.disabled = busy;
  detail.textContent = !navigator.serial || !window.isSecureContext ?
    "Desktop Chrome or Edge with Web Serial over HTTPS is required." :
    wipe ? "Full wipe · writes the 16 MB image at 0x0 · ALL saved data is removed" :
           "App-only update · writes at 0x10000 · no full-flash erase";
}
for (const input of modeInputs) input.addEventListener("change", () => {
  wipeWord.value = "";
  progress.hidden = true;
  progressLabel.textContent = "";
  updateControls();
});
wipeWord.addEventListener("input", updateControls);

async function sha256(bytes) {
  const hash = await crypto.subtle.digest("SHA-256", bytes);
  return [...new Uint8Array(hash)].map((x) => x.toString(16).padStart(2,"0")).join("");
}
function checkManifest(data) {
  if (!data || !/^\d+\.\d+\.\d+$/.test(data.version || "")) throw new Error("Invalid release version.");
  for (const mode of ["update","wipe"]) {
    const entry = data.files?.[mode];
    const expectedName = `meshink-${data.version}-${mode === "wipe" ? "full-wipe" : "update"}.bin`;
    if (!entry || entry.name !== expectedName || !/^[a-f0-9]{64}$/.test(entry.sha256) ||
        !Number.isSafeInteger(entry.size) || entry.size < 1024 ||
        (mode === "wipe" && entry.size !== FULL_WIPE_SIZE) ||
        (mode === "update" && entry.size > FULL_WIPE_SIZE - UPDATE_ADDRESS)) {
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
    logArea.textContent = `Ready to flash MeshInk v${manifest.version}.\nSelect Update to keep your settings, or explicitly select Full wipe for a clean installation.`;
  } catch (error) {
    siteStatus.textContent = "Firmware not available";
    log(`ERROR: ${error.message}`);
  }
  updateControls();
}

async function flash() {
  if (!ready()) return;
  const mode = selectedMode();
  const wipe = mode === "wipe";
  const activeManifest = manifest;
  if (wipe && !window.confirm(
    "FINAL CONFIRMATION: Erase the ENTIRE 16 MB flash? This permanently deletes your MeshCore identity, radio settings, contacts, messages and all stored data."
  )) return;
  busy = true;
  updateControls();
  let transport = null;
  let completed = false;
  try {
    log(`Preparing MeshInk v${activeManifest.version} ${wipe ? "FULL WIPE" : "UPDATE"}…`);
    const item = activeManifest.files[mode];
    const response = await fetch(`./assets/${item.name}`, { cache: "no-store" });
    if (!response.ok) throw new Error(`Firmware download failed (HTTP ${response.status}).`);
    const bytes = new Uint8Array(await response.arrayBuffer());
    if (bytes.length !== item.size) throw new Error("Firmware size mismatch; nothing was flashed.");
    log("Checking firmware SHA-256 before opening the serial port…");
    if (await sha256(bytes) !== item.sha256) throw new Error("Firmware checksum mismatch; nothing was flashed.");
    log("Firmware checksum verified. Choose the T5 USB serial port.");
    const port = await navigator.serial.requestPort();
    transport = new Transport(port, false);
    const loader = new ESPLoader({
      transport, baudrate: 115200, debugLogging: false,
      terminal: { clean() {}, write(message) { log(message); }, writeLine(message) { log(message); } }
    });
    const chip = String(await loader.main());
    log(`Connected to ${chip}.`);
    if (!/ESP32-S3/i.test(chip)) throw new Error("This image is only for ESP32-S3. No flash was written.");
    progress.hidden = false;
    progress.value = 0;
    progressLabel.textContent = "0%";
    log(wipe ? "Erasing entire flash, then writing 16 MB image at 0x0…" :
               "Writing application at 0x10000 with eraseAll=false; NVS and message storage are left alone.");
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
        progressLabel.textContent = `${percent}% written`;
      }
    });
    completed = true;
    progress.value = 100;
    progressLabel.textContent = "100% · flashing complete";
    log("Flashing completed. Attempting to restart the T5…");
    try { await loader.after("hard_reset"); }
    catch (error) { log(`Automatic reset was unavailable: ${error.message}. Press RESET on the T5.`); }
    siteStatus.textContent = `MeshInk v${activeManifest.version} flashed successfully`;
    log("SUCCESS. If the T5 does not restart, press RESET.");
  } catch (error) {
    if (!completed) {
      siteStatus.textContent = "Flashing did not complete";
      log(`ERROR: ${error.message || String(error)}`);
      if (error.name === "NotFoundError") log("No serial port selected; no flash operation started.");
      log("If an update was interrupted during writing, do not assume the firmware is bootable. Reconnect and retry.");
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
updateControls();
loadLatest();
