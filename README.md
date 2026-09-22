# MeshInk

<p align="center">
  <img src="docs/meshink-logo.svg" alt="MeshInk — Stay Connected, Further" width="720">
</p>

**MeshInk turns the LILYGO T5 E-Paper S3 Pro into a standalone MeshCore handheld.** Read and send messages, see nearby nodes, use offline maps, manage GPS and radio settings, and keep the device useful for long periods without needing a phone connected.

It is designed around the T5's large 4.7-inch e-paper touchscreen: information stays visible without constantly powering the display, the frontlight is there when you need it, and a dedicated standby mode cuts background activity while the mesh remains useful.

## What MeshInk can do

- **Standalone MeshCore messaging** — contacts, direct messages and channel conversations directly on the T5.
- **Touch-first e-paper interface** — large controls and layouts designed specifically for the 540×960 display.
- **Offline maps** — view your own position and known mesh nodes without an internet connection.
- **GPS built in** — position, fix status, location sharing controls and configurable GPS behaviour.
- **Mesh discovery** — see recently heard nodes, inspect their details and send zero-hop or flood adverts.
- **Bluetooth companion mode** — restart into the standard MeshCore BLE companion mode when you want to use a phone or another companion app.
- **Regional radio presets** — selectable MeshCore presets, including NZ Narrow and other supported regions.
- **Battery-friendly operation** — e-paper, configurable frontlight behaviour, automatic standby and a manual long-press standby/wake control.
- **Message alerts** — visual e-paper/frontlight indication for new messages while the device is in standby.
- **Device settings on the T5** — node name, radio, privacy, GPS, timezone, display, frontlight and power options are available without reflashing.
- **Persistent local history** — recent conversations remain available on the device.
- **MeshInk identity** — the MeshInk splash and About screen show the firmware version and configured node name.

## The idea

MeshInk is for people who want MeshCore to feel like a self-contained field communicator rather than a radio peripheral. The phone connection is optional: the T5 can be the interface.

The project uses upstream MeshCore for the mesh networking itself, while the T5-specific interface, display, storage, power management, GPS integration and hardware support live in this repository.

## Hardware

MeshInk currently targets the **LILYGO T5 E-Paper S3 Pro** with the 4.7-inch e-paper display, ESP32-S3, SX1262 LoRa radio and GPS hardware used by this project.

## Installing

Prebuilt firmware is produced by the GitHub Actions workflow for the `t5-unified` target. For a normal update, use the versioned application image. A complete image is also produced for first-time installation or when the flash layout needs to be installed from scratch.

> **Radio settings matter.** Select the preset appropriate for your country and local MeshCore network before transmitting.

## Using MeshInk

On first setup, choose a node name and radio preset. After that, MeshInk opens into the main Contacts view. The bottom navigation provides **Contacts**, **Channels**, **Maps** and **More**.

From **More** you can discover nodes, advertise your node, open Settings, or restart into Bluetooth companion mode. A long press of the BOOT button enters or leaves standby; a short press forces a clean e-paper redraw.

## Building from source

For developers, the main product environment is:

```sh
git submodule update --init --recursive
pio run -e t5-unified
```

MeshCore is kept as an upstream submodule. T5 board support and MeshInk's UI/runtime code are kept outside it so upstream MeshCore updates remain easier to integrate.

The current firmware version is defined by the `T5_FIRMWARE_VERSION` build flag for the unified target. Release workflows use that version when naming binaries.

## Project status

MeshInk is approaching its first finished release. The core handheld experience is working: live MeshCore data, messaging, channels, discovery, GPS, offline maps, radio/settings UI, Bluetooth companion mode, standby/power controls and device-local history are all part of the current firmware.

Hardware testing is still important, especially after changes affecting radio behaviour, e-paper refresh, GPS, power management or flash layout.

## Credits

MeshInk builds on **MeshCore** and the open-source libraries used by the LILYGO T5 ecosystem. MeshCore remains an upstream dependency rather than a fork embedded into the application code.

---

**MeshInk — Stay Connected, Further.**
