import { serial as webUsbSerial } from "./vendor/web-serial-polyfill.js";

const $ = (id) => document.getElementById(id);
const connectButton = $("connect-button");
const disconnectButton = $("disconnect-button");
const reconnectButton = $("reconnect-button");
const baudSelect = $("baud-select");
const statusLine = $("serial-status");
const logArea = $("serial-log");
const copyButton = $("copy-button");
const clearButton = $("clear-button");
const autoscroll = $("autoscroll");

let activePort = null;
let rememberedPort = null;
let rememberedInfo = null;
let reader = null;
let readLoopPromise = null;
let generation = 0;
let connecting = false;
let connected = false;
let everConnected = false;
let userDisconnecting = false;
let activeBackendName = "";

function isAndroidPlatform() {
  return navigator.userAgentData?.platform === "Android" || /Android/i.test(navigator.userAgent || "");
}
function nativeSerialAvailable() {
  return !!navigator.serial;
}
function webUsbSerialAvailable() {
  return isAndroidPlatform() && !!navigator.usb && !!webUsbSerial;
}
function serialBackend() {
  // Current Android Chrome can expose native Web Serial. Prefer it for a
  // long-lived terminal because it survives CDC traffic better than the
  // WebUSB compatibility layer on devices that support both. Keep WebUSB as
  // the fallback used on Android versions without native Web Serial.
  if (nativeSerialAvailable()) return { api: navigator.serial, name: "Web Serial" };
  if (webUsbSerialAvailable()) return { api: webUsbSerial, name: "Android WebUSB" };
  return null;
}
function serialAvailable() {
  return window.isSecureContext && !!serialBackend();
}
function setStatus(message, kind = "") {
  statusLine.textContent = message;
  statusLine.dataset.kind = kind;
}
function updateLogButtons() {
  const empty = logArea.textContent.length === 0;
  copyButton.disabled = empty;
  clearButton.disabled = empty;
}
function updateControls() {
  const available = serialAvailable();
  connectButton.disabled = !available || connecting || connected;
  disconnectButton.disabled = !available || connecting || !connected;
  reconnectButton.disabled = !available || connecting || connected || !everConnected;
  baudSelect.disabled = connecting || connected;
}
function appendDeviceText(text) {
  if (!text) return;
  logArea.append(document.createTextNode(text));
  updateLogButtons();
  if (autoscroll.checked) logArea.scrollTop = logArea.scrollHeight;
}
function readPortInfo(port) {
  try {
    const info = port?.getInfo?.() || {};
    return {
      usbVendorId: Number.isInteger(info.usbVendorId) ? info.usbVendorId : null,
      usbProductId: Number.isInteger(info.usbProductId) ? info.usbProductId : null
    };
  } catch {
    return { usbVendorId: null, usbProductId: null };
  }
}
function samePortInfo(a, b) {
  if (!a || !b) return false;
  if (a.usbVendorId === null || b.usbVendorId === null) return false;
  return a.usbVendorId === b.usbVendorId &&
         a.usbProductId === b.usbProductId;
}
function portDescription(port) {
  const info = readPortInfo(port);
  const parts = [];
  if (info.usbVendorId !== null) parts.push("VID " + info.usbVendorId.toString(16).padStart(4, "0"));
  if (info.usbProductId !== null) parts.push("PID " + info.usbProductId.toString(16).padStart(4, "0"));
  return parts.length ? " · " + parts.join(" ") : "";
}
async function grantedPorts(api) {
  try {
    if (typeof api?.getPorts !== "function") return [];
    return await api.getPorts();
  } catch {
    return [];
  }
}
async function findRememberedGrantedPort(api) {
  const ports = await grantedPorts(api);
  if (!ports.length) return null;

  if (rememberedPort && ports.includes(rememberedPort)) return rememberedPort;
  if (rememberedInfo) {
    const match = ports.find((candidate) => samePortInfo(readPortInfo(candidate), rememberedInfo));
    if (match) return match;
  }
  // If this origin has permission for exactly one serial device, it is the
  // least surprising reconnect target and avoids another Android picker.
  return ports.length === 1 ? ports[0] : null;
}
async function safeClose(port) {
  if (!port) return;
  try { await port.close(); } catch { /* unplugged/already closed */ }
}
async function readFromPort(port, token) {
  const decoder = new TextDecoder();
  let localReader = null;
  try {
    if (!port.readable) throw new Error("Serial port opened without a readable stream.");
    localReader = port.readable.getReader();
    reader = localReader;
    while (token === generation) {
      const { value, done } = await localReader.read();
      if (done) break;
      if (value?.length) appendDeviceText(decoder.decode(value, { stream: true }));
    }
  } catch (error) {
    if (token === generation && !userDisconnecting) {
      setStatus("Serial connection lost: " + (error.message || String(error)), "error");
    }
  } finally {
    if (localReader) {
      try { localReader.releaseLock(); } catch { /* already released */ }
    }
    if (reader === localReader) reader = null;
    if (token !== generation) return;

    appendDeviceText(decoder.decode());
    connected = false;
    everConnected = true;
    rememberedPort = port;
    rememberedInfo = readPortInfo(port);
    await safeClose(port);
    if (activePort === port) activePort = null;

    if (!userDisconnecting) {
      setStatus("Disconnected. Press Reconnect to reuse the authorized T5; use Connect only if permission is lost.", "warn");
    }
    updateControls();
  }
}
async function openPort(port, backendName) {
  const baudRate = Number(baudSelect.value);
  if (!Number.isSafeInteger(baudRate) || baudRate <= 0) throw new Error("Invalid baud rate.");

  activePort = port;
  rememberedPort = port;
  rememberedInfo = readPortInfo(port);
  activeBackendName = backendName;
  await port.open({ baudRate });

  connected = true;
  everConnected = true;
  userDisconnecting = false;
  const token = ++generation;
  setStatus("Connected at " + baudRate + " baud via " + backendName + portDescription(port), "ok");
  updateControls();
  readLoopPromise = readFromPort(port, token);
}
async function connect() {
  if (connecting || connected || !serialAvailable()) return;
  connecting = true;
  updateControls();

  const backend = serialBackend();
  try {
    // Reuse an already-authorized device first. Once the user has granted a
    // T5 to this origin, ordinary reconnects should not reopen the picker.
    let port = await findRememberedGrantedPort(backend.api);
    if (!port) {
      setStatus("Choose the T5 USB serial port…");
      // requestPort() remains directly inside the Connect gesture path.
      port = await backend.api.requestPort();
    } else {
      setStatus("Opening the previously authorized T5…");
    }
    await openPort(port, backend.name);
  } catch (error) {
    connected = false;
    if (error?.name === "NotFoundError") {
      setStatus("No serial port selected.", "warn");
    } else {
      setStatus("Could not open serial port: " + (error.message || String(error)), "error");
    }
    if (activePort && !connected) {
      await safeClose(activePort);
      activePort = null;
    }
  } finally {
    connecting = false;
    updateControls();
  }
}
async function reconnect() {
  if (connecting || connected || !serialAvailable()) return;
  connecting = true;
  updateControls();
  setStatus("Looking for the previously authorized T5…");

  const backend = serialBackend();
  try {
    let port = await findRememberedGrantedPort(backend.api);
    if (!port && rememberedPort) port = rememberedPort;
    if (!port) {
      setStatus("No authorized T5 is available. Press Connect to choose it again.", "warn");
      return;
    }
    await openPort(port, backend.name);
  } catch (error) {
    connected = false;
    if (activePort) {
      await safeClose(activePort);
      activePort = null;
    }
    // Do not invoke requestPort() here. Reconnect is intentionally a no-picker
    // operation; Connect is the explicit permission/device-selection action.
    setStatus("Reconnect failed: " + (error.message || String(error)) + ". If the T5 re-enumerated, press Connect once.", "warn");
  } finally {
    connecting = false;
    updateControls();
  }
}
async function disconnect() {
  if ((!connected && !reader) || userDisconnecting) return;
  userDisconnecting = true;
  setStatus("Disconnecting…");
  const port = activePort;
  rememberedPort = port || rememberedPort;
  rememberedInfo = port ? readPortInfo(port) : rememberedInfo;
  ++generation;
  const activeReader = reader;

  try {
    if (activeReader) {
      try { await activeReader.cancel(); } catch { /* device may already be gone */ }
    }
    if (readLoopPromise) {
      try { await readLoopPromise; } catch { /* read errors are handled in loop */ }
    }
    await safeClose(port);
  } finally {
    reader = null;
    readLoopPromise = null;
    activePort = null;
    connected = false;
    everConnected = true;
    userDisconnecting = false;
    setStatus("Disconnected by user. Reconnect will reuse the authorized T5.");
    updateControls();
  }
}
async function copyAll() {
  const text = logArea.textContent;
  if (!text) return;
  let copied = false;
  try {
    await navigator.clipboard.writeText(text);
    copied = true;
  } catch {
    const helper = document.createElement("textarea");
    helper.value = text;
    helper.setAttribute("readonly", "");
    helper.style.position = "fixed";
    helper.style.left = "-10000px";
    document.body.append(helper);
    helper.select();
    copied = document.execCommand("copy");
    helper.remove();
  }
  const previous = copyButton.textContent;
  copyButton.textContent = copied ? "Copied" : "Copy failed";
  window.setTimeout(() => { copyButton.textContent = previous; }, 1200);
}
function clearWindow() {
  logArea.textContent = "";
  updateLogButtons();
  logArea.scrollTop = 0;
}

connectButton.addEventListener("click", connect);
reconnectButton.addEventListener("click", reconnect);
disconnectButton.addEventListener("click", disconnect);
copyButton.addEventListener("click", copyAll);
clearButton.addEventListener("click", clearWindow);

if (!serialAvailable()) {
  setStatus("Serial access unavailable. Use Chrome/Edge on desktop or Chrome on Android over HTTPS.", "error");
} else {
  const backend = serialBackend();
  setStatus("Ready · " + backend.name + (isAndroidPlatform() ? " on Android." : "."));
}
updateLogButtons();
updateControls();
