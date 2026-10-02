#pragma once

#include <stddef.h>

// MeshCore direct-message text payload limit. Keep MeshInk's editor, transient
// buffers and flash journal aligned so a valid direct message is never
// truncated before it reaches the MeshCore send path.
constexpr size_t MESHINK_MESSAGE_TEXT_MAX=160;
constexpr size_t MESHINK_MESSAGE_TEXT_BYTES=MESHINK_MESSAGE_TEXT_MAX+1;
