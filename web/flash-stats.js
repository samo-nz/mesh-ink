import { STATS_ENDPOINT } from "./stats-config.js";

// Never transmit a device identifier, IP, serial log, filename or message.
// The two events are browser-reported observations, not attested installs.
const VERSION = /^\d+\.\d+\.\d+(?:-rc\.\d+)?$/;
const EVENTS = new Set(["download", "flash"]);
const MODES = new Set(["update", "wipe"]);

export function reportFlashEvent(version, mode, event) {
  if (!STATS_ENDPOINT || !VERSION.test(version) || !MODES.has(mode) || !EVENTS.has(event)) return;
  try {
    // Fire and forget. Telemetry must never interfere with the USB transaction
    // or cause flashing to be reported as failed.
    const body = JSON.stringify({ version, mode, event });
    void fetch(STATS_ENDPOINT.replace(/\/$/, "") + "/event", {
      method: "POST",
      mode: "cors",
      headers: { "Content-Type": "text/plain" },
      body,
      keepalive: true
    }).catch(() => {});
  } catch { /* optional measurement only */ }
}
