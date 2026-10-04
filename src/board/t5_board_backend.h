#pragma once

#include "board_profile.h"

inline const char* meshink_board_name() {
    return T5_BOARD_LABEL;
}

inline bool meshink_board_has_gps() {
    return T5_HAS_GPS != 0;
}

void meshink_board_begin_companion();
struct MeshInkDeepSleepRadioProbe {
    bool valid=false;
    bool transport_ok=false;
    uint8_t dio1=0;
    uint8_t busy=0;
    uint16_t irq=0;
    uint16_t packet_len=0;
    uint8_t status=0;
};

void meshink_board_start_local_radio_settle();
void meshink_board_begin_local();
void meshink_board_begin_local_rx_wake(bool packet_wake);
void meshink_board_boot_complete();
bool meshink_board_woke_from_radio();
bool meshink_board_woke_from_primary_button();
bool meshink_board_woke_from_timer();
bool meshink_board_radio_irq_asserted();
bool meshink_board_service_asserted_radio_irq();
bool meshink_board_probe_deep_sleep_radio(MeshInkDeepSleepRadioProbe& probe);
void meshink_board_restore_deep_sleep_wake_pads();
void meshink_board_prepare_retained_aux_wake();
void meshink_board_release_retained_radio_holds();
bool meshink_board_enter_deep_sleep_standby();
bool meshink_board_return_to_retained_deep_sleep();

// deepsleep28 split diagnostic: isolate SX1262 DIO1 repeatability from
// ESP32-S3 deep-sleep wake-source repeatability.
bool meshink_board_diag_radio_begin();
bool meshink_board_diag_radio_poll(uint32_t sequence);
bool meshink_board_diag_gps_activity();
void meshink_board_diag_radio_transport_probe(const char* phase);
void meshink_board_diag_radio_snapshot(const char* phase);
void meshink_board_diag_restore_button_wake();
bool meshink_board_diag_enter_button_only_deep_sleep(bool first_entry);
void meshink_board_companion_exit_feedback_begin();
void meshink_board_companion_release_resources();
