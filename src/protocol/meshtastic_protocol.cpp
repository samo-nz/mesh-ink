#include <Arduino.h>
#include <pb_encode.h>
#include <pb_decode.h>
#include "../../lib/Meshtastic/src/mesh/generated/meshtastic/mesh.pb.h"
#include "mesh_protocol_backend.h"
#include "meshtastic_official_phoneapi.h"
#include "../../include/meshtastic_official_version.h"
#include "../ui_onboarding.h"
#include <cstring>

// MeshInk adapter for the OFFICIAL Meshtastic ToRadio/FromRadio protocol.
// MeshInk never implements encryption, packet routing or ACK logic here.
namespace {
static bool ready=false;
static uint32_t my_node=0;
static uint32_t destination=0xFFFFFFFFu;
static char title_buf[40]="PRIMARY";
static char peer_name[40]="";
static char text_buf[241]="";
static UiMessage last_message={"","",false,UiMessageState::Received,"MESHTASTIC"};
static bool has_message=false;
static UiListEntry primary={"PRIMARY","MESHTASTIC CHANNEL","",0,UiNodeRole::Unknown};
static UiListEntry peer={"","","",0,UiNodeRole::Client};
static const UiListEntry empty={"","","",0,UiNodeRole::Unknown};
class Provider final:public UiDataProvider {
public:
 size_t conversation_count()const override{return has_message?1:0;}
 const UiListEntry& conversation(size_t)const override{return destination==0xFFFFFFFFu?primary:peer;}
 bool open_conversation(size_t i)override{return i==0&&has_message;}
 size_t map_node_count()const override{return 0;}
 bool map_node(size_t,UiMapNode&)const override{return false;}
 bool open_map_node(size_t)override{return false;}
 size_t contact_count()const override{return peer_name[0]?1:0;}
 const UiListEntry& contact(size_t i)const override{return i==0?peer:empty;}
 bool open_contact(size_t i)override{if(i||!peer_name[0])return false;strcpy(title_buf,peer_name);return true;}
 size_t channel_count()const override{return 1;}
 const UiListEntry& channel(size_t i)const override{return i==0?primary:empty;}
 bool open_channel(size_t i)override{if(i)return false;destination=0xFFFFFFFFu;strcpy(title_buf,"PRIMARY");return true;}
 size_t advert_count()const override{return 0;}
 const UiListEntry& advert(size_t)const override{return empty;}
 bool open_advert(size_t)override{return false;}
 bool active_node_details(UiNodeDetails&)const override{return false;}
 bool add_active_node()override{return false;}
 bool remove_active_contact()override{return false;}
 bool request_active_node_info(UiNodeInfoRequest)override{return false;}
 bool login_active_node(const char*,bool)override{return false;}
 bool active_node_saved_password(char*,size_t)const override{return false;}
 const char* active_title()const override{return title_buf;}
 bool active_is_channel()const override{return destination==0xFFFFFFFFu;}
 size_t active_message_count()const override{return has_message?1:0;}
 const UiMessage& active_message(size_t)const override{return last_message;}
};
static Provider provider;
void drain(){
 uint8_t bytes[meshtastic_FromRadio_size]{};
 for(unsigned i=0;i<8 && meshink_official_phoneapi_has_data();++i){
   const size_t n=meshink_official_phoneapi_receive(bytes,sizeof(bytes));
   if(!n)break;
   meshtastic_FromRadio response=meshtastic_FromRadio_init_zero;
   pb_istream_t s=pb_istream_from_buffer(bytes,n);
   if(!pb_decode(&s,meshtastic_FromRadio_fields,&response))continue;
   if(response.which_payload_variant==meshtastic_FromRadio_my_info_tag){
     my_node=response.my_info.my_node_num;continue;
   }
   if(response.which_payload_variant!=meshtastic_FromRadio_packet_tag)continue;
   const meshtastic_MeshPacket& p=response.packet;
   if(p.which_payload_variant!=meshtastic_MeshPacket_decoded_tag ||
      p.decoded.portnum!=meshtastic_PortNum_TEXT_MESSAGE_APP)continue;
   size_t nbytes=p.decoded.payload.size;
   if(nbytes>=sizeof(text_buf))nbytes=sizeof(text_buf)-1;
   memcpy(text_buf,p.decoded.payload.bytes,nbytes);text_buf[nbytes]=0;
   last_message={text_buf,"",p.from==my_node,p.from==my_node?UiMessageState::Sent:UiMessageState::Received,"MESHTASTIC"};
   has_message=true;
   if(p.from!=my_node){
     snprintf(peer_name,sizeof(peer_name),"!%08lx",(unsigned long)p.from);
     peer={peer_name,"MESHTASTIC NODE","",0,UiNodeRole::Client};
   }
   ui_request_data_refresh("meshtastic-native");
 }
}
void start(){ready=meshink_official_phoneapi_open(1);}
void tick(){if(ready)drain();}
bool running(){return ready;}
void stop(){meshink_official_phoneapi_close();ready=false;}
UiDataProvider* get_provider(){return &provider;}
bool send(const char* text){
 if(!ready||!text||!*text)return false;
 meshtastic_ToRadio data=meshtastic_ToRadio_init_zero;
 data.which_payload_variant=meshtastic_ToRadio_packet_tag;
 data.packet.to=destination;
 data.packet.which_payload_variant=meshtastic_MeshPacket_decoded_tag;
 data.packet.decoded.portnum=meshtastic_PortNum_TEXT_MESSAGE_APP;
 const size_t len=strlen(text);
 if(len>sizeof(data.packet.decoded.payload.bytes))return false;
 memcpy(data.packet.decoded.payload.bytes,text,len);
 data.packet.decoded.payload.size=len;
 data.packet.want_ack=destination!=0xFFFFFFFFu;
 uint8_t bytes[meshtastic_ToRadio_size]{};
 pb_ostream_t s=pb_ostream_from_buffer(bytes,sizeof(bytes));
 return pb_encode(&s,meshtastic_ToRadio_fields,&data) &&
        meshink_official_phoneapi_submit(bytes,s.bytes_written);
}
const MeshInkProtocolBackend& backend(){
 static const MeshInkProtocolBackend value=[](){
   MeshInkProtocolBackend b{};
   b.descriptor.id=2;b.descriptor.name="MESHTASTIC";
   b.descriptor.core_name="Meshtastic";
   b.descriptor.core_version=MESHINK_OFFICIAL_MESHTASTIC_TAG;
   b.setup=start;b.loop=tick;b.is_running=running;b.prepare_shutdown=stop;
   b.provider=get_provider;b.refresh_ui_data=tick;b.send_active=send;
   return b;
 }();
 return value;
}
}
const MeshInkProtocolBackend* meshink_protocol_backend_slot_2(){return &backend();}
