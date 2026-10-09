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

The upstream `main.h` includes headless `Screen.h` which still declares an
OLED geometry enum; we use the **official pinned OLED header dependency** to
compile those declarations without compiling the Meshtastic screen runtime.
`USE_THREAD_NAMES` matches the upstream ArduinoThread configuration.
The build PRE script also applies one fail-closed *temporary checkout* guard
to `Power.h` so its unused IDF5 ADC declarations don't break MeshInk's IDF4
toolchain; the upstream Git submodule reference and protocol implementations
stay untouched. This guard can be removed if the target framework upgrades.

The unified build explicitly selects C++17 for Meshtastic's native
`std::optional` and digit separators, and pins upstream's `ErriezCRC32`
dependency for NodeDB. Both protocol cores compile against the same C++
standard within the firmware; no changes to hardware abstraction.

### Headless protocol modules

The selected upstream module set includes official Admin, PKI key verification,
NodeInfo, NeighborInfo, routing, text messaging, status and TraceRoute.
Board-facing implementations that MeshInk already owns (input, Bluetooth,
serial, displays, battery telemetry, alert hardware) and optional standalone
Paxcounter, StoreForward and sensor/demo modules are disabled at the build
boundary. This avoids pulling extra device-firmware dependencies into the
protocol core; network protocol formats remain upstream.

### Text-message UI ownership

Meshtastic's upstream TextMessageModule also includes its standalone buzzer,
OLED renderer and PowerFSM wake behaviour. The MeshInk build uses an idempotent,
fail-closed compatibility guard in its temporary upstream checkout, compiling
the **unchanged official message handling and observer notifications** while
excluding the separate Meshtastic display/alert/wake block. MeshInk's own
FromRadio provider drives the e-paper UI. The pinned upstream module remains
version-controlled and unmodified.

## Official SX1262 factory (added)

`src/protocol/meshtastic_radio.cpp` constructs **the upstream**
`SX1262Interface` with the shared MeshInk board SPI bus, generic native
module descriptor, 2.4 V T5 TCXO supplied by board code, and the official
`LockingArduinoHal`. It calls native `init()` and transfers the radio's
ownership to `Router::addInterface()`, following the same boot-exclusive
ownership as MeshCore. The T5 board descriptor enables DIO2 RF switching through a small
MeshInk subclass **after upstream SX1262 initialization**, matching the
existing MeshCore/LilyGO post-init order without changing upstream code or
globally changing the MeshCore build flags. No SX1262 packet code is duplicated in MeshInk.

**Lifecycle integration remains:** official `NodeDB`, crypto, SPI locking,
router, and service must be initialized before
`meshink_meshtastic_attach_radio(router)` is invoked. The factory does not
invoke MeshCore's wrapper or create a second `setup()`. Merely compiling
this factory does not make Meshtastic operational.
