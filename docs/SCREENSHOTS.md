# Screenshot capture branch

This branch adds framebuffer screenshots to the MeshInk unified firmware.

## Capture a screenshot

1. Flash the `screenshots` branch unified firmware.
2. Insert the microSD card used by MeshInk.
3. Navigate to the screen you want to capture.
4. **Short-press BOOT** (less than 2 seconds).
5. MeshInk saves the framebuffer before drawing the confirmation toast.

Files are written as standard 8-bit grayscale BMP images:

```text
/screenshots/meshink-0001.bmp
/screenshots/meshink-0002.bmp
...
```

The capture uses the exact EPDiy framebuffer and preserves the active screen orientation. A portrait screen is saved at 540×960; the landscape keyboard is saved at 960×540.

The normal **2-second BOOT hold** is unchanged and still enters/leaves standby.

## Notes

- Screenshot capture is enabled only for the `t5-unified` environment on this branch via `T5_SCREENSHOT_CAPTURE=1`.
- The screenshot writer reuses the map subsystem's existing SD mount and shared SPI configuration.
- The current screen is captured **before** the `SCREENSHOT SAVED` / `SCREENSHOT FAILED` toast is drawn.
- If the SD card cannot be mounted or written, the firmware prints a `[T5-SHOT]` error on USB serial and shows a failure toast.
