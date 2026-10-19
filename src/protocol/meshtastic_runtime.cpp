#include "meshtastic_runtime.h"
#include "../setup_short_name.h"
#include "meshtastic_companion.h"
#include "meshtastic_platform.cpp" // headless service definitions (single TU)
#include "meshtastic_radio.h"
#include "meshtastic_worker.h"
#include "../../lib/Meshtastic/src/FSCommon.h"
#include "../../lib/Meshtastic/src/SPILock.h"
#include "../../lib/Meshtastic/src/PowerMon.h"
#include "../../lib/Meshtastic/src/mesh/TransmitHistory.h"
#include "../../lib/Meshtastic/src/PowerFSM.h"
#include "../../lib/Meshtastic/src/PowerStatus.h"
#include "../../lib/Meshtastic/src/airtime.h"
#include "../../lib/Meshtastic/src/concurrency/OSThread.h"
#include "../../lib/Meshtastic/src/mesh/NodeDB.h"
#include "../../lib/Meshtastic/src/mesh/Channels.h"
#include <Preferences.h>
#include "../../lib/Meshtastic/src/mesh/ReliableRouter.h"
#include "../../lib/Meshtastic/src/mesh/MeshService.h"
#include "../../lib/Meshtastic/src/mesh/CryptoEngine.h"
#include "../../lib/Meshtastic/src/mesh/RadioLibInterface.h"
#include "../../lib/Meshtastic/src/mesh/MeshRadio.h"
#include "../../lib/Meshtastic/src/mesh/Throttle.h"
#include "../../lib/Meshtastic/src/modules/Modules.h"
#include "../../lib/Meshtastic/src/modules/PositionModule.h"
#include "../../lib/Meshtastic/src/modules/NodeInfoModule.h"
#include "../../lib/Meshtastic/src/mesh/PositionPrecision.h"
#include "../../lib/Meshtastic/src/gps/RTC.h"
#include "../hardware/gps.h"
#include "../timezone_service.h"
#include "../hardware/board.h"
#include "../hardware/io_coordinator.h"
#include "../hardware/rtc.h"
#include "../../lib/Meshtastic/src/mqtt/MQTT.h"
#include "../../lib/Meshtastic/src/main.h"
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include <Arduino.h>
#include <new>
#include <memory>
#include <cstring>
#include <cctype>
#include <cstdio>
#include <SPIFFS.h>

// Missing upstream-main globals: strictly the protocol engine's state.
// There is no second device application or hardware initialization.
Router* router=nullptr;
MQTT* mqtt=nullptr;              // MeshInk does not start a second network bridge
meshtastic::PowerStatus* powerStatus=nullptr;
bool pauseBluetoothLogging=false;
bool runASAP=false;
uint32_t rebootAtMsec=0;
uint32_t shutdownAtMsec=0;
bool suppressRebootBanner=true;
bool isUSBPowered=false;
uint32_t serialSinceMsec=0;
uint32_t timeLastPowered=0;
SPISettings spiSettings(4000000, MSBFIRST, SPI_MODE0);

// MQTT is intentionally not instantiated for this standalone e-paper client.
// Official router/PhoneAPI call these methods only when mqtt is non-null.
void MQTT::onSend(const meshtastic_MeshPacket&,const meshtastic_MeshPacket&,ChannelIndex){}
void MQTT::onClientProxyReceive(meshtastic_MqttClientProxyMessage){}

void updateBatteryLevel(uint8_t){
    // MeshInk owns the power gauge and status-bar display, not Meshtastic BLE.
}
meshtastic_DeviceMetadata getDeviceMetadata(){
    meshtastic_DeviceMetadata meta=meshtastic_DeviceMetadata_init_default;
    strncpy(meta.firmware_version,APP_VERSION,sizeof(meta.firmware_version)-1);
    meta.firmware_version[sizeof(meta.firmware_version)-1]=0;
    meta.role=config.device.role;
    meta.hw_model=owner.hw_model;
    meta.hasBluetooth=meshink_meshtastic_companion_active();
    meta.hasWifi=false;
    meta.hasEthernet=false;
    meta.hasPKC=true;
    return meta;
}

namespace {
bool native_ready=false;
bool ever_started=false;
bool onboarding_radio_staged=false;
// The receiver is serviced by MeshInk on the UI core. The worker only sees
// an immutable position/time snapshot, never the live MicroNMEA state.
portMUX_TYPE gps_snapshot_mutex=portMUX_INITIALIZER_UNLOCKED;
MeshInkMeshtasticGpsSnapshot gps_snapshot{};
bool gps_snapshot_received=false;

// Only the native router knows when a retry actually fires. Observe its
// protected pending map AFTER upstream runOnce(), without changing the
// retransmission algorithm or timing. Events cross cores through the worker
// FIFO; MeshInk's UI never reads native mutable routing structures.
class MeshInkProgressRouter final : public ReliableRouter {
    struct LastReport { uint32_t id=0; uint8_t count=0; };
    LastReport reported_[24]{};
    size_t next_slot_=0;
public:
    int32_t runOnce() override {
        const int32_t delay_ms=ReliableRouter::runOnce();
        if(!nodeDB)return delay_ms;
        const uint32_t self=nodeDB->getNodeNum();
        for(const auto& entry:pending){
            const PendingPacket& entry_state=entry.second;
            const auto* packet=entry_state.packet;
            if(!packet||packet->from!=self||isBroadcast(packet->to)||
               entry_state.initialNumRetransmissions!=NUM_RELIABLE_UNICAST_ATTEMPTS-1)
                continue;
            const uint8_t total=entry_state.initialNumRetransmissions;
            if(entry_state.numRetransmissions>total)continue;
            const uint8_t completed=total-entry_state.numRetransmissions;
            if(!completed)continue;
            LastReport* previous=nullptr;
            for(auto& slot:reported_)
                if(slot.id==packet->id){previous=&slot;break;}
            if(previous&&previous->count>=completed)continue;
            if(!meshink_meshtastic_worker_note_retry(packet->id,completed,total))
                continue; // leave last-report unchanged and offer again
            if(!previous){
                previous=&reported_[next_slot_];
                next_slot_=(next_slot_+1)%24;
            }
            previous->id=packet->id;
            previous->count=completed;
#if MESHINK_MESHTASTIC_HW_TEST_LOG
            Serial.printf("[MT-RETRY] packet=%08lx retry=%u/%u\\n",
                          (unsigned long)packet->id,(unsigned)completed,(unsigned)total);
#endif
        }
        return delay_ms;
    }
};

// Observe Meshtastic's native WarmNodeStore snapshot and any interrupted
// SafeFile staging copy without modifying either. A 16-byte header read is
// required by upstream; a short read cannot be confused with CRC failure.
#if MESHINK_MESHTASTIC_HW_TEST_LOG
void report_warmstore_file(const char* path){
    File f=SPIFFS.open(path,"r");
    if(!f){
        Serial.printf("[MT-WARM] %s absent\n",path);
        return;
    }
    const size_t size=f.size();
    uint8_t header[16]{};
    const size_t n=f.read(header,sizeof(header));
    // Only the snapshot HEADER is inspected, never node identities or keys.
    const uint32_t magic=n==sizeof(header)
        ?(uint32_t)header[0]|((uint32_t)header[1]<<8)|
         ((uint32_t)header[2]<<16)|((uint32_t)header[3]<<24)
        :0;
    const unsigned count=n==sizeof(header)
        ?(unsigned)header[8]|((unsigned)header[9]<<8):0;
    Serial.printf("[MT-WARM] %s size=%lu header_read=%u/16 magic=%08lx count=%u %s\n",
        path,(unsigned long)size,(unsigned)n,(unsigned long)magic,count,
        n==sizeof(header)?"header-present":"SHORT-HEADER");
    f.close();
}
#endif

// The MeshInk UI stores its display name and timezone in t5-ui NVS; the
// upstream Meshtastic NodeDB is authoritative for the companion app. Migrate
// only Meshtastic's untouched default values. Never overwrite changes made
// later through the Android app, keys, channels, or LoRa configuration.
// The UI saves first-time radio selections while the native engine is OFF.
// Install verified settings immediately after NodeDB opens, before Router,
// modules, PhoneAPI or SX1262. No live Admin packet or concurrent writer.
bool native_apply_onboarding_radio(){
    Preferences draft;
    if(!draft.begin("mt-onboard",true))return true;
    const bool pending=draft.getBool("pending",false);
    const auto region=(meshtastic_Config_LoRaConfig_RegionCode)draft.getUChar("region",0);
    const auto preset=(meshtastic_Config_LoRaConfig_ModemPreset)draft.getUChar("preset",255);
    const uint8_t hops=draft.getUChar("hops",0);
    draft.end();
    if(!pending){
        // A previous wizard used to mark setup complete without applying LoRa.
        // Recover automatically without deleting native identity, channels or
        // either protocol's journals.
        if(config.lora.region==meshtastic_Config_LoRaConfig_RegionCode_UNSET){
            Preferences ui;
            if(ui.begin("t5-ui",false)){
                if(ui.getBool("setup_mst",false)){
                    ui.putBool("setup_mst",false);
                    Serial.println("[MT-SETUP] invalid legacy completion: region UNSET; wizard required");
                }
                ui.end();
            }
            return false;
        }
        return true;
    }
    if(region==meshtastic_Config_LoRaConfig_RegionCode_UNSET||hops<1||hops>7){
        Serial.println("[MT-SETUP] rejected invalid pending region/hops");
        return false;
    }
    static meshtastic_LoRaRegionPresetMap map{};
    getRegionPresetMap(map); // Same pinned official legality map as PhoneAPI.
    bool permitted=false;
    for(size_t i=0;i<map.region_groups_count&&!permitted;++i){
        if(map.region_groups[i].region!=region)continue;
        const unsigned group=map.region_groups[i].group_index;
        if(group>=map.groups_count)break;
        for(size_t j=0;j<map.groups[group].presets_count;++j)
            if(map.groups[group].presets[j]==preset){permitted=true;break;}
    }
    if(!permitted){
        Serial.println("[MT-SETUP] pending region/preset not in official map");
        return false;
    }
    config.lora.region=region;
    config.lora.use_preset=true;
    config.lora.modem_preset=preset;
    config.lora.hop_limit=hops;
    config.lora.tx_enabled=true;
    if(!nodeDB->saveToDisk(SEGMENT_CONFIG)){
        Serial.println("[MT-SETUP] native radio config save failed; setup pending");
        return false;
    }
    File verify=SPIFFS.open("/prefs/config.proto","r");
    const bool stored=verify&&verify.size()>0;
    if(verify)verify.close();
    if(!stored){
        Serial.println("[MT-SETUP] native radio config file unavailable after save");
        return false;
    }
    onboarding_radio_staged=true;
    Serial.printf("[MT-SETUP] native radio saved region=%u preset=%u hops=%u\\n",
                  (unsigned)region,(unsigned)preset,(unsigned)hops);
    return true;
}

bool native_finish_onboarding(){
    if(!onboarding_radio_staged)return true;
    Preferences ui;
    if(!ui.begin("t5-ui",false))return false;
    const bool identity_pending=ui.getBool("mt_name_pending",false);
    const String long_name=ui.getString("mt_long",identity_pending?ui.getString("name",""):"");
    const String short_name=ui.getString("mt_short","");
    if(identity_pending||!long_name.length()||!short_name.length()||
       strcmp(owner.long_name,long_name.c_str())||
       strcmp(owner.short_name,short_name.c_str())){
        ui.end();
        Serial.println("[MT-SETUP] identity not yet saved; setup remains pending");
        return false;
    }
    // Only claim setup is complete after native radio and identity were saved.
    const bool saved=ui.putBool("setup_mst",true)==1&&
                     ui.putBool("complete",true)==1&&
                     ui.putUChar("setup_return",0)==1&&
                     ui.putUChar("setup_choice",0)==1;
    ui.end();
    if(!saved)return false;
    Preferences verify;
    if(!verify.begin("t5-ui",true))return false;
    const bool confirmed=verify.getBool("setup_mst",false);
    verify.end();
    if(!confirmed)return false;
    Preferences draft;
    if(draft.begin("mt-onboard",false)){
        if(!draft.remove("pending"))
            Serial.println("[MT-SETUP] WARN pending marker retained; will revalidate next boot");
        draft.end();
    }
    Serial.println("[MT-SETUP] native radio + identity verified; setup complete");
    return true;
}

void bridge_ui_preferences_after_nodedb(){
    Preferences pref;
    String name, short_name;
    bool identity_pending=false;
    if(pref.begin("t5-ui",true)){
        name=pref.getString("mt_long",pref.getBool("mt_name_pending",false)?pref.getString("name",""):"");
        short_name=pref.getString("mt_short","");
        identity_pending=pref.getBool("mt_name_pending",false);
        pref.end();
    }
    // Only an explicit initial setup confirmation may change the native owner.
    // Never migrate from a shared/old name on ordinary boot or after restore.
    if(identity_pending&&name.length()&&short_name.length()){
        meshtastic_User updated=owner; // preserve pubkey, id and all other fields
        snprintf(updated.long_name,sizeof(updated.long_name),"%s",name.c_str());
        snprintf(updated.short_name,sizeof(updated.short_name),"%s",short_name.c_str());
        if(nodeDB->updateUser(nodeDB->getNodeNum(),updated)){
            owner=updated;
            const bool saved=nodeDB->saveToDisk(SEGMENT_DEVICESTATE|SEGMENT_NODEDATABASE);
            Serial.printf("[MT-BRIDGE] native name=%s short=%s saved=%u\n",
                          owner.long_name,owner.short_name,saved?1U:0U);
            if(saved&&identity_pending){
                Preferences done;
                if(done.begin("t5-ui",false)){
                    done.remove("mt_name_pending");
                    done.end();
                }
            }
        }else Serial.println("[MT-BRIDGE] WARN native name update not accepted");
    }

    int changed=0;
    // One shared POSIX rule is authoritative for MeshCore, MeshInk and
    // Meshtastic. Meshtastic's config.device.tzdef is a protocol-facing view,
    // updated from that rule on every startup, not a second timezone store.
    char rule[65]{};
    if(meshink_timezone::read(rule) &&
       strcmp(config.device.tzdef,rule)!=0){
        snprintf(config.device.tzdef,sizeof(config.device.tzdef),"%s",rule);
        changed|=SEGMENT_CONFIG;
        Serial.printf("[MT-BRIDGE] shared timezone applied to Meshtastic: %s\n",
                      config.device.tzdef);
    }
#if ENV_INCLUDE_GPS == 1
    // MeshInk owns the real L76K UART, even though Meshtastic's *second*
    // GPS driver is excluded. Report actual board capability to PhoneAPI.
    if(meshink_board_has_gps() &&
       config.position.gps_mode==meshtastic_Config_PositionConfig_GpsMode_NOT_PRESENT){
        config.position.gps_mode=
            meshink_gps_constellation_mode()==MeshInkGpsConstellationMode::None
              ?meshtastic_Config_PositionConfig_GpsMode_DISABLED
              :meshtastic_Config_PositionConfig_GpsMode_ENABLED;
        changed|=SEGMENT_CONFIG;
        Serial.printf("[MT-BRIDGE] native GPS capability mode=%u\\n",
                      (unsigned)config.position.gps_mode);
    }
#endif
    if(changed && !nodeDB->saveToDisk(changed))
        Serial.println("[MT-BRIDGE] WARN native device config persistence failed");
}
}

void meshink_meshtastic_native_set_gps_snapshot(const MeshInkMeshtasticGpsSnapshot& snapshot){
    portENTER_CRITICAL(&gps_snapshot_mutex);
    gps_snapshot=snapshot;
    gps_snapshot_received=true;
    portEXIT_CRITICAL(&gps_snapshot_mutex);
}

bool meshink_meshtastic_native_read_identity(char* long_name,size_t long_capacity,
                                            char* short_name,size_t short_capacity){
    if(!native_ready||!long_name||long_capacity<2||!short_name||short_capacity<2||
       !owner.long_name[0]||!owner.short_name[0])return false;
    snprintf(long_name,long_capacity,"%s",owner.long_name);
    snprintf(short_name,short_capacity,"%s",owner.short_name);
    return true;
}

bool meshink_meshtastic_native_begin(){
    if(ever_started)return native_ready;
    ever_started=true;
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] engine boot: initial heap=%u psram=%u\n",
                  (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
#endif
    // Mirror the official firmware's initialization order, without running
    // any of its display, phone BLE, USB, GPS or power application startup.
    concurrency::hasBeenSetup=true;
    concurrency::OSThread::setup();
    initSPI();
    // MeshInk owns USB serial; use only the in-process official PhoneAPI.
    powerMonInit();
    serialSinceMsec=millis();
    fsInit(); // uses MeshInk's already-mounted SPIFFS, no autoformat
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    // Inspect both paths BEFORE NodeDB opens warm.dat; don't create, remove,
    // rename, migrate or rewrite possible recovery content.
    report_warmstore_file("/prefs/warm.dat");
    report_warmstore_file("/prefs/warm.dat.tmp");
#endif
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] official SPI lock, power monitor, SPIFFS initialized; heap=%u\n",
                  (unsigned)ESP.getFreeHeap());
#endif
    nodeDB=new(std::nothrow) NodeDB();
    if(!nodeDB){
        Serial.println("[MeshInk/MT] NodeDB failed");
        return false;
    }
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] NodeDB ready node=!%08lx region=%u preset=%u tx_enabled=%u heap=%u\n",
                  (unsigned long)nodeDB->getNodeNum(),(unsigned)config.lora.region,
                  (unsigned)config.lora.modem_preset,config.lora.tx_enabled?1U:0U,
                  (unsigned)ESP.getFreeHeap());
#endif
    if(!native_apply_onboarding_radio())return false;
    // Privacy fail-closed: if the user explicitly disabled sharing, clear
    // any stale native on-wire channel precision BEFORE the radio is attached.
    // A power loss during an earlier setting change must never re-enable
    // location transmissions on the next boot. Perform this only when needed,
    // before the UI/radio service runs, rather than an asynchronous Admin
    // reconfiguration after startup. Do not alter an enabled user's channels.
    {
        Preferences location_prefs;
        if(location_prefs.begin("mt-location",true)){
            const bool configured=location_prefs.getBool("configured",false);
            const bool share=location_prefs.getBool("master",false);
            location_prefs.end();
            if(configured&&!share){
                unsigned disabled=0;
                for(unsigned i=0;i<channels.getNumChannels();++i){
                    auto& channel=channels.getByIndex(i);
                    if(channel.role==meshtastic_Channel_Role_DISABLED||
                       !channel.has_settings||!channel.settings.has_module_settings)
                        continue;
                    if(channel.settings.module_settings.position_precision){
                        channel.settings.module_settings.position_precision=0;
                        ++disabled;
                    }
                }
                if(disabled){
                    const bool persisted=nodeDB->saveToDisk(SEGMENT_CHANNELS);
                    Serial.printf("[MT-LOC] cold-start privacy guard: disabled %u channels; saved=%u\n",
                                  disabled,persisted?1u:0u);
                }
            }
        }
    }
    // Never let a position restored from the official NodeDB act as a fresh
    // GNSS fix after reboot. MeshInk keeps the historical map location in
    // its own separate NVS record; upstream RF positions require a new fix.
    // An intentional user-configured fixed position is not a GNSS fix.
    if(!config.position.fixed_position)
        nodeDB->clearLocalPosition();
    TransmitHistory::getInstance()->loadFromDisk();
    if(nodeStatus)nodeStatus->observe(&nodeDB->newStatus);
    router=new(std::nothrow) MeshInkProgressRouter();
    service=new(std::nothrow) MeshService();
    if(!router||!service){
        Serial.println("[MeshInk/MT] Router or MeshService allocation failed");
        return false;
    }
    service->init();
    bridge_ui_preferences_after_nodedb();
    if(!native_finish_onboarding())return false;
    if(!airTime)airTime=new(std::nothrow) AirTime();
    if(!airTime){
        Serial.println("[MeshInk/MT] Airtime allocator failed");
        return false;
    }
    if(!powerStatus)powerStatus=new(std::nothrow) meshtastic::PowerStatus();
    if(!powerStatus)return false;
    setupModules();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] official Router/MeshService/modules ready; heap=%u psram_free=%u\n",
                  (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram());
#endif
    PowerFSM_setup(); // official excluded-power-FSM no-op, MeshInk owns sleep
    native_ready=meshink_meshtastic_attach_radio(*router);
    if(!native_ready)Serial.println("[MeshInk/MT] Native radio initialization failed");
    else {
        Serial.println("[MeshInk/MT] Native Meshtastic node initialized");
        nodeDB->notifyObservers(true); // Match official startup; independent of debug mode.
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        Serial.printf("[MT-TEST] engine READY heap=%u free_psram=%u stack_watermark_words=%u\n",
                      (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
#endif
    }
    return native_ready;
}


void meshink_meshtastic_native_gps_update(){
#if ENV_INCLUDE_GPS == 1
    if(!native_ready||!nodeDB||!service)return;
    MeshInkMeshtasticGpsSnapshot snapshot{};
    portENTER_CRITICAL(&gps_snapshot_mutex);
    const bool have_snapshot=gps_snapshot_received;
    snapshot=gps_snapshot;
    portEXIT_CRITICAL(&gps_snapshot_mutex);
    if(!have_snapshot)return;

    // The proven MeshInk RTC owns physical timekeeping. Official Meshtastic's
    // protocol-only logical clock follows a snapshot from the owning UI task.
    if(getRTCQuality()==RTCQualityNone && snapshot.rtc_valid){
        const uint32_t utc=snapshot.utc;
        if(utc>=1609459200UL){
            timeval tv{};tv.tv_sec=utc;
            perhapsSetRTC(RTCQualityDevice,&tv);
#if MESHINK_MESHTASTIC_HW_TEST_LOG
            Serial.printf("[MT-TEST] MeshInk RTC -> Meshtastic logical time %lu\n",(unsigned long)utc);
#endif
        }
    }

    static bool had_fix=false;
    static uint32_t last_ms=0,last_stamp=0;
    static long last_lat=0,last_lon=0;
    const MeshInkGpsStatus& fix=snapshot.fix;
    const bool enabled=snapshot.enabled;
    const bool valid=enabled&&fix.available&&fix.valid&&
                     fix.error==MeshInkGpsError::None&&
                     fix.latitude>=-90000000L&&fix.latitude<=90000000L&&
                     fix.longitude>=-180000000L&&fix.longitude<=180000000L;
    if(!valid){
        if(had_fix){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
            Serial.printf("[MT-TEST] MeshInk GPS fix lost/off: enabled=%u available=%u error=%u\n",
                          enabled?1U:0U,fix.available?1U:0U,(unsigned)fix.error);
#endif
        }
        // Match the official receiver's lost-lock handoff: do not let the
        // upstream PositionModule periodically rebroadcast a cached GNSS
        // location after MeshInk's GPS has been disabled or lost its fix.
        // A deliberately configured fixed Meshtastic position is unaffected.
        if(had_fix && !config.position.fixed_position)
            nodeDB->clearLocalPosition();
        had_fix=false;
        return; // Never present a stale fix as new.
    }

    // Once MeshInk's board-owned RTC confirms a GPS-authoritative clock,
    // upgrade only the protocol clock-quality metadata. Meshtastic does not
    // operate the GPS receiver or hardware RTC.
    if(snapshot.gps_authoritative && snapshot.rtc_valid &&
       getRTCQuality()<RTCQualityGPS){
        const uint32_t utc=snapshot.utc;
        if(utc>=1609459200UL){
            timeval tv{};tv.tv_sec=utc;
            perhapsSetRTC(RTCQualityGPS,&tv);
#if MESHINK_MESHTASTIC_HW_TEST_LOG
            Serial.printf("[MT-TEST] MeshInk GPS-time authority -> Meshtastic logical clock %lu\n",
                          (unsigned long)utc);
#endif
        }
    }

    const uint32_t now=millis();
    const bool fresh=(!had_fix||last_stamp!=fix.timestamp||
                      last_lat!=fix.latitude||last_lon!=fix.longitude);
    if(!fresh || (had_fix&&(uint32_t)(now-last_ms)<5000UL))return;
    had_fix=true;last_ms=now;last_stamp=fix.timestamp;
    last_lat=fix.latitude;last_lon=fix.longitude;

    // Meshtastic users can explicitly configure a fixed position. Respect
    // that protocol preference without changing MeshInk's GNSS hardware.
    if(config.position.fixed_position){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        static bool fixed_logged=false;
        if(!fixed_logged){
            fixed_logged=true;
            Serial.println("[MT-TEST] MeshInk GPS fix: retaining configured Meshtastic fixed position");
        }
#endif
        return;
    }

    meshtastic_Position p=meshtastic_Position_init_default;
    // MeshInk's MicroNMEA reports degrees*1e6; official positions use *1e7.
    p.latitude_i=static_cast<int32_t>(fix.latitude*10L);
    p.longitude_i=static_cast<int32_t>(fix.longitude*10L);
    p.has_latitude_i=true;p.has_longitude_i=true;
    p.location_source=meshtastic_Position_LocSource_LOC_INTERNAL;
    p.sats_in_view=fix.satellites>0?(uint32_t)fix.satellites:0U;
    p.timestamp=fix.timestamp;
    if(snapshot.rtc_valid)p.time=snapshot.utc;

    // Route ONLY into the genuine native NodeDB and PositionModule. Upstream
    // handles position precision/privacy, broadcasts and smart-movement rules.
    nodeDB->updatePosition(nodeDB->getNodeNum(),p,RX_SRC_LOCAL);
    if(positionModule)positionModule->handleNewPosition();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] MeshInk GPS -> official position sats=%u fix_ts=%lu utc=%lu module=%u\n",
                  (unsigned)p.sats_in_view,(unsigned long)p.timestamp,
                  (unsigned long)p.time,positionModule?1U:0U);
#endif
#endif
}

bool meshink_meshtastic_native_quick_broadcast(bool location){
    // Only the pinned native worker may touch upstream router/module state.
    // Meshtastic enforces channel privacy/precision and packet routing.
    if(!native_ready||!config.lora.tx_enabled||!airTime||!nodeDB)return false;
    if(location){
        if(!positionModule)return false;
        // Restored positions cannot masquerade as a fresh GPS fix.
        if(!config.position.fixed_position&&!nodeDB->hasLocalPositionSinceBoot())return false;
        uint8_t channel=0;
        if(!findPositionChannel(channel))return false;
        const bool tracker=config.device.role==meshtastic_Config_DeviceConfig_Role_TRACKER||
                           config.device.role==meshtastic_Config_DeviceConfig_Role_TAK_TRACKER;
        if(!airTime->isTxAllowedChannelUtil(!tracker))return false;
        return positionModule->sendOurPosition(NODENUM_BROADCAST,false,channel);
    }
    if(!nodeInfoModule||!airTime->isTxAllowedAirUtil())return false;
    return nodeInfoModule->sendOurNodeInfo(NODENUM_BROADCAST,false);
}

void meshink_meshtastic_native_loop(){
    if(!native_ready)return;
    runASAP=false;
    // Match upstream main.cpp's radio RX/recovery upkeep. The native radio
    // handles real DIO1 IRQ, missed-edge polling and RX re-arming itself.
    if (auto* radio = RadioLibInterface::instance) {
        static uint32_t lastIrqPoll=0;
        if(!Throttle::isWithinTimespanMs(lastIrqPoll,1000)){
            lastIrqPoll=millis();
            radio->pollMissedIrqs();
        }
        static uint32_t lastMaintenance=0;
        if(!Throttle::isWithinTimespanMs(lastMaintenance,AGC_RESET_INTERVAL_MS)){
            lastMaintenance=millis();
            radio->updateNoiseFloor();
            radio->periodicRadioMaintenance();
        }
    }
    service->loop();
    // Run the genuine Meshtastic router/modules without sleeping MeshInk UI.
    concurrency::mainController.runOrDelay();
    // NodeDB already holds live config in RAM. Commit any saved segments that
    // were deferred while the e-paper panel was physically updating.
    static uint32_t last_storage_flush=0;
    const uint32_t flush_now=millis();
    if ((uint32_t)(flush_now-last_storage_flush)>=500UL) {
        last_storage_flush=flush_now;
        const int pending=meshink_io_take_deferred_meshtastic_save();
        if (pending && !nodeDB->saveToDisk(pending)) {
            meshink_io_defer_meshtastic_save(pending);
            Serial.println("[MeshInk/IO] ERROR: deferred config commit failed");
        }
        // The official transmission-history cache independently stays dirty
        // until a successful commit; this retry is a no-op when clean.
        if(transmitHistory) (void)transmitHistory->saveToDisk();
    }
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    static uint32_t lastDiagnostic=0;
    if(!lastDiagnostic || (uint32_t)(millis()-lastDiagnostic)>=30000UL){
        lastDiagnostic=millis();
        const auto queue=router->getQueueStatus();
        Serial.printf("[MT-TEST] heartbeat uptime=%lus node=!%08lx region=%u tx=%u queue=%u/%u heap=%u psram=%u stack_words=%u\n",
                      (unsigned long)(millis()/1000UL),(unsigned long)nodeDB->getNodeNum(),
                      (unsigned)config.lora.region,config.lora.tx_enabled?1U:0U,
                      (unsigned)queue.free,(unsigned)queue.maxlen,
                      (unsigned)ESP.getFreeHeap(),(unsigned)ESP.getFreePsram(),
                      (unsigned)uxTaskGetStackHighWaterMark(nullptr));
        meshink_meshtastic_radio_report();
    }
#endif
}

void meshink_meshtastic_native_stop(){
    // Best-effort save before a requested backup/reboot. If flash rejects a
    // final native save, the backup still captures the latest durable files;
    // this warning is not a reason to discard an otherwise sound archive.
    // The worker must nevertheless finish shutdown before storage is copied.
    const int pending=meshink_io_take_deferred_meshtastic_save();
    if (pending && nodeDB && !nodeDB->saveToDisk(pending)) {
        meshink_io_defer_meshtastic_save(pending);
        Serial.println("[MeshInk/IO] ERROR: shutdown with unsaved config");
    }
    if (transmitHistory && !transmitHistory->saveToDisk())
        Serial.println("[MeshInk/IO] WARN: shutdown with unsaved TX history");
    // Hardware is exclusively owned by the boot-selected router. Final shutdown
    // is followed by the MeshInk reboot-to-switch; no hot swapping of ISR state.
    native_ready=false;
}
