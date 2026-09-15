#pragma once

// MeshCore's ESP32 board helper expects these device-owned hooks. The build
// gate declares them; the later board milestone will define the radio and
// sensor objects and explicitly initialize their hardware.
#include <helpers/radiolib/CustomSX1262Wrapper.h>
#include <helpers/sensors/EnvironmentSensorManager.h>

extern CustomSX1262Wrapper radio_driver;
extern EnvironmentSensorManager sensors;
