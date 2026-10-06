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
const SERIAL_BUFFER_SIZE = 8192;
const READ_RECOVERY_DELAY_MS = 20;
const sleep = (ms) => new Promise((resolve) => setTimeout(resolve, ms));

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

function isAndroidPlatform() {
  return navigator.userAgentData?.platform === "Android" || /Android/i.test(navigator.userAgent || "");
}
function serialBackend() {
  // Android Chrome must use the WebUSB serial polyfill. Some Android builds
  // expose navigator.serial but do not enumerate this ESP32-S3 CDC device.
  if (isAndroidPlatform()) {
    return navigator.usb && webUsbSerial ? { api: webUsbSerial, name: "Android WebUSB" } : null;
  }
  return navigator.serial ? { api: navigator.serial, name: "Web Serial" } : null;
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
  if (!a || !b || a.usbVendorId === null || b.usbVendorId === null) return false;
  return a.usbVendorId === b.usbVendorId && a.usbProductId === b.usbProductId;
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
    return typeof api?.getPorts === "function" ? await api.getPorts() : [];
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
  return ports.length === 1 ? ports[0] : null;
}
async function safeClose(port) {
  if (!port) return;
  try { await port.close(); } catch { /* unplugged or already closed */ }
}
async function readFromPort(port, token) {
  const decoder = new TextDecoder();
  let recoverableErrors = 0;

  // Web Serial deliberately replaces the readable stream after a recoverable
  // read error. The WebUSB polyfill follows that model too: its readable
  // getter creates a fresh stream while the underlying USB device is still
  // open. Do NOT interpret one reader ending or throwing as a USB disconnect.
  while (token === generation && !userDisconnecting) {
    const stream = port.readable;
    if (!stream) break;

    let localReader = null;
    let streamFailed = false;
    try {
      localReader = stream.getReader();
      reader = localReader;

      while (token === generation && !userDisconnecting) {
        const { value, done } = await localReader.read();
        if (done) break;
        if (value?.length) {
          appendDeviceText(decoder.decode(value, { stream: true }));
          recoverableErrors = 0;
        }
      }
    } catch (error) {
      streamFailed = true;
      ++recoverableErrors;
      // A read exception can be non-fatal. After releasing this reader the
      // outer loop checks port.readable and acquires the replacement stream.
      if (recoverableErrors === 1) {
        setStatus("Serial stream hiccup — recovering without closing the USB port…", "warn");
      }
    } finally {
      if (localReader) {
        try { localReader.releaseLock(); } catch { /* stream already released */ }
      }
      if (reader === localReader) reader = null;
    }

    if (token !== generation || userDisconnecting) return;

    // A fatal removal makes readable null. If it is still non-null (or the
    // polyfill can construct a replacement), keep the connection alive.
    if (!port.readable) break;

    // Prevent a tight retry loop on Android if WebUSB reports a transient USB
    // transfer error. Even repeated recoverable errors leave the port open.
    if (streamFailed || recoverableErrors) await sleep(READ_RECOVERY_DELAY_MS);
  }

  if (token !== generation) return;
  appendDeviceText(decoder.decode());
  connected = false;
  everConnected = true;
  rememberedPort = port;
  rememberedInfo = readPortInfo(port);
  await safeClose(port);
  if (activePort === port) activePort = null;

  if (!userDisconnecting) {
    setStatus("USB device disconnected. Wait for it to return, then press Reconnect.", "warn");
  }
  updateControls();
}
async function openPort(port, backendName) {
  const baudRate = Number(baudSelect.value);
  if (!Number.isSafeInteger(baudRate) || baudRate <= 0) throw new Error("Invalid baud rate.");

  activePort = port;
  rememberedPort = port;
  rememberedInfo = readPortInfo(port);
  await port.open({ baudRate, bufferSize: SERIAL_BUFFER_SIZE });

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
    let port = await findRememberedGrantedPort(backend.api);
    if (!port) {
      setStatus("Choose a USB serial device…");
      // Keep requestPort() directly in the user-triggered Connect path so
      // Android Chrome/WebUSB is allowed to show its permission picker.
      port = await backend.api.requestPort();
    } else {
      setStatus("Opening the previously authorized serial device…");
    }
    await openPort(port, backend.name);
  } catch (error) {
    connected = false;
    if (error?.name === "NotFoundError") setStatus("No serial device selected.", "warn");
    else setStatus("Could not open serial device: " + (error.message || String(error)), "error");
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
  setStatus("Looking for the previously authorized serial device…");

  const backend = serialBackend();
  try {
    // A reset can create a new JS port object. Match an already-authorized
    // re-enumerated device by USB VID/PID before falling back to the old object.
    let port = await findRememberedGrantedPort(backend.api);
    if (!port && rememberedPort) port = rememberedPort;
    if (!port) {
      setStatus("Previous device is not currently authorized. Press Connect to select it again.", "warn");
      return;
    }
    await openPort(port, backend.name);
  } catch (error) {
    connected = false;
    if (activePort) {
      await safeClose(activePort);
      activePort = null;
    }
    setStatus("Reconnect failed: " + (error.message || String(error)) + ". Wait for USB to reappear, or use Connect if permission was lost.", "warn");
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
      try { await readLoopPromise; } catch { /* handled in read loop */ }
    }
    await safeClose(port);
  } finally {
    reader = null;
    readLoopPromise = null;
    activePort = null;
    connected = false;
    everConnected = true;
    userDisconnecting = false;
    setStatus("Disconnected by user. Press Reconnect to reopen the previous device.");
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
  setStatus("Ready · " + serialBackend().name + ".");
}
updateLogButtons();
updateControls();
