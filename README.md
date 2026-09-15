# MeshCore T5 Pro

A new firmware project for the LILYGO T5 E-Paper S3 Pro (915 MHz hardware with GPS). MeshCore is an unmodified Git submodule at `lib/MeshCore`, tracking the upstream [`meshcore-dev/MeshCore`](https://github.com/meshcore-dev/MeshCore) main branch. Our radio board, local UI, BLE companion, storage and power code live outside that submodule.

`t5-pro` is a **build gate**, not usable MeshCore firmware. `t5-companion` builds upstream MeshCore's BLE companion example against our independent T5 board adapter. It initializes the e-paper once to print “MESHCORE / BT COMPANION MODE”, powers the panel off and releases its shared pins before MeshCore initializes the SX1262. No continuous display or touch task runs. The previous PaperUI archive is only a hardware reference; none of its application or UI source is imported here.

The product target has two persisted boot modes. Handheld mode runs MeshCore locally and presents an e-paper/touch interface with the Android companion app's contacts, channels, conversations, compose and settings flow. Bluetooth companion mode leaves the native UI inactive and exposes the official MeshCore companion interface. Each mode starts only the hardware and tasks it needs. NZ radio preset support includes 917.375 MHz and must be verified with hardware before normal transmission.

## Build

Run `git submodule update --init --recursive`, then `pio run -e t5-companion`. CI uploads `t5-pro-companion-alpha.bin` and its SHA-256 digest. To remove board checkpoint logs in later builds, change `-DT5_DIAGNOSTICS=1` to `-DT5_DIAGNOSTICS=0` in `platformio.ini`; all our diagnostic lines start `[T5]` at 115200 baud. MeshCore's own logging is independent. The submodule and external e-paper driver are pinned for repeatable builds.

The companion alpha has not yet been verified on hardware. The display refresh, LoRa pin handoff, BLE app compatibility, GPS wiring, and battery draw all need device tests. The handheld native UI and persisted mode selection are still future work. Its current BLE pairing PIN is `123456` for initial debugging and should be changed before regular use.
