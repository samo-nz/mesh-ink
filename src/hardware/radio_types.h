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
