# MeshInk

<p align="center">
  <img src="docs/file_00000000248c820a81311144dc2df47d.png" alt="MeshInk — Stay Connected, Further" width="720">
</p>

**MeshInk is standalone MeshCore firmware for the LILYGO T5 E-Paper S3 Pro.**

It provides a touchscreen interface for messaging, channels, contacts, maps and node management directly on the T5, without requiring a phone for normal use. Bluetooth companion mode is also available when you want to use the standard MeshCore apps.

## Screenshots

<table>
  <tr>
    <td align="center"><img src="screenshots/contacts.webp" width="220"><br><sub>Contacts</sub></td>
    <td align="center"><img src="screenshots/conversation.webp" width="220"><br><sub>Conversation</sub></td>
    <td align="center"><img src="screenshots/channels.webp" width="220"><br><sub>Channels</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="screenshots/discovered.webp" width="220"><br><sub>Discovered nodes</sub></td>
    <td align="center"><img src="screenshots/node-info1.webp" width="220"><br><sub>Node information</sub></td>
    <td align="center"><img src="screenshots/node-info2.webp" width="220"><br><sub>Node information — page 2</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="screenshots/maps-mylocation.webp" width="220"><br><sub>Map — my location</sub></td>
    <td align="center"><img src="screenshots/maps-node.webp" width="220"><br><sub>Map — node location</sub></td>
    <td align="center"><img src="screenshots/advertise.webp" width="220"><br><sub>Advertise</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="screenshots/more.webp" width="220"><br><sub>More</sub></td>
    <td align="center"><img src="screenshots/settings.webp" width="220"><br><sub>Settings</sub></td>
    <td align="center"><img src="screenshots/settings-location.webp" width="220"><br><sub>Location settings</sub></td>
  </tr>
  <tr>
    <td align="center"><img src="screenshots/bt-companion.webp" width="220"><br><sub>Bluetooth companion mode</sub></td>
    <td align="center"><img src="screenshots/about.webp" width="220"><br><sub>About</sub></td>
    <td></td>
  </tr>
</table>

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

## Web flasher

The easiest way to install or update MeshInk is with the **[MeshInk Web Flasher](https://samo-nz.github.io/mesh-ink/)**.

1. Open the flasher in a desktop version of **Chrome or Edge**.
2. Connect the T5 with a **USB data cable**.
3. Choose **Update** if MeshInk is already installed, or **Install MeshInk** for a new/clean installation.
4. Select the T5's USB serial port when prompted and follow the on-screen instructions.
5. If the device is not detected, hold **BOOT**, briefly press **RESET**, then release **BOOT** and try again.

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

A long press of the BOOT button enters or leaves standby. A short press refreshes the e-paper display.

### Maps: controls and gestures

Open **Maps** from the bottom navigation. Drag one finger to pan; use **+** and **−** to change zoom, or tap the **target** button to centre on your device's current or last known GPS position. Tap a map node marker to open its details. While **Maps** is displayed, pinch two fingers apart/together to zoom around their starting midpoint, **double-tap** the map to zoom in one level at the first tap's location, or **triple-tap** to zoom out one level at that location. These multi-touch and multi-tap zoom gestures apply **only on Maps**; typing and other screens retain their normal single-tap controls.

Copy 256×256 raster PNG tiles to the SD card as `/maps/Z/X/Y.png`, or put a **PMTiles v3 raster PNG** archive at `/maps/osm-bright/osm-bright.pmtiles`. Archives at `/maps/<name>/<file>.pmtiles` or directly in `/maps/<file>.pmtiles` also work. See [Offline map files](#offline-map-files) for supported formats and limitations.

## Offline map files

MeshInk reads 256×256 raster PNG map tiles from the SD card. Either put loose
tiles in `/maps/Z/X/Y.png`, or copy a **PMTiles v3 raster PNG** archive to
`/maps/osm-bright/osm-bright.pmtiles`, matching the tile downloader's suggested
SD-card layout. Any other `/maps/<name>/<file>.pmtiles` or
`/maps/<file>.pmtiles` layout is also supported. MeshInk discovers up to eight
archives in the maps folder and its immediate subfolders, checking them when
a loose PNG tile is unavailable. Loose tiles take priority; archives are
checked in SD-directory order. The normal parent-zoom fallback and RAM tile
cache still apply.

PMTiles directories may be uncompressed or gzip-compressed. The embedded reader
retrieves only the requested tile bytes from SD rather than unpacking the whole
archive; it supports unwrapped PNG payloads and directory sizes up to 512 KiB
after decompression. The ESP32 SD file API used here supports 32-bit seek
offsets, so tile data beyond 4 GiB is not supported. Vector tiles (MVT),
JPEG/WebP, and separately gzip-compressed tile payloads are **not** rendered.
A typical Protomaps vector `.pmtiles` map therefore needs to be rendered to
raster PNG tiles before it can be used on MeshInk.

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
