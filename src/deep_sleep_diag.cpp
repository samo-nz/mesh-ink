#include "deep_sleep_diag.h"

#include <Preferences.h>
#include <string.h>

namespace {

static constexpr uint32_t DIAG_MAGIC=0x44534C50UL; // "DSLP"
static constexpr uint8_t DIAG_VERSION=2;
static constexpr size_t DIAG_EVENT_COUNT=16;

struct DeepSleepDiagEvent {
    uint32_t uptime_ms=0;
    uint32_t value=0;
    uint16_t aux=0;
    uint8_t stage=0;
    uint8_t reserved=0;
};

struct DeepSleepDiagRecord {
    uint32_t magic=DIAG_MAGIC;
    uint32_t sequence=0;
    uint8_t version=DIAG_VERSION;
    uint8_t count=0;
    uint8_t next=0;
    uint8_t reserved=0;
    DeepSleepDiagEvent events[DIAG_EVENT_COUNT]{};
};

static DeepSleepDiagRecord record{};
static bool loaded=false;

static const char* stage_name(uint8_t stage) {
    switch((MeshInkDeepSleepDiagStage)stage) {
        case MeshInkDeepSleepDiagStage::SleepEnter: return "sleep-enter";
        case MeshInkDeepSleepDiagStage::WakeRadio: return "wake-radio";
        case MeshInkDeepSleepDiagStage::WakeButton: return "wake-button";
        case MeshInkDeepSleepDiagStage::HeadlessStart: return "headless-start";
        case MeshInkDeepSleepDiagStage::WakePacketCaptured: return "wake-packet-captured";
        case MeshInkDeepSleepDiagStage::WakePacketCaptureFail: return "wake-packet-capture-fail";
        case MeshInkDeepSleepDiagStage::RadioReinitOk: return "radio-reinit-ok";
        case MeshInkDeepSleepDiagStage::RadioReinitFail: return "radio-reinit-fail";
        case MeshInkDeepSleepDiagStage::RadioResumeOk: return "radio-resume-ok";
        case MeshInkDeepSleepDiagStage::RadioResumeFail: return "radio-resume-fail";
        case MeshInkDeepSleepDiagStage::SpiffsOk: return "spiffs-ok";
        case MeshInkDeepSleepDiagStage::SpiffsFail: return "spiffs-fail";
        case MeshInkDeepSleepDiagStage::MeshCoreBeginEnter: return "meshcore-begin-enter";
        case MeshInkDeepSleepDiagStage::MeshCoreBeginReturn: return "meshcore-begin-return";
        case MeshInkDeepSleepDiagStage::RuntimeReady: return "runtime-ready";
        case MeshInkDeepSleepDiagStage::FirstLoop: return "first-loop";
        case MeshInkDeepSleepDiagStage::FirstMeshActivity: return "first-mesh-activity";
        case MeshInkDeepSleepDiagStage::WakePacketInjected: return "wake-packet-injected";
        case MeshInkDeepSleepDiagStage::ReSleepAttempt: return "resleep-attempt";
        case MeshInkDeepSleepDiagStage::ButtonProbe: return "button-probe";
        default: return "unknown";
    }
}

static void load_record() {
    if(loaded)return;
    loaded=true;
    Preferences prefs;
    if(!prefs.begin("ds-diag",true))return;
    const size_t len=prefs.getBytesLength("record");
    if(len==sizeof(record))prefs.getBytes("record",&record,sizeof(record));
    prefs.end();
    if(record.magic!=DIAG_MAGIC||record.version!=DIAG_VERSION||
       record.count>DIAG_EVENT_COUNT||record.next>=DIAG_EVENT_COUNT) {
        record={};
        record.magic=DIAG_MAGIC;
        record.version=DIAG_VERSION;
    }
}

static void save_record() {
    Preferences prefs;
    if(!prefs.begin("ds-diag",false))return;
    prefs.putBytes("record",&record,sizeof(record));
    prefs.end();
}

}

void meshink_deep_sleep_diag_mark(MeshInkDeepSleepDiagStage stage,
                                  uint32_t value,
                                  uint16_t aux) {
    load_record();
    if(stage==MeshInkDeepSleepDiagStage::SleepEnter)record.sequence++;
    DeepSleepDiagEvent& event=record.events[record.next];
    event.uptime_ms=millis();
    event.value=value;
    event.aux=aux;
    event.stage=(uint8_t)stage;
    record.next=(uint8_t)((record.next+1)%DIAG_EVENT_COUNT);
    if(record.count<DIAG_EVENT_COUNT)record.count++;
    save_record();
}

bool meshink_deep_sleep_diag_has_history() {
    load_record();
    return record.count!=0;
}

void meshink_deep_sleep_diag_replay() {
    load_record();
    if(!record.count) {
        Serial.println("[T5-DEEPSLEEP-FLASH] no retained deep-sleep journal");
        return;
    }
    Serial.printf("[T5-DEEPSLEEP-FLASH] replay cycle=%lu events=%u\n",
                  (unsigned long)record.sequence,(unsigned)record.count);
    const uint8_t first=(uint8_t)((record.next+DIAG_EVENT_COUNT-record.count)%DIAG_EVENT_COUNT);
    for(uint8_t i=0;i<record.count;i++) {
        const DeepSleepDiagEvent& event=record.events[(first+i)%DIAG_EVENT_COUNT];
        Serial.printf("[T5-DEEPSLEEP-FLASH] #%u stage=%s uptime=%lums value=%lu aux=%u\n",
                      (unsigned)i,stage_name(event.stage),
                      (unsigned long)event.uptime_ms,
                      (unsigned long)event.value,
                      (unsigned)event.aux);
    }
}
