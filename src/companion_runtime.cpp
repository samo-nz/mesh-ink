#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <esp32-hal-cpu.h>
#include <esp_random.h>
#include <helpers/MultiSerialInterface.h>
#include <helpers/esp32/SerialBLEInterface.h>
#include <BLEAdvertising.h>
#include "../lib/MeshCore/examples/companion_radio/DataStore.cpp"
// Upstream MyMesh.h declares a global `MyMesh the_mesh` for its stock
// companion example, but MyMesh.cpp itself never references that global.
// Rename only that imported declaration so MeshInk can define a derived
// MeshInkMesh instance with receive-persistence hooks below.
#define the_mesh meshcore_upstream_example_the_mesh
#include "../lib/MeshCore/examples/companion_radio/MyMesh.cpp"
#undef the_mesh
#include "companion_runtime.h"
#include "companion_notice.h"
#include "local_mesh_runtime.h"
#include "message_store.h"
#include "ui_onboarding.h"
#include "t5_logging.h"
#include "hardware/board.h"
#include "hardware/gps.h"
#include "hardware/rtc.h"
#include "hardware/radio.h"
#include "hardware/buttons.h"
#include "hardware/power.h"

// Device-owned composition root around the upstream MeshCore companion
// classes. MeshInk adds persistence hooks without changing the phone protocol.
static bool companion_mode_active=false;

struct PendingCompanionSend {
    bool active=false;
    MeshInkMessageKind kind=MeshInkMessageKind::Direct;
    uint8_t key[7]{};
    size_t key_len=0;
    uint8_t attempt=0;
    uint32_t timestamp=0;
    char text[MESHINK_MESSAGE_TEXT_BYTES]{};
};

struct CompanionAckRef {
    uint32_t ack=0;
    uint32_t sequence=0;
    bool route_flood=false;
};

class MeshInkBLEInterface final : public SerialBLEInterface {
    PendingCompanionSend pending_{};
    CompanionAckRef ack_refs_[8]{};
    uint8_t next_ack_ref_=0;

    void remember_ack(uint32_t ack,uint32_t sequence,bool route_flood){
        if(!ack||!sequence)return;
        CompanionAckRef& slot=ack_refs_[next_ack_ref_];
        slot.ack=ack;
        slot.sequence=sequence;
        slot.route_flood=route_flood;
        next_ack_ref_=(uint8_t)((next_ack_ref_+1)%8);
    }

    bool deliver_ack(uint32_t ack){
        if(!ack)return false;
        for(auto& ref:ack_refs_){
            if(ref.ack!=ack||!ref.sequence)continue;
            meshink_message_store().update_outgoing(
                ref.sequence,UiMessageState::Delivered,ack,ref.route_flood);
            const uint32_t delivered_sequence=ref.sequence;
            for(auto& item:ack_refs_)if(item.sequence==delivered_sequence)item={};
            return true;
        }
        return meshink_message_store().mark_delivered_by_ack(ack);
    }

    void remember_app_send(const uint8_t* frame,size_t len){
        pending_={};
        if(!companion_mode_active||!frame||!len)return;
        if(frame[0]==2&&len>=14&&frame[1]==TXT_TYPE_PLAIN){
            pending_.active=true;
            pending_.kind=MeshInkMessageKind::Direct;
            pending_.key_len=6;
            pending_.attempt=frame[2];
            memcpy(&pending_.timestamp,frame+3,4);
            memcpy(pending_.key,frame+7,6);
            const size_t text_len=min(sizeof(pending_.text)-1,len-(size_t)13);
            memcpy(pending_.text,frame+13,text_len);
            pending_.text[text_len]=0;
        }else if(frame[0]==3&&len>=8&&frame[1]==TXT_TYPE_PLAIN){
            pending_.active=true;
            pending_.kind=MeshInkMessageKind::Channel;
            pending_.key_len=1;
            pending_.key[0]=frame[2];
            memcpy(&pending_.timestamp,frame+3,4);
            const size_t text_len=min(sizeof(pending_.text)-1,len-(size_t)7);
            memcpy(pending_.text,frame+7,text_len);
            pending_.text[text_len]=0;
        }
    }

    uint32_t commit_pending(UiMessageState state,uint32_t ack=0){
        if(!pending_.active)return 0;
        auto& journal=meshink_message_store();
        // Only an explicit direct retry is allowed to reuse a prior journal
        // record. Two intentional attempt-0 messages can legitimately have
        // identical text and second-resolution timestamps and must stay distinct.
        uint32_t sequence=0;
        if(pending_.kind==MeshInkMessageKind::Direct&&pending_.attempt>0){
            sequence=journal.find_matching_outgoing(
                pending_.kind,pending_.key,pending_.key_len,
                pending_.timestamp,pending_.text);
        }
        if(!sequence){
            sequence=journal.append(
                pending_.kind,pending_.key,pending_.key_len,pending_.text,
                pending_.timestamp,state,ack,MeshInkMessageOrigin::CompanionApp);
        }else{
            journal.update_state(sequence,state);
            if(ack)journal.update_ack(sequence,ack);
        }
        return sequence;
    }

    void observe_mesh_response(const uint8_t* frame,size_t len){
        if(!companion_mode_active||!frame||!len)return;

        // End-to-end ACKs can arrive after the phone has already started a
        // later retry. Retain MeshCore's eight in-flight ACK hashes locally so
        // any valid attempt can complete the one logical journal message.
        if(frame[0]==0x82&&len>=5){
            uint32_t ack=0;memcpy(&ack,frame+1,4);
            deliver_ack(ack);
        }

        if(!pending_.active)return;
        if(pending_.kind==MeshInkMessageKind::Direct&&frame[0]==6&&len>=10){
            uint32_t ack=0;memcpy(&ack,frame+2,4);
            UiMessageState state=UiMessageState::Sent;
            if(pending_.attempt>=1&&pending_.attempt<=5)
                state=(UiMessageState)((uint8_t)UiMessageState::Retrying1+pending_.attempt-1);
            const bool route_flood=frame[1]!=0;
            const uint32_t sequence=commit_pending(state,ack);
            if(sequence){
                meshink_message_store().update_route(sequence,route_flood);
                remember_ack(ack,sequence,route_flood);
            }
            pending_={};
        }else if(pending_.kind==MeshInkMessageKind::Channel&&frame[0]==0){
            commit_pending(UiMessageState::Sent);
            pending_={};
        }else if(frame[0]==1){
            pending_={};
        }
    }

public:
    size_t checkRecvFrame(uint8_t* dest) override {
        const size_t len=SerialBLEInterface::checkRecvFrame(dest);
        if(len)remember_app_send(dest,len);
        return len;
    }
    size_t writeFrame(const uint8_t* src,size_t len) override {
        observe_mesh_response(src,len);
        return SerialBLEInterface::writeFrame(src,len);
    }
};

class MeshInkMesh final : public MyMesh {
public:
    using MyMesh::MyMesh;

protected:
    void onMessageRecv(const ContactInfo& from,mesh::Packet* pkt,
                       uint32_t sender_timestamp,const char* text) override {
        if(companion_mode_active){
            meshink_message_store().append(
                MeshInkMessageKind::Direct,from.id.pub_key,6,text,sender_timestamp,
                UiMessageState::Received,0,MeshInkMessageOrigin::CompanionApp,
                pkt!=nullptr,pkt?(int8_t)(pkt->getSNR()*4.0f):0,
                (pkt&&pkt->isRouteFlood())?pkt->path_len:MESHINK_MESSAGE_PATH_UNKNOWN);
        }
        MyMesh::onMessageRecv(from,pkt,sender_timestamp,text);
    }

    void onSignedMessageRecv(const ContactInfo& from,mesh::Packet* pkt,
                             uint32_t sender_timestamp,const uint8_t* sender_prefix,
                             const char* text) override {
        if(companion_mode_active){
            meshink_message_store().append(
                MeshInkMessageKind::Direct,from.id.pub_key,6,text,sender_timestamp,
                UiMessageState::Received,0,MeshInkMessageOrigin::CompanionApp,
                pkt!=nullptr,pkt?(int8_t)(pkt->getSNR()*4.0f):0,
                (pkt&&pkt->isRouteFlood())?pkt->path_len:MESHINK_MESSAGE_PATH_UNKNOWN);
        }
        MyMesh::onSignedMessageRecv(from,pkt,sender_timestamp,sender_prefix,text);
    }

    void onChannelMessageRecv(const mesh::GroupChannel& channel,mesh::Packet* pkt,
                              uint32_t timestamp,const char* text) override {
        const int index=findChannelIdx(channel);
        if(companion_mode_active){
            if(index>=0&&index<MAX_GROUP_CHANNELS){
                const uint8_t key=(uint8_t)index;
                meshink_message_store().append(
                    MeshInkMessageKind::Channel,&key,1,text,timestamp,
                    UiMessageState::Received,0,MeshInkMessageOrigin::CompanionApp,
                    pkt!=nullptr,pkt?(int8_t)(pkt->getSNR()*4.0f):0,
                    (pkt&&pkt->isRouteFlood())?pkt->path_len:MESHINK_MESSAGE_PATH_UNKNOWN);
            }
            MyMesh::onChannelMessageRecv(channel,pkt,timestamp,text);
            return;
        }

        // Standalone MeshInk owns its journal directly. Persist at the exact
        // callback where MeshCore has authenticated/decrypted the group packet,
        // bypassing the companion offline-queue/serial-sync handoff. This is
        // deliberately channel-only: the proven direct-message ACK path stays
        // unchanged.
        if(index>=0&&index<MAX_GROUP_CHANNELS){
            local_mesh_receive_channel_from_core(
                (uint8_t)index,timestamp,text,pkt!=nullptr,
                pkt?(int8_t)(pkt->getSNR()*4.0f):0,
                (pkt&&pkt->isRouteFlood())?pkt->path_len:MESHINK_MESSAGE_PATH_UNKNOWN);
            return;
        }

        // Unknown-channel fallback preserves upstream diagnostic behavior.
        MyMesh::onChannelMessageRecv(channel,pkt,timestamp,text);
    }
};

static MultiSerialInterface interface_manager;
static MeshInkBLEInterface bluetooth_interface;
static DataStore store(SPIFFS, meshink_rtc_meshcore());
static StdRNG fast_rng;
static SimpleMeshTables tables;
void local_mesh_on_frame(const uint8_t*, size_t);

class LocalSerial final : public BaseSerialInterface {
    bool enabled=false;
    bool message_sync_required=false;
    bool message_sync_inflight=false;
    uint8_t command[MAX_FRAME_SIZE+1]{};
    size_t command_len=0;

    static bool is_sync_message_response(uint8_t code) {
        return code==7||code==8||code==16||code==17||code==27;
    }

public:
    void enable() override { enabled=true; }
    void disable() override { enabled=false; }
    bool isEnabled() const override { return enabled; }
    bool isConnected() const override { return true; }
    bool isWriteBusy() const override { return false; }

    size_t writeFrame(const uint8_t* frame,size_t len) override {
        if(!frame||!len)return 0;

        if(len==1&&frame[0]==0x83){
            // PUSH_CODE_MSG_WAITING is only a hint that MeshCore's RAM queue is
            // non-empty. Drain until CMD_SYNC_NEXT_MESSAGE explicitly returns
            // RESP_CODE_NO_MORE_MESSAGES instead of counting push notifications.
            message_sync_required=true;
            Serial.println("[T5-DEEPSLEEP] msg-waiting: MeshCore receive queue requires drain");
            return len;
        }

        if(message_sync_inflight){
            if(frame[0]==10){ // RESP_CODE_NO_MORE_MESSAGES
                message_sync_inflight=false;
                message_sync_required=false;
                Serial.println("[T5-DEEPSLEEP] sync-empty: MeshCore receive queue fully drained");
                local_mesh_on_frame(frame,len);
                return len;
            }
            if(is_sync_message_response(frame[0])){
                // Persist the returned message first, then immediately request
                // another one on the next MeshCore loop. This guarantees the
                // queue is empty before headless deep sleep is allowed.
                message_sync_inflight=false;
                message_sync_required=true;
                local_mesh_on_frame(frame,len);
                Serial.printf("[T5-DEEPSLEEP] message-frame-consumed frame=0x%02x; continuing queue drain\n",
                              (unsigned)frame[0]);
                return len;
            }
        }

        local_mesh_on_frame(frame,len);
        return len;
    }

    size_t checkRecvFrame(uint8_t* frame) override {
        if(command_len){
            const size_t len=command_len;
            memcpy(frame,command,len);
            command_len=0;
            return len;
        }
        if(message_sync_required&&!message_sync_inflight){
            frame[0]=10; // CMD_SYNC_NEXT_MESSAGE
            message_sync_inflight=true;
            Serial.println("[T5-DEEPSLEEP] sync-request: CMD_SYNC_NEXT_MESSAGE");
            return 1;
        }
        return 0;
    }

    bool enqueue(const uint8_t* frame,size_t len){
        if(!frame||!len||len>sizeof(command)||command_len)return false;
        memcpy(command,frame,len);
        command_len=len;
        return true;
    }

    bool messageSyncPending() const {
        return message_sync_required||message_sync_inflight;
    }

    void resetMessageSync() {
        message_sync_required=false;
        message_sync_inflight=false;
    }
};

static LocalSerial local_interface;
static bool local_runtime_ready=false;
static bool local_rx_wake_runtime=false;
static bool local_rx_wake_promoted_to_ui=false;
static bool local_interactive_services_ready=false;
static uint32_t local_rx_wake_last_activity=0;
static uint32_t local_rx_wake_last_report=0;
static uint32_t local_rx_wake_sleep_retry=0;
static uint32_t local_rx_wake_button_started=0;
static MeshInkRadioStats local_rx_wake_stats{};
static constexpr uint32_t LOCAL_RX_WAKE_QUIET_MS=40000UL;

static MeshInkPowerSleepCheck local_mesh_headless_power_check(
        MeshInkPowerCriticalState& power) {
    // EPDiy owns the shared IDF I2C driver whenever any UI display session is
    // initialized (normal interactive UI or retained headless alert). Reuse it
    // instead of provoking ESP-IDF's duplicate "i2c driver install error".
    if(ui_display_session_active())
        return meshink_power_deep_sleep_check(power);

    MeshInkPowerSleepCheck result=MeshInkPowerSleepCheck::Unavailable;
    if(meshink_power_begin_minimal_bus()){
        result=meshink_power_deep_sleep_check(power);
        meshink_power_end_minimal_bus();
    }
    return result;
}

static void companion_set_low_power_cpu() {
    static constexpr uint32_t COMPANION_CPU_MHZ=80;
    const bool accepted=setCpuFrequencyMhz(COMPANION_CPU_MHZ);
    const uint32_t actual=getCpuFrequencyMhz();
    if(!accepted||actual!=COMPANION_CPU_MHZ)
        Serial.printf("[T5-ERROR] companion CPU target=%lu actual=%luMHz\n",
                      (unsigned long)COMPANION_CPU_MHZ,(unsigned long)actual);
}

static void companion_configure_ble_scan_response(const char* prefix,const char* node_name) {
    // Legacy BLE advertising/scan-response payloads are capped at 31 bytes.
    // MeshCore's default ESP32 helper duplicates the 128-bit UART service UUID
    // into the scan response along with name/TX power, which overflows that
    // budget and makes Bluedroid print "Partial data write into ADV". Keep the
    // service UUID in the primary advertisement; use the scan response only
    // for the discoverable device name.
    char scan_name[30]{};
    const int full_len=snprintf(scan_name,sizeof(scan_name),"%s%s",
                                prefix?prefix:"",node_name?node_name:"");
    BLEAdvertisementData scan_response;
    const bool truncated=full_len<0||full_len>=(int)sizeof(scan_name);
    if(truncated)scan_response.setShortName(scan_name);
    else scan_response.setName(scan_name);
    BLEDevice::getAdvertising()->setScanResponseData(scan_response);
}

MeshInkMesh the_mesh(meshink_radio_meshcore(), fast_rng, meshink_rtc_meshcore(), tables, store);
MyMesh& t5_mesh() { return the_mesh; }
bool local_mesh_enqueue_command(const uint8_t* frame,size_t len){return local_interface.enqueue(frame,len);}

static uint32_t local_contacts_save_due=0;
static bool local_persist_contact(const ContactInfo& contact) {
    return contact.type!=ADV_TYPE_NONE;
}
void local_mesh_schedule_contacts_save() {
    // Match upstream MeshCore's lazy contact-write cadence to coalesce bursts
    // of messages/telemetry and avoid unnecessary flash writes.
    local_contacts_save_due=millis()+5000UL;
}
void local_mesh_flush_contacts_save_now() {
    if(!local_contacts_save_due)return;
    store.saveContacts(&the_mesh,local_persist_contact);
    local_contacts_save_due=0;
}
void local_mesh_flush_contacts_save_if_due() {
    if(local_contacts_save_due&&(int32_t)(millis()-local_contacts_save_due)>=0)
        local_mesh_flush_contacts_save_now();
}

void companion_setup() {
    companion_mode_active=true;
    T5_DEBUGLN(T5_LOG_MESH,"[T5-BOOT] starting upstream MeshCore companion runtime");
    meshink_show_companion_notice();
    meshink_board_begin_companion();
    if (!meshink_radio_initialize()) {
        Serial.printf("[T5-ERROR] %s initialization failed\n",meshink_radio_name());
        while (true) delay(1000);
    }
    fast_rng.begin(meshink_radio_rng_seed());
    const bool storage_mounted=SPIFFS.begin(true);
    if(storage_mounted)Serial.println("[T5-INIT] storage=SPIFFS OK");
    else Serial.println("[T5-ERROR] SPIFFS unavailable in companion mode");
    if(storage_mounted&&!meshink_message_store().begin())
        Serial.println("[T5-ERROR] MeshInk message journal unavailable in companion mode");
    store.begin();
    the_mesh.begin(false);
    bluetooth_interface.begin(BLE_NAME_PREFIX, the_mesh.getNodePrefs()->node_name,
                              the_mesh.getBLEPin());
    companion_configure_ble_scan_response(BLE_NAME_PREFIX,the_mesh.getNodePrefs()->node_name);
    interface_manager.addInterface(InterfaceType::Bluetooth, &bluetooth_interface);
    the_mesh.startInterface(interface_manager);
    meshink_gps_service_begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    meshink_board_boot_complete();

    // Companion mode has no local UI/render workload. Once BLE, MeshCore,
    // radio and optional GPS setup have completed, 80 MHz is enough for the
    // steady-state service loop while keeping the ESP32-S3 peripheral/APB
    // domain at its normal rate. Test10 validates this on hardware.
    companion_set_low_power_cpu();
}

void companion_loop() {
    the_mesh.loop();
    interface_manager.loop();
    meshink_gps_service_loop();
    meshink_rtc_tick();

    // The cache64 release uses Arduino + ESP-IDF with the task watchdog
    // enabled. Unlike the local UI path, companion mode previously returned
    // here without ever blocking/yielding, so loopTask could starve IDLE1
    // until the watchdog fired. A 1 ms delay yields CPU1 to FreeRTOS while
    // keeping MeshCore/BLE servicing effectively continuous.
    delay(1);
}

static bool companion_persist_contact(const ContactInfo& contact) {
    return contact.type!=ADV_TYPE_NONE;
}

void companion_prepare_exit() {
    meshink_board_companion_exit_feedback_begin();

    const bool ble_connected=bluetooth_interface.isConnected();

    // Upstream SerialBLEInterface::disable() always calls disconnect(last_conn_id).
    // If the phone has already disconnected, that stale ID makes Bluedroid emit
    // "Unknown connection ID". Detach the transport from MeshCore instead of
    // issuing a redundant disconnect. BLEDevice::deinit() below then shuts the
    // controller/host stack down cleanly in both cases.
    if(ble_connected){
        interface_manager.disable();

        // Give the requested disconnect/GAP callback a short bounded window
        // to settle before shutting the Bluetooth stack down.
        const uint32_t settle_started=millis();
        while(millis()-settle_started<100){
            meshink_gps_service_loop();
            meshink_rtc_tick();
            delay(1);
        }
    }else{
        interface_manager.removeInterface(&bluetooth_interface);
        interface_manager.disable();
    }
    BLEDevice::deinit(false);

    the_mesh.savePrefs();
    store.saveContacts(&the_mesh,companion_persist_contact);
    store.saveChannels(&the_mesh);

#if ENV_INCLUDE_GPS == 1
    meshink_gps_shutdown();
#endif

    meshink_radio_power_off();
    meshink_board_companion_release_resources();
    SPIFFS.end();

}

void local_mesh_setup() {
    companion_mode_active=false;
    local_rx_wake_runtime=false;
    local_interactive_services_ready=false;
    T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] starting upstream MeshCore runtime; Bluetooth disabled");

    // The H752-01 LoRa/GPS rail has already been settling throughout the
    // splash. Mount internal SPIFFS before crossing the board settle barrier
    // so this independent flash work consumes the otherwise idle remainder of
    // LilyGO's required 1500 ms rail delay. Keep MeshCore datastore/core
    // lifecycle ordering unchanged.
    bool storage_mounted=SPIFFS.begin(false);
    if(!storage_mounted){
        T5_DEBUGLN(T5_LOG_MESH,"[T5-STORE] SPIFFS mount failed; showing storage initialization splash");
        ui_show_storage_initializing();
        storage_mounted=SPIFFS.begin(true);
    }
    if(storage_mounted)Serial.println("[T5-INIT] storage=SPIFFS OK");
    else Serial.println("[T5-ERROR] SPIFFS unavailable after recovery attempt");

    meshink_board_begin_local();
    const bool radio_ready=meshink_radio_initialize();
    if (!radio_ready) {
        const MeshInkRadioFailureClass failure=meshink_radio_classify_failure();
        Serial.printf("[T5-ERROR] %s unavailable; failure-class=%u\n",meshink_radio_name(),(unsigned)failure);
        ui_show_radio_failure(failure);
        return;
    }
    fast_rng.begin(meshink_radio_rng_seed());
    store.begin();
    the_mesh.begin(true);
    the_mesh.startInterface(local_interface);
    // Negotiate companion-protocol v3 for the internal standalone interface.
    // V3 receive frames add SNR and path metadata without changing on-air packets.
    const uint8_t local_protocol_query[2]={22,3}; // CMD_DEVICE_QUERY, app protocol v3
    if(!local_interface.enqueue(local_protocol_query,sizeof(local_protocol_query)))
        Serial.println("[T5-ERROR] local MeshCore protocol negotiation queue busy");
    meshink_gps_service_begin();
#if ENV_INCLUDE_GPS == 1
    // MeshCore defaults GPS off even though the receiver on this board shares
    // the always-on LoRa rail. For a NEW local-UI setup, default the SOFTWARE
    // GPS provider to ON with continuous reads (interval=0). Do not override
    // any completed setup's GPS preferences, including an explicit OFF.
    // Remember the one-time initialization so an incomplete setup that has
    // since changed GPS settings is not reset on its next boot.
    Preferences initial_gps;
    if(initial_gps.begin("t5-ui",false)){
        const bool configured=initial_gps.getBool("complete",false);
        const bool default_applied=initial_gps.getBool("gps_default_v1",false);
        if(!configured&&!default_applied){
            auto* settings=the_mesh.getNodePrefs();
            settings->gps_enabled=1;
            settings->gps_interval=0;
            the_mesh.savePrefs();
            initial_gps.putBool("gps_default_v1",true);
        }
        initial_gps.end();
    }
    the_mesh.applyGpsPrefs();
#endif
    local_interactive_services_ready=true;
    local_mesh_runtime_begin();
    ui_use_data_provider(local_mesh_provider());
    ui_mesh_ready();
    ui_apply_initial_radio_preset(); // fix first boot's displayed-vs-active radio mismatch
    local_runtime_ready=true;
    T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] ready name='%s' contacts=%d\n",the_mesh.getNodeName(),the_mesh.getNumContacts());
}

static bool local_mesh_setup_retained_wake(bool require_packet,const char* reason) {
    const uint32_t started=millis();
    local_interface.resetMessageSync();
    companion_mode_active=false;
    local_rx_wake_runtime=false;
    local_rx_wake_promoted_to_ui=false;
    local_interactive_services_ready=false;
    local_runtime_ready=false;
    Serial.printf("[T5-DEEPSLEEP] retained startup: reason=%s require-packet=%u\n",
                  reason?reason:"unknown",require_packet?1U:0U);

    meshink_board_begin_local_rx_wake(require_packet);
    const bool radio_ready=require_packet
        ?meshink_radio_resume_rx_wake()
        :meshink_radio_resume_retained_wake();
    if(!radio_ready){
        Serial.println("[T5-DEEPSLEEP] retained startup failed before MeshCore");
        return false;
    }

    if(!SPIFFS.begin(false)){
        Serial.println("[T5-DEEPSLEEP] retained startup failed: SPIFFS mount unavailable");
        return false;
    }
    Serial.printf("[T5-DEEPSLEEP] SPIFFS mounted +%lums\n",(unsigned long)(millis()-started));

    ui_prepare_headless_rx_wake();

    fast_rng.begin(esp_random());
    store.begin();
    Serial.printf("[T5-DEEPSLEEP] datastore ready +%lums; entering MeshCore begin()\n",
                  (unsigned long)(millis()-started));

    the_mesh.begin(true);
    // Dispatcher::begin() has now called the radio wrapper begin(), which sees
    // BD_STARTUP_RX_PACKET and latches the staged-packet ready flag.
    if(require_packet)board.finishLocalRxWakeCapture();
    the_mesh.startInterface(local_interface);
    const uint8_t local_protocol_query[2]={22,3};
    if(!local_interface.enqueue(local_protocol_query,sizeof(local_protocol_query)))
        Serial.println("[T5-DEEPSLEEP] WARNING: local protocol v3 negotiation queue busy");
    local_mesh_runtime_begin();

    local_runtime_ready=true;
    local_rx_wake_runtime=true;
    local_rx_wake_stats=meshink_radio_stats();
    local_rx_wake_last_activity=millis();
    local_rx_wake_last_report=0;
    local_rx_wake_sleep_retry=0;
    local_rx_wake_button_started=0;
    Serial.printf("[T5-DEEPSLEEP] MeshCore retained runtime READY +%lums rx=%lu err=%lu tx=%lu rxmode=%u\n",
                  (unsigned long)(millis()-started),
                  (unsigned long)local_rx_wake_stats.packets_received,
                  (unsigned long)local_rx_wake_stats.receive_errors,
                  (unsigned long)local_rx_wake_stats.packets_sent,
                  local_rx_wake_stats.continuous_rx?1U:0U);
    return true;
}

bool local_mesh_setup_rx_wake() {
    return local_mesh_setup_retained_wake(true,"radio");
}

bool local_mesh_setup_button_wake() {
    return local_mesh_setup_retained_wake(false,"button");
}

void local_mesh_service_startup() {
    if(!local_runtime_ready)return;
    the_mesh.loop();
    local_mesh_flush_contacts_save_if_due();
}

bool local_mesh_rx_wake_promoted() {
    return local_rx_wake_promoted_to_ui;
}

void local_mesh_prepare_interactive_services() {
    if(!local_runtime_ready||local_interactive_services_ready)return;

    // Radio-first retained wake skipped RTC/GNSS startup. At UI promotion the
    // display has already installed the shared I2C bus, so restore only the
    // interactive RTC/GPS services without touching the running SX1262.
    meshink_rtc_begin();
#if ENV_INCLUDE_GPS == 1
    meshink_gps_prepare_runtime();
#endif
    meshink_gps_service_begin();
#if ENV_INCLUDE_GPS == 1
    the_mesh.applyGpsPrefs();
#endif
    meshink_gps_service_loop();
    local_interactive_services_ready=true;
    Serial.printf("[T5-DEEPSLEEP] interactive peripherals ready rtc=%u gps=%u\n",
                  meshink_rtc_valid()?1U:0U,
                  meshink_gps_read_status().available?1U:0U);
}

bool local_mesh_promote_to_ui(const char* source) {
    if(!local_runtime_ready)return false;

    // Accepted BOOT-to-UI promotion gets immediate visible acknowledgement.
    // This PWM path is independent of EPDiy/I2C, so it is safe whether the
    // headless display session already exists or has never been initialized.
    meshink_power_frontlight_begin();
    meshink_power_frontlight_set(100);
    Serial.printf("[T5-DEEPSLEEP] UI boot requested source=%s; frontlight=100%%\n",
                  source?source:"unknown");

    MeshInkPowerCriticalState wake_power{};
    const MeshInkPowerSleepCheck wake_result=
        local_mesh_headless_power_check(wake_power);
    Serial.printf("[T5-DEEPSLEEP] pre-UI battery check source=%s result=%u voltage=%s%umV\n",
                  source?source:"unknown",(unsigned)wake_result,
                  wake_power.battery_mv_valid?"":"unavailable/",
                  wake_power.battery_mv_valid?(unsigned)wake_power.battery_mv:0U);
    if(wake_result==MeshInkPowerSleepCheck::Critical){
        local_mesh_prepare_shutdown();
        meshink_board_companion_release_resources();
        SPIFFS.end();
        ui_minimal_low_battery_shutdown(wake_power,source?source:"deep-ui");
    }
    if(!ui_promote_headless_to_interactive())return false;

    local_rx_wake_runtime=false;
    local_rx_wake_promoted_to_ui=true;
    Serial.println("[T5-DEEPSLEEP] retained MeshCore promoted to interactive UI without radio restart");
    return true;
}

bool local_mesh_enter_deep_sleep_standby() {
    if(!local_runtime_ready){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: local MeshCore is not running");
        return false;
    }
    const MeshInkRadioStats stats=meshink_radio_stats();
    if(!stats.continuous_rx)return false;
    if(local_interface.messageSyncPending()){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: received-message queue drain is still pending");
        return false;
    }
    if(meshink_board_radio_irq_asserted()){
        const bool dispatched=meshink_board_service_asserted_radio_irq();
        Serial.printf("[T5-DEEPSLEEP] sleep deferred: pending SX1262 IRQ dispatched=%u before persistence checks\n",
                      dispatched?1U:0U);
        return false;
    }
    if(ui_headless_display_busy()){
        Serial.println("[T5-DEEPSLEEP] sleep deferred: headless display/alert session is active");
        return false;
    }
    local_mesh_flush_contacts_save_now();

    uint32_t durable_sequence=0;
    size_t durable_count=0;
    if(!meshink_message_store().sync_and_verify_for_deep_sleep(
            durable_sequence,durable_count)){
        Serial.printf("[T5-DEEPSLEEP] sleep deferred: journal durability verify FAILED disk-seq=%lu disk-count=%u\n",
                      (unsigned long)durable_sequence,(unsigned)durable_count);
        return false;
    }
    Serial.printf("[T5-DEEPSLEEP] journal durable before sleep seq=%lu count=%u\n",
                  (unsigned long)durable_sequence,(unsigned)durable_count);

    MeshInkPowerCriticalState sleep_power{};
    const MeshInkPowerSleepCheck sleep_power_result=
        local_mesh_headless_power_check(sleep_power);
    if(sleep_power_result==MeshInkPowerSleepCheck::Unavailable)
        Serial.println("[T5-DEEPSLEEP] pre-sleep battery check unavailable: I2C start failed");
    Serial.printf("[T5-DEEPSLEEP] pre-sleep battery check result=%u voltage=%s%umV\n",
                  (unsigned)sleep_power_result,
                  sleep_power.battery_mv_valid?"":"unavailable/",
                  sleep_power.battery_mv_valid?(unsigned)sleep_power.battery_mv:0U);
    if(sleep_power_result==MeshInkPowerSleepCheck::Critical){
        Serial.println("[T5-DEEPSLEEP] critical battery before re-sleep; minimal shutdown path");
        local_mesh_prepare_shutdown();
        meshink_board_companion_release_resources();
        SPIFFS.end();
        ui_minimal_low_battery_shutdown(sleep_power,"deep-rx");
    }

    // Preserve the proven capture -> clean radio reset -> replay sequence.
    // Keep the display session initialized throughout the 40-second headless
    // window, then power the physical panel/frontlight down only at the final
    // deep-sleep handoff. If a packet races this step the initialized session
    // remains reusable for the resulting unread redraw.
    if(meshink_board_radio_irq_asserted()){
        const bool dispatched=meshink_board_service_asserted_radio_irq();
        Serial.printf("[T5-DEEPSLEEP] sleep deferred: SX1262 IRQ appeared during handoff dispatched=%u\n",
                      dispatched?1U:0U);
        return false;
    }
    ui_quiesce_display_for_deep_sleep();
    if(meshink_board_radio_irq_asserted()){
        const bool dispatched=meshink_board_service_asserted_radio_irq();
        Serial.printf("[T5-DEEPSLEEP] sleep deferred: SX1262 IRQ arrived during display quiesce dispatched=%u\n",
                      dispatched?1U:0U);
        return false;
    }

    const MeshInkRadioStats handoff_stats=meshink_radio_stats();
    Serial.printf("[T5-DEEPSLEEP] sleep handoff rx=%lu err=%lu tx=%lu rxmode=%u\n",
                  (unsigned long)handoff_stats.packets_received,
                  (unsigned long)handoff_stats.receive_errors,
                  (unsigned long)handoff_stats.packets_sent,handoff_stats.continuous_rx?1U:0U);
    return meshink_board_enter_deep_sleep_standby();
}

void local_mesh_rx_wake_loop() {
    if(!local_rx_wake_runtime||!local_runtime_ready){delay(10);return;}

    // DIO1 is normally edge/ISR driven. Retained operation also polls its
    // level before every MeshCore pass so a missed GPIO edge cannot leave a
    // received packet stuck in the SX1262 and unacknowledged.
    if(meshink_board_radio_irq_asserted())
        meshink_board_service_asserted_radio_irq();

    the_mesh.loop();
    local_mesh_flush_contacts_save_if_due();

    if(ui_headless_message_alert_pending()){
        MeshInkPowerCriticalState alert_power{};
        const MeshInkPowerSleepCheck alert_power_result=
            local_mesh_headless_power_check(alert_power);
        Serial.printf("[T5-DEEPSLEEP] pre-alert battery check result=%u voltage=%s%umV\n",
                      (unsigned)alert_power_result,
                      alert_power.battery_mv_valid?"":"unavailable/",
                      alert_power.battery_mv_valid?(unsigned)alert_power.battery_mv:0U);
        if(alert_power_result==MeshInkPowerSleepCheck::Critical){
            local_mesh_prepare_shutdown();
            meshink_board_companion_release_resources();
            SPIFFS.end();
            ui_minimal_low_battery_shutdown(alert_power,"deep-message-alert");
        }
    }
    ui_service_headless_message_alert();

    const uint32_t now_ms=millis();
    const MeshInkRadioStats now=meshink_radio_stats();
    const bool activity=
        now.packets_received!=local_rx_wake_stats.packets_received||
        now.receive_errors!=local_rx_wake_stats.receive_errors||
        now.packets_sent!=local_rx_wake_stats.packets_sent;
    if(activity){
        Serial.printf("[T5-DEEPSLEEP] mesh activity +%lums rx=%lu(+%ld) err=%lu(+%ld) tx=%lu(+%ld) rxmode=%u\n",
                      (unsigned long)now_ms,
                      (unsigned long)now.packets_received,
                      (long)now.packets_received-(long)local_rx_wake_stats.packets_received,
                      (unsigned long)now.receive_errors,
                      (long)now.receive_errors-(long)local_rx_wake_stats.receive_errors,
                      (unsigned long)now.packets_sent,
                      (long)now.packets_sent-(long)local_rx_wake_stats.packets_sent,
                      now.continuous_rx?1U:0U);
        local_rx_wake_last_activity=now_ms;
        local_rx_wake_stats=now;
    }else if(now.continuous_rx!=local_rx_wake_stats.continuous_rx){
        Serial.printf("[T5-DEEPSLEEP] radio mode changed rxmode=%u\n",now.continuous_rx?1U:0U);
        local_rx_wake_stats=now;
    }

    if(!local_rx_wake_last_report||now_ms-local_rx_wake_last_report>=5000UL){
        local_rx_wake_last_report=now_ms;
        Serial.printf("[T5-DEEPSLEEP] headless heartbeat idle=%lums rx=%lu err=%lu tx=%lu rxmode=%u\n",
                      (unsigned long)(now_ms-local_rx_wake_last_activity),
                      (unsigned long)now.packets_received,
                      (unsigned long)now.receive_errors,
                      (unsigned long)now.packets_sent,
                      now.continuous_rx?1U:0U);
    }

    const bool pressed=meshink_primary_button_pressed();
    if(pressed&&!local_rx_wake_button_started)local_rx_wake_button_started=now_ms;
    if(pressed&&local_rx_wake_button_started&&now_ms-local_rx_wake_button_started>=2000UL){
        Serial.println("[T5-DEEPSLEEP] BOOT held during headless RX runtime; promoting running MeshCore into UI");
        if(local_mesh_promote_to_ui("deep-button-runtime"))return;
        Serial.println("[T5-DEEPSLEEP] UI promotion failed; staying in headless RX runtime");
    }
    if(!pressed)local_rx_wake_button_started=0;

    // MeshCore can delay flood processing for up to 32 seconds. Keep the
    // retained runtime alive for 40 quiet seconds, resetting on any
    // RX/TX/error activity, before attempting to re-enter deep sleep.
    if(now_ms-local_rx_wake_last_activity>=LOCAL_RX_WAKE_QUIET_MS&&
       (int32_t)(now_ms-local_rx_wake_sleep_retry)>=0){
        local_rx_wake_sleep_retry=now_ms+1000;
        if(now.continuous_rx){
            Serial.printf("[T5-DEEPSLEEP] headless quiet for %lums; re-entering deep sleep\n",
                          (unsigned long)LOCAL_RX_WAKE_QUIET_MS);
            if(!local_mesh_enter_deep_sleep_standby())
                Serial.println("[T5-DEEPSLEEP] re-sleep deferred; MeshCore will keep running and retry");
        }
    }
    delay(1);
}

bool local_mesh_is_running(){return local_runtime_ready;}
