# MeshInk hardware portability architecture

MeshInk should be portable to another ESP32 + e-paper + LoRa device by adding
a hardware package, not by editing application screens, conversations, maps,
or MeshCore behavior throughout the project.

The original LILYGO H752 and the M5Stack PaperMono are design probes for this
architecture only. Neither board is implemented by this document.

## Design rules

1. Application code must not know GPIO numbers, I2C addresses, SPI bus choices,
   PMIC register layouts, display-controller APIs, or framebuffer packing.
2. Chip-specific libraries stay behind hardware interfaces.
3. Board packages compose reusable drivers and own sequencing: power rails,
   reset order, bus setup, wake/shutdown, and shared-peripheral arbitration.
4. Optional hardware is expressed through capabilities rather than scattered
   board-name conditionals.
5. A new board should initially be selected at compile time. Runtime board
   autodetection is optional later and must never risk driving an unknown
   display or power circuit.
6. The currently field-tested H752-01/V2 implementation remains the regression
   reference while interfaces are introduced incrementally.

## Target layering

```text
MeshInk application
  UI / messages / contacts / maps / settings / companion mode
                         |
                  hardware services
                         |
       +-----------+-----+------+-----------+
       |           |            |           |
    Display      Input        Radio       Power
    Storage      Location     RTC         Frontlight
       |           |            |           |
       +-----------+---- board package -----+
                         |
                    physical board
```

A board package should be small. It selects implementations, declares
capabilities, owns bus/power sequencing, and contains the electrical mapping.
It should not contain MeshInk screen or messaging logic.

A future directory layout may evolve toward:

```text
src/
  app/                         # board-independent MeshInk logic
  hardware/
    interfaces/                # stable component contracts
    drivers/                   # reusable chip/controller implementations
      sx1262/
      gt911/
      ...
    boards/
      lilygo_t5_h752_01/       # field-tested board package
      <future-board>/
```

The exact names can change during refactoring; the separation is the important
part.

## Component contracts

### Display

The application should draw through a display/rendering service rather than
EPDiy functions or a controller-specific framebuffer.

The contract needs:

- native width and height
- logical orientation
- grayscale capability
- framebuffer or drawing surface access through an implementation-neutral type
- pixel, rectangle and image/blit primitives
- full refresh
- optional partial refresh
- panel power/sleep lifecycle
- capability reporting such as partial-refresh support

Display code must own physical framebuffer packing. Application and map code
must not assume two 4-bit pixels per byte, EPDiy rotation rules, a fixed
960x540 physical panel, or a specific waveform API.

For performance-sensitive Maps rendering, expose a bulk primitive such as a
grayscale span/tile blit. A display backend may provide an optimized version;
the generic fallback can use normal drawing primitives.

### UI geometry

Screen size is part of hardware capability, not an application constant.

The UI should progressively move from raw 540x960 coordinates toward shared
layout metrics derived from the current viewport. This does not require a
responsive web-style layout; it requires a small set of consistent margins,
header/status/navigation heights, list-row sizes, keyboard geometry and map
viewport bounds.

This is necessary for future panels that are not 540x960 in portrait.

Touch coordinates must be transformed into the same logical coordinate system
before they reach screen-specific handlers.

### Input / touch

The input service should report logical touch/button events.

It owns:

- controller initialization
- reset/interrupt handling
- coordinate rotation/transformation
- touch enable/disable for standby
- physical button mapping

The application should not care whether touch is GT911, FT6336-class, or
something else.

### Radio

MeshCore may continue to use RadioLib/SX1262, but board electrical control must
sit behind a radio-platform interface.

The board-facing operations need to support more than direct GPIOs:

- initialize/select SPI bus
- radio power on/off
- reset
- IRQ attach/detach
- busy/NSS access as required by the radio driver
- antenna/RF switch mode where the board requires it

These operations may be implemented with direct GPIO, an I/O expander, a PMIC,
or another controller. Application code must not assume which.

Radio preset/frequency behavior remains MeshInk/MeshCore logic and should not
be duplicated in board packages.

### Power

Power is a board service, not a collection of PMIC register calls in UI code.

Expose concepts such as:

- battery voltage/percentage when available
- external-power/charging state
- low-battery capability and confidence
- peripheral rail control
- frontlight power/brightness if coupled to the power system
- shutdown / ship mode
- wake-source preparation

A board may have a fuel gauge, an ADC-only battery measurement, a charger/PMIC,
or no meaningful battery telemetry. The application must degrade gracefully.

### Storage

Storage should expose mount/readiness and a filesystem used by Maps.

The board package owns:

- SD chip select
- SPI bus selection
- safe bus initialization
- optional card-detect/power control

Map archive and PNG logic should remain independent of the board.

### Location

Location is optional.

The service may be backed by:

- onboard GNSS
- an external GNSS provider
- MeshCore's saved static node location
- no live location hardware

UI capability decisions should depend on what is available, not on a board
name. Maps remain useful even when live self-location is unavailable.

### RTC

The application should consume one clock service. A board package can provide
a hardware RTC or an appropriate fallback implementation.

### Frontlight

Treat frontlight as a capability/service rather than a fixed PWM GPIO. Some
boards may expose PWM directly, some through a controller, and some not at all.

## Capability model

Prefer capabilities such as:

```text
touch
frontlight
removable_storage
live_gnss
static_location
hardware_rtc
battery_voltage
battery_percentage
charging_state
hardware_shutdown
display_partial_refresh
display_gray_levels
```

Avoid UI tests such as `if board == H752` or `if board == PaperMono`.
Screens should react to capabilities.

## Porting checklist for a future board

A first port should ideally require only:

1. Create the board package and build environment.
2. Implement/select a display backend.
3. Implement/select touch/buttons.
4. Implement the SX1262 board control and SPI binding.
5. Implement power/battery/shutdown behavior.
6. Bind SD/storage and RTC.
7. Bind GNSS if present; otherwise declare it absent.
8. Declare capabilities.
9. Pass common host/CI tests.
10. Validate the board-specific hardware on a physical device.

No Contacts, Conversations, Maps business logic, settings navigation, message
storage, or MeshCore protocol code should require board-specific edits.

## Design probes

### Original LILYGO H752

The old H752 reminds us that even two products sold under the same T5 family can
have different display buses, power arrangements, radio wiring, battery
measurement and GNSS capability. Its experimental implementation is preserved
separately and should not drive the architecture through board-name checks.

### M5Stack PaperMono

PaperMono is a useful portability target because it combines an ESP32-S3,
e-paper, touch, microSD and SX1262 but with substantially different supporting
hardware and a different display resolution/controller.

The architecture therefore must allow:

- a different e-paper controller and physical resolution
- a different touch controller
- LoRa reset/antenna/power actions implemented through controller devices
  rather than only direct GPIO
- a dedicated LoRa SPI bus
- board-owned power-management sequencing
- no assumption that onboard GNSS exists

No PaperMono implementation should be added until the common interfaces are
stable enough that the port is mostly a new hardware package.

## Development policy

- `main`: released, hardware-validated behavior.
- `testing`: portability refactors and features that remain testable on the
  current H752-01/V2 Pro.
- experimental hardware branches: incomplete board ports that must not be
  treated as supported hardware.

Every portability refactor should preserve V2 behavior and pass CI before the
next hardware dependency is extracted.
