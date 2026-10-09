#include <Arduino.h>
#include <pb_encode.h>
#include <pb_decode.h>
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
    return value;
  }();
  return b;
}
}
const MeshInkProtocolBackend* meshink_protocol_backend_slot_2(){return &backend();}
