#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <pb_decode.h>
#include <time.h>

#include <libmeshtastic_leaf.h>
#include <MeshCryptoPKI.h>
#include <MeshNodeId.h>
#include <MeshPayload.h>
#include <meshtastic/leafdata.pb.h>

#include "mesh_protocol_backend.h"
#include "../hardware/board.h"
#include "../hardware/gps.h"
#include "../hardware/radio.h"
#include "../hardware/rtc.h"
#include "../message_store.h"
#include "../ui_onboarding.h"

namespace {

using libmeshtastic_leaf::BROADCAST_ADDR;
using libmeshtastic_leaf::MeshConfig;
using libmeshtastic_leaf::MeshCryptoPKI;
using libmeshtastic_leaf::MeshNodeId;
using libmeshtastic_leaf::MeshPacket;
using libmeshtastic_leaf::MeshPayloadCodec;
using libmeshtastic_leaf::MeshRegion;
using libmeshtastic_leaf::ModemPreset;
using libmeshtastic_leaf::NodeNum;
using libmeshtastic_leaf::RadioConfig;
using libmeshtastic_leaf::RegionCode;

constexpr uint8_t MESHTASTIC_PROTOCOL_ID=2;
constexpr size_t MAX_NODES=50;
constexpr size_t MAX_ACTIVE_MESSAGES=MESHINK_MESSAGE_CAPACITY;

struct ListStorage {
    UiListEntry entry{};
    char title[42]{};
    char subtitle[72]{};
    char time[16]{};
};

struct NodeRecord {
    bool used=false;
    bool hidden=false;
    NodeNum node=0;
    uint8_t public_key[32]{};
    bool has_public_key=false;
    uint8_t role=meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE;
    uint32_t last_seen=0;
    uint32_t last_node_info=0;
    int16_t last_rssi=0;
    float last_snr=0.0f;
    uint8_t last_hops=0;
    char long_name[40]{};
    char short_name[5]{};
    char identity[16]{};
};

struct MessageView {
    UiMessage entry{};
    char text[MESHINK_MESSAGE_TEXT_BYTES]{};
    char time[16]{};
    char network[48]{};
};

static libmeshtastic_leaf::libmeshtastic_leaf leaf;
static MeshConfig mesh_config{};
static bool runtime_ready=false;
static uint8_t my_public_key[32]{};
static uint8_t my_private_key[32]{};
static char radio_summary[64]="ANZ / LongFast";
static uint32_t pending_packet_id=0;
static uint32_t pending_message_sequence=0;
static bool protocol_settings_dirty=false;

static void bind(ListStorage& item) {
    item.entry.title=item.title;
    item.entry.subtitle=item.subtitle;
    item.entry.time=item.time;
}

static void bind(MessageView& item) {
    item.entry.text=item.text;
    item.entry.time=item.time;
    item.entry.network=item.network;
}

static void node_key(NodeNum node,uint8_t out[7]) {
    memset(out,0,7);
    memcpy(out,&node,sizeof(node));
}

static NodeNum node_from_key(const uint8_t* key) {
    NodeNum node=0;
    if(key)memcpy(&node,key,sizeof(node));
    return node;
}

static uint32_t now_utc() {
    const uint32_t rtc=meshink_rtc_current_time();
    return rtc?rtc:(uint32_t)time(nullptr);
}

static void format_clock(uint32_t timestamp,char* out,size_t len) {
    if(!out||!len)return;
    if(!timestamp){strncpy(out,"--:--",len-1);out[len-1]=0;return;}
    time_t raw=(time_t)timestamp;
    struct tm local{};
    if(localtime_r(&raw,&local))snprintf(out,len,"%02d:%02d",local.tm_hour,local.tm_min);
    else {strncpy(out,"--:--",len-1);out[len-1]=0;}
}

static void format_age(uint32_t timestamp,char* out,size_t len) {
    if(!out||!len)return;
    if(!timestamp){strncpy(out,"UNKNOWN",len-1);out[len-1]=0;return;}
    const uint32_t now=now_utc();
    const uint32_t age=now>=timestamp?now-timestamp:0;
    if(age<60)snprintf(out,len,"JUST NOW");
    else if(age<3600)snprintf(out,len,"%lu MIN AGO",(unsigned long)(age/60));
    else if(age<86400)snprintf(out,len,"%lu HOUR%s AGO",
        (unsigned long)(age/3600),age/3600==1?"":"S");
    else snprintf(out,len,"%lu DAY%s AGO",
        (unsigned long)(age/86400),age/86400==1?"":"S");
}

static const char* role_label(uint8_t role) {
    switch((meshtastic_Config_DeviceConfig_Role)role){
        case meshtastic_Config_DeviceConfig_Role_CLIENT:return "CLIENT";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_MUTE:return "CLIENT MUTE";
        case meshtastic_Config_DeviceConfig_Role_ROUTER:return "ROUTER";
        case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:return "ROUTER CLIENT";
        case meshtastic_Config_DeviceConfig_Role_REPEATER:return "REPEATER";
        case meshtastic_Config_DeviceConfig_Role_TRACKER:return "TRACKER";
        case meshtastic_Config_DeviceConfig_Role_SENSOR:return "SENSOR";
        case meshtastic_Config_DeviceConfig_Role_TAK:return "TAK";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_HIDDEN:return "CLIENT HIDDEN";
        case meshtastic_Config_DeviceConfig_Role_LOST_AND_FOUND:return "LOST & FOUND";
        case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:return "TAK TRACKER";
        case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:return "ROUTER LATE";
        case meshtastic_Config_DeviceConfig_Role_CLIENT_BASE:return "CLIENT BASE";
        default:return "CLIENT";
    }
}

static UiNodeRole shared_role(uint8_t role) {
    switch((meshtastic_Config_DeviceConfig_Role)role){
        case meshtastic_Config_DeviceConfig_Role_ROUTER:
        case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
        case meshtastic_Config_DeviceConfig_Role_REPEATER:
        case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
            return UiNodeRole::Relay;
        case meshtastic_Config_DeviceConfig_Role_TRACKER:
        case meshtastic_Config_DeviceConfig_Role_SENSOR:
        case meshtastic_Config_DeviceConfig_Role_TAK_TRACKER:
            return UiNodeRole::Sensor;
        default:
            return UiNodeRole::Client;
    }
}

static void make_short_name(const char* name,char out[5]) {
    size_t p=0;
    if(name){
        for(const char* s=name;*s&&p<4;++s){
            const char c=*s;
            if((c>='A'&&c<='Z')||(c>='0'&&c<='9'))out[p++]=c;
            else if(c>='a'&&c<='z')out[p++]=(char)(c-'a'+'A');
        }
    }
    while(p<4)out[p++]='M';
    out[4]=0;
}

class MeshtasticUiProvider final:public UiDataProvider {
    NodeRecord nodes_[MAX_NODES]{};
    ListStorage contacts_[MAX_NODES]{};
    uint8_t contact_node_index_[MAX_NODES]{};
    size_t contact_count_=0;
    ListStorage conversations_[MAX_NODES]{};
    uint8_t conversation_node_index_[MAX_NODES]{};
    size_t conversation_count_=0;
    ListStorage channel_{};
    uint16_t active_indices_[MAX_ACTIVE_MESSAGES]{};
    size_t active_count_=0;
    bool active_channel_=true;
    NodeNum active_node_=0;
    char active_title_[42]="# PRIMARY";
    mutable MessageView active_message_view_{};
    uint32_t active_revision_=1;
    uint16_t direct_unread_total_=0;
    uint16_t channel_unread_total_=0;

    NodeRecord* find(NodeNum node) {
        for(auto& item:nodes_)if(item.used&&item.node==node)return &item;
        return nullptr;
    }
    const NodeRecord* find(NodeNum node)const {
        for(const auto& item:nodes_)if(item.used&&item.node==node)return &item;
        return nullptr;
    }
    NodeRecord* ensure(NodeNum node) {
        if(!node||node==BROADCAST_ADDR)return nullptr;
        if(auto* found=find(node))return found;
        for(auto& item:nodes_)if(!item.used){
            item=NodeRecord{};
            item.used=true;
            item.node=node;
            snprintf(item.identity,sizeof(item.identity),"!%08lx",(unsigned long)node);
            strncpy(item.long_name,item.identity,sizeof(item.long_name)-1);
            char short_name[5]{};
            MeshNodeId::getShortName(node,short_name);
            strncpy(item.short_name,short_name,sizeof(item.short_name)-1);
            return &item;
        }
        return nullptr;
    }
    bool message_matches_active(const MeshInkStoredMessage& item)const {
        if(meshink_message_protocol(item)!=MESHTASTIC_PROTOCOL_ID)return false;
        if(active_channel_)
            return item.kind==(uint8_t)MeshInkMessageKind::Channel&&item.key[0]==0;
        return item.kind==(uint8_t)MeshInkMessageKind::Direct&&
               node_from_key(item.key)==active_node_;
    }
    uint8_t unread_for(NodeNum node)const {
        uint8_t count=0;
        MeshInkStoredMessage item{};
        auto& store=meshink_message_store();
        for(size_t i=0;i<store.count();++i){
            if(!store.read(i,item)||item.sequence==0||
               meshink_message_protocol(item)!=MESHTASTIC_PROTOCOL_ID||
               item.kind!=(uint8_t)MeshInkMessageKind::Direct||
               node_from_key(item.key)!=node)continue;
            if(item.flags&MESHINK_MESSAGE_READ_THROUGH)count=0;
            else if((item.flags&MESHINK_MESSAGE_UNREAD)&&count<255)++count;
        }
        return count;
    }
    void sync_unread() {
        direct_unread_total_=0;
        channel_unread_total_=0;
        MeshInkStoredMessage item{};
        uint8_t channel_unread=0;
        struct PeerUnread {NodeNum node=0;uint8_t count=0;} peers[MAX_NODES]{};
        size_t peer_count=0;
        auto& store=meshink_message_store();
        for(size_t i=0;i<store.count();++i){
            if(!store.read(i,item)||item.sequence==0||
               meshink_message_protocol(item)!=MESHTASTIC_PROTOCOL_ID)continue;
            if(item.kind==(uint8_t)MeshInkMessageKind::Channel){
                if(item.key[0]!=0)continue;
                if(item.flags&MESHINK_MESSAGE_READ_THROUGH)channel_unread=0;
                else if((item.flags&MESHINK_MESSAGE_UNREAD)&&channel_unread<255)++channel_unread;
                continue;
            }
            if(item.kind!=(uint8_t)MeshInkMessageKind::Direct)continue;
            const NodeNum node=node_from_key(item.key);
            size_t p=0;
            while(p<peer_count&&peers[p].node!=node)++p;
            if(p==peer_count&&peer_count<MAX_NODES){
                peers[p].node=node;peers[p].count=0;++peer_count;
            }
            if(p>=MAX_NODES)continue;
            if(item.flags&MESHINK_MESSAGE_READ_THROUGH)peers[p].count=0;
            else if((item.flags&MESHINK_MESSAGE_UNREAD)&&peers[p].count<255)++peers[p].count;
        }
        channel_unread_total_=channel_unread;
        for(size_t p=0;p<peer_count;++p)direct_unread_total_+=peers[p].count;
        ui_status_set_unread(direct_unread_total_);
        ui_status_set_channel_unread(channel_unread_total_);
    }
    void rebuild_active() {
        active_count_=0;
        MeshInkStoredMessage item{};
        auto& store=meshink_message_store();
        for(size_t i=0;i<store.count()&&active_count_<MAX_ACTIVE_MESSAGES;++i){
            if(!store.read(i,item)||item.sequence==0||!message_matches_active(item))continue;
            active_indices_[active_count_++]=(uint16_t)i;
        }
        ++active_revision_;
    }
    void mark_active_read() {
        uint8_t key[7]{};
        MeshInkMessageKind kind=MeshInkMessageKind::Channel;
        size_t key_len=1;
        if(active_channel_)key[0]=0;
        else {node_key(active_node_,key);kind=MeshInkMessageKind::Direct;key_len=4;}
        if(!meshink_message_store().mark_read_through(
                kind,key,key_len,MESHTASTIC_PROTOCOL_ID))
            Serial.println("[T5-MESHTASTIC] failed to persist read marker");
        sync_unread();
    }

public:
    MeshtasticUiProvider() {
        for(auto& item:contacts_)bind(item);
        for(auto& item:conversations_)bind(item);
        bind(channel_);
        bind(active_message_view_);
    }

    void begin() {
        auto& store=meshink_message_store();
        store.begin();
        // Reconstruct placeholder peers from durable Meshtastic direct history.
        MeshInkStoredMessage message{};
        for(size_t i=0;i<store.count();++i){
            if(!store.read(i,message)||message.sequence==0||
               meshink_message_protocol(message)!=MESHTASTIC_PROTOCOL_ID||
               message.kind!=(uint8_t)MeshInkMessageKind::Direct)continue;
            ensure(node_from_key(message.key));
        }
        refresh(true);
    }

    bool lookup_key(NodeNum node,uint8_t out[32])const {
        const NodeRecord* item=find(node);
        if(!item||!item->has_public_key)return false;
        if(MeshNodeId::nodeNumFromPublicKey(item->public_key,32)!=node)return false;
        memcpy(out,item->public_key,32);
        return true;
    }

    bool learn_node_info(const MeshPacket& packet) {
        meshtastic_User user=meshtastic_User_init_zero;
        pb_istream_t stream=pb_istream_from_buffer(packet.payload,packet.payloadLen);
        if(!pb_decode(&stream,meshtastic_User_fields,&user)){
            Serial.printf("[T5-MESHTASTIC] invalid NodeInfo protobuf from !%08lx\n",
                          (unsigned long)packet.header.from);
            return false;
        }
        if(user.public_key.size!=32||
           !MeshCryptoPKI::isUsablePublicKey(user.public_key.bytes)||
           MeshNodeId::nodeNumFromPublicKey(user.public_key.bytes,32)!=packet.header.from||
           !packet.hasSignature||
           !MeshCryptoPKI::verifyPayload(
               user.public_key.bytes,packet.header.from,packet.header.id,
               (uint32_t)packet.portNum,packet.payload,packet.payloadLen,
               packet.signature)){
            Serial.printf("[T5-MESHTASTIC] rejected unverified NodeInfo from !%08lx\n",
                          (unsigned long)packet.header.from);
            return false;
        }
        NodeRecord* item=ensure(packet.header.from);
        if(!item)return false;
        item->has_public_key=true;
        memcpy(item->public_key,user.public_key.bytes,32);
        item->role=(uint8_t)user.role;
        item->last_seen=now_utc();
        item->last_node_info=item->last_seen;
        item->last_rssi=packet.rxRssi;
        item->last_snr=packet.rxSnr;
        const uint8_t start=packet.header.getHopStart();
        const uint8_t left=packet.header.getHopLimit();
        item->last_hops=start>=left?(uint8_t)(start-left):0;
        if(user.long_name[0])strncpy(item->long_name,user.long_name,sizeof(item->long_name)-1);
        if(user.short_name[0])strncpy(item->short_name,user.short_name,sizeof(item->short_name)-1);
        snprintf(item->identity,sizeof(item->identity),"!%08lx",(unsigned long)item->node);
        refresh(true);
        ui_request_data_refresh("meshtastic-nodeinfo");
        return true;
    }

    void receive_text(const MeshPacket& packet) {
        if(packet.header.from==leaf.getNodeNum())return;
        char text[MESHINK_MESSAGE_TEXT_BYTES]{};
        const size_t n=min(packet.payloadLen,sizeof(text)-1);
        memcpy(text,packet.payload,n);
        text[n]=0;
        if(!text[0])return;
        const uint32_t timestamp=now_utc();
        const int8_t snr_q4=(int8_t)constrain((int)lroundf(packet.rxSnr*4.0f),-128,127);
        const uint8_t start=packet.header.getHopStart();
        const uint8_t left=packet.header.getHopLimit();
        const uint8_t hops=start>=left?(uint8_t)(start-left):0;
        const bool channel=packet.header.to==BROADCAST_ADDR;
        bool visible=false;
        uint8_t key[7]{};
        MeshInkMessageKind kind;
        if(channel){
            key[0]=0;
            kind=MeshInkMessageKind::Channel;
            visible=ui_chat_is_visible(true)&&active_channel_;
        }else{
            NodeRecord* item=ensure(packet.header.from);
            if(item){
                item->last_seen=timestamp;item->last_rssi=packet.rxRssi;
                item->last_snr=packet.rxSnr;item->last_hops=hops;
            }
            node_key(packet.header.from,key);
            kind=MeshInkMessageKind::Direct;
            visible=ui_chat_is_visible(false)&&!active_channel_&&active_node_==packet.header.from;
        }
        const uint32_t sequence=meshink_message_store().append(
            kind,key,channel?1:4,text,timestamp,UiMessageState::Received,0,
            MeshInkMessageOrigin::LocalUi,true,snr_q4,hops,!visible,
            MESHTASTIC_PROTOCOL_ID);
        if(!sequence){
            Serial.println("[T5-MESHTASTIC] failed to journal received text");
            return;
        }
        if(message_matches_active_from(kind,key))rebuild_active();
        refresh(true);
        sync_unread();
        ui_notify_message_received(channel);
    }

    bool message_matches_active_from(MeshInkMessageKind kind,const uint8_t* key)const {
        if(active_channel_)return kind==MeshInkMessageKind::Channel&&key&&key[0]==0;
        return kind==MeshInkMessageKind::Direct&&key&&node_from_key(key)==active_node_;
    }

    void delivered(uint32_t request_id) {
        if(!pending_packet_id||request_id!=pending_packet_id||!pending_message_sequence)return;
        if(meshink_message_store().update_state(pending_message_sequence,UiMessageState::Delivered)){
            pending_packet_id=0;
            pending_message_sequence=0;
            rebuild_active();
            ui_request_data_refresh("meshtastic-delivered");
        }
    }

    void refresh(bool force=false) {
        (void)force;
        sync_unread();

        contact_count_=0;
        for(size_t i=0;i<MAX_NODES&&contact_count_<MAX_NODES;++i){
            NodeRecord& node=nodes_[i];
            if(!node.used||node.hidden)continue;
            ListStorage& out=contacts_[contact_count_];
            out=ListStorage{};bind(out);
            strncpy(out.title,node.long_name[0]?node.long_name:node.identity,sizeof(out.title)-1);
            char age[28]{};format_age(node.last_seen,age,sizeof(age));
            snprintf(out.subtitle,sizeof(out.subtitle),"%s  HEARD %.35s",role_label(node.role),age);
            format_clock(node.last_seen,out.time,sizeof(out.time));
            out.entry.unread=unread_for(node.node);
            out.entry.role=shared_role(node.role);
            contact_node_index_[contact_count_]=(uint8_t)i;
            ++contact_count_;
        }

        channel_=ListStorage{};bind(channel_);
        strncpy(channel_.title,"# PRIMARY",sizeof(channel_.title)-1);
        snprintf(channel_.subtitle,sizeof(channel_.subtitle),"MESHTASTIC PUBLIC / %s",
                 MeshRegion::getPresetName(mesh_config.radio.preset,true));
        strcpy(channel_.time,"");
        channel_.entry.unread=(uint8_t)min((uint16_t)255,channel_unread_total_);
        channel_.entry.role=UiNodeRole::Unknown;

        int16_t latest[MAX_NODES];
        for(auto& i:latest)i=-1;
        MeshInkStoredMessage message{};
        auto& store=meshink_message_store();
        for(size_t logical=0;logical<store.count();++logical){
            if(!store.read(logical,message)||message.sequence==0||
               meshink_message_protocol(message)!=MESHTASTIC_PROTOCOL_ID||
               message.kind!=(uint8_t)MeshInkMessageKind::Direct)continue;
            const NodeNum node_num=node_from_key(message.key);
            for(size_t n=0;n<MAX_NODES;++n){
                if(nodes_[n].used&&!nodes_[n].hidden&&nodes_[n].node==node_num){
                    latest[n]=(int16_t)logical;break;
                }
            }
        }
        conversation_count_=0;
        for(size_t n=0;n<MAX_NODES&&conversation_count_<MAX_NODES;++n){
            if(latest[n]<0||!nodes_[n].used||nodes_[n].hidden||
               !store.read((size_t)latest[n],message))continue;
            ListStorage& out=conversations_[conversation_count_];
            out=ListStorage{};bind(out);
            strncpy(out.title,nodes_[n].long_name[0]?nodes_[n].long_name:nodes_[n].identity,
                    sizeof(out.title)-1);
            strncpy(out.subtitle,message.text,sizeof(out.subtitle)-1);
            format_clock(message.timestamp,out.time,sizeof(out.time));
            out.entry.unread=unread_for(nodes_[n].node);
            out.entry.role=shared_role(nodes_[n].role);
            conversation_node_index_[conversation_count_]=(uint8_t)n;
            ++conversation_count_;
        }
    }

    size_t conversation_count()const override{return conversation_count_;}
    const UiListEntry& conversation(size_t index)const override{
        static UiListEntry empty{};
        return index<conversation_count_?conversations_[index].entry:empty;
    }
    bool open_conversation(size_t index)override{
        if(index>=conversation_count_)return false;
        const NodeRecord& node=nodes_[conversation_node_index_[index]];
        active_channel_=false;active_node_=node.node;
        strncpy(active_title_,node.long_name[0]?node.long_name:node.identity,sizeof(active_title_)-1);
        rebuild_active();mark_active_read();return true;
    }

    size_t map_node_count()const override{return 0;}
    bool map_node(size_t,UiMapNode&)const override{return false;}
    bool open_map_node(size_t)override{return false;}

    size_t contact_count()const override{return contact_count_;}
    const UiListEntry& contact(size_t index)const override{
        static UiListEntry empty{};
        return index<contact_count_?contacts_[index].entry:empty;
    }
    bool open_contact(size_t index)override{
        if(index>=contact_count_)return false;
        const NodeRecord& node=nodes_[contact_node_index_[index]];
        active_channel_=false;active_node_=node.node;
        strncpy(active_title_,node.long_name[0]?node.long_name:node.identity,sizeof(active_title_)-1);
        rebuild_active();mark_active_read();return true;
    }

    size_t channel_count()const override{return 1;}
    const UiListEntry& channel(size_t index)const override{
        static UiListEntry empty{};
        return index==0?channel_.entry:empty;
    }
    bool open_channel(size_t index)override{
        if(index!=0)return false;
        active_channel_=true;active_node_=0;strcpy(active_title_,"# PRIMARY");
        rebuild_active();mark_active_read();return true;
    }

    size_t advert_count()const override{return 0;}
    const UiListEntry& advert(size_t)const override{
        static UiListEntry empty{};return empty;
    }
    bool open_advert(size_t)override{return false;}

    bool active_node_details(UiNodeDetails& out)const override{
        if(active_channel_)return false;
        const NodeRecord* node=find(active_node_);
        if(!node)return false;
        static char seen[32],advert[32],route[32],position[24];
        format_age(node->last_seen,seen,sizeof(seen));
        format_age(node->last_node_info,advert,sizeof(advert));
        if(node->last_hops)snprintf(route,sizeof(route),"%u HOP%s",
            (unsigned)node->last_hops,node->last_hops==1?"":"S");
        else strcpy(route,"DIRECT / ZERO HOP");
        strcpy(position,"NO POSITION RECEIVED");
        out=UiNodeDetails{};
        out.name=node->long_name[0]?node->long_name:node->identity;
        out.identity=node->identity;
        out.last_seen=seen;
        out.route=route;
        out.position=position;
        out.status="NOT AVAILABLE";
        out.telemetry="NOT AVAILABLE";
        out.path="NOT AVAILABLE";
        out.trace="NOT AVAILABLE";
        out.access_level="";
        out.role_label=role_label(node->role);
        out.role=shared_role(node->role);
        out.capabilities=0;
        out.saved_contact=!node->hidden;
        out.advert_age=advert;
        out.position_source="POSITION DECODING NOT ENABLED";
        return true;
    }

    bool add_active_node()override{
        NodeRecord* node=find(active_node_);if(!node)return false;
        node->hidden=false;refresh(true);return true;
    }
    bool remove_active_contact()override{
        NodeRecord* node=find(active_node_);if(!node)return false;
        node->hidden=true;refresh(true);return true;
    }
    bool request_active_node_info(UiNodeInfoRequest)override{return false;}
    bool login_active_node(const char*,bool)override{return false;}
    bool active_node_saved_password(char*,size_t)const override{return false;}

    const char* active_title()const override{return active_title_;}
    bool active_is_channel()const override{return active_channel_;}
    size_t active_message_count()const override{return active_count_;}
    const UiMessage& active_message(size_t index)const override{
        active_message_view_=MessageView{};bind(active_message_view_);
        if(index>=active_count_)return active_message_view_.entry;
        MeshInkStoredMessage message{};
        if(!meshink_message_store().read(active_indices_[index],message))
            return active_message_view_.entry;
        strncpy(active_message_view_.text,message.text,sizeof(active_message_view_.text)-1);
        format_clock(message.timestamp,active_message_view_.time,sizeof(active_message_view_.time));
        active_message_view_.entry.outgoing=message.state!=(uint8_t)UiMessageState::Received;
        active_message_view_.entry.state=(UiMessageState)message.state;
        if(message.flags&MESHINK_MESSAGE_HAS_RX){
            snprintf(active_message_view_.network,sizeof(active_message_view_.network),
                     "MESHTASTIC  SNR %.1f  %u HOP%s",
                     (double)message.snr_q4/4.0,
                     (unsigned)message.path_len,message.path_len==1?"":"S");
        }else strcpy(active_message_view_.network,"MESHTASTIC");
        return active_message_view_.entry;
    }
    uint16_t direct_unread_total()const override{return direct_unread_total_;}
    uint16_t channel_unread_total()const override{return channel_unread_total_;}
    uint32_t active_message_revision()const override{return active_revision_;}
};

static MeshtasticUiProvider provider;

static bool pki_key_lookup(NodeNum node,uint8_t pub_key[32]) {
    return provider.lookup_key(node,pub_key);
}

static bool load_identity() {
    Preferences prefs;
    if(!prefs.begin("meshtastic",false))return false;
    bool have_private=prefs.getBytesLength("private")==sizeof(my_private_key)&&
        prefs.getBytes("private",my_private_key,sizeof(my_private_key))==sizeof(my_private_key);
    bool have_public=prefs.getBytesLength("public")==sizeof(my_public_key)&&
        prefs.getBytes("public",my_public_key,sizeof(my_public_key))==sizeof(my_public_key);
    bool valid=have_private&&have_public&&
        MeshCryptoPKI::isUsablePublicKey(my_public_key)&&
        MeshNodeId::isUsableNodeNum(MeshNodeId::nodeNumFromPublicKey(my_public_key,32));
    if(!valid&&have_private){
        valid=MeshCryptoPKI::regeneratePublicKey(my_public_key,my_private_key)&&
              MeshCryptoPKI::isUsablePublicKey(my_public_key)&&
              MeshNodeId::isUsableNodeNum(MeshNodeId::nodeNumFromPublicKey(my_public_key,32));
        if(valid)prefs.putBytes("public",my_public_key,sizeof(my_public_key));
    }
    if(!valid){
        if(!libmeshtastic_leaf::libmeshtastic_leaf::generateKeyPair(
                my_public_key,my_private_key)){
            prefs.end();return false;
        }
        const bool private_saved=prefs.putBytes("private",my_private_key,sizeof(my_private_key))==sizeof(my_private_key);
        const bool public_saved=prefs.putBytes("public",my_public_key,sizeof(my_public_key))==sizeof(my_public_key);
        valid=private_saved&&public_saved;
    }
    prefs.end();
    return valid;
}

static void update_radio_summary() {
    const char* preset_name=MeshRegion::getPresetName(mesh_config.radio.preset);
    const float frequency=MeshRegion::getFrequency(
        mesh_config.radio.region,mesh_config.radio.preset,preset_name);
    snprintf(radio_summary,sizeof(radio_summary),"%s / %s / %.3f MHz",
        MeshRegion::getRegionName(mesh_config.radio.region),preset_name,(double)frequency);
}

static void save_config() {
    Preferences prefs;
    if(!prefs.begin("meshtastic",false))return;
    prefs.putUChar("region",(uint8_t)mesh_config.radio.region);
    prefs.putUChar("preset",(uint8_t)mesh_config.radio.preset);
    prefs.putUChar("hop",mesh_config.hopLimit);
    prefs.end();
}

static bool supported_region(RegionCode region) {
    if(region==libmeshtastic_leaf::REGION_UNSET||
       region==libmeshtastic_leaf::REGION_LORA_24)return false;
    return MeshRegion::getRegion(region)!=nullptr;
}

static void load_config() {
    Preferences prefs;
    uint8_t region=(uint8_t)libmeshtastic_leaf::REGION_ANZ;
    uint8_t preset=(uint8_t)libmeshtastic_leaf::PRESET_LONG_FAST;
    uint8_t hop=3;
    if(prefs.begin("meshtastic",true)){
        region=prefs.getUChar("region",region);
        preset=prefs.getUChar("preset",preset);
        hop=prefs.getUChar("hop",hop);
        prefs.end();
    }
    if(!supported_region((RegionCode)region))
        region=(uint8_t)libmeshtastic_leaf::REGION_ANZ;
    if(preset>(uint8_t)libmeshtastic_leaf::PRESET_MEDIUM_TURBO)
        preset=(uint8_t)libmeshtastic_leaf::PRESET_LONG_FAST;
    mesh_config=MeshConfig{};
    mesh_config.radio.region=(RegionCode)region;
    mesh_config.radio.preset=(ModemPreset)preset;
    mesh_config.hopLimit=constrain(hop,(uint8_t)1,(uint8_t)7);
    protocol_settings_dirty=false;
    update_radio_summary();
}

enum : uint16_t {
    MESHTASTIC_SETTING_REGION = 1,
    MESHTASTIC_SETTING_MODEM_PRESET = 2,
    MESHTASTIC_SETTING_HOP_LIMIT = 3,
    MESHTASTIC_SETTING_APPLY_RESTART = 4,
};

static size_t protocol_settings_count() {
    return 4;
}

static bool protocol_settings_item(size_t index, MeshInkProtocolSettingItem& item) {
    static char hop_value[16];
    item = MeshInkProtocolSettingItem{};
    switch(index){
        case 0:
            item={MESHTASTIC_SETTING_REGION,"REGION",
                  MeshRegion::getRegionName(mesh_config.radio.region),true};
            return true;
        case 1:
            item={MESHTASTIC_SETTING_MODEM_PRESET,"MODEM PRESET",
                  MeshRegion::getPresetName(mesh_config.radio.preset,true),true};
            return true;
        case 2:
            snprintf(hop_value,sizeof(hop_value),"%u HOPS",(unsigned)mesh_config.hopLimit);
            item={MESHTASTIC_SETTING_HOP_LIMIT,"HOP LIMIT",hop_value,true};
            return true;
        case 3:
            item={MESHTASTIC_SETTING_APPLY_RESTART,"APPLY RADIO CHANGES",
                  protocol_settings_dirty?"RESTART REQUIRED":"CURRENT SETTINGS ACTIVE",
                  protocol_settings_dirty};
            return true;
        default:
            return false;
    }
}

static MeshInkProtocolSettingResult activate_protocol_setting(uint16_t id) {
    switch(id){
        case MESHTASTIC_SETTING_REGION:{
            size_t count=0;
            const auto* regions=MeshRegion::getAllRegions(count);
            if(!regions||!count)return MeshInkProtocolSettingResult::Failed;
            size_t current=0;
            while(current<count&&regions[current].code!=mesh_config.radio.region)++current;
            for(size_t step=1;step<=count;++step){
                const RegionCode next=regions[(current+step)%count].code;
                if(!supported_region(next))continue;
                mesh_config.radio.region=next;
                save_config();
                protocol_settings_dirty=true;
                update_radio_summary();
                return MeshInkProtocolSettingResult::RestartRequired;
            }
            return MeshInkProtocolSettingResult::Failed;
        }
        case MESHTASTIC_SETTING_MODEM_PRESET:
            mesh_config.radio.preset=(ModemPreset)(
                ((uint8_t)mesh_config.radio.preset+1)%
                ((uint8_t)libmeshtastic_leaf::PRESET_MEDIUM_TURBO+1));
            save_config();
            protocol_settings_dirty=true;
            update_radio_summary();
            return MeshInkProtocolSettingResult::RestartRequired;
        case MESHTASTIC_SETTING_HOP_LIMIT:
            mesh_config.hopLimit=(uint8_t)(mesh_config.hopLimit>=7?1:mesh_config.hopLimit+1);
            save_config();
            protocol_settings_dirty=true;
            return MeshInkProtocolSettingResult::RestartRequired;
        case MESHTASTIC_SETTING_APPLY_RESTART:
            return protocol_settings_dirty
                ?MeshInkProtocolSettingResult::RestartNow
                :MeshInkProtocolSettingResult::Unchanged;
        default:
            return MeshInkProtocolSettingResult::Failed;
    }
}

static void apply_owner(const char* name) {
    char long_name[25]="MeshInk";
    if(name&&name[0]){
        strncpy(long_name,name,sizeof(long_name)-1);
        long_name[sizeof(long_name)-1]=0;
    }
    char short_name[5]{};
    make_short_name(long_name,short_name);
    leaf.setOwner(long_name,short_name);
}

static void apply_name(const char* name) {
    apply_owner(name);
    if(runtime_ready)leaf.sendNodeInfo();
}

static bool name_character_allowed(char c) {
    return c>=32&&c<127;
}

static size_t node_name_max_length() {
    // MeshInk owns the shared node-name buffer and currently stores 20
    // characters. Leaf can advertise longer names, but the shared editor must
    // never report a larger writable limit than its product-owned storage.
    return 20;
}

static const char* radio_summary_value() {
    return radio_summary;
}

static void setup() {
    runtime_ready=false;
    bool storage_mounted=SPIFFS.begin(false);
    if(!storage_mounted){
        ui_show_storage_initializing();
        storage_mounted=SPIFFS.begin(true);
    }
    if(!storage_mounted){
        Serial.println("[T5-MESHTASTIC] SPIFFS unavailable");
        return;
    }

    meshink_board_begin_local();
    if(!meshink_radio_initialize()){
        ui_show_radio_failure(meshink_radio_classify_failure());
        return;
    }
    if(!meshink_radio_set_lora_crc(2)){
        Serial.println("[T5-MESHTASTIC] failed to enable Meshtastic LoRa CRC");
        ui_show_radio_failure(MeshInkRadioFailureClass::RadioFault);
        return;
    }
    PhysicalLayer* phy=meshink_radio_radiolib();
    if(!phy){
        Serial.println("[T5-MESHTASTIC] RadioLib PhysicalLayer unavailable");
        return;
    }

    meshink_gps_service_begin();
    load_config();
    if(!load_identity()){
        Serial.println("[T5-MESHTASTIC] failed to load/generate identity");
        return;
    }

    leaf.setMyPublicKey(my_public_key);
    leaf.setMyPrivateKey(my_private_key);
    leaf.onReceivePKI(pki_key_lookup);

    char stored_name[25]="MeshInk";
    Preferences ui;
    if(ui.begin("t5-ui",true)){
        const String name=ui.getString("name","MeshInk");
        strncpy(stored_name,name.c_str(),sizeof(stored_name)-1);
        ui.end();
    }
    apply_owner(stored_name);

    if(!leaf.begin(mesh_config,phy)){
        Serial.println("[T5-MESHTASTIC] libmeshtastic-leaf begin failed");
        return;
    }
    leaf.setDefaultChannel();
    provider.begin();
    ui_use_data_provider(&provider);
    runtime_ready=true;
    ui_mesh_ready();

    Serial.printf("[T5-MESHTASTIC] ready core=1.0.0 node=!%08lx radio=%s\n",
        (unsigned long)leaf.getNodeNum(),radio_summary);
}

static void handle_packet(const MeshPacket& packet) {
    if(packet.portNum==meshtastic_PortNum_NODEINFO_APP){
        provider.learn_node_info(packet);
        return;
    }
    if(packet.portNum==meshtastic_PortNum_TEXT_MESSAGE_APP){
        provider.receive_text(packet);
        return;
    }
    if(packet.portNum==meshtastic_PortNum_ROUTING_APP&&packet.requestId&&
       MeshPayloadCodec::isRoutingAck(packet.payload,packet.payloadLen)){
        provider.delivered(packet.requestId);
    }
}

static void loop() {
    if(!runtime_ready){delay(10);return;}
    leaf.update();
    while(leaf.available()){
        MeshPacket packet;
        const auto result=leaf.receive(packet);
        if(result==libmeshtastic_leaf::ReceiveResult::OK)handle_packet(packet);
        else if(result!=libmeshtastic_leaf::ReceiveResult::NO_PACKET)
            Serial.printf("[T5-MESHTASTIC] receive result=%u\n",(unsigned)result);
    }
    meshink_gps_service_loop();
    meshink_rtc_tick();
}

static UiDataProvider* data_provider() {
    return &provider;
}

static void refresh_ui_data() {
    provider.refresh(true);
}

static bool send_active(const char* text) {
    if(!runtime_ready||!text||!text[0])return false;
    const size_t len=min(strlen(text),(size_t)MESHINK_MESSAGE_TEXT_MAX);
    uint32_t packet_id=0;
    uint8_t key[7]{};
    MeshInkMessageKind kind=MeshInkMessageKind::Channel;
    if(provider.active_is_channel()){
        key[0]=0;
        packet_id=leaf.sendData(meshtastic_PortNum_TEXT_MESSAGE_APP,
            (const uint8_t*)text,len,BROADCAST_ADDR,false);
    }else{
        // active_title is presentation; derive destination from the provider's active detail identity.
        UiNodeDetails details{};
        if(!provider.active_node_details(details)||!details.identity||details.identity[0]!='!')return false;
        const NodeNum node=(NodeNum)strtoul(details.identity+1,nullptr,16);
        node_key(node,key);
        kind=MeshInkMessageKind::Direct;
        uint8_t remote_key[32]{};
        if(provider.lookup_key(node,remote_key))
            packet_id=leaf.sendDataPKI(meshtastic_PortNum_TEXT_MESSAGE_APP,
                (const uint8_t*)text,len,node,remote_key,true);
        else
            packet_id=leaf.sendData(meshtastic_PortNum_TEXT_MESSAGE_APP,
                (const uint8_t*)text,len,node,true);
    }
    if(!packet_id)return false;

    char stored[MESHINK_MESSAGE_TEXT_BYTES]{};
    memcpy(stored,text,len);stored[len]=0;
    const uint32_t sequence=meshink_message_store().append(
        kind,key,kind==MeshInkMessageKind::Channel?1:4,stored,now_utc(),
        UiMessageState::Sent,packet_id,MeshInkMessageOrigin::LocalUi,
        false,0,MESHINK_MESSAGE_PATH_UNKNOWN,false,MESHTASTIC_PROTOCOL_ID);
    if(!sequence)return false;
    if(kind==MeshInkMessageKind::Direct){
        pending_packet_id=packet_id;
        pending_message_sequence=sequence;
    }
    provider.refresh(true);
    // Re-open current conversation index cache without changing selection.
    if(provider.active_is_channel())provider.open_channel(0);
    else {
        UiNodeDetails details{};
        if(provider.active_node_details(details)){
            for(size_t i=0;i<provider.contact_count();++i){
                const UiListEntry& entry=provider.contact(i);
                if(entry.title&&details.name&&!strcmp(entry.title,details.name)){
                    provider.open_contact(i);break;
                }
            }
        }
    }
    ui_request_data_refresh("meshtastic-send");
    return true;
}

static void prepare_shutdown() {
    if(runtime_ready)leaf.end();
    runtime_ready=false;
    meshink_radio_power_off();
}

static bool is_running() {
    return runtime_ready;
}

static const MeshInkProtocolBackend& backend() {
    static const MeshInkProtocolBackend value=[]{
        MeshInkProtocolBackend b{};
        b.descriptor.id=MESHTASTIC_PROTOCOL_ID;
        b.descriptor.name="MESHTASTIC";
        b.descriptor.core_name="libmeshtastic-leaf";
        b.descriptor.core_version="1.0.0 (bd542d6e)";
        b.descriptor.capabilities=0;

        b.setup=setup;
        b.loop=loop;
        b.is_running=is_running;
        b.prepare_shutdown=prepare_shutdown;

        b.provider=data_provider;
        b.refresh_ui_data=refresh_ui_data;
        b.send_active=send_active;

        b.settings_count=protocol_settings_count;
        b.settings_item=protocol_settings_item;
        b.settings_activate=activate_protocol_setting;

        b.apply_name=apply_name;
        b.name_character_allowed=name_character_allowed;
        b.node_name_max_length=node_name_max_length;
        b.radio_summary=radio_summary_value;
        return b;
    }();
    return value;
}

} // namespace

const MeshInkProtocolBackend* meshink_protocol_backend_slot_2() {
    return &backend();
}
