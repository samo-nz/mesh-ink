"""Apply the upstream Arduino-ESP32 SD uninit lifetime fix to the pinned core.

Arduino-ESP32 2.0.17's sdcard_uninit() frees the card while AcquireSPI still
owns a pointer to its SPI object; the guard's destructor then uses freed memory.
The upstream fix (espressif/arduino-esp32#10122) ends the transaction before
unregistering and freeing the SD card. This is also necessary on failed SD.begin()
when no card is inserted.

PlatformIO's framework package is patched reproducibly on each build, not
forked or globally upgraded. Refuse unknown source layouts.
"""

from pathlib import Path

Import("env")

framework_dir = env.PioPlatform().get_package_dir("framework-arduinoespressif32")
if not framework_dir:
    raise RuntimeError("MeshInk SD patch: Arduino-ESP32 framework package is missing")

sd_path = Path(framework_dir) / "libraries" / "SD" / "src" / "sd_diskio.cpp"
if not sd_path.is_file():
    raise RuntimeError(f"MeshInk SD patch: expected source missing: {sd_path}")

source = sd_path.read_text(encoding="utf-8")
marker = "MESHINK_SD_UNINIT_LIFETIME_FIX"
old = """    AcquireSPI lock(card);
    sdTransaction(pdrv, GO_IDLE_STATE, 0, NULL);
    ff_diskio_register(pdrv, NULL);
"""
new = """    { // MESHINK_SD_UNINIT_LIFETIME_FIX (arduino-esp32 PR #10122)
        AcquireSPI lock(card);
        sdTransaction(pdrv, GO_IDLE_STATE, 0, NULL);
    } // Release SPI transaction before freeing card.
    ff_diskio_register(pdrv, NULL);
"""

if marker in source:
    if source.count(marker) != 1 or source.count(new) != 1:
        raise RuntimeError("MeshInk SD patch: existing patch does not match expected fix")
    print(f"[MeshInk] Arduino SD lifetime fix already applied: {sd_path}")
elif source.count(old) == 1:
    sd_path.write_text(source.replace(old, new, 1), encoding="utf-8")
    print(f"[MeshInk] Applied Arduino SD lifetime fix: {sd_path}")
else:
    raise RuntimeError(
        "MeshInk SD patch: unsupported Arduino-ESP32 source; "
        "verify upstream sdcard_uninit() before upgrading the framework"
    )
