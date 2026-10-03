#pragma once

#include <Arduino.h>

enum class MeshInkDeepSleepDiagStage : uint8_t {
    SleepEnter = 1,
    WakeRadio,
    WakeButton,
    HeadlessStart,
    RadioResumeOk,
    RadioResumeFail,
    SpiffsOk,
    SpiffsFail,
    MeshCoreBeginEnter,
    MeshCoreBeginReturn,
    RuntimeReady,
    FirstLoop,
    FirstMeshActivity,
    ReSleepAttempt,
    ButtonProbe,
};

void meshink_deep_sleep_diag_mark(MeshInkDeepSleepDiagStage stage,
                                  uint32_t value=0,
                                  uint16_t aux=0);
void meshink_deep_sleep_diag_replay();
bool meshink_deep_sleep_diag_has_history();
