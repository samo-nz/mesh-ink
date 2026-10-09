# Official Meshtastic core integration: mesh-agnostic

**Current status: official source and in-process PhoneAPI transport staged, NOT a working radio node.**

## Three layers

- MeshInk owns SX1262 arbitration, GPS, RTC, screen, keyboard, storage policy and power.
- Meshtastic helper translates protocol-native state to MeshInk's shared UI/backend.
- Official Meshtastic owns routing, NodeDB, encryption, channels, ACKs and network protocol behaviour.

`lib/Meshtastic` pins upstream `meshtastic/firmware` at `v2.8.1.8e6a88d` (`8e6a88d06f44cad26f1e8d7cd402939ccacb9b4c`), just like the existing `lib/MeshCore` submodule. Never modify upstream files in place; adapter/glue belongs in MeshInk.

`src/protocol/meshtastic_official_phoneapi.cpp` subclasses the official PhoneAPI for in-process ToRadio/FromRadio commands. **It does not initialize MeshService, NodeDB, Router or the radio**, and is intentionally omitted from the current firmware source filter.

## Work required before replacing Leaf

1. Prove the official core source can compile/link with MeshInk's toolchain. Meshtastic upstream uses a newer PIOArduino/ESP-IDF combination; any toolchain change must preserve MeshInk's PSRAM and hardware behaviour.
2. Extract official NodeDB, routing, encryption, channels, MeshService and scheduling into one MeshInk-owned lifecycle (no second setup/loop).
3. Implement MeshInk `RadioInterface` RX/TX and status callbacks for SX1262 without concurrent MeshCore ownership. Wire GPS/RTC, power and storage to existing services.
4. Connect ToRadio/FromRadio and native features through the Meshtastic helper and UiDataProvider; test preservation/migration of saved Meshtastic data.
5. Pass unified firmware link/6MiB OTA size checks and **hardware** RX/TX, direct PKI, channel encryption, ACK/routing, deep sleep and protocol switching tests.
6. Only then remove `libmeshtastic-leaf`. **Retain `meshtastic/Crypto`**: official CryptoEngine uses AES/CTR, Curve25519 and XEdDSA.

The active firmware still runs Leaf and the existing working MeshCore backend; there is no safe-to-flash official-core binary yet.
