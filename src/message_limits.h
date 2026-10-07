#pragma once

#include <stddef.h>

// Shared MeshInk editor/storage cap. Protocol helpers must either accept this
// payload size or reject/segment it explicitly; shared UI/storage code never
// reaches into an upstream protocol core to discover its frame limit.
constexpr size_t MESHINK_MESSAGE_TEXT_MAX=160;
constexpr size_t MESHINK_MESSAGE_TEXT_BYTES=MESHINK_MESSAGE_TEXT_MAX+1;
