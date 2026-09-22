# MeshInk

<p align="center">
  <img src="docs/file_00000000248c820a81311144dc2df47d.png" alt="MeshInk — Stay Connected, Further" width="720">
</p>

**MeshInk is standalone MeshCore firmware for the LILYGO T5 E-Paper S3 Pro.**

It provides a touchscreen interface for messaging, channels, contacts, maps and node management directly on the T5, without requiring a phone for normal use. Bluetooth companion mode is also available when you want to use the standard MeshCore apps.

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

Prebuilt firmware is produced by the GitHub Actions workflow for the `t5-unified` target. For a normal update, use the versioned application image. A complete image is also produced for first-time installation or when the flash layout needs to be installed from scratch.

> **Radio settings matter.** Select the preset appropriate for your country and local MeshCore network before transmitting.

## Using MeshInk

On first setup, choose a node name and radio preset. MeshInk then opens into the main Contacts view.

The bottom navigation provides **Contacts**, **Channels**, **Maps** and **More**. From More you can discover nodes, advertise your node, open Settings, or restart into Bluetooth companion mode.

A long press of the BOOT button enters or leaves standby. A short press forces a clean e-paper redraw.

## Building from source

The main firmware environment is:

```sh
git submodule update --init --recursive
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
