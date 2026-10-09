#include <Arduino.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/admin.pb.h"
#include <esp_system.h>
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include "mesh_protocol_backend.h"
#include "meshtastic_official_phoneapi.h"
#include "meshtastic_ui_data.h"
#include "../../include/meshtastic_official_version.h"
#include "../ui_onboarding.h"
#include "../hardware/storage.h"
#include <cstring>

// MeshInk handles the UI; all routing, channel crypto and network
// retransmissions remain in the pinned official Meshtastic core.
namespace {
bool ready=false;
void start(){
    // Follow MeshCore's shared storage-first startup ordering. The selected
    // protocol owns no board filesystems or data partitions itself.
    const bool storage_ready=meshink_storage_mount_internal_safe();
    if(!storage_ready)ui_show_storage_initializing();
    meshink_meshtastic_ui_begin();
    ui_use_data_provider(meshink_meshtastic_ui_provider());
    // Native Router, NodeDB and MeshService must initialize before PhoneAPI.
    // This seam does not initialize a second firmware application.
    ready=meshink_official_phoneapi_open(1);
}
void poll(){
    if(!ready)return;
    uint8_t bytes[meshtastic_FromRadio_size]{};
    for(unsigned i=0;i<12&&meshink_official_phoneapi_has_data();++i){
        const size_t n=meshink_official_phoneapi_receive(bytes,sizeof(bytes));
        if(!n)break;
        meshtastic_FromRadio data=meshtastic_FromRadio_init_zero;
        pb_istream_t input=pb_istream_from_buffer(bytes,n);
        if(pb_decode(&input,meshtastic_FromRadio_fields,&data))
            meshink_meshtastic_ui_receive(data);
    }
}
bool send(const char* text){
    if(!ready||!text||!*text)return false;
    uint32_t dest=0;
    uint8_t channel=0;
    if(!meshink_meshtastic_ui_destination(dest,channel))return false;
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
    return queued;
}
void stop(){meshink_official_phoneapi_close();ready=false;}
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
