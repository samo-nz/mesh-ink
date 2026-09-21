#pragma once

// miniz generates this header in its CMake/Meson builds. PlatformIO consumes
// the pinned sources directly, so provide the static-library definition used
// by miniz's own test build.
#ifndef MINIZ_EXPORT
#define MINIZ_EXPORT
#endif
