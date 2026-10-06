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

let port = null;
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
function usesWebUsbSerial() {
  return isAndroidPlatform() && !!navigator.usb;
}
function serialApi() {
  return usesWebUsbSerial() ? webUsbSerial : navigator.serial;
}
function serialAvailable() {
  return window.isSecureContext && !!serialApi();
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
function portDescription(activePort) {
  try {
    const info = activePort?.getInfo?.();
    if (!info) return "";
    const parts = [];
    if (Number.isInteger(info.usbVendorId)) parts.push("VID " + info.usbVendorId.toString(16).padStart(4, "0"));
    if (Number.isInteger(info.usbProductId)) parts.push("PID " + info.usbProductId.toString(16).padStart(4, "0"));
    return parts.length ? " · " + parts.join(" ") : "";
  } catch {
    return "";
  }
}
async function safeClose(activePort) {
  if (!activePort) return;
  try { await activePort.close(); } catch { /* unplugged/already closed */ }
}
async function readFromPort(activePort, token) {
  const decoder = new TextDecoder();
  try {
    while (token === generation && activePort.readable) {
      const localReader = activePort.readable.getReader();
      reader = localReader;
      try {
        while (token === generation) {
          const { value, done } = await localReader.read();
          if (done) break;
          if (value?.length) appendDeviceText(decoder.decode(value, { stream: true }));
        }
      } finally {
        try { localReader.releaseLock(); } catch { /* already released */ }
        if (reader === localReader) reader = null;
      }
      if (token !== generation || !activePort.readable) break;
    }
  } catch (error) {
    if (token === generation && !userDisconnecting) {
      setStatus("Serial connection lost: " + (error.message || String(error)), "error");
    }
  } finally {
    if (token !== generation) return;
    appendDeviceText(decoder.decode());
    connected = false;
    everConnected = true;
    await safeClose(activePort);
    if (port === activePort) port = null;
    if (!userDisconnecting) setStatus("Disconnected. Wait for the T5 USB port to return, then press Reconnect.", "warn");
    updateControls();
  }
}
async function chooseAndConnect(isReconnect) {
  if (connecting || connected || !serialAvailable()) return;
  connecting = true;
  updateControls();
  setStatus(isReconnect ? "Choose the T5 USB port again…" : "Choose the T5 USB serial port…");

  try {
    // requestPort() is deliberately the first awaited operation after the
    // button gesture so Android Chrome/WebUSB keeps permission to show its
    // device picker.
    const chosenPort = await serialApi().requestPort();
    const baudRate = Number(baudSelect.value);
    if (!Number.isSafeInteger(baudRate) || baudRate <= 0) throw new Error("Invalid baud rate.");

    port = chosenPort;
    await port.open({ baudRate });
    connected = true;
    everConnected = true;
    userDisconnecting = false;
    const token = ++generation;
    setStatus("Connected at " + baudRate + " baud" + portDescription(port), "ok");
    updateControls();
    readLoopPromise = readFromPort(port, token);
  } catch (error) {
    connected = false;
    if (error?.name === "NotFoundError") {
      setStatus("No serial port selected.", "warn");
    } else {
      setStatus("Could not open serial port: " + (error.message || String(error)), "error");
      everConnected = everConnected || !!port;
    }
    if (port && !connected) {
      await safeClose(port);
      port = null;
    }
    updateControls();
  } finally {
    connecting = false;
    updateControls();
  }
}
async function disconnect() {
  if ((!connected && !reader) || userDisconnecting) return;
  userDisconnecting = true;
  setStatus("Disconnecting…");
  const activePort = port;
  ++generation;
  const activeReader = reader;

  try {
    if (activeReader) {
      try { await activeReader.cancel(); } catch { /* device may already be gone */ }
    }
    if (readLoopPromise) {
      try { await readLoopPromise; } catch { /* read errors are handled in loop */ }
    }
    await safeClose(activePort);
  } finally {
    reader = null;
    readLoopPromise = null;
    port = null;
    connected = false;
    everConnected = true;
    userDisconnecting = false;
    setStatus("Disconnected by user. Press Reconnect when ready.");
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

connectButton.addEventListener("click", () => chooseAndConnect(false));
reconnectButton.addEventListener("click", () => chooseAndConnect(true));
disconnectButton.addEventListener("click", disconnect);
copyButton.addEventListener("click", copyAll);
clearButton.addEventListener("click", clearWindow);

if (!serialAvailable()) {
  setStatus("Serial access unavailable. Use Chrome/Edge on desktop or Chrome on Android over HTTPS.", "error");
} else if (usesWebUsbSerial()) {
  setStatus("Ready. Android detected; WebUSB serial compatibility is active.", "ok");
} else {
  setStatus("Ready. Press Connect and choose the T5 USB serial port.");
}
updateLogButtons();
updateControls();
