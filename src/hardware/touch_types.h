#pragma once

#include <stdint.h>
#include "orientation.h"

// Board-independent touch vocabulary. The application consumes logical touch
// coordinates and contact state; controller registers, reset sequencing and
// electrical pins belong to the selected hardware backend.
struct MeshInkTouchPoint {
    int16_t x = 0;
    int16_t y = 0;
};

struct MeshInkTouchPrimarySample {
    int16_t x = 0;
    int16_t y = 0;
    bool pressed = false;
    bool home = false;
};

struct MeshInkTouchContacts {
    uint8_t count = 0;
    MeshInkTouchPoint points[2]{};
    bool home = false;
};
