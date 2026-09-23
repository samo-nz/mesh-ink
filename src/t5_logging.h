#pragma once

// Production firmware logs only concise startup milestones and actionable
// errors. Enable all diagnostics with -DT5_DIAGNOSTICS=1 (replace the
// production flag), or enable just one subsystem using -DT5_LOG_GPS=1 etc.
// The disabled branches are compile-time constants: their format arguments
// are never evaluated and their strings are omitted by the optimizer.
#ifndef T5_DIAGNOSTICS
#define T5_DIAGNOSTICS 0
#endif

#ifndef T5_LOG_UI
#define T5_LOG_UI T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_TOUCH
#define T5_LOG_TOUCH T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_POWER
#define T5_LOG_POWER T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_GPS
#define T5_LOG_GPS T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_MESH
#define T5_LOG_MESH T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_MAP
#define T5_LOG_MAP T5_DIAGNOSTICS
#endif
#ifndef T5_LOG_BOARD
#define T5_LOG_BOARD T5_DIAGNOSTICS
#endif

#define T5_DEBUGF(enabled, ...) do { if (enabled) Serial.printf(__VA_ARGS__); } while (0)
#define T5_DEBUGLN(enabled, message) do { if (enabled) Serial.println(message); } while (0)
