# MeshCore T5 Pro

A new firmware project for the LILYGO T5 E-Paper S3 Pro (915 MHz hardware with GPS). MeshCore is an unmodified Git submodule at `lib/MeshCore`, tracking the upstream [`meshcore-dev/MeshCore`](https://github.com/meshcore-dev/MeshCore) main branch. Our radio board, local UI, BLE companion, storage and power code live outside that submodule.

`t5-pro` is a **build gate**, not usable MeshCore firmware. `t5-companion` builds upstream MeshCore's BLE companion example against our independent T5 board adapter. It physically clears the e-paper, prints “MESHCORE / BT COMPANION MODE” once, powers the panel off and releases its shared pins before MeshCore initializes the SX1262. The T5's BQ27220 fuel gauge supplies battery voltage to the companion protocol, with a 30-second read cache. No continuous display or touch task runs. The previous PaperUI archive is only a hardware reference; none of its application or UI source is imported here.

The product target has two persisted boot modes. Handheld mode runs MeshCore locally and presents an e-paper/touch interface with the Android companion app's contacts, channels, conversations, compose and settings flow. Bluetooth companion mode leaves the native UI inactive and exposes the official MeshCore companion interface. Each mode starts only the hardware and tasks it needs. NZ radio preset support includes 917.375 MHz and must be verified with hardware before normal transmission.

## Build

Run `git submodule update --init --recursive`, then `pio run -e t5-companion`. CI uploads a versioned `t5-pro-companion-0.0.1.bin` and its SHA-256 digest. All future release binaries use `major.milestone.release`: increase the rightmost digit for every new test release, the middle digit when a functional milestone is achieved, and the first digit for a major firmware milestone. Update `T5_FIRMWARE_VERSION` in the workflow for each release and never reuse a published binary filename. To remove board checkpoint logs in later builds, change `-DT5_DIAGNOSTICS=1` to `-DT5_DIAGNOSTICS=0` in `platformio.ini`; all our diagnostic lines start `[T5]` at 115200 baud. MeshCore's own logging is independent. The submodule and external e-paper driver are pinned for repeatable builds.

The Actions ZIP also contains `t5-pro-companion-0.0.1-complete.bin` with the correct **16 MB flash header**, plus the matching bootloader, partition table and Arduino OTA initializer. A first installation can flash the complete image at `0x0`. An app-only upload of `t5-pro-companion-0.0.1.bin` at `0x10000` assumes this partition layout and corrected bootloader are already installed. Replacing a partition table may make old contacts/messages inaccessible; keep a backup if needed. Monitor serial at 115200 baud and capture `[T5]` lines through MeshCore BLE startup.

Companion mode is now a hardware-verified milestone and remains available as
the `t5-companion` target. The `t5-ui-onboarding` target begins milestone 0.1.0
with Bluetooth disabled: a MeshCore-style welcome screen, region presets,
device-name entry and touch OSK. Keeping these as separate targets during UI
bring-up prevents experimental display/input work from destabilizing the
working companion build. They will later be joined behind the persisted boot
mode described above, selected before BLE or UI resources are allocated.

Starting with 0.1.1, `t5-unified` is the product test target. Local UI is the
default. Its menu can set a one-shot flag and restart into the verified Bluetooth
companion application; that flag is consumed at boot, so the next restart returns
to local UI. In companion mode only, holding BOOT for two seconds immediately
restarts into local UI; releasing the button is not required. In UI mode the
frontlight remains steadily on and is independent of e-paper refreshes until a
user-selectable frontlight policy is added.
Both paths display the same centrally defined firmware version.

Release 0.1.3 gives both boot splashes matching title/version geometry and adds
the companion BOOT-button exit instruction. The onboarding keyboard is now a
reusable show/hide component with a top number row, upper/lowercase shift and a
symbols layer. Local UI screens no longer repeat Bluetooth state or the firmware
version after the splash.

Milestone 0.2.0 introduces the persistent local-UI status bar. It reads the
onboard RTC and BQ27220 fuel gauge, reserves live unread-message and GPS state
slots, and remains present on every post-boot screen. The reusable keyboard is
anchored flush to the display bottom with larger key labels.

Release 0.2.1 replaces status labels with compact GPS, direct-message, channel
and battery icons, enlarges the status typography, centres the clock, and adds
a reusable 1.5-second high-contrast toast overlay for action confirmations.

Milestone 0.4.0 starts the live upstream MeshCore runtime in local UI mode with
Bluetooth disabled. Contacts, channels, message reception/transmission, GPS
state, identity, radio values and app-derived contact/privacy settings now use
MeshCore data and preferences. The 0.3.0 navigable inspection UI remains the
presentation layer, with larger conversation text. The adapter code stays
outside the MeshCore submodule so upstream updates remain straightforward.

Milestone 0.3.0 was the complete, navigable local-UI inspection prototype based on
the open-source MeshCore companion application's information architecture. It
adds contacts, direct and channel conversations, contact details, channels,
discovery, settings, radio, GPS, display/power and about screens with realistic
mock data. Map and external-node connection flows are intentionally omitted.
All screen data is read through `UiDataProvider`, allowing a later MeshCore
provider to replace the mock provider without coupling protocol code to views.
