#pragma once
#include <stdint.h>

enum class MeshInkRadioFailureClass : uint8_t {
    Unknown = 0,
    MissingHardwareVariant,
    RadioFault
};

struct MeshInkRadioStats {
    bool continuous_rx;
    uint32_t packets_received;
    uint32_t receive_errors;
    uint32_t packets_sent;
    bool boosted_gain;
};

// Board-provided SX126x electrical description used by protocol-native drivers.
// No board pin constants belong in MeshInk protocol helpers or upstream code.
struct MeshInkSX126xModuleConfig {
    int16_t chip_select;
    int16_t dio1;
    int16_t reset;
    int16_t busy;
    float tcxo_voltage;
    bool dio2_rf_switch;
};
