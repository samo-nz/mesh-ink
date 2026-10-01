#pragma once

// MeshInk board capability/profile definitions.
//
// H752-01 is the current T5 E-Paper S3 Pro / Pro Lite hardware.
// H752 is the older ESP32-S3 board with a different display interface.
// The real H752 build is introduced separately; keeping the electrical
// profile here prevents UI/runtime code from accumulating hard-coded pins.
#ifndef T5_BOARD_H752_01
#define T5_BOARD_H752_01 1
#endif

#ifndef T5_BOARD_H752
#define T5_BOARD_H752 (!T5_BOARD_H752_01)
#endif

#if T5_BOARD_H752_01
#define T5_BOARD_LABEL "H752-01"
#define T5_HAS_GPS 1
#define T5_PIN_I2C_SDA 39
#define T5_PIN_I2C_SCL 40
#define T5_PIN_SPI_MISO 21
#define T5_PIN_SPI_MOSI 13
#define T5_PIN_SPI_SCLK 14
#define T5_PIN_TOUCH_INT 3
#define T5_PIN_TOUCH_RST 9
#define T5_PIN_FRONTLIGHT 11
#define T5_PIN_BOOT_BUTTON 0
#define T5_PIN_SD_CS 12
#define T5_PIN_LORA_CS 46
#define T5_PIN_LORA_IRQ 10
#define T5_PIN_LORA_RST 1
#define T5_PIN_LORA_BUSY 47
#else
#define T5_BOARD_LABEL "H752"
#define T5_HAS_GPS 0
#define T5_PIN_I2C_SDA 6
#define T5_PIN_I2C_SCL 5
#define T5_PIN_SPI_MISO 8
#define T5_PIN_SPI_MOSI 17
#define T5_PIN_SPI_SCLK 18
#define T5_PIN_TOUCH_INT 15
#define T5_PIN_TOUCH_RST 41
#define T5_PIN_FRONTLIGHT 40
#define T5_PIN_BOOT_BUTTON 0
#define T5_PIN_SD_CS 16
#define T5_PIN_LORA_CS 46
#define T5_PIN_LORA_IRQ 3
#define T5_PIN_LORA_RST 43
#define T5_PIN_LORA_BUSY 44
#endif

#ifndef T5_UI_HAS_GPS
#define T5_UI_HAS_GPS T5_HAS_GPS
#endif

// Optional keyboard tuning hooks. Shared keyboard geometry scales from the
// active logical display size; a future board profile only needs to override
// these when its bezel/touch/display proportions need a small correction.
// Zeroes preserve the field-tested H752-01 and H752 T5 layout exactly.
#ifndef MESHINK_KEYBOARD_PORTRAIT_X_OFFSET
#define MESHINK_KEYBOARD_PORTRAIT_X_OFFSET 0
#endif
#ifndef MESHINK_KEYBOARD_PORTRAIT_Y_OFFSET
#define MESHINK_KEYBOARD_PORTRAIT_Y_OFFSET 0
#endif
#ifndef MESHINK_KEYBOARD_LANDSCAPE_X_OFFSET
#define MESHINK_KEYBOARD_LANDSCAPE_X_OFFSET 0
#endif
#ifndef MESHINK_KEYBOARD_LANDSCAPE_Y_OFFSET
#define MESHINK_KEYBOARD_LANDSCAPE_Y_OFFSET 0
#endif
#ifndef MESHINK_KEYBOARD_KEY_HEIGHT_DELTA
#define MESHINK_KEYBOARD_KEY_HEIGHT_DELTA 0
#endif
#ifndef MESHINK_KEYBOARD_ROW_GAP_DELTA
#define MESHINK_KEYBOARD_ROW_GAP_DELTA 0
#endif
