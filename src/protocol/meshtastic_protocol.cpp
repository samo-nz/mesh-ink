#include <Arduino.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/admin.pb.h"
#include <esp_system.h>
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include "mesh_protocol_backend.h"
#include "meshtastic_official_phoneapi.h"
#include "meshtastic_runtime.h"
#include "meshtastic_ui_data.h"
#include "../../include/meshtastic_official_version.h"
#include "../ui_onboarding.h"
#include "../hardware/storage.h"
#include "../hardware/board.h"
#include "../hardware/gps.h"
#include "../hardware/rtc.h"
#include <cstring>

// MeshInk handles the UI; all routing, channel crypto and network
// retransmissions remain in the pinned official Meshtastic core.
namespace {
bool ready=false;
bool gps_stable_fix=false,gps_candidate_fix=false;
uint32_t gps_candidate_since=0,gps_next_status=0;
void update_shared_gps_status(){
#if ENV_INCLUDE_GPS == 1
    const uint32_t now=millis();
    if((int32_t)(now-gps_next_status)<0)return;
    gps_next_status=now+(ui_is_standby()?10000UL:1000UL);
    const bool enabled=meshink_gps_constellation_mode()!=MeshInkGpsConstellationMode::None;
    const MeshInkGpsStatus status=enabled?meshink_gps_read_status():MeshInkGpsStatus{};
    const bool valid=enabled&&status.error==MeshInkGpsError::None&&status.valid;
    if(!enabled||status.error!=MeshInkGpsError::None){
        gps_stable_fix=false;gps_candidate_fix=false;gps_candidate_since=now;
    }else{
        if(gps_candidate_fix!=valid){
            gps_candidate_fix=valid;gps_candidate_since=now;
        }
        if(valid==gps_stable_fix||now-gps_candidate_since>=3000U)
            gps_stable_fix=valid;
    }
    ui_status_set_gps(enabled,gps_stable_fix,
        gps_stable_fix?status.satellites:0,
        gps_stable_fix?status.latitude:0,
        gps_stable_fix?status.longitude:0,
        gps_stable_fix?status.timestamp:0,status.error);
#endif
}
void shared_gps_mode_changed(MeshInkGpsConstellationMode mode){
#if ENV_INCLUDE_GPS == 1
    meshink_gps_set_provider_enabled(mode!=MeshInkGpsConstellationMode::None);
    gps_stable_fix=false;gps_candidate_fix=false;
    gps_candidate_since=millis();gps_next_status=0;
    update_shared_gps_status();
#else
    (void)mode;
#endif
}
bool shared_my_location(long& latitude,long& longitude){
#if ENV_INCLUDE_GPS == 1
    const MeshInkGpsStatus status=meshink_gps_read_status();
    if(status.valid&&status.error==MeshInkGpsError::None){
        latitude=status.latitude;longitude=status.longitude;return true;
    }
#endif
    latitude=longitude=0;return false;
}
void start(){
    // Follow MeshCore's shared storage-first startup ordering. The selected
    // protocol owns no board filesystems or data partitions itself.
    const bool storage_ready=meshink_storage_mount_internal_safe();
    if(!storage_ready)ui_show_storage_initializing();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] protocol startup storage=%s heap=%u\n",
                  storage_ready?"mounted":"MOUNT FAILED",(unsigned)ESP.getFreeHeap());
#endif
    // Same board-owned power/SPI settling boundary as MeshCore's local boot.
    // The official Meshtastic SX1262 driver initializes AFTER this handoff.
    meshink_board_begin_local();
    meshink_meshtastic_ui_begin();
    ui_use_data_provider(meshink_meshtastic_ui_provider());
    // Native Router, NodeDB and MeshService must initialize before PhoneAPI.
    // This seam does not initialize a second firmware application.
    const bool engine_ready=meshink_meshtastic_native_begin();
    const bool phone_ready=engine_ready&&meshink_official_phoneapi_open(1);
    ready=engine_ready&&phone_ready;
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] handoff engine=%u PhoneAPI=%u ready=%u heap=%u\n",
                  engine_ready?1U:0U,phone_ready?1U:0U,ready?1U:0U,
                  (unsigned)ESP.getFreeHeap());
#endif
#if ENV_INCLUDE_GPS == 1
    meshink_gps_service_begin();
    // MeshInk GNSS preferences control the sole physical receiver.
    shared_gps_mode_changed(meshink_gps_constellation_mode());
    gps_next_status=0;update_shared_gps_status();
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.println("[MT-TEST] GPS: MeshInk driver active; native Meshtastic GPS excluded");
#endif
#endif
}
void poll(){
#if ENV_INCLUDE_GPS == 1
    // Same proven physical GNSS parser and OFF-state UART drain as MeshCore.
    meshink_gps_service_loop();
    meshink_gps_background_tick();
#endif
    meshink_rtc_tick();
    update_shared_gps_status();
    if(!ready)return;
    meshink_meshtastic_native_gps_update();
    meshink_meshtastic_native_loop();
    uint8_t bytes[meshtastic_FromRadio_size]{};
    for(unsigned i=0;i<12&&meshink_official_phoneapi_has_data();++i){
        const size_t n=meshink_official_phoneapi_receive(bytes,sizeof(bytes));
        if(!n)break;
        meshtastic_FromRadio data=meshtastic_FromRadio_init_zero;
        pb_istream_t input=pb_istream_from_buffer(bytes,n);
        if(pb_decode(&input,meshtastic_FromRadio_fields,&data)){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
            switch(data.which_payload_variant){
                case meshtastic_FromRadio_my_info_tag:
                    Serial.printf("[MT-TEST] PhoneAPI my_info node=!%08lx\n",
                                  (unsigned long)data.my_info.my_node_num);break;
                case meshtastic_FromRadio_region_presets_tag:
                    Serial.printf("[MT-TEST] PhoneAPI region presets groups=%u mappings=%u\n",
                                  (unsigned)data.region_presets.groups_count,
                                  (unsigned)data.region_presets.region_groups_count);break;
                case meshtastic_FromRadio_config_complete_id_tag:
                    Serial.printf("[MT-TEST] PhoneAPI config_complete nonce=%lu\n",
                                  (unsigned long)data.config_complete_id);break;
                case meshtastic_FromRadio_config_tag:
                    if(data.config.which_payload_variant==meshtastic_Config_lora_tag){
                        const auto& lc=data.config.payload_variant.lora;
                        Serial.printf("[MT-TEST] PhoneAPI LoRa config region=%u preset=%u use_preset=%u tx=%u hop=%u\n",
                                      (unsigned)lc.region,(unsigned)lc.modem_preset,
                                      lc.use_preset?1U:0U,lc.tx_enabled?1U:0U,
                                      (unsigned)lc.hop_limit);
                    }
                    break;
                case meshtastic_FromRadio_channel_tag:
                    Serial.printf("[MT-TEST] PhoneAPI channel slot=%d role=%u has_settings=%u\n",
                                  (int)data.channel.index,(unsigned)data.channel.role,
                                  data.channel.has_settings?1U:0U);break;
                case meshtastic_FromRadio_queueStatus_tag:
                    Serial.printf("[MT-TEST] PhoneAPI queue_status packet=%08lx result=%ld free=%u/%u\n",
                                  (unsigned long)data.queueStatus.mesh_packet_id,
                                  (long)data.queueStatus.res,(unsigned)data.queueStatus.free,
                                  (unsigned)data.queueStatus.maxlen);break;
                case meshtastic_FromRadio_packet_tag:
                    Serial.printf("[MT-TEST] FromRadio packet id=%08lx from=!%08lx to=!%08lx ch=%u port=%u request=%08lx\n",
                                  (unsigned long)data.packet.id,(unsigned long)data.packet.from,
                                  (unsigned long)data.packet.to,(unsigned)data.packet.channel,
                                  (unsigned)(data.packet.which_payload_variant==meshtastic_MeshPacket_decoded_tag?
                                  data.packet.decoded.portnum:0),
                                  (unsigned long)(data.packet.which_payload_variant==meshtastic_MeshPacket_decoded_tag?
                                  data.packet.decoded.request_id:0));break;
                default:break;
            }
#endif
            meshink_meshtastic_ui_receive(data);
        }else{
            Serial.printf("[MeshInk/MT] ERROR: FromRadio protobuf decode failed, len=%u\n",(unsigned)n);
        }
    }
}
bool send(const char* text){
    if(!ready||!text||!*text)return false;
    uint32_t dest=0;
    uint8_t channel=0;
    if(!meshink_meshtastic_ui_destination(dest,channel)){
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        Serial.println("[MT-TEST] send rejected: missing active destination");
#endif
        return false;
    }
    meshtastic_ToRadio request=meshtastic_ToRadio_init_zero;
    request.which_payload_variant=meshtastic_ToRadio_packet_tag;
    request.packet.to=dest;
    request.packet.channel=channel;
    request.packet.id=esp_random();
    if(!request.packet.id)request.packet.id=1;
    request.packet.want_ack=dest!=0xffffffffu;
    request.packet.which_payload_variant=meshtastic_MeshPacket_decoded_tag;
    request.packet.decoded.portnum=meshtastic_PortNum_TEXT_MESSAGE_APP;
    const size_t n=strlen(text);
    if(n>sizeof(request.packet.decoded.payload.bytes))return false;
    memcpy(request.packet.decoded.payload.bytes,text,n);
    request.packet.decoded.payload.size=n;
    uint8_t bytes[meshtastic_ToRadio_size]{};
    pb_ostream_t output=pb_ostream_from_buffer(bytes,sizeof(bytes));
    if(!pb_encode(&output,meshtastic_ToRadio_fields,&request))return false;
    const bool queued=meshink_official_phoneapi_submit(bytes,output.bytes_written);
    meshink_meshtastic_ui_sent(request.packet.id,text,queued);
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    Serial.printf("[MT-TEST] ToRadio text id=%08lx to=!%08lx channel=%u length=%u want_ack=%u accepted=%u\n",
                  (unsigned long)request.packet.id,(unsigned long)dest,
                  (unsigned)channel,(unsigned)n,request.packet.want_ack?1U:0U,queued?1U:0U);
#endif
    return queued;
}
void stop(){
    meshink_official_phoneapi_close();
    meshink_meshtastic_native_stop();
    ready=false;
}
bool running(){return ready;}
const MeshInkProtocolBackend& backend(){
  static const MeshInkProtocolBackend b=[](){
    MeshInkProtocolBackend value{};
    value.descriptor={2,"MESHTASTIC","Meshtastic",MESHINK_OFFICIAL_MESHTASTIC_TAG,0};
    value.setup=start;value.loop=poll;value.is_running=running;
    value.prepare_shutdown=stop;value.provider=meshink_meshtastic_ui_provider;
    value.refresh_ui_data=poll;value.send_active=send;
    value.radio_summary=meshink_meshtastic_ui_radio_summary;
    value.settings_count=meshink_meshtastic_ui_settings_count;
    value.settings_item=meshink_meshtastic_ui_settings_item;
    value.gps_mode_changed=shared_gps_mode_changed;
    value.my_location=shared_my_location;
    value.setup_region_count=meshink_meshtastic_ui_region_count;
    value.setup_region_name=meshink_meshtastic_ui_region_name;
    value.setup_preset_count_for_region=meshink_meshtastic_ui_preset_count;
    value.setup_preset_name_for_region=meshink_meshtastic_ui_preset_name;
    value.setup_preset_index_for_region=meshink_meshtastic_ui_preset_index;
    value.setup_commit_radio=meshink_meshtastic_ui_commit_radio;
    return value;
  }();
  return b;
}
}
const MeshInkProtocolBackend* meshink_protocol_backend_slot_2(){return &backend();}

bool meshink_meshtastic_submit_channel(const meshtastic_Channel& channel){
    // Use official AdminModule commands aimed at the current device. Its
    // native handler owns key/channel persistence and configuration changes.
    if(!ready||!meshink_meshtastic_ui_own_node())return false;
    meshtastic_AdminMessage admin=meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant=meshtastic_AdminMessage_set_channel_tag;
    admin.set_channel=channel;
    meshtastic_ToRadio request=meshtastic_ToRadio_init_zero;
    request.which_payload_variant=meshtastic_ToRadio_packet_tag;
    request.packet.to=meshink_meshtastic_ui_own_node();
    request.packet.channel=0;
    request.packet.id=esp_random();
    if(!request.packet.id)request.packet.id=1;
    request.packet.want_ack=true;
    request.packet.which_payload_variant=meshtastic_MeshPacket_decoded_tag;
    request.packet.decoded.portnum=meshtastic_PortNum_ADMIN_APP;
    pb_ostream_t inner=pb_ostream_from_buffer(
        request.packet.decoded.payload.bytes,
        sizeof(request.packet.decoded.payload.bytes));
    if(!pb_encode(&inner,meshtastic_AdminMessage_fields,&admin))return false;
    request.packet.decoded.payload.size=inner.bytes_written;
    uint8_t encoded[meshtastic_ToRadio_size]{};
    pb_ostream_t outer=pb_ostream_from_buffer(encoded,sizeof(encoded));
    return pb_encode(&outer,meshtastic_ToRadio_fields,&request)&&
           meshink_official_phoneapi_submit(encoded,outer.bytes_written);
}

bool meshink_meshtastic_submit_lora_config(const meshtastic_Config_LoRaConfig& lora){
    if(!ready||!meshink_meshtastic_ui_own_node())return false;
    meshtastic_AdminMessage admin=meshtastic_AdminMessage_init_zero;
    admin.which_payload_variant=meshtastic_AdminMessage_set_config_tag;
    admin.set_config.which_payload_variant=meshtastic_Config_lora_tag;
    admin.set_config.payload_variant.lora=lora;
    meshtastic_ToRadio request=meshtastic_ToRadio_init_zero;
    request.which_payload_variant=meshtastic_ToRadio_packet_tag;
    request.packet.to=meshink_meshtastic_ui_own_node();
    request.packet.id=esp_random();if(!request.packet.id)request.packet.id=1;
    request.packet.want_ack=true;
    request.packet.which_payload_variant=meshtastic_MeshPacket_decoded_tag;
    request.packet.decoded.portnum=meshtastic_PortNum_ADMIN_APP;
    pb_ostream_t inner=pb_ostream_from_buffer(request.packet.decoded.payload.bytes,
                                               sizeof(request.packet.decoded.payload.bytes));
    if(!pb_encode(&inner,meshtastic_AdminMessage_fields,&admin))return false;
    request.packet.decoded.payload.size=inner.bytes_written;
    uint8_t wire[meshtastic_ToRadio_size]{};
    pb_ostream_t outer=pb_ostream_from_buffer(wire,sizeof(wire));
    return pb_encode(&outer,meshtastic_ToRadio_fields,&request)&&
           meshink_official_phoneapi_submit(wire,outer.bytes_written);
}
