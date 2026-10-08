#include <Arduino.h>
#include <Preferences.h>
#include <SPIFFS.h>
#include <pb_decode.h>
#include <time.h>
#include <esp_heap_caps.h>
#include <new>

#include <libmeshtastic_leaf.h>
#include <MeshCryptoPKI.h>
#include <MeshNodeId.h>
#include <MeshPayload.h>
#include <meshtastic/leafdata.pb.h>

#include "mesh_protocol_backend.h"
#include "meshtastic_wire.h"
#include "../hardware/power.h"
#include "../hardware/board.h"
#include "../hardware/gps.h"
#include "../hardware/radio.h"
#include "../hardware/rtc.h"
#include "../message_store.h"
#include "../backup_restore.h"
#include "../hardware/storage.h"
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
constexpr const char* NODES_PATH="/meshtastic_nodes.bin";
constexpr const char* NODES_STAGING="/meshtastic_nodes.tmp";
constexpr uint32_t NODES_MAGIC=0x314E544DU; // MTN1
constexpr uint32_t POSITION_MAGIC=0x3150544DU; // MTP1
constexpr const char* POSITIONS_PATH="/meshtastic_positions.bin";
constexpr const char* POSITIONS_STAGE="/mt_positions.tmp";

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

// Position records are kept separate from NodeRecord so existing saved
// Meshtastic NodeInfo snapshots remain binary-compatible.
struct PositionRecord {
    NodeNum node=0;
    int32_t latitude=0;  // degrees x 1e6, MeshInk's existing map convention
    int32_t longitude=0;
    uint32_t received_utc=0;
};

struct TelemetryRecord {
    NodeNum node=0;
    uint8_t battery=0;
    float volts=0;
    uint32_t received=0;
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
static bool protocol_settings_dirty=false;
static uint8_t position_interval_min=0; // zero means strictly opt-in
static uint8_t position_precision=0;   // 0 exact, 1 ~100m, 2 ~1km
static uint8_t telemetry_interval_min=0;
static uint8_t nodeinfo_interval_hours=3;
static bool carrier_sense_enabled=true;
static uint32_t last_position_send_ms=0;
static uint32_t last_telemetry_send_ms=0;
static uint32_t last_message_packet_id=0;
static uint32_t last_message_sequence=0;
static uint32_t sent_count=0,received_count=0,send_refused=0,radio_errors=0;
static uint32_t position_received_count=0;
static void update_radio_summary();
// Leaf currently implements just the standard public primary channel.
// Secondary channel controls are present in the shared UI but intentionally
// make no radio or persistent configuration changes until Leaf can listen
// to multiple Meshtastic channels at once.
static void update_radio_summary();

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
    PositionRecord positions_[MAX_NODES]{};
    TelemetryRecord telemetry_[MAX_NODES]{};
    uint32_t position_saved_at_=0;
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

    const PositionRecord* position_for(NodeNum id)const{
        for(const auto& p:positions_)if(p.node==id)return &p;
        return nullptr;
    }
    PositionRecord* writable_position(NodeNum id){
        for(auto& p:positions_)if(p.node==id)return &p;
        for(auto& p:positions_)if(!p.node){p.node=id;return &p;}
        return nullptr;
    }
    bool save_positions(){
        File f=SPIFFS.open(POSITIONS_STAGE,"w");if(!f)return false;
        uint32_t header[2]={POSITION_MAGIC,0};
        for(const auto& p:positions_)if(p.node)++header[1];
        bool ok=f.write((const uint8_t*)header,sizeof(header))==sizeof(header);
        for(const auto& p:positions_)if(ok&&p.node)
            ok=f.write((const uint8_t*)&p,sizeof(p))==sizeof(p);
        f.flush();f.close();
        if(!ok){SPIFFS.remove(POSITIONS_STAGE);return false;}
        // Keep previously valid map coordinates until the replacement exists.
        constexpr const char* OLD="/mt_positions.old";
        if(SPIFFS.exists(OLD))SPIFFS.remove(OLD);
        const bool previous=SPIFFS.exists(POSITIONS_PATH);
        if(previous&&!SPIFFS.rename(POSITIONS_PATH,OLD)){
            SPIFFS.remove(POSITIONS_STAGE);return false;
        }
        if(!SPIFFS.rename(POSITIONS_STAGE,POSITIONS_PATH)){
            if(previous)SPIFFS.rename(OLD,POSITIONS_PATH);
            return false;
        }
        position_saved_at_=millis();
        return true;
    }
    void load_positions(){
        // A first-use Meshtastic node has no received positions yet. Missing
        // storage is normal and must never be treated as a corrupt snapshot.
        if(!SPIFFS.exists(POSITIONS_PATH))return;
        File f=SPIFFS.open(POSITIONS_PATH,"r");
        if(!f||f.isDirectory()){
            Serial.printf("[T5-MESHTASTIC] WARN existing position snapshot cannot be opened path=%s; preserving\n",
                          POSITIONS_PATH);
            if(f)f.close();
            return;
        }
        const size_t size=f.size();
        uint32_t header[2]{};
        const size_t bytes_read=f.read((uint8_t*)header,sizeof(header));
        bool valid=bytes_read==sizeof(header)&&header[0]==POSITION_MAGIC&&
                   header[1]<=MAX_NODES&&
                   size==sizeof(header)+(size_t)header[1]*sizeof(PositionRecord);
        // Parse into a temporary collection. Never publish a partially loaded
        // set of markers if the snapshot is truncated or malformed.
        PositionRecord parsed[MAX_NODES]{};
        for(size_t i=0;valid&&i<header[1];++i){
            valid=f.read((uint8_t*)&parsed[i],sizeof(PositionRecord))==
                  sizeof(PositionRecord);
            if(!valid)break;
            const auto& p=parsed[i];
            if(!p.node||p.node==BROADCAST_ADDR||
               p.latitude < -90000000||p.latitude > 90000000||
               p.longitude < -180000000||p.longitude > 180000000){
                valid=false;
                break;
            }
            for(size_t previous=0;previous<i;++previous)
                if(parsed[previous].node==p.node){valid=false;break;}
        }
        f.close();
        if(!valid){
            Serial.printf("[T5-MESHTASTIC] WARN invalid position snapshot path=%s size=%u header_bytes=%u magic=%08lx count=%lu expected_record=%u; preserving\n",
                          POSITIONS_PATH,(unsigned)size,(unsigned)bytes_read,
                          (unsigned long)header[0],(unsigned long)header[1],
                          (unsigned)sizeof(PositionRecord));
            // Do not erase or replace potentially recoverable coordinates.
            // Move them aside with a short, unused SPIFFS-compatible filename.
            char archive[32]{};
            for(unsigned attempt=0;attempt<=16;++attempt){
                if(attempt==0)
                    snprintf(archive,sizeof(archive),"/mt_positions.bad");
                else
                    snprintf(archive,sizeof(archive),"/mt_positions.bad.%u",attempt);
                if(SPIFFS.exists(archive))continue;
                if(SPIFFS.rename(POSITIONS_PATH,archive))
                    Serial.printf("[T5-MESHTASTIC] invalid position snapshot archived as %s\n",archive);
                else
                    Serial.printf("[T5-MESHTASTIC] WARN could not archive position snapshot as %s; original retained\n",
                                  archive);
                return;
            }
            Serial.println("[T5-MESHTASTIC] WARN position recovery filenames exhausted; original retained");
            return;
        }
        for(size_t i=0;i<header[1];++i){
            const PositionRecord& p=parsed[i];
            PositionRecord* slot=writable_position(p.node);
            if(slot){
                *slot=p;
                if(NodeRecord* node=ensure(p.node))
                    node->last_seen=max(node->last_seen,p.received_utc);
            }
        }
        if(header[1])
            Serial.printf("[T5-MESHTASTIC] loaded %lu received-node positions\n",
                          (unsigned long)header[1]);
    }
public:
    void receive_telemetry(const MeshPacket& packet){
        if(packet.header.from==leaf.getNodeNum())return;
        const auto data=meshink_mt_wire::decode_device_telemetry(packet.payload,
                                                               packet.payloadLen);
        if(!data.valid)return;
        NodeRecord* node=ensure(packet.header.from);
        if(!node)return;
        TelemetryRecord* record=nullptr;
        for(auto& t:telemetry_)if(t.node==packet.header.from){record=&t;break;}
        if(!record)for(auto& t:telemetry_)if(!t.node){record=&t;break;}
        if(!record)return;
        record->node=packet.header.from;
        record->battery=data.battery;record->volts=data.voltage;
        record->received=now_utc();
        node->last_seen=record->received;
        node->last_rssi=packet.rxRssi;node->last_snr=packet.rxSnr;
        refresh(true);ui_request_data_refresh("meshtastic-telemetry");
    }
    void message_state_changed(){rebuild_active();}
    void receive_position(const MeshPacket& packet){
        if(packet.header.from==leaf.getNodeNum())return;
        const auto decoded=meshink_mt_wire::decode_position(packet.payload,packet.payloadLen);
        if(!decoded.valid)return;
        ++position_received_count;
        NodeRecord* node=ensure(packet.header.from);
        PositionRecord* pos=writable_position(packet.header.from);
        if(!node||!pos)return;
        const bool first=pos->received_utc==0;
        pos->latitude=decoded.lat_e6;pos->longitude=decoded.lon_e6;
        pos->received_utc=now_utc();
        node->last_seen=pos->received_utc;
        node->last_rssi=packet.rxRssi;node->last_snr=packet.rxSnr;
        const uint8_t start=packet.header.getHopStart();
        const uint8_t left=packet.header.getHopLimit();
        node->last_hops=start>=left?(uint8_t)(start-left):0;
        // Throttle flash even if another node publishes every few seconds.
        if(first||millis()-position_saved_at_>=300000UL)
            if(!save_positions())Serial.println("[T5-MESHTASTIC] WARN position save failed");
        refresh(true);
        ui_request_data_refresh("meshtastic-position");
    }
private:
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

    bool save_nodes(){
        File file=SPIFFS.open(NODES_STAGING,"w");
        if(!file)return false;
        uint16_t count=0;
        for(const auto& item:nodes_)if(item.used)++count;
        const uint32_t header[2]={NODES_MAGIC,count};
        bool ok=file.write((const uint8_t*)header,sizeof(header))==sizeof(header);
        for(const auto& item:nodes_)if(ok&&item.used)
            ok=file.write((const uint8_t*)&item,sizeof(item))==sizeof(item);
        file.flush();file.close();
        if(!ok){SPIFFS.remove(NODES_STAGING);return false;}
        // Preserve the original in a separate recovery file until the new
        // snapshot has been completely written to flash.
        SPIFFS.remove("/meshtastic_nodes.old");
        const bool had_previous=SPIFFS.exists(NODES_PATH);
        if(had_previous&&!SPIFFS.rename(NODES_PATH,"/meshtastic_nodes.old"))return false;
        if(!SPIFFS.rename(NODES_STAGING,NODES_PATH)){
            if(had_previous)SPIFFS.rename("/meshtastic_nodes.old",NODES_PATH);
            return false;
        }
        return true;
    }
    void load_nodes(){
        File file=SPIFFS.open(NODES_PATH,"r");
        if(!file)return;
        uint32_t header[2]{};
        if(file.read((uint8_t*)header,sizeof(header))!=sizeof(header)||
           header[0]!=NODES_MAGIC||header[1]>MAX_NODES||
           file.size()!=sizeof(header)+header[1]*sizeof(NodeRecord)){
            file.close();return;
        }
        for(size_t i=0;i<header[1];++i){
            NodeRecord item{};
            if(file.read((uint8_t*)&item,sizeof(item))!=sizeof(item))break;
            if(!item.used||!MeshNodeId::isUsableNodeNum(item.node))continue;
            NodeRecord* slot=ensure(item.node);
            if(!slot)break;
            *slot=item;
            if(slot->has_public_key&&
               (!MeshCryptoPKI::isUsablePublicKey(slot->public_key)||
                MeshNodeId::nodeNumFromPublicKey(slot->public_key,32)!=slot->node))
                slot->has_public_key=false;
        }
        file.close();
    }
    void begin() {
        load_nodes();
        load_positions();
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
        // Avoid rewriting flash for each identical periodic NodeInfo advert.
        const bool durable_change=!item->has_public_key||
            memcmp(item->public_key,user.public_key.bytes,32)!=0||
            item->role!=(uint8_t)user.role||
            (user.long_name[0]&&strncmp(item->long_name,user.long_name,sizeof(item->long_name)-1))||
            (user.short_name[0]&&strncmp(item->short_name,user.short_name,sizeof(item->short_name)-1));
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
        if(durable_change&&!save_nodes())Serial.println("[T5-MESHTASTIC] WARN node snapshot could not be saved");
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
        ++received_count;
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
        if(last_message_packet_id==request_id){
            last_message_sequence=0;
            last_message_packet_id=0; // A genuine ACK always wins over local TX status.
        }
        // The journal already stores each outgoing packet ID; a single global
        // pending slot loses earlier ACKs when consecutive messages are sent.
        if(request_id&&meshink_message_store().mark_delivered_by_ack(
                request_id,MESHTASTIC_PROTOCOL_ID)){
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

    size_t map_node_count()const override{
        size_t n=0;
        for(const auto& p:positions_){
            const NodeRecord* node=p.node?find(p.node):nullptr;
            if(node&&!node->hidden)++n;
        }
        return n;
    }
    bool map_node(size_t index,UiMapNode& out)const override{
        for(const auto& p:positions_){
            const NodeRecord* node=p.node?find(p.node):nullptr;
            if(!node||node->hidden)continue;
            if(index--!=0)continue;
            out=UiMapNode{};
            strncpy(out.name,node->long_name[0]?node->long_name:node->identity,
                    sizeof(out.name)-1);
            node_key(node->node,out.key);
            out.latitude=p.latitude;out.longitude=p.longitude;
            out.role=shared_role(node->role);
            out.advertised_at=p.received_utc;
            // This marks a location learned from radio GPS telemetry.
            // Map marker navigation remains unchanged for BOTH protocols.
            out.gps_from_reply=true;
            const uint32_t now=now_utc();
            const uint32_t age=now>=p.received_utc?now-p.received_utc:0;
            out.gps_received_millis=millis()-
                (uint32_t)min((uint64_t)age*1000ULL,(uint64_t)0x7FFFFFFFUL);
            return true;
        }
        return false;
    }
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
    size_t channel_name_limit()const override{return 12;}
    size_t channel_capacity()const override{return 8;}
    // The public primary channel must never be replaced or removed.
    // UiDataProvider's default management methods deliberately return false.
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
        static char seen[32],advert[32],route[32],position[48];
        static char telemetry_note[64],status_note[48];
        format_age(node->last_seen,seen,sizeof(seen));
        format_age(node->last_node_info,advert,sizeof(advert));
        if(node->last_hops)snprintf(route,sizeof(route),"%u HOP%s",
            (unsigned)node->last_hops,node->last_hops==1?"":"S");
        else strcpy(route,"DIRECT / ZERO HOP");
        const PositionRecord* pos=position_for(node->node);
        if(pos)snprintf(position,sizeof(position),"%.5f, %.5f",
                        (double)pos->latitude/1000000.0,
                        (double)pos->longitude/1000000.0);
        else strcpy(position,"NO POSITION RECEIVED");
        out=UiNodeDetails{};
        out.name=node->long_name[0]?node->long_name:node->identity;
        out.identity=node->identity;
        out.last_seen=seen;
        out.route=route;
        out.position=position;
        snprintf(status_note,sizeof(status_note),"RSSI %d dBm / SNR %.1f dB",
                 (int)node->last_rssi,(double)node->last_snr);
        out.status=status_note;
        const TelemetryRecord* latest=nullptr;
        for(const auto& t:telemetry_)if(t.node==node->node){latest=&t;break;}
        if(latest)
            snprintf(telemetry_note,sizeof(telemetry_note),
                     "BATTERY %u%% / %.2fV",(unsigned)latest->battery,
                     (double)latest->volts);
        else strcpy(telemetry_note,"NOT RECEIVED");
        out.telemetry=telemetry_note;
        out.path="NOT AVAILABLE";
        out.trace="NOT AVAILABLE";
        out.access_level="";
        out.role_label=role_label(node->role);
        out.role=shared_role(node->role);
        out.capabilities=0;
        out.saved_contact=!node->hidden;
        out.advert_age=advert;
        out.position_source=pos?"MESHTASTIC POSITION PACKET":"NO POSITION RECEIVED";
        if(pos){out.latitude=pos->latitude;out.longitude=pos->longitude;}
        return true;
    }

    bool add_active_node()override{
        NodeRecord* node=find(active_node_);if(!node)return false;
        node->hidden=false;save_nodes();refresh(true);return true;
    }
    bool remove_active_contact()override{
        NodeRecord* node=find(active_node_);if(!node)return false;
        node->hidden=true;save_nodes();refresh(true);return true;
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

// Construct Meshtastic-only UI collections after protocol selection, in PSRAM.
// Keeping this object in .bss used internal RAM even on MeshCore boots.
static MeshtasticUiProvider* provider=nullptr;

static bool pki_key_lookup(NodeNum node,uint8_t pub_key[32]) {
    return provider&&provider->lookup_key(node,pub_key);
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
    const float frequency=mesh_config.radio.channelNum?
        MeshRegion::getFrequencyForSlot(mesh_config.radio.region,
                                       mesh_config.radio.preset,mesh_config.radio.channelNum):
        MeshRegion::getFrequency(mesh_config.radio.region,mesh_config.radio.preset,preset_name);
    snprintf(radio_summary,sizeof(radio_summary),"%s / %s / %.3f MHz",
        MeshRegion::getRegionName(mesh_config.radio.region),preset_name,(double)frequency);
}

static void save_config() {
    Preferences prefs;
    if(!prefs.begin("meshtastic",false))return;
    prefs.putUChar("region",(uint8_t)mesh_config.radio.region);
    prefs.putUChar("preset",(uint8_t)mesh_config.radio.preset);
    prefs.putUChar("hop",mesh_config.hopLimit);
    prefs.putChar("power",mesh_config.radio.txPower);
    prefs.putULong("slot",mesh_config.radio.channelNum);
    prefs.putUChar("pos_int",position_interval_min);
    prefs.putUChar("pos_prec",position_precision);
    prefs.putUChar("tel_int",telemetry_interval_min);
    prefs.putUChar("info_int",nodeinfo_interval_hours);
    prefs.putBool("carrier",carrier_sense_enabled);
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
    int8_t power=0;
    uint32_t slot=0;
    position_interval_min=0;
    position_precision=0;
    telemetry_interval_min=0;
    nodeinfo_interval_hours=3;
    carrier_sense_enabled=true;
    if(prefs.begin("meshtastic",true)){
        region=prefs.getUChar("region",region);
        preset=prefs.getUChar("preset",preset);
        hop=prefs.getUChar("hop",hop);
        power=prefs.getChar("power",0);
        slot=prefs.getULong("slot",0);
        position_interval_min=prefs.getUChar("pos_int",0);
        position_precision=prefs.getUChar("pos_prec",0);
        telemetry_interval_min=prefs.getUChar("tel_int",0);
        nodeinfo_interval_hours=prefs.getUChar("info_int",3);
        carrier_sense_enabled=prefs.getBool("carrier",true);
        prefs.end();
    }
    if(!supported_region((RegionCode)region))
        region=(uint8_t)libmeshtastic_leaf::REGION_ANZ;
    if(preset>(uint8_t)libmeshtastic_leaf::PRESET_MEDIUM_TURBO)
        preset=(uint8_t)libmeshtastic_leaf::PRESET_LONG_FAST;
    mesh_config=MeshConfig{};
    mesh_config.radio.region=(RegionCode)region;
    mesh_config.radio.preset=(ModemPreset)preset;
    const auto info=MeshRegion::getRegion(mesh_config.radio.region);
    const auto params=MeshRegion::getModemParams(mesh_config.radio.preset);
    const uint32_t slots=info?info->numSlots(params.bw):0;
    mesh_config.radio.channelNum=slot<=slots?slot:0;
    // A manually selected power may never exceed local regional limits
    // or the T5 SX1262's supported output range.
    const int max_power=min(22,(int)MeshRegion::getPowerLimit(mesh_config.radio.region));
    mesh_config.radio.txPower=(power>=2&&power<=max_power)?power:0;
    mesh_config.hopLimit=constrain(hop,(uint8_t)1,(uint8_t)7);
    if(position_interval_min!=15&&position_interval_min!=30&&
       position_interval_min!=60)position_interval_min=0;
    if(position_precision>2)position_precision=0;
    if(telemetry_interval_min!=15&&telemetry_interval_min!=30&&
       telemetry_interval_min!=60)telemetry_interval_min=0;
    if(nodeinfo_interval_hours!=0&&nodeinfo_interval_hours!=1&&
       nodeinfo_interval_hours!=3&&nodeinfo_interval_hours!=6)
        nodeinfo_interval_hours=3;
    protocol_settings_dirty=false;
    update_radio_summary();
}

// The first-use wizard commits its selected region/preset only on confirmation.
static RegionCode wizard_region(size_t index) {
    size_t count=0,valid=0;
    const auto* regions=MeshRegion::getAllRegions(count);
    for(size_t i=0;regions&&i<count;++i){
        if(!supported_region(regions[i].code))continue;
        if(valid++==index)return regions[i].code;
    }
    return libmeshtastic_leaf::REGION_UNSET;
}
static size_t wizard_region_count() {
    size_t count=0,valid=0;
    const auto* regions=MeshRegion::getAllRegions(count);
    for(size_t i=0;regions&&i<count;++i)if(supported_region(regions[i].code))++valid;
    return valid;
}
static const char* wizard_region_name(size_t index) {
    const auto region=wizard_region(index);
    return supported_region(region)?MeshRegion::getRegionName(region):"";
}
static size_t wizard_preset_count() {
    return (size_t)libmeshtastic_leaf::PRESET_MEDIUM_TURBO+1;
}
static const char* wizard_preset_name(size_t index) {
    return index<wizard_preset_count()?MeshRegion::getPresetName((ModemPreset)index,true):"";
}
static bool wizard_commit_radio(size_t region,size_t preset,uint8_t hops) {
    const auto selected=wizard_region(region);
    if(!supported_region(selected)||preset>=wizard_preset_count()||hops<1||hops>7)return false;
    mesh_config.radio.region=selected;
    mesh_config.radio.preset=(ModemPreset)preset;
    mesh_config.hopLimit=hops;
    save_config();update_radio_summary();protocol_settings_dirty=true;
    return true;
}


enum : uint16_t {
 MT_REGION=1, MT_PRESET, MT_HOPS, MT_APPLY, MT_POWER, MT_SLOT,
 MT_POS_INTERVAL, MT_POS_PRECISION, MT_POS_NOW, MT_TEL_INTERVAL, MT_TEL_NOW,
 MT_INFO_INTERVAL, MT_INFO_NOW, MT_CARRIER, MT_AIRTIME, MT_NODE_ID
};
static uint8_t cycle_interval(uint8_t n){return n==0?15:n==15?30:n==30?60:0;}
static bool send_position_now(){
 if(!runtime_ready)return false;
 const auto gps=meshink_gps_read_status();
 if(!gps.valid||
    meshink_gps_constellation_mode()==MeshInkGpsConstellationMode::None)
    return false; // Disabled GPS and stale/no-fix positions must not transmit.
 int32_t lat=(int32_t)gps.latitude,lon=(int32_t)gps.longitude;
 if(position_precision){
  const int32_t q=position_precision==1?1000:10000;
  lat=((lat>=0?lat+q/2:lat-q/2)/q)*q;
  lon=((lon>=0?lon+q/2:lon-q/2)/q)*q;
 }
 uint8_t bytes[16]{};
 size_t length=meshink_mt_wire::encode_position(bytes,lat,lon);
 if(!length)return false;
 const uint32_t packet=leaf.sendData(meshtastic_PortNum_POSITION_APP,bytes,length,
                                     BROADCAST_ADDR,false);
 if(!packet){++send_refused;return false;}
 last_position_send_ms=millis();
 Serial.printf("[T5-MESHTASTIC] position queued id=%lu\n",(unsigned long)packet);
 return true;
}
static bool send_telemetry_now(){
 if(!runtime_ready)return false;
 uint8_t percent=0;uint16_t mv=0;
 if(!meshink_power_read_battery_percent(percent)||
    !meshink_power_read_battery_mv(mv))return false;
 uint8_t bytes[32]{};
 const size_t length=meshink_mt_wire::encode_device_telemetry(
     bytes,now_utc(),percent,(float)mv/1000.0f);
 if(!length)return false;
 const uint32_t packet=leaf.sendData(meshtastic_PortNum_TELEMETRY_APP,bytes,length,
                                     BROADCAST_ADDR,false);
 if(!packet){++send_refused;return false;}
 last_telemetry_send_ms=millis();
 Serial.printf("[T5-MESHTASTIC] telemetry queued id=%lu\n",(unsigned long)packet);
 return true;
}
static size_t protocol_settings_count(){return 16;}
static bool protocol_settings_item(size_t n,MeshInkProtocolSettingItem& item){
 static char text[48],other[48];
 item={};
 switch(n){
 case 0:item={MT_REGION,"REGION",MeshRegion::getRegionName(mesh_config.radio.region),true};return true;
 case 1:item={MT_PRESET,"MODEM PRESET",MeshRegion::getPresetName(mesh_config.radio.preset,true),true};return true;
 case 2:snprintf(text,sizeof(text),"%u HOPS",(unsigned)mesh_config.hopLimit);
        item={MT_HOPS,"HOP LIMIT",text,true};return true;
 case 3:if(mesh_config.radio.txPower)
         snprintf(text,sizeof(text),"%d dBm",(int)mesh_config.radio.txPower);
        else strcpy(text,"AUTO / REGION");
        item={MT_POWER,"TX POWER",text,true};return true;
 case 4:if(mesh_config.radio.channelNum)
         snprintf(text,sizeof(text),"SLOT %lu",(unsigned long)mesh_config.radio.channelNum);
        else strcpy(text,"AUTOMATIC");
        item={MT_SLOT,"FREQUENCY SLOT",text,true};return true;
 case 5:if(position_interval_min)
         snprintf(text,sizeof(text),"EVERY %u MIN",(unsigned)position_interval_min);
        else strcpy(text,"OFF");
        item={MT_POS_INTERVAL,"SHARE GPS POSITION",text,true};return true;
 case 6:item={MT_POS_PRECISION,"GPS PRECISION",
        position_precision==0?"EXACT":position_precision==1?"ABOUT 100 METRES":"ABOUT 1 KM",true};return true;
 case 7:item={MT_POS_NOW,"SEND POSITION NOW",
        meshink_gps_read_status().valid?"GPS FIX AVAILABLE":"NO GPS FIX",true};return true;
 case 8:if(telemetry_interval_min)
         snprintf(text,sizeof(text),"EVERY %u MIN",(unsigned)telemetry_interval_min);
        else strcpy(text,"OFF");
        item={MT_TEL_INTERVAL,"BATTERY TELEMETRY",text,true};return true;
 case 9:item={MT_TEL_NOW,"SEND BATTERY NOW","DEVICE METRICS",true};return true;
 case 10:if(nodeinfo_interval_hours)
          snprintf(other,sizeof(other),"EVERY %u HOURS",(unsigned)nodeinfo_interval_hours);
         else strcpy(other,"OFF");
         item={MT_INFO_INTERVAL,"NODEINFO INTERVAL",other,true};return true;
 case 11:item={MT_INFO_NOW,"ANNOUNCE NODE NOW","PUBLIC NAME / KEY",true};return true;
 case 12:item={MT_CARRIER,"CARRIER SENSE",carrier_sense_enabled?"ON":"OFF",true};return true;
 case 13:{const auto air=leaf.getAirtime();
          snprintf(text,sizeof(text),"TX %.1f%% / CH %.1f%%",
                   (double)air.txUtilizationPercent,(double)air.channelUtilizationPercent);
          item={MT_AIRTIME,"AIRTIME / CHANNEL",text,false};return true;}
 case 14:snprintf(text,sizeof(text),"!%08lx",(unsigned long)leaf.getNodeNum());
         item={MT_NODE_ID,"MY NODE ID",text,false};return true;
 case 15:item={MT_APPLY,"APPLY RADIO CHANGES",
         protocol_settings_dirty?"RESTART REQUIRED":"CURRENT SETTINGS ACTIVE",
         protocol_settings_dirty};return true;
 default:return false;
 }
}
static MeshInkProtocolSettingResult activate_protocol_setting(uint16_t id){
 switch(id){
 case MT_REGION:{
  size_t count=0;const auto* list=MeshRegion::getAllRegions(count);
  if(!list||!count)return MeshInkProtocolSettingResult::Failed;
  size_t current=0;
  while(current<count&&list[current].code!=mesh_config.radio.region)++current;
  for(size_t step=1;step<=count;++step){
   const auto next=list[(current+step)%count].code;
   if(!supported_region(next))continue;
   mesh_config.radio.region=next;mesh_config.radio.channelNum=0;
   if(mesh_config.radio.txPower>MeshRegion::getPowerLimit(next))
       mesh_config.radio.txPower=0;
   save_config();protocol_settings_dirty=true;update_radio_summary();
   return MeshInkProtocolSettingResult::RestartRequired;
  }
  return MeshInkProtocolSettingResult::Failed;
 }
 case MT_PRESET:
  mesh_config.radio.preset=(ModemPreset)(((uint8_t)mesh_config.radio.preset+1)%
                     ((uint8_t)libmeshtastic_leaf::PRESET_MEDIUM_TURBO+1));
  mesh_config.radio.channelNum=0;
  save_config();protocol_settings_dirty=true;update_radio_summary();
  return MeshInkProtocolSettingResult::RestartRequired;
 case MT_HOPS:
  mesh_config.hopLimit=(uint8_t)(mesh_config.hopLimit>=7?1:mesh_config.hopLimit+1);
  save_config();protocol_settings_dirty=true;
  return MeshInkProtocolSettingResult::RestartRequired;
 case MT_POWER:{
  const int8_t options[]={0,10,14,17,20,22};
  const int max_power=min(22,(int)MeshRegion::getPowerLimit(mesh_config.radio.region));
  size_t current=0;while(current<6&&options[current]!=mesh_config.radio.txPower)
      ++current;
  for(size_t attempt=1;attempt<=6;++attempt){
   const int8_t candidate=options[(current+attempt)%6];
   if(candidate!=0&&candidate>max_power)continue;
   mesh_config.radio.txPower=candidate;break;
  }
  save_config();protocol_settings_dirty=true;
  return MeshInkProtocolSettingResult::RestartRequired;
 }
 case MT_SLOT:{
  const auto* region=MeshRegion::getRegion(mesh_config.radio.region);
  if(!region)return MeshInkProtocolSettingResult::Failed;
  const auto params=MeshRegion::getModemParams(mesh_config.radio.preset);
  const uint32_t count=region->numSlots(params.bw);
  mesh_config.radio.channelNum=mesh_config.radio.channelNum>=count?
                               0:mesh_config.radio.channelNum+1;
  save_config();protocol_settings_dirty=true;update_radio_summary();
  return MeshInkProtocolSettingResult::RestartRequired;
 }
 case MT_POS_INTERVAL:
  position_interval_min=cycle_interval(position_interval_min);
  last_position_send_ms=millis();save_config();
  return MeshInkProtocolSettingResult::Saved;
 case MT_POS_PRECISION:
  position_precision=(uint8_t)((position_precision+1)%3);save_config();
  return MeshInkProtocolSettingResult::Saved;
 case MT_POS_NOW:
  return send_position_now()?MeshInkProtocolSettingResult::Saved:
                             MeshInkProtocolSettingResult::Failed;
 case MT_TEL_INTERVAL:
  telemetry_interval_min=cycle_interval(telemetry_interval_min);
  last_telemetry_send_ms=millis();save_config();
  return MeshInkProtocolSettingResult::Saved;
 case MT_TEL_NOW:
  return send_telemetry_now()?MeshInkProtocolSettingResult::Saved:
                              MeshInkProtocolSettingResult::Failed;
 case MT_INFO_INTERVAL:
  nodeinfo_interval_hours=nodeinfo_interval_hours==0?1:
                          nodeinfo_interval_hours==1?3:
                          nodeinfo_interval_hours==3?6:0;
  leaf.setNodeInfoInterval((uint32_t)nodeinfo_interval_hours*3600U);
  save_config();return MeshInkProtocolSettingResult::Saved;
 case MT_INFO_NOW:
  return runtime_ready&&leaf.sendNodeInfo()?
         MeshInkProtocolSettingResult::Saved:MeshInkProtocolSettingResult::Failed;
 case MT_CARRIER:
  carrier_sense_enabled=!carrier_sense_enabled;
  leaf.setCarrierSense(carrier_sense_enabled);
  save_config();return MeshInkProtocolSettingResult::Saved;
 case MT_APPLY:
  return protocol_settings_dirty?MeshInkProtocolSettingResult::RestartNow:
                                  MeshInkProtocolSettingResult::Unchanged;
 default:return MeshInkProtocolSettingResult::Failed;
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
    const bool storage_mounted=meshink_storage_mount_internal_safe();
    if(!storage_mounted)ui_show_storage_initializing();
    if(!storage_mounted){
        Serial.println("[T5-MESHTASTIC] SPIFFS unavailable");
        return;
    }

    if(!meshink_backup_recover_pending()){
        Serial.println("[T5-MESHTASTIC] interrupted restore recovery failed");
        return;
    }
    // Initialize shared board services for the setup UI, but defer LoRa start
    // until the region has been explicitly confirmed.
    meshink_board_begin_local();
    // No Meshtastic RF transmissions before the region is confirmed in setup.
    Preferences wizard_gate;
    bool configured=false;
    if(wizard_gate.begin("t5-ui",true)){
        configured=wizard_gate.getBool("setup_mst",false);
        wizard_gate.end();
    }
    if(!configured){
        Serial.println("[T5-MESHTASTIC] first-use setup pending; LoRa startup deferred");
        return;
    }
    if(!provider){
        void* memory=heap_caps_malloc(sizeof(MeshtasticUiProvider),
                                      MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
        if(!memory){
            Serial.println("[T5-MESHTASTIC] PSRAM unavailable for protocol UI provider");
            return;
        }
        provider=new (memory) MeshtasticUiProvider();
    }

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
    leaf.setDefaultChannel(); // exactly one standard public primary channel
    leaf.setCarrierSense(carrier_sense_enabled);
    leaf.setNodeInfoInterval((uint32_t)nodeinfo_interval_hours*3600U);
    last_position_send_ms=millis();
    last_telemetry_send_ms=millis();
    provider->begin();
    ui_use_data_provider(provider);
    runtime_ready=true;
    ui_mesh_ready();

    Serial.printf("[T5-MESHTASTIC] ready core=1.0.0 node=!%08lx radio=%s\n",
        (unsigned long)leaf.getNodeNum(),radio_summary);
}

static void handle_packet(const MeshPacket& packet) {
    if(packet.portNum==meshtastic_PortNum_TELEMETRY_APP){
        provider->receive_telemetry(packet);return;
    }
    if(packet.portNum==meshtastic_PortNum_POSITION_APP){
        provider->receive_position(packet);
        return;
    }
    if(packet.portNum==meshtastic_PortNum_NODEINFO_APP){
        provider->learn_node_info(packet);
        return;
    }
    if(packet.portNum==meshtastic_PortNum_TEXT_MESSAGE_APP){
        provider->receive_text(packet);
        return;
    }
    if(packet.portNum==meshtastic_PortNum_ROUTING_APP&&packet.requestId&&
       MeshPayloadCodec::isRoutingAck(packet.payload,packet.payloadLen)){
        provider->delivered(packet.requestId);
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
    // A nonzero send ID means queued, not transmitted. Leaf reports
    // in-flight/backoff as isTransmitting() and only clears it after RF finish.
    // Direct delivery still requires a separate genuine routing ACK.
    if(last_message_sequence&&!leaf.isTransmitting()){
        const bool failed=leaf.getLastSendResult()==
                           libmeshtastic_leaf::SendResult::RADIO_ERROR;
        const UiMessageState state=failed?UiMessageState::Failed:UiMessageState::Sent;
        if(meshink_message_store().update_state(last_message_sequence,state)){
            if(failed)++radio_errors;
            Serial.printf("[T5-MESHTASTIC] TX complete seq=%lu state=%s\n",
                          (unsigned long)last_message_sequence,failed?"FAILED":"SENT");
            last_message_sequence=0;last_message_packet_id=0;
            provider->message_state_changed();
            ui_request_data_refresh("meshtastic-tx");
        }
    }
    meshink_gps_service_loop();
    // Background packets are opt-in and lowest priority. Do not interrupt
    // messages, pending acknowledgements, or an on-air transmission.
    const uint32_t now=millis();
    if(!leaf.isTransmitting()&&!leaf.hasPendingAck()&&!last_message_sequence){
        if(position_interval_min&&
           now-last_position_send_ms>=(uint32_t)position_interval_min*60000UL)
            (void)send_position_now();
        else if(telemetry_interval_min&&
                now-last_telemetry_send_ms>=(uint32_t)telemetry_interval_min*60000UL)
            (void)send_telemetry_now();
    }
    meshink_rtc_tick();
}

static UiDataProvider* data_provider() {
    return provider;
}

static void refresh_ui_data() {
    if(provider)provider->refresh(true);
}

static bool send_active(const char* text) {
    if(!runtime_ready||!text||!text[0])return false;
    const size_t len=min(strlen(text),(size_t)MESHINK_MESSAGE_TEXT_MAX);
    uint32_t packet_id=0;
    uint8_t key[7]{};
    MeshInkMessageKind kind=MeshInkMessageKind::Channel;
    if(provider->active_is_channel()){
        key[0]=0;
        packet_id=leaf.sendData(meshtastic_PortNum_TEXT_MESSAGE_APP,
            (const uint8_t*)text,len,BROADCAST_ADDR,false);
    }else{
        // active_title is presentation; derive destination from the provider's active detail identity.
        UiNodeDetails details{};
        if(!provider->active_node_details(details)||!details.identity||details.identity[0]!='!')return false;
        const NodeNum node=(NodeNum)strtoul(details.identity+1,nullptr,16);
        node_key(node,key);
        kind=MeshInkMessageKind::Direct;
        uint8_t remote_key[32]{};
        if(provider->lookup_key(node,remote_key))
            packet_id=leaf.sendDataPKI(meshtastic_PortNum_TEXT_MESSAGE_APP,
                (const uint8_t*)text,len,node,remote_key,true);
        else
            packet_id=leaf.sendData(meshtastic_PortNum_TEXT_MESSAGE_APP,
                (const uint8_t*)text,len,node,true);
    }
    if(!packet_id){
        ++send_refused;
        Serial.printf("[T5-MESHTASTIC] text rejected reason=%u\n",
                      (unsigned)leaf.getLastSendResult());
        return false;
    }

    char stored[MESHINK_MESSAGE_TEXT_BYTES]{};
    memcpy(stored,text,len);stored[len]=0;
    const uint32_t sequence=meshink_message_store().append(
        kind,key,kind==MeshInkMessageKind::Channel?1:4,stored,now_utc(),
        UiMessageState::Sending,packet_id,MeshInkMessageOrigin::LocalUi,
        false,0,MESHINK_MESSAGE_PATH_UNKNOWN,false,MESHTASTIC_PROTOCOL_ID);
    if(!sequence){
        Serial.println("[T5-MESHTASTIC] send accepted but flash journal failed");
        return false;
    }
    last_message_sequence=sequence;
    last_message_packet_id=packet_id;
    ++sent_count;
    provider->refresh(true);
    // Re-open current conversation index cache without changing selection.
    if(provider->active_is_channel())provider->open_channel(0);
    else {
        UiNodeDetails details{};
        if(provider->active_node_details(details)){
            for(size_t i=0;i<provider->contact_count();++i){
                const UiListEntry& entry=provider->contact(i);
                if(entry.title&&details.name&&!strcmp(entry.title,details.name)){
                    provider->open_contact(i);break;
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


static bool meshtastic_request_diagnostics(){return runtime_ready;}
static const char* meshtastic_diag_core(){
    static char info[100];
    snprintf(info,sizeof(info),"Leaf 1.0.0 | node !%08lx | public primary channel",
             (unsigned long)leaf.getNodeNum());
    return info;
}
static const char* meshtastic_diag_radio(){
    static char info[144];
    const auto air=leaf.getAirtime();
    snprintf(info,sizeof(info),
             "%s | power %s | slot %lu | airtime %.1f%% | channel %.1f%%",
             radio_summary,mesh_config.radio.txPower?"MANUAL":"AUTO",
             (unsigned long)mesh_config.radio.channelNum,
             (double)air.txUtilizationPercent,
             (double)air.channelUtilizationPercent);
    return info;
}
static const char* meshtastic_diag_packets(){
    static char info[170];
    snprintf(info,sizeof(info),
             "TX queued %lu | RX text %lu | RX position %lu | refused %lu | RF errors %lu | waiting ACK %s",
             (unsigned long)sent_count,(unsigned long)received_count,
             (unsigned long)position_received_count,
             (unsigned long)send_refused,(unsigned long)radio_errors,
             leaf.hasPendingAck()?"YES":"NO");
    return info;
}

static const MeshInkProtocolBackend& backend() {
    static const MeshInkProtocolBackend value=[]{
        MeshInkProtocolBackend b{};
        b.descriptor.id=MESHTASTIC_PROTOCOL_ID;
        b.descriptor.name="MESHTASTIC";
        b.descriptor.core_name="libmeshtastic-leaf";
        b.descriptor.core_version="1.0.0 (bd542d6e)";
        b.descriptor.capabilities=MESHINK_PROTOCOL_CAP_DIAGNOSTICS;

        b.setup=setup;
        b.loop=loop;
        b.is_running=is_running;
        b.prepare_shutdown=prepare_shutdown;

        b.provider=data_provider;
        b.refresh_ui_data=refresh_ui_data;
        b.send_active=send_active;

        b.setup_region_count=wizard_region_count;
        b.setup_region_name=wizard_region_name;
        b.setup_preset_count=wizard_preset_count;
        b.setup_preset_name=wizard_preset_name;
        b.setup_commit_radio=wizard_commit_radio;

        b.settings_count=protocol_settings_count;
        b.settings_item=protocol_settings_item;
        b.settings_activate=activate_protocol_setting;

        b.apply_name=apply_name;
        b.name_character_allowed=name_character_allowed;
        b.node_name_max_length=node_name_max_length;
        b.radio_summary=radio_summary_value;
        b.request_diagnostics=meshtastic_request_diagnostics;
        b.diagnostics_busy=[]()->bool{return false;};
        b.diagnostics_core=meshtastic_diag_core;
        b.diagnostics_radio=meshtastic_diag_radio;
        b.diagnostics_packets=meshtastic_diag_packets;
        return b;
    }();
    return value;
}

} // namespace

const MeshInkProtocolBackend* meshink_protocol_backend_slot_2() {
    return &backend();
}
