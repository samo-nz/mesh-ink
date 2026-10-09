# Dual official protocol cores — development integration

The `mesh-agnostic` branch keeps MeshInk's existing reboot-to-switch protocol slots. MeshCore uses the pinned `lib/MeshCore` source. Meshtastic source is pinned to official `meshtastic/firmware` tag `v2.8.1.8e6a88d` in `lib/Meshtastic`.

The third-party Leaf dependency and its old 1,736-line helper and custom wire codec have been removed. `meshtastic/Crypto` remains selected for official Meshtastic's XEdDSA, Curve25519 and AES. No Leaf data migration is required: the previous data is backed up.

The new `src/protocol/meshtastic_protocol.cpp` uses official ToRadio/FromRadio protobufs and the in-process `PhoneAPI` subclass in `meshtastic_official_phoneapi.cpp`. It registers the second MeshInk protocol slot. The native runtime still needs its upstream NodeDB/Router/MeshService/CryptoEngine lifecycle and a MeshInk SX1262 RadioInterface; compilation and radio operation must be established through development CI and device testing. No special release gates or warning UI are added; use the normal development cycle.

MeshInk continues to own e-paper UI, input, GPS, RTC, radio electrical details, storage, battery, and reboot-based protocol selection. Neither protocol's network implementation should be copied into shared UI code.

## Hardware-neutral integration boundary

The official Meshtastic implementation is an upstream submodule, not a copied
board firmware. `include/meshtastic_compat/variant.h` is deliberately
headless and pin-free; the ESP32-S3/SX1262 hardware services remain owned by
MeshInk. The existing `unified_main.cpp` setup/loop controls the splash,
per-protocol startup, GPS/RTC/power ordering, radio lifecycle and UI handoff.
The Meshtastic runtime adapter must initialize upstream NodeDB, MeshService,
Router and the official SX1262Interface from that same boot callback, with
one exclusive radio owner and no second Arduino setup/loop.

Deep-sleep wake hooks can be populated later; do not duplicate MeshCore's
board-specific wake code inside the official Meshtastic engine.

## Shared native-radio device contract

`hardware/radio_types.h` defines `MeshInkSX126xModuleConfig` with no T5
GPIO constants. The board backend implements `meshink_radio_native_module_config()`,
`meshink_radio_native_spi_bus()` and `meshink_radio_prepare_native_spi_bus()`.
The future native Meshtastic radio factory should create upstream
`SX1262Interface` using those three hardware calls, configure its TCXO and
DIO2 RF switch, and attach it to the official `Router`. It must never call
the MeshCore-specific `meshink_radio_initialize()`.

This abstraction also prepares the future retained-SX1262 deep-sleep path
without implementing it before normal messaging works.

## Curated PlatformIO upstream build

`tools/prepare_meshtastic_library.py` runs before PlatformIO library discovery
and writes a disposable `library.json` into the checked-out Meshtastic
submodule. Its source whitelist is maintained in
`tools/official_meshtastic_library.json`, compiling only native official
Meshtastic networking and dependencies, not the upstream display, USB,
Bluetooth, power-manager or standalone main program. The upstream commit stays
pinned and all permanent changes stay in MeshInk.

`MESHTASTIC_EXCLUDE_POWER_FSM` and `MESHTASTIC_EXCLUDE_GPS` prevent a second
sleep controller and a second GPS device manager. MeshInk still owns those
physical services. Additional linker/runtime adaptation is expected before
encrypted radio networking is hardware-testable.
