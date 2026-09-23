# MeshInk

<p align="center">
  <img src="docs/file_00000000248c820a81311144dc2df47d.png" alt="MeshInk — Stay Connected, Further" width="720">
</p>

**MeshInk is standalone MeshCore firmware for the LILYGO T5 E-Paper S3 Pro.**

It provides a touchscreen interface for messaging, channels, contacts, maps and node management directly on the T5, without requiring a phone for normal use. Bluetooth companion mode is also available when you want to use the standard MeshCore apps.

## Boot artwork and storage initialization (v1.5.4)

The splash and About screens use the original
`docs/file_00000000248c820a81311144dc2df47d.png` artwork.
Before either UI target builds, `tools/generate_logo.py` converts the image
into a flash-resident, 520×347, 2-bit grayscale bitmap. The device does
not decode an SVG or PNG at boot.

The boot splash normally displays **STARTING UP...** beneath the logo. After
the radio initializes, MeshInk tests whether the existing SPIFFS filesystem
mounts without formatting. Only if that mount fails does the splash change
to **INITIALISING STORAGE...**, before the existing format-on-failure path
runs. This covers a blank filesystem after a full-wipe installation and
recovery after filesystem corruption; ordinary subsequent boots keep the
shorter generic message. Formatting blank SPIFFS may take around 20 seconds.

The setup/home screen and touch sampler start only after storage and mesh
initialization finish, so taps made during the splash cannot be replayed into
the setup keyboard. An update flash preserves existing settings and messages;
a full-wipe flash erases them. Radio and GPS behavior are unchanged.

## Serial logging and battery use (v1.5.5)

The normal local UI and Bluetooth companion builds use
`-DT5_DIAGNOSTICS=0`. They print a short boot summary (firmware, GPS
receiver, filesystem and mesh startup) plus relevant hardware, storage and
operation errors. Continuous touch coordinates, display refresh traces,
message/radio counters, GPS fix and OFF-state probe reports, and periodic
battery/charger diagnostic polling are off in production. The GPS software
watchdog and OFF-state UART draining **remain active**: removing diagnostics
must not change the receiver's normal operation.

To investigate a problem, change the corresponding target's
`-DT5_DIAGNOSTICS=0` in `platformio.ini` to `-DT5_DIAGNOSTICS=1`, then
rebuild/reflash; this restores the detailed diagnostic build. For targeted
logs without the full diagnostic output, leave it at 0 and add one build
flag under `[env:t5-unified]`: `-DT5_LOG_GPS=1`,
`-DT5_LOG_TOUCH=1`, `-DT5_LOG_POWER=1`, `-DT5_LOG_UI=1`,
`-DT5_LOG_MESH=1`, `-DT5_LOG_MAP=1`, or `-DT5_LOG_BOARD=1`.
The global flag also enables detailed RTC, battery gauge and GPS UART
probes. Remove the temporary flag and rebuild for production.

This reduces serial formatting/transmission and diagnostic polling;
**battery-life improvement has not been measured**. The GPS and LoRa
still share their power rail, and receiver current is not reduced merely
by disabling serial debug logs. Boot-time ESP-IDF/EPDiy warnings may still
print independently of MeshInk's log settings.

## Touch, display and maps refinements (v1.5.8)

A short press of the physical **BOOT** button refreshes the *current* screen
without changing navigation or keyboard state. A capacitive **Home** press
returns to Contacts (or Welcome during unfinished setup), clears pending
touches, and prevents the Home release from being replayed as an ordinary
tap. Holding BOOT for two seconds still enters/leaves standby.

The disabled-GPS slash and message-envelope strokes are thicker. Maps
has 66×66 zoom and **ME** locate buttons (previously 44×44), with matching
touch areas and a visible black-on-white locate icon. Opening Maps from its
tab centres on the device's current GPS location if valid, or the last
verified location if available. Last verified coordinates are retained in
local preferences, with infrequent writes for battery and flash life; they
are a map-navigation fallback only, never represented as a live GPS fix.
Opening a *specific contact's* position on the map does not override that
explicit centre. The own-device map marker uses the same thick target icon
as the GPS-fix status indicator.

New/erased devices default to **30% frontlight brightness**. Firmware updates
do **not** overwrite an existing saved brightness level; change that in
Settings → Display & Power if you want 30% on an already-configured device.

The firmware build also runs `python tools/check_ui_regressions.py` to guard
the key screen state and map-button geometry. These source-level checks and
the compile do not replace a touch, e-paper, or GPS test on physical hardware.

## Radio presets and clean-install setup (v1.5.7)

On a clean/full-wipe installation, MeshCore's compiled radio defaults can
differ from the preset displayed on the welcome screen. Before the first
interactive setup screen, MeshInk now applies the selected preset to MeshCore
(including frequency, spreading factor, bandwidth, coding rate and path-hash
size). The default welcome-screen preset is **NZ NARROW** (917.375 MHz,
SF7, BW62.5 kHz, CR5, 2-byte path hash). Other presets apply their own
displayed parameters when selected. **KEEP CURRENT** deliberately makes no
radio changes.

Radio presets have explicit numeric settings rather than parsing their UI
description strings. `python tools/check_radio_presets.py` verifies all 28
entries (27 configured presets and KEEP CURRENT), including each displayed
frequency, bandwidth, SF, CR and path-hash size; the release build runs
this check automatically. A preset that fails to apply is not selected or
saved. Normal boots after setup is complete do not override the existing
saved radio configuration.

These checks validate firmware values, not regional legal requirements,
over-the-air compatibility or measured LoRa performance. For communication,
other nodes still need compatible RF parameters and matching public-channel
settings. Use the **update** firmware to retain your existing MeshCore
settings/messages; the full-wipe image clears them.

## Features

- Direct messages and channel messaging
- Contacts and node information
- Node discovery and advertisements
- Offline maps with mesh-node positions
- GPS and location sharing
- MeshCore Bluetooth companion mode
- Configurable LoRa region and radio presets
- E-paper interface with frontlight controls
- Low-power standby with long-press wake/sleep
- New-message indication while in standby
- Local message history
- On-device configuration for node, radio, GPS, display and power settings

## Hardware

MeshInk currently targets the **LILYGO T5 E-Paper S3 Pro**, using its 4.7-inch 540×960 e-paper touchscreen, ESP32-S3, SX1262 LoRa radio and GPS.

## Installing

Download the latest release from the [MeshInk Releases](https://github.com/samo-nz/mesh-ink/releases) page. Releases contain only the unified MeshInk firmware:

- **`meshink-VERSION-update.bin`** — use this when MeshInk is already installed. Flash it at **0x10000**. It updates the application and leaves the rest of flash alone.
- **`meshink-VERSION-full-wipe.bin`** — use this for a new device or a completely clean reinstall. Flash it at **0x0**. This image is padded to the full 16 MB flash, so it overwrites old settings, messages and other stored data.

A data-capable USB cable is required. If the computer does not see the T5 in flashing mode, hold **BOOT**, press and release **RESET**, then release **BOOT**.

### Windows

1. Install [Python](https://www.python.org/downloads/windows/) if it is not already installed. During installation, enable **Add Python to PATH**.
2. Open **Command Prompt** in the folder containing the downloaded `.bin` file.
3. Install Espressif's flashing tool:

   ```bat
   py -m pip install esptool
   ```

4. Connect the T5 by USB and put it into flashing mode if necessary. In **Device Manager → Ports**, note its COM port, for example `COM5`.
5. For a normal update:

   ```bat
   py -m esptool --chip esp32s3 -p COM5 write-flash 0x10000 meshink-VERSION-update.bin
   ```

   For a new installation / complete wipe:

   ```bat
   py -m esptool --chip esp32s3 -p COM5 write-flash 0x0 meshink-VERSION-full-wipe.bin
   ```

6. Press **RESET** after flashing if the T5 does not restart automatically.

### macOS

1. Install [Python 3](https://www.python.org/downloads/macos/) if needed, then open **Terminal**.
2. Install esptool:

   ```sh
   python3 -m pip install --user esptool
   ```

3. Connect the T5 and put it into flashing mode if necessary. Find its port with:

   ```sh
   ls /dev/cu.*
   ```

   It will normally look like `/dev/cu.usbmodem...` or `/dev/cu.usbserial...`.

4. Change to the folder containing the downloaded firmware and flash either the update:

   ```sh
   python3 -m esptool --chip esp32s3 -p /dev/cu.YOUR_PORT write-flash 0x10000 meshink-VERSION-update.bin
   ```

   or the full wipe:

   ```sh
   python3 -m esptool --chip esp32s3 -p /dev/cu.YOUR_PORT write-flash 0x0 meshink-VERSION-full-wipe.bin
   ```

5. Press **RESET** if the T5 does not restart automatically.

### Linux

1. Open a terminal and make sure Python 3 and pip are installed. On Debian/Ubuntu this is:

   ```sh
   sudo apt install python3 python3-pip
   python3 -m pip install --user esptool
   ```

2. Connect the T5 and put it into flashing mode if necessary. Find its port with:

   ```sh
   ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null
   ```

3. Change to the folder containing the downloaded firmware and flash either the update:

   ```sh
   python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 write-flash 0x10000 meshink-VERSION-update.bin
   ```

   or the full wipe:

   ```sh
   python3 -m esptool --chip esp32s3 -p /dev/ttyACM0 write-flash 0x0 meshink-VERSION-full-wipe.bin
   ```

4. If you get a serial-port permission error, add your user to the serial-port group (commonly `dialout` on Debian/Ubuntu), log out and back in, then retry.
5. Press **RESET** if the T5 does not restart automatically.

If esptool reports that it cannot connect, repeat the BOOT/RESET sequence and try again. Espressif's [ESP32-S3 boot-mode guide](https://docs.espressif.com/projects/esptool/en/latest/esp32s3/advanced-topics/boot-mode-selection.html) has additional troubleshooting information.

> **Radio settings matter.** Select the preset appropriate for your country and local MeshCore network before transmitting.

## Using MeshInk

On first setup, choose a node name and radio preset. MeshInk then opens into the main Contacts view.

The bottom navigation provides **Contacts**, **Channels**, **Maps** and **More**. From More you can discover nodes, advertise your node, open Settings, or restart into Bluetooth companion mode.

A long press of the BOOT button enters or leaves standby. A short press returns to Contacts (Home).

### GPS status and configuration (v1.5.0)

The bold GPS status icon indicates OFF, searching or fixed. Its satellite
count is shown **only when the device is awake and has a current fix**.
The number is hidden in standby and while GPS is OFF or searching.

On the 9600-baud L76K, MeshInk automatically requests compact **RMC + GGA**
NMEA output at the unchanged positioning rate whenever the GPS provider
starts. The former NMEA settings toggle and its saved preference are ignored.
For receiver troubleshooting, developers can compile with
`-DT5_GPS_FULL_NMEA_DIAGNOSTIC=1` to request full standard NMEA output
instead; this option is not present in the on-device settings.
The MIA-M10Q receiver is not sent these PCAS commands.

The **GPS POWER SAVING** screen retains a user-selectable constellation
setting: unchanged (no constellation command), GPS only, GPS + GLONASS,
GPS + BeiDou or all three. The choice is stored locally. GPS-only can
reduce receiver workload but may degrade reception in obstructed locations.
Compact NMEA reduces UART and host parsing workload. Neither change has
a verified measured effect on receiver current or battery life.

**GPS OFF and timed position intervals stop the MeshCore software GPS
provider; they do not electrically power down the receiver.** LoRa and
GPS share a switched 3.3 V supply, so MeshInk never disables it to turn
off GPS. The previously unsuccessful PCAS12 and PMTK161 standby
experiments are not used. The radio remains active in normal standby.

## Building from source

The main firmware environment is:

```sh
git submodule update --init --recursive
python -m pip install Pillow
pio run -e t5-unified
```

MeshCore is kept as an upstream submodule. T5 board support and MeshInk's UI/runtime code are kept outside it to make upstream updates easier to integrate.

The firmware version is defined by the `T5_FIRMWARE_VERSION` build flag for the unified target.

## Development

**MeshInk was developed by Samo using ChatGPT.**

The project grew from experimentation with the LILYGO T5 hardware and MeshCore into a standalone e-paper MeshCore client.

## Acknowledgements

MeshInk is built on the work of several open-source projects. Many thanks to their developers and contributors:

- [MeshCore](https://github.com/meshcore-dev/MeshCore) — mesh networking protocol and core firmware that MeshInk is built around.
- [EPDiy](https://github.com/vroland/epdiy) — e-paper display support.
- [PNGdec](https://github.com/bitbank2/PNGdec) — embedded PNG decoding.
- [miniz](https://github.com/richgel999/miniz) — compression and decompression support.
- [MicroNMEA](https://github.com/stevemarple/MicroNMEA) — GPS/NMEA parsing.
- [Adafruit BusIO](https://github.com/adafruit/Adafruit_BusIO) — hardware bus abstractions.
- [base64](https://github.com/Densaugeo/base64_arduino) — Base64 encoding and decoding.

Thank you to everyone who develops and maintains these projects. MeshInk would not exist without their work.
