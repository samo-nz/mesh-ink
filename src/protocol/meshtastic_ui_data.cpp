#include "meshtastic_ui_data.h"
#include "../ui_onboarding.h"
#include "../message_store.h"
#include <pb_decode.h>
#include <esp_system.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <new>

// MeshInk's shared views, not a reimplementation of Meshtastic networking.
// The selected core supplies node, channel, configuration, packet and ACK data
// via *official* FromRadio structures. This cache is allocated only when
// Meshtastic is the boot-selected protocol.
namespace {
constexpr size_t kMaxNodes=64, kMaxChannels=8, kMaxMessages=96;
constexpr uint32_t kBroadcast=0xffffffffu;
// Upstream NodeInfoModule suppresses repeat responses from the same requester
// for 12 hours. Retry only following NEW traffic after that window.
constexpr uint32_t kNodeInfoRetryMs=12UL*60UL*60UL*1000UL;
constexpr uint32_t kNodeInfoGlobalSpacingMs=60UL*1000UL;
constexpr uint32_t kNodeInfoFailedQueueRetryMs=5000UL;
struct Channel {
    uint8_t index=0;
    bool enabled=false;
    char name[16]{};
    char subtitle[28]{};
    UiListEntry row{};
};
struct Node {
    uint32_t number=0;
    bool position_valid=false;
    int32_t latitude=0,longitude=0;
    uint32_t heard=0;
    bool has_battery=false;
    uint32_t battery=0;
    char name[40]{};
    char identity[16]{};
    char role_label[24]{};
    char subtitle[48]{};
    char heard_text[16]{};
    // UI-only discovery state; never change official NodeDB/persisted identity.
    bool name_known=false,name_request_pending=false,name_request_sent=false;
    uint32_t last_name_request_ms=0;
    uint8_t heard_channel=0;
    UiNodeRole role=UiNodeRole::Unknown;
    UiListEntry row{};
};
struct Message {
    uint32_t packet_id=0;
    uint32_t peer=0;
    uint8_t channel=0;
    bool broadcast=false, unread=false;
    uint32_t journal_sequence=0;
    char text[241]{};
    char time[16]{};
    UiMessage row{};
};
struct State {
    uint32_t me=0, revision=0;
    meshtastic_User local_user=meshtastic_User_init_zero;
    bool local_user_ready=false;
    bool name_tx_seen=false,name_attempt_seen=false;
    uint32_t last_name_tx_ms=0,last_name_attempt_ms=0;
    uint32_t selected_node=0;
    uint8_t selected_channel=0;
    bool selected_is_channel=true;
    bool config_received=false;
    bool config_complete=false;
    bool region_map_received=false;
    meshtastic_LoRaRegionPresetMap region_map{};
    meshtastic_Config_LoRaConfig lora{};
    bool tx_enabled=true;
    uint8_t tx_power=0;
    uint32_t hops=0;
    meshtastic_Config_LoRaConfig_RegionCode region=meshtastic_Config_LoRaConfig_RegionCode_UNSET;
    meshtastic_Config_LoRaConfig_ModemPreset preset=meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
    Channel channels[kMaxChannels]{};
    Node nodes[kMaxNodes]{};
    Message messages[kMaxMessages]{};
    size_t node_count=0,message_count=0;
    char region_text[24]="UNSET";
    char preset_text[28]="LONG FAST";
    char power_text[20]="DEFAULT";
    char hops_text[20]="DEFAULT";
    char radio_text[86]="MESHTASTIC";
    char location_text[80]{};
    char telemetry_text[50]{};
};
State* state=nullptr;
bool journal_ready=false;
constexpr uint8_t kProtocolId=2;
UiListEntry empty_entry={"","","",0,UiNodeRole::Unknown};
UiMessage empty_message={"","",false,UiMessageState::Received,""};

const char* region_name(meshtastic_Config_LoRaConfig_RegionCode r){
    switch(r){
      case meshtastic_Config_LoRaConfig_RegionCode_ANZ:return "ANZ";
      case meshtastic_Config_LoRaConfig_RegionCode_NZ_865:return "NZ 865";
      case meshtastic_Config_LoRaConfig_RegionCode_US:return "US";
      case meshtastic_Config_LoRaConfig_RegionCode_EU_433:return "EU 433";
      case meshtastic_Config_LoRaConfig_RegionCode_EU_868:return "EU 868";
      case meshtastic_Config_LoRaConfig_RegionCode_CN:return "CHINA";
      case meshtastic_Config_LoRaConfig_RegionCode_JP:return "JAPAN";
      case meshtastic_Config_LoRaConfig_RegionCode_KR:return "KOREA";
      case meshtastic_Config_LoRaConfig_RegionCode_IN:return "INDIA";
      case meshtastic_Config_LoRaConfig_RegionCode_LORA_24:return "2.4 GHZ";
      case meshtastic_Config_LoRaConfig_RegionCode_UNSET:return "UNSET";
      default:return "OTHER";
    }
}
const char* preset_name(meshtastic_Config_LoRaConfig_ModemPreset p){
    switch(p){
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:return "LONG FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:return "LONG SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:return "MEDIUM FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:return "MEDIUM SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:return "SHORT FAST";
      default:return "CUSTOM PRESET";
    }
}
UiNodeRole translate_role(meshtastic_Config_DeviceConfig_Role r){
    switch(r){
      case meshtastic_Config_DeviceConfig_Role_ROUTER:
      case meshtastic_Config_DeviceConfig_Role_ROUTER_CLIENT:
      case meshtastic_Config_DeviceConfig_Role_ROUTER_LATE:
      case meshtastic_Config_DeviceConfig_Role_REPEATER:return UiNodeRole::Relay;
      case meshtastic_Config_DeviceConfig_Role_SENSOR:
      case meshtastic_Config_DeviceConfig_Role_TRACKER:return UiNodeRole::Sensor;
      default:return UiNodeRole::Client;
    }
}
const char* role_name(UiNodeRole role){
    switch(role){
      case UiNodeRole::Relay:return "ROUTER / RELAY";
      case UiNodeRole::Sensor:return "TRACKER / SENSOR";
      case UiNodeRole::Client:return "CLIENT";
      default:return "UNKNOWN";
    }
}
void format_time(char* dest,size_t cap,uint32_t seconds){
    dest[0]=0;
    if(!seconds)return;
    time_t t=(time_t)seconds;
    struct tm timeinfo{};
    if(localtime_r(&t,&timeinfo))
        snprintf(dest,cap,"%02u:%02u",(unsigned)timeinfo.tm_hour,(unsigned)timeinfo.tm_min);
}
void changed(){
    if(!state)return;
    ++state->revision;
    ui_request_data_refresh("meshtastic-native-ui");
}
void setup_channel(Channel& c,uint8_t number,const char* name,const char* subtitle){
    c.index=number;c.enabled=true;
    snprintf(c.name,sizeof(c.name),"%s",name&&*name?name:"DEFAULT");
    snprintf(c.subtitle,sizeof(c.subtitle),"%s",subtitle);
    c.row={c.name,c.subtitle,"",0,UiNodeRole::Unknown};
}
size_t channel_count(){
    if(!state)return 0;
    size_t n=0;
    for(const auto& c:state->channels)if(c.enabled)++n;
    return n;
}
Channel* channel_by_ordinal(size_t ordinal){
    if(!state)return nullptr;
    for(auto& c:state->channels)
        if(c.enabled && ordinal--==0)return &c;
    return nullptr;
}
Channel* channel_by_number(uint8_t number){
    if(!state||number>=kMaxChannels)return nullptr;
    Channel& c=state->channels[number];
    if(!c.enabled)setup_channel(c,number,number?"SECONDARY":"PRIMARY",
                               number?"SECONDARY CHANNEL":"PRIMARY CHANNEL");
    return &c;
}
Node* find_node(uint32_t number){
    if(!state||!number||number==state->me)return nullptr;
    for(size_t i=0;i<state->node_count;++i)
        if(state->nodes[i].number==number)return &state->nodes[i];
    return nullptr;
}
Node* node_for(uint32_t number){
    if(!state||!number||number==state->me)return nullptr;
    if(Node* n=find_node(number))return n;
    if(state->node_count>=kMaxNodes)return nullptr;
    Node& n=state->nodes[state->node_count++];
    n.number=number;
    snprintf(n.identity,sizeof(n.identity),"!%08lx",(unsigned long)number);
    // Distinguish an unknown name from an actual callsign. The unmodified
    // identity remains available in node details and for radio addressing.
    snprintf(n.name,sizeof(n.name),"[%s]",n.identity);
    snprintf(n.role_label,sizeof(n.role_label),"CLIENT");
    snprintf(n.subtitle,sizeof(n.subtitle),"MESHTASTIC NODE");
    n.role=UiNodeRole::Client;
    n.row={n.name,n.subtitle,n.heard_text,0,n.role};
    return &n;
}
bool has_messages_for(uint32_t node){
    if(!state)return false;
    for(size_t i=0;i<state->message_count;++i)
        if(!state->messages[i].broadcast && state->messages[i].peer==node)return true;
    return false;
}
uint16_t unread(bool channel){
    if(!state)return 0;
    uint16_t n=0;
    for(size_t i=0;i<state->message_count;++i)
        if(state->messages[i].broadcast==channel && state->messages[i].unread)++n;
    return n;
}
void update_unread(){
    if(!state)return;
    ui_status_set_unread(unread(false));
    ui_status_set_channel_unread(unread(true));
    for(auto& c:state->channels)c.row.unread=0;
    for(size_t i=0;i<state->node_count;++i)state->nodes[i].row.unread=0;
    for(size_t i=0;i<state->message_count;++i){
        const Message& m=state->messages[i];
        if(!m.unread)continue;
        if(m.broadcast){
            if(m.channel<kMaxChannels && state->channels[m.channel].row.unread<255)
                ++state->channels[m.channel].row.unread;
        }else if(Node* n=find_node(m.peer)){
            if(n->row.unread<255)++n->row.unread;
        }
    }
}
void message_key(const Message& m,uint8_t (&key)[7],size_t& bytes){
    memset(key,0,sizeof(key));
    if(m.broadcast){key[0]=m.channel;bytes=1;return;}
    bytes=4;
    for(unsigned i=0;i<4;++i)key[i]=(uint8_t)(m.peer>>(8*i));
}
void append_to_journal(Message& m,uint32_t timestamp,bool received,int8_t snr4=0){
    if(!journal_ready)return;
    uint8_t key[7]{};size_t key_size=0;
    message_key(m,key,key_size);
    m.journal_sequence=meshink_message_store().append(
        m.broadcast?MeshInkMessageKind::Channel:MeshInkMessageKind::Direct,
        key,key_size,m.text,timestamp,m.row.state,m.packet_id,
        MeshInkMessageOrigin::LocalUi,received,snr4,
        MESHINK_MESSAGE_PATH_UNKNOWN,m.unread,kProtocolId);
}
bool active_matches(const Message& m){
    if(!state)return false;
    return state->selected_is_channel?(m.broadcast&&m.channel==state->selected_channel)
                                     :(!m.broadcast&&m.peer==state->selected_node);
}
void read_active(){
    if(!state)return;
    bool updated=false;
    for(size_t i=0;i<state->message_count;++i)
        if(active_matches(state->messages[i])&&state->messages[i].unread){
            state->messages[i].unread=false;updated=true;
        }
    if(updated){
        if(journal_ready){
            Message anchor{};
            anchor.broadcast=state->selected_is_channel;
            anchor.channel=state->selected_channel;
            anchor.peer=state->selected_node;
            uint8_t key[7]{};size_t key_size=0;
            message_key(anchor,key,key_size);
            meshink_message_store().mark_read_through(
                anchor.broadcast?MeshInkMessageKind::Channel:MeshInkMessageKind::Direct,
                key,key_size,kProtocolId);
        }
        update_unread();
    }
}
void set_active_channel(uint8_t number){
    if(!state)return;
    state->selected_is_channel=true;state->selected_channel=number;
    channel_by_number(number);
    read_active();changed();
}
void set_active_node(uint32_t number){
    if(!state||!node_for(number))return;
    state->selected_is_channel=false;state->selected_node=number;
    read_active();changed();
}
Message* append_message(){
    if(!state)return nullptr;
    if(state->message_count==kMaxMessages){
        // Bounded in-memory view cache. A durable journal can back the same
        // UiDataProvider without protocol-specific UI changes.
        for(size_t i=1;i<kMaxMessages;++i)state->messages[i-1]=state->messages[i];
        --state->message_count;
    }
    Message& m=state->messages[state->message_count++];
    m=Message{};
    return &m;
}
bool duplicate(uint32_t id,uint32_t peer,bool broadcast){
    if(!state||!id)return false;
    for(size_t i=0;i<state->message_count;++i)
        if(state->messages[i].packet_id==id &&
           state->messages[i].peer==peer&&state->messages[i].broadcast==broadcast)
            return true;
    return false;
}
void update_node_user(Node& n,const meshtastic_User& user){
    // Name announcements and PhoneAPI NodeInfo snapshots update the same
    // contact. Never replace a previously known name with a blank one.
    const char* name=user.long_name[0]?user.long_name:
                     (user.short_name[0]?user.short_name:nullptr);
    if(name){
        snprintf(n.name,sizeof(n.name),"%s",name);
        n.name_known=true;
        n.name_request_pending=false;
    }
    n.role=translate_role(user.role);
    snprintf(n.role_label,sizeof(n.role_label),"%s",role_name(n.role));
    n.row.role=n.role;
}
void note_node(const meshtastic_NodeInfo& info){
    // Save the local User protobuf from the trusted PhoneAPI handshake for
    // standard NODEINFO_APP requests, as the Android app does. Never pull
    // mutable NodeDB globals across the worker/UI core boundary.
    if(state&&info.num==state->me){
        if(info.has_user){
            state->local_user=info.user;
            state->local_user_ready=true;
        }
        return;
    }
    Node* n=node_for(info.num);
    if(!n)return;
    if(info.has_user)update_node_user(*n,info.user);
    n->heard=info.last_heard;
    format_time(n->heard_text,sizeof(n->heard_text),n->heard);
    if(info.has_position&&info.position.has_latitude_i&&info.position.has_longitude_i){
        n->position_valid=true;
        n->latitude=info.position.latitude_i;n->longitude=info.position.longitude_i;
    }
    if(info.has_device_metrics&&info.device_metrics.has_battery_level){
        n->has_battery=true;n->battery=info.device_metrics.battery_level;
    }
    changed();
}
void note_channel(const meshtastic_Channel& input){
    if(!state||input.index<0||input.index>=static_cast<int>(kMaxChannels))return;
    Channel& ch=state->channels[input.index];
    if(input.role==meshtastic_Channel_Role_DISABLED){ch.enabled=false;changed();return;}
    setup_channel(ch,(uint8_t)input.index,
                  input.has_settings?input.settings.name:"",
                  input.role==meshtastic_Channel_Role_PRIMARY?"PRIMARY CHANNEL":"SECONDARY CHANNEL");
    changed();
}
void note_config(const meshtastic_Config& config){
    if(!state||config.which_payload_variant!=meshtastic_Config_lora_tag)return;
    const auto& l=config.payload_variant.lora;
    state->config_received=true;
    state->lora=l;
    state->region=l.region;state->preset=l.modem_preset;
    state->tx_enabled=l.tx_enabled;state->tx_power=(uint8_t)std::max(0,(int)l.tx_power);
    state->hops=l.hop_limit;
    snprintf(state->region_text,sizeof(state->region_text),"%s",region_name(l.region));
    snprintf(state->preset_text,sizeof(state->preset_text),"%s",l.use_preset?preset_name(l.modem_preset):"CUSTOM");
    snprintf(state->power_text,sizeof(state->power_text),"%s",l.tx_power>0?"MANUAL":"DEFAULT");
    if(l.tx_power>0)snprintf(state->power_text,sizeof(state->power_text),"%u DBM",(unsigned)l.tx_power);
    snprintf(state->hops_text,sizeof(state->hops_text),"%lu",(unsigned long)l.hop_limit);
    snprintf(state->radio_text,sizeof(state->radio_text),"%s / %s / TX %s",
             state->region_text,state->preset_text,l.tx_enabled?"ON":"OFF");
    changed();
}
// A PhoneAPI acceptance only says the packet entered the official service.
// The subsequent official queue-status result can still reject it (e.g. region
// unset or airtime exhausted); surface that instead of showing a false send.
void note_queue_status(const meshtastic_QueueStatus& queue){
    if(!state||!queue.mesh_packet_id||queue.res==0)return;
    for(size_t i=0;i<state->message_count;++i){
        Message& m=state->messages[i];
        if(m.packet_id!=queue.mesh_packet_id||!m.row.outgoing)continue;
        if(m.row.state==UiMessageState::Sending||m.row.state==UiMessageState::Sent){
            m.row.state=UiMessageState::Failed;
            if(journal_ready&&m.journal_sequence)
                meshink_message_store().update_state(m.journal_sequence,m.row.state);
            changed();
        }
    }
}
void note_routing(const meshtastic_MeshPacket& packet){
    const uint32_t ref=packet.decoded.request_id;
    if(!state||!ref)return;
    meshtastic_Routing response=meshtastic_Routing_init_zero;
    pb_istream_t pb=pb_istream_from_buffer(packet.decoded.payload.bytes,packet.decoded.payload.size);
    if(!pb_decode(&pb,meshtastic_Routing_fields,&response))return;
    const bool bad=response.which_variant==meshtastic_Routing_error_reason_tag &&
                   response.error_reason!=meshtastic_Routing_Error_NONE;
    for(size_t i=0;i<state->message_count;++i){
        Message& m=state->messages[i];
        if(m.packet_id==ref&&!m.broadcast&&!m.unread){
            if(m.row.state==UiMessageState::Sending||m.row.state==UiMessageState::Sent){
                m.row.state=bad?UiMessageState::Failed:UiMessageState::Delivered;
                if(journal_ready&&m.journal_sequence)
                    meshink_message_store().update_state(m.journal_sequence,m.row.state);
                changed();
            }
        }
    }
}

void note_position(const meshtastic_MeshPacket& packet){
    // The official PositionModule decodes/routes the radio packet. This is
    // only MeshInk's read-only map projection of the official FromRadio copy.
    if(!state||!packet.from||packet.from==state->me)return;
    meshtastic_Position position=meshtastic_Position_init_zero;
    pb_istream_t input=pb_istream_from_buffer(
        packet.decoded.payload.bytes,packet.decoded.payload.size);
    if(!pb_decode(&input,meshtastic_Position_fields,&position))return;
    if(!position.has_latitude_i||!position.has_longitude_i||
       position.latitude_i<-900000000||position.latitude_i>900000000||
       position.longitude_i<-1800000000||position.longitude_i>1800000000)
        return;
    Node* n=node_for(packet.from);
    if(!n)return;
    const bool changed_coords=!n->position_valid||
        n->latitude!=position.latitude_i||n->longitude!=position.longitude_i;
    n->position_valid=true;
    n->latitude=position.latitude_i;n->longitude=position.longitude_i;
    if(packet.has_rx_time){
        n->heard=packet.rx_time;
        format_time(n->heard_text,sizeof(n->heard_text),n->heard);
    }
#if MESHINK_MESHTASTIC_HW_TEST_LOG
    if(changed_coords)Serial.printf("[MT-TEST] map: position from=!%08lx precision=as-received\n",
                                    (unsigned long)packet.from);
#endif
    if(changed_coords)changed();
}

void note_packet(const meshtastic_MeshPacket& p){
    if(!state||p.which_payload_variant!=meshtastic_MeshPacket_decoded_tag)return;
    if(p.decoded.portnum==meshtastic_PortNum_NODEINFO_APP){
        // Official Meshtastic NODEINFO_APP (port 4) carries a User protobuf.
        // PhoneAPI may surface this received packet separately from the
        // NodeInfo database snapshot. Both update the same contact view.
        if(!p.from||p.from==state->me)return;
        meshtastic_User user=meshtastic_User_init_zero;
        pb_istream_t input=pb_istream_from_buffer(
            p.decoded.payload.bytes,p.decoded.payload.size);
        if(!pb_decode(&input,meshtastic_User_fields,&user))return;
        Node* n=node_for(p.from);
        if(!n)return;
        update_node_user(*n,user);
        if(p.has_rx_time){
            n->heard=p.rx_time;
            format_time(n->heard_text,sizeof(n->heard_text),n->heard);
        }
#if MESHINK_MESHTASTIC_HW_TEST_LOG
        Serial.printf("[MT-TEST] node identity from=!%08lx name=%s\n",
                      (unsigned long)p.from,n->name);
#endif
        changed();
        return;
    }
    if(p.decoded.portnum==meshtastic_PortNum_ROUTING_APP){note_routing(p);return;}
    if(p.decoded.portnum==meshtastic_PortNum_POSITION_APP){note_position(p);return;}
    if(p.decoded.portnum!=meshtastic_PortNum_TEXT_MESSAGE_APP)return;
    const bool broadcast=p.to==kBroadcast;
    const bool outgoing=p.from==state->me && state->me!=0;
    const uint32_t peer=broadcast?0:(outgoing?p.to:p.from);
    if(!broadcast)node_for(peer);
    const uint8_t chan=std::min((unsigned)p.channel,(unsigned)(kMaxChannels-1));
    if(broadcast)channel_by_number(chan);
    if(duplicate(p.id,peer,broadcast))return;
    Message* msg=append_message();
    if(!msg)return;
    msg->packet_id=p.id;msg->peer=peer;msg->channel=chan;msg->broadcast=broadcast;
    const size_t copied=std::min((size_t)p.decoded.payload.size,sizeof(msg->text)-1);
    memcpy(msg->text,p.decoded.payload.bytes,copied);msg->text[copied]=0;
    format_time(msg->time,sizeof(msg->time),p.has_rx_time?p.rx_time:(uint32_t)time(nullptr));
    msg->unread=!outgoing&&!active_matches(*msg);
    msg->row={msg->text,msg->time,outgoing,outgoing?UiMessageState::Sent:UiMessageState::Received,""};
    const uint32_t timestamp=p.has_rx_time?p.rx_time:(uint32_t)time(nullptr);
    const float scaled=p.rx_snr*4.0f;
    const int snr4=std::max(-128,std::min(127,(int)scaled));
    append_to_journal(*msg,timestamp,!outgoing,(int8_t)snr4);
    update_unread();
    changed();
    if(!outgoing)ui_notify_message_received(broadcast);
}

bool valid_channel_name(const char* text){
    if(!text||!*text)return false;
    const size_t len=strlen(text);
    if(!len||len>11)return false;
    for(size_t i=0;i<len;++i)
        if((unsigned char)text[i]<32||(unsigned char)text[i]>126)return false;
    return true;
}
int hex_value(char c){
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
bool assign_psk(meshtastic_Channel& channel,const char* key_hex){
    if(!key_hex||!*key_hex){
        channel.settings.psk.size=32;
        esp_fill_random(channel.settings.psk.bytes,32);
        return true;
    }
    const size_t len=strlen(key_hex);
    if(len!=32&&len!=64)return false;
    channel.settings.psk.size=len/2;
    for(size_t i=0;i<len;i+=2){
        const int a=hex_value(key_hex[i]),b=hex_value(key_hex[i+1]);
        if(a<0||b<0)return false;
        channel.settings.psk.bytes[i/2]=(uint8_t)((a<<4)|b);
    }
    return true;
}
class NativeProvider final : public UiDataProvider {
    mutable UiNodeDetails details_{};
public:
    size_t conversation_count()const override{
        if(!state)return 0;
        size_t count=channel_count();
        for(size_t i=0;i<state->node_count;++i)
            if(has_messages_for(state->nodes[i].number))++count;
        return count;
    }
    const UiListEntry& conversation(size_t index)const override{
        if(!state)return empty_entry;
        const size_t channels=channel_count();
        if(index<channels){Channel* c=channel_by_ordinal(index);return c?c->row:empty_entry;}
        index-=channels;
        for(size_t i=0;i<state->node_count;++i)
            if(has_messages_for(state->nodes[i].number)&&index--==0)return state->nodes[i].row;
        return empty_entry;
    }
    bool open_conversation(size_t index)override{
        if(!state)return false;
        const size_t channels=channel_count();
        if(index<channels){Channel* c=channel_by_ordinal(index);if(!c)return false;
            set_active_channel(c->index);return true;}
        index-=channels;
        for(size_t i=0;i<state->node_count;++i)
            if(has_messages_for(state->nodes[i].number)&&index--==0){
                set_active_node(state->nodes[i].number);return true;
            }
        return false;
    }
    size_t map_node_count()const override{
        if(!state)return 0;
        size_t count=0;
        for(size_t i=0;i<state->node_count;++i)if(state->nodes[i].position_valid)++count;
        return count;
    }
    bool map_node(size_t index,UiMapNode& out)const override{
        if(!state)return false;
        for(size_t i=0;i<state->node_count;++i){
            const Node& n=state->nodes[i];if(!n.position_valid)continue;
            if(index--!=0)continue;
            out={};snprintf(out.name,sizeof(out.name),"%s",n.name);
            for(unsigned b=0;b<4;++b)out.key[b]=(uint8_t)(n.number>>(8*b));
            // Official Meshtastic stores degrees * 1e7; shared MeshInk maps
            // consume degrees * 1e6, just like the tested MeshCore view.
            out.latitude=n.latitude/10;out.longitude=n.longitude/10;
            out.role=n.role;out.advertised_at=n.heard;return true;
        }
        return false;
    }
    bool open_map_node(size_t index)override{
        UiMapNode node{};
        if(!map_node(index,node))return false;
        uint32_t n=0;for(unsigned i=0;i<4;++i)n|=(uint32_t)node.key[i]<<(8*i);
        set_active_node(n);return true;
    }
    size_t contact_count()const override{return state?state->node_count:0;}
    const UiListEntry& contact(size_t i)const override{
        return state&&i<state->node_count?state->nodes[i].row:empty_entry;
    }
    bool open_contact(size_t i)override{
        if(!state||i>=state->node_count)return false;
        set_active_node(state->nodes[i].number);return true;
    }
    size_t channel_count()const override{return ::channel_count();}
    const UiListEntry& channel(size_t i)const override{
        Channel* c=channel_by_ordinal(i);return c?c->row:empty_entry;
    }
    bool open_channel(size_t i)override{
        Channel* c=channel_by_ordinal(i);if(!c)return false;
        set_active_channel(c->index);return true;
    }
    size_t channel_name_limit()const override{return 11;}
    size_t channel_capacity()const override{return kMaxChannels;}
    bool channel_management_available()const override{
        return state&&state->config_complete&&state->me!=0;
    }
    bool channel_removable(size_t index)const override{
        Channel* c=channel_by_ordinal(index);
        return channel_management_available()&&c&&c->index!=0;
    }
    bool create_channel(const char* name,const char* key_hex)override{
        if(!channel_management_available()||!valid_channel_name(name))return false;
        int vacant=-1;
        for(unsigned i=1;i<kMaxChannels;++i)
            if(!state->channels[i].enabled){vacant=(int)i;break;}
        if(vacant<0)return false;
        meshtastic_Channel c=meshtastic_Channel_init_zero;
        c.index=(int8_t)vacant;
        c.role=meshtastic_Channel_Role_SECONDARY;
        c.has_settings=true;
        snprintf(c.settings.name,sizeof(c.settings.name),"%s",name);
        if(!assign_psk(c,key_hex))return false;
        // The local official AdminModule saves the setting. Do not implement
        // channel cryptography or persistence in MeshInk's UI helper.
        return meshink_meshtastic_submit_channel(c);
    }
    bool delete_channel(size_t index)override{
        if(!channel_removable(index))return false;
        Channel* ch=channel_by_ordinal(index);if(!ch)return false;
        meshtastic_Channel c=meshtastic_Channel_init_zero;
        c.index=(int8_t)ch->index;
        c.role=meshtastic_Channel_Role_DISABLED;
        return meshink_meshtastic_submit_channel(c);
    }
    size_t advert_count()const override{return 0;}
    const UiListEntry& advert(size_t)const override{return empty_entry;}
    bool open_advert(size_t)override{return false;}
    bool active_node_details(UiNodeDetails& out)const override{
        if(!state||state->selected_is_channel)return false;
        Node* n=find_node(state->selected_node);if(!n)return false;
        details_={};
        details_.name=n->name;details_.identity=n->identity;
        details_.last_seen=n->heard_text;details_.role=n->role;
        details_.role_label=n->role_label;details_.route=n->subtitle;
        details_.capabilities=0;
        details_.saved_contact=true;
        if(n->position_valid){
            snprintf(state->location_text,sizeof(state->location_text),
                     "%.5f, %.5f",n->latitude/1e7,n->longitude/1e7);
            details_.position=state->location_text;
            // Node-details map actions use the same MeshInk microdegree contract.
            details_.latitude=n->latitude/10;details_.longitude=n->longitude/10;
        }
        if(n->has_battery){
            snprintf(state->telemetry_text,sizeof(state->telemetry_text),
                     "BATTERY %lu%%",(unsigned long)n->battery);
            details_.telemetry=state->telemetry_text;
        }
        out=details_;return true;
    }
    bool add_active_node()override{return false;}
    bool remove_active_contact()override{return false;}
    bool request_active_node_info(UiNodeInfoRequest)override{return false;}
    bool login_active_node(const char*,bool)override{return false;}
    bool active_node_saved_password(char*,size_t)const override{return false;}
    const char* active_title()const override{
        if(!state)return "MESHTASTIC";
        if(state->selected_is_channel){
            Channel* c=channel_by_number(state->selected_channel);
            return c?c->name:"PRIMARY";
        }
        Node* n=find_node(state->selected_node);
        return n?n->name:"MESHTASTIC";
    }
    bool active_is_channel()const override{return !state||state->selected_is_channel;}
    size_t active_message_count()const override{
        if(!state)return 0;
        size_t count=0;for(size_t i=0;i<state->message_count;++i)
            if(active_matches(state->messages[i]))++count;
        return count;
    }
    const UiMessage& active_message(size_t i)const override{
        if(!state)return empty_message;
        for(size_t j=0;j<state->message_count;++j){
            Message& m=state->messages[j];
            if(!active_matches(m))continue;
            if(i--!=0)continue;
            m.row.text=m.text;m.row.time=m.time;
            return m.row;
        }
        return empty_message;
    }
    uint16_t direct_unread_total()const override{return unread(false);}
    uint16_t channel_unread_total()const override{return unread(true);}
    uint32_t active_message_revision()const override{return state?state->revision:0;}
};
NativeProvider provider;
} // namespace

void meshink_meshtastic_ui_begin(){
    // Keep the large view cache off the ESP32 loop-task stack and allocate it
    // only for a Meshtastic boot, never for MeshCore.
    delete state;
    state=new(std::nothrow) State();
    journal_ready=false;
    if(!state)return;
    setup_channel(state->channels[0],0,"PRIMARY","PRIMARY CHANNEL");
    journal_ready=meshink_message_store().begin();
    if(journal_ready){
        MeshInkMessageStore& store=meshink_message_store();
        for(size_t i=0;i<store.count();++i){
            MeshInkStoredMessage record{};
            if(!store.read(i,record)||meshink_message_protocol(record)!=kProtocolId)continue;
            const bool broadcast=record.kind==(uint8_t)MeshInkMessageKind::Channel;
            uint32_t peer=0;
            if(!broadcast)for(unsigned b=0;b<4;++b)peer|=(uint32_t)record.key[b]<<(8*b);
            if(!broadcast&&!peer)continue;
            Message* msg=append_message();
            if(!msg)break;
            msg->broadcast=broadcast;msg->peer=peer;msg->channel=broadcast?record.key[0]:0;
            msg->packet_id=record.ack;msg->journal_sequence=record.sequence;
            msg->unread=(record.flags&MESHINK_MESSAGE_UNREAD)!=0;
            snprintf(msg->text,sizeof(msg->text),"%s",record.text);
            format_time(msg->time,sizeof(msg->time),record.timestamp);
            msg->row={msg->text,msg->time,
                record.state!=(uint8_t)UiMessageState::Received,
                (UiMessageState)record.state,""};
            if(broadcast)channel_by_number(msg->channel);
            else node_for(peer);
        }
    }
    update_unread();
}
UiDataProvider* meshink_meshtastic_ui_provider(){return state?&provider:nullptr;}
bool meshink_meshtastic_ui_destination(uint32_t& node,uint8_t& channel){
    if(!state)return false;
    node=state->selected_is_channel?kBroadcast:state->selected_node;
    channel=state->selected_channel;
    return node!=0;
}
void meshink_meshtastic_ui_sent(uint32_t id,const char* text,bool success){
    if(!state||!success||!text)return;
    Message* m=append_message();if(!m)return;
    m->packet_id=id;
    m->broadcast=state->selected_is_channel;
    m->channel=state->selected_channel;
    m->peer=m->broadcast?0:state->selected_node;
    snprintf(m->text,sizeof(m->text),"%s",text);
    format_time(m->time,sizeof(m->time),(uint32_t)time(nullptr));
    m->row={m->text,m->time,true,m->broadcast?UiMessageState::Sent:UiMessageState::Sending,""};
    append_to_journal(*m,(uint32_t)time(nullptr),false);
    changed();
}
void meshink_meshtastic_ui_receive(const meshtastic_FromRadio& response){
    if(!state)return;
    switch(response.which_payload_variant){
      case meshtastic_FromRadio_my_info_tag:
        state->me=response.my_info.my_node_num;changed();break;
      case meshtastic_FromRadio_node_info_tag:
        note_node(response.node_info);break;
      case meshtastic_FromRadio_channel_tag:
        note_channel(response.channel);break;
      case meshtastic_FromRadio_config_tag:
        note_config(response.config);break;
      case meshtastic_FromRadio_packet_tag: {
        const auto& p=response.packet;
        // The mesh packet itself is the discovery event. NodeInfo snapshots
        // and restored old messages do not trigger any over-the-air request.
        const bool incoming=p.from&&p.from!=state->me;
        const bool existed=incoming&&find_node(p.from);
        note_packet(p);
        if(incoming) {
            Node* n=node_for(p.from);
            if(n){
                if(p.has_rx_time){
                    n->heard=p.rx_time;
                    format_time(n->heard_text,sizeof(n->heard_text),n->heard);
                }
                if(!existed)changed();
                if(!n->name_known&&!n->name_request_pending&&
                   (!n->name_request_sent||
                    (uint32_t)(millis()-n->last_name_request_ms)>=kNodeInfoRetryMs)){
                    // PhoneAPI uses channel slots 0..7; encrypted/raw RF
                    // headers may still carry a channel hash instead.
                    n->heard_channel=p.channel<kMaxChannels?(uint8_t)p.channel:0;
                    n->name_request_pending=true;
                }
            }
        }
        break;
      }
      case meshtastic_FromRadio_queueStatus_tag:
        note_queue_status(response.queueStatus);break;
      case meshtastic_FromRadio_region_presets_tag:
        state->region_map=response.region_presets;
        state->region_map_received=true;
        changed();break;
      case meshtastic_FromRadio_config_complete_id_tag:
        state->config_complete=true;
        changed();break;
      default:break;
    }
}
bool meshink_meshtastic_ui_next_name_request(uint32_t& node,uint8_t& channel,
                                             meshtastic_User& local_user){
    if(!state||!state->config_complete||!state->me||!state->local_user_ready)return false;
    const uint32_t now=millis();
    if(state->name_tx_seen&&
       (uint32_t)(now-state->last_name_tx_ms)<kNodeInfoGlobalSpacingMs)return false;
    if(state->name_attempt_seen&&
       (uint32_t)(now-state->last_name_attempt_ms)<kNodeInfoFailedQueueRetryMs)return false;
    for(size_t i=0;i<state->node_count;++i){
        const Node& n=state->nodes[i];
        if(!n.name_request_pending||n.name_known)continue;
        node=n.number;
        channel=n.heard_channel;
        local_user=state->local_user;
        return true;
    }
    return false;
}
void meshink_meshtastic_ui_name_request_result(uint32_t number,bool accepted){
    if(!state)return;
    const uint32_t now=millis();
    state->name_attempt_seen=true;
    state->last_name_attempt_ms=now;
    if(!accepted)return;
    if(Node* n=find_node(number)){
        n->name_request_pending=false;
        n->name_request_sent=true;
        n->last_name_request_ms=now;
    }
    state->name_tx_seen=true;
    state->last_name_tx_ms=now;
}
const char* meshink_meshtastic_ui_radio_summary(){
    return state?state->radio_text:"MESHTASTIC";
}
size_t meshink_meshtastic_ui_settings_count(){return 4;}
bool meshink_meshtastic_ui_settings_item(size_t i,MeshInkProtocolSettingItem& item){
    if(!state)return false;
    switch(i){
      case 0:item={1,"LORA REGION",state->region_text,false};return true;
      case 1:item={2,"MODEM PRESET",state->preset_text,false};return true;
      case 2:item={3,"HOP LIMIT",state->hops_text,false};return true;
      case 3:item={4,"TX POWER",state->power_text,false};return true;
      default:return false;
    }
}

uint32_t meshink_meshtastic_ui_own_node(){return state?state->me:0;}

namespace {
struct NativeRegion {
    meshtastic_Config_LoRaConfig_RegionCode code;
    const char* label;
};
// These are official upstream region enum values. Never add the SX1280-only
// 2.4 GHz region to an SX1262 build. Licensing, permitted RF parameters and
// actual preset constraints remain the responsibility of upstream Meshtastic.
const NativeRegion regions[]={
    {meshtastic_Config_LoRaConfig_RegionCode_ANZ,"ANZ"},
    {meshtastic_Config_LoRaConfig_RegionCode_NZ_865,"NZ 865"},
    {meshtastic_Config_LoRaConfig_RegionCode_US,"US"},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_433,"EU 433"},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_868,"EU 868"},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_866,"EU 866"},
    {meshtastic_Config_LoRaConfig_RegionCode_EU_N_868,"EU NARROW"},
    {meshtastic_Config_LoRaConfig_RegionCode_CN,"CHINA"},
    {meshtastic_Config_LoRaConfig_RegionCode_JP,"JAPAN"},
    {meshtastic_Config_LoRaConfig_RegionCode_KR,"KOREA"},
    {meshtastic_Config_LoRaConfig_RegionCode_IN,"INDIA"},
    {meshtastic_Config_LoRaConfig_RegionCode_TW,"TAIWAN"},
    {meshtastic_Config_LoRaConfig_RegionCode_TH,"THAILAND"},
    {meshtastic_Config_LoRaConfig_RegionCode_MY_919,"MALAYSIA 919"},
    {meshtastic_Config_LoRaConfig_RegionCode_SG_923,"SINGAPORE 923"},
    {meshtastic_Config_LoRaConfig_RegionCode_BR_902,"BRAZIL 902"}
};
const meshtastic_LoRaPresetGroup* permitted_group(size_t region){
    if(!state||!state->region_map_received||
       region>=sizeof(regions)/sizeof(regions[0]))return nullptr;
    const auto& map=state->region_map;
    for(size_t i=0;i<map.region_groups_count;++i){
        const auto& mapping=map.region_groups[i];
        if(mapping.region==regions[region].code&&mapping.group_index<map.groups_count)
            return &map.groups[mapping.group_index];
    }
    return nullptr;
}
meshtastic_Config_LoRaConfig_ModemPreset selected_preset(size_t region,size_t index){
    if(const auto* group=permitted_group(region)){
        if(index<group->presets_count)return group->presets[index];
        return group->default_preset;
    }
    return meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST;
}
const char* preset_label(meshtastic_Config_LoRaConfig_ModemPreset p){
    switch(p){
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_FAST:return "LONG FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_SLOW:return "LONG SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_VERY_LONG_SLOW:return "VERY LONG SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_SLOW:return "MEDIUM SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_FAST:return "MEDIUM FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_SLOW:return "SHORT SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_FAST:return "SHORT FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_MODERATE:return "LONG MODERATE";
      case meshtastic_Config_LoRaConfig_ModemPreset_SHORT_TURBO:return "SHORT TURBO";
      case meshtastic_Config_LoRaConfig_ModemPreset_LONG_TURBO:return "LONG TURBO";
      case meshtastic_Config_LoRaConfig_ModemPreset_LITE_FAST:return "LITE FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_LITE_SLOW:return "LITE SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_NARROW_FAST:return "NARROW FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_NARROW_SLOW:return "NARROW SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_TINY_FAST:return "TINY FAST";
      case meshtastic_Config_LoRaConfig_ModemPreset_TINY_SLOW:return "TINY SLOW";
      case meshtastic_Config_LoRaConfig_ModemPreset_MEDIUM_TURBO:return "MEDIUM TURBO";
      default:return "UNKNOWN";
    }
}
}
size_t meshink_meshtastic_ui_region_count(){
    return sizeof(regions)/sizeof(regions[0]);
}
const char* meshink_meshtastic_ui_region_name(size_t i){
    return i<meshink_meshtastic_ui_region_count()?regions[i].label:"";
}
size_t meshink_meshtastic_ui_preset_count(size_t region){
    if(region>=meshink_meshtastic_ui_region_count())return 0;
    if(const auto* group=permitted_group(region))return group->presets_count;
    // A stale/missing upstream legality map cannot authorize extra choices.
    return 1;
}
const char* meshink_meshtastic_ui_preset_name(size_t r,size_t p){
    if(r>=meshink_meshtastic_ui_region_count()||
       p>=meshink_meshtastic_ui_preset_count(r))return "";
    return preset_label(selected_preset(r,p));
}
int meshink_meshtastic_ui_preset_index(size_t r,size_t p){
    return r<meshink_meshtastic_ui_region_count()&&
           p<meshink_meshtastic_ui_preset_count(r)?(int)p:-1;
}
bool meshink_meshtastic_ui_commit_radio(size_t r,size_t p,uint8_t hops){
    if(!state||!state->config_complete||!state->config_received||
       r>=meshink_meshtastic_ui_region_count()||hops<1||hops>7)return false;
    const auto* allowed=permitted_group(r);
    // Only commit combinations confirmed by the pinned official engine.
    if(!allowed||p>=allowed->presets_count)return false;
    meshtastic_Config_LoRaConfig next=state->lora;
    next.region=regions[r].code;
    next.use_preset=true;
    next.modem_preset=allowed->presets[p];
    next.hop_limit=hops;
    next.tx_enabled=true;
    return meshink_meshtastic_submit_lora_config(next);
}
