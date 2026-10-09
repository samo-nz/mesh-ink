# Official Meshtastic runtime integration - H752-01

Status: **experimental source build only**. The production MeshInk branch and its Leaf backend remain unchanged. This is NOT a flashable replacement.

The official Meshtastic H752-01 board supports the same SX1262 GPIOs, 2.4 V TCXO and GPS serial pins as MeshInk. We begin by building a **headless** version of upstream 2.8.1, compiled with Meshtastic's own exclusion flags rather than deleting upstream networking code. The overlay intentionally avoids the official screen, InkHUD, touchscreen and UI startup.

## Reproduce on a development computer

1. Clone official meshtastic/firmware at tag v2.8.1.8e6a88d as official-meshtastic.
2. Run: python tools/official_meshtastic/prepare_headless.py official-meshtastic
3. Run: cd official-meshtastic && pio run -e meshink-h752-headless

Only the new board-variant files are created in the temporary clone. The upstream source is otherwise unchanged.

**DO NOT FLASH THE RESULTING BINARY.** The standalone official build uses its own 16 MB flash partitions, not MeshInk's existing two 6 MB OTA partitions. Flashing it over MeshInk could destroy messages, protocol journals, maps, settings and backup data. The workflow only compiles and reports size.

## Integration work still required before Leaf can be removed

- Adapt official Meshtastic setup/loop into a selectively booted MeshInk backend, with no duplicate entry point.
- Retain upstream NodeDB, routing, security, message handling and PhoneAPI; make MeshInk the in-process API client.
- Share exactly one SX1262 driver, GPS parser, RTC and low-power controller between MeshCore and Meshtastic.
- Use official NodeDB as Meshtastic's authoritative node state; map PhoneAPI updates to the existing UiDataProvider, UI messages, contacts, maps and channel widgets.
- Preserve the current MeshInk 250-message-per-protocol journals, flash partitions, identities and NVS settings. Define a reversible data migration and test before writing any new persistent NodeDB files.
- Test radio, signed NodeInfo, ACKs, direct and channel messages, deep sleep, memory, CPU and current draw on real H752-01 hardware.
- Only after that: remove libmeshtastic-leaf from the production dependencies, remove its code and compare the resulting binary size.

This branch is intentionally a safe engineering stage, not a completed Meshtastic engine migration.
