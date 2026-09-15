# MeshCore T5 Pro

A new firmware project for the LILYGO T5 E-Paper S3 Pro (915 MHz hardware with GPS). MeshCore is an unmodified Git submodule at `lib/MeshCore`, tracking the upstream [`meshcore-dev/MeshCore`](https://github.com/meshcore-dev/MeshCore) main branch. Our radio board, local UI, BLE companion, storage and power code live outside that submodule.

The first committed binary is a **build gate**, not usable MeshCore firmware: it only checks that the ESP32-S3 toolchain can compile a program referencing the pinned upstream MeshCore library. It does not initialize LoRa, GPS, BLE, touch or the display. Do not flash it to test messaging. The previous PaperUI alpha archive is only a hardware reference; none of its application or UI source is imported here.

The product target has two persisted boot modes. Handheld mode runs MeshCore locally and presents an e-paper/touch interface with the Android companion app's contacts, channels, conversations, compose and settings flow. Bluetooth companion mode leaves the native UI inactive and exposes the official MeshCore companion interface. Each mode starts only the hardware and tasks it needs. NZ radio preset support includes 917.375 MHz and must be verified with hardware before normal transmission.

## Build

Run `git submodule update --init --recursive`, then `pio run -e t5-pro`. CI runs the same build and uploads a binary and SHA-256 digest. The submodule is pinned to a known main commit so builds are repeatable; updating MeshCore is a separate submodule-pointer commit followed by full radio/BLE/device validation.

The build gate intentionally does not claim battery-life, UI-latency or radio compatibility results. Those require a usable firmware and measurements on the T5 Pro.
