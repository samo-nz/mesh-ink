#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <time.h>
#include "local_mesh_runtime.h"
#include "companion_runtime.h"
#include "ui_onboarding.h"
#include "board/target.h"
#include "t5_logging.h"
#include <helpers/sensors/LPPDataHelpers.h>
#include "../lib/MeshCore/examples/companion_radio/MyMesh.h"

extern MyMesh& t5_mesh();

namespace {
constexpr size_t MAX_UI_CONTACTS=16;
constexpr size_t MAX_MAP_NODES=50;
constexpr size_t MAX_UI_CHANNELS=8;
constexpr size_t MAX_UI_ADVERTS=16;
constexpr size_t MAX_STORED_MESSAGES=96;
constexpr uint32_t STORE_MAGIC=0x354D3554; // T5M5
constexpr uint16_t STORE_VERSION=1;
constexpr char STORE_PATH[]="/ui_messages.bin";

// gps_interval in upstream MeshCore controls how often coordinates are copied,
// not receiver power. Timed modes here pause the SOFTWARE GPS provider after
// a fix; the L76K stays powered because LoRa shares its supply. No physical
// GNSS standby is implemented or claimed. Opt-in PCAS03/04 receiver tuning
// is independent of this historical software-only duty cycle.
static bool gps_duty_sleeping=false;
static uint32_t gps_duty_next_wake=0;
static uint32_t gps_duty_awake_since=0;
static uint32_t gps_duty_wake_stamp=0;
static bool gps_duty_reset=true;

static void reset_gps_duty_cycle(){
    gps_duty_reset=true;
    gps_duty_next_wake=0;
    gps_duty_awake_since=0;
    gps_duty_wake_stamp=0;
}

enum class MessageKind:uint8_t{Direct=0,Channel=1};
struct StoreHeader{uint32_t magic;uint16_t version;uint16_t capacity;uint16_t head;uint16_t count;uint32_t sequence;};
struct StoredMessage{uint32_t sequence;uint32_t timestamp;uint32_t ack;uint8_t kind;uint8_t state;uint8_t key[7];char text[145];};
struct ListStorage{UiListEntry entry{};char title[34]{};char subtitle[72]{};char time[10]{};uint8_t key[7]{};uint8_t channel_index=0;};
struct MessageView{UiMessage entry{};char text[145]{};char time[10]{};};
struct UnreadPeer{uint8_t key[6]{};uint8_t count=0;bool used=false;};
struct DiscoveredContact{uint8_t prefix[7]{};uint8_t frame[192]{};uint8_t len=0;};

static void format_time(uint32_t timestamp,char out[10]){
    time_t raw=timestamp?(time_t)timestamp:time(nullptr);struct tm value{};localtime_r(&raw,&value);
    snprintf(out,10,"%02d:%02d",value.tm_hour,value.tm_min);
}
static const char* state_text(UiMessageState state){
    switch(state){case UiMessageState::Sending:return "SENDING";case UiMessageState::Sent:return "SENT";
        case UiMessageState::Delivered:return "DELIVERED";case UiMessageState::Failed:return "FAILED";
        case UiMessageState::Retrying1:return "RETRYING 1/5";case UiMessageState::Retrying2:return "RETRYING 2/5";
        case UiMessageState::Retrying3:return "RETRYING 3/5";case UiMessageState::Retrying4:return "RETRYING 4/5";
        case UiMessageState::Retrying5:return "RETRYING 5/5";default:return "";}
}
static void format_last_seen(uint32_t timestamp,char out[72]){
    if(!timestamp){strcpy(out,"LAST SEEN UNKNOWN");return;}
    const uint32_t now=(uint32_t)time(nullptr),age=now>timestamp?now-timestamp:0;
    if(age<60)strcpy(out,"LAST SEEN JUST NOW");
    else if(age<3600)snprintf(out,72,"LAST SEEN %lu MIN AGO",(unsigned long)(age/60));
    else if(age<86400)snprintf(out,72,"LAST SEEN %lu HOUR%s AGO",(unsigned long)(age/3600),age/3600==1?"":"S");
    else snprintf(out,72,"LAST SEEN %lu DAY%s AGO",(unsigned long)(age/86400),age/86400==1?"":"S");
}

class MessageStore{
    StoreHeader header_{STORE_MAGIC,STORE_VERSION,MAX_STORED_MESSAGES,0,0,0};
    StoredMessage records_[MAX_STORED_MESSAGES]{};
    void write_header(){File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;f.seek(0);f.write((uint8_t*)&header_,sizeof(header_));f.close();}
    void write_record(uint16_t physical){File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;f.seek(sizeof(StoreHeader)+physical*sizeof(StoredMessage));f.write((uint8_t*)&records_[physical],sizeof(StoredMessage));f.close();}
    void create(){
        header_={STORE_MAGIC,STORE_VERSION,MAX_STORED_MESSAGES,0,0,0};memset(records_,0,sizeof(records_));
        File f=SPIFFS.open(STORE_PATH,"w");if(!f){Serial.println("[T5-STORE] unable to create message store");return;}
        f.write((uint8_t*)&header_,sizeof(header_));f.write((uint8_t*)records_,sizeof(records_));f.close();
        Serial.printf("[T5-STORE] created fixed store: %u messages, %u bytes\n",(unsigned)MAX_STORED_MESSAGES,(unsigned)(sizeof(header_)+sizeof(records_)));
    }
public:
    void begin(){
        File f=SPIFFS.open(STORE_PATH,"r");
        if(!f||f.size()!=(int)(sizeof(header_)+sizeof(records_))){if(f)f.close();create();return;}
        f.read((uint8_t*)&header_,sizeof(header_));f.read((uint8_t*)records_,sizeof(records_));f.close();
        if(header_.magic!=STORE_MAGIC||header_.version!=STORE_VERSION||header_.capacity!=MAX_STORED_MESSAGES||header_.head>=MAX_STORED_MESSAGES||header_.count>MAX_STORED_MESSAGES){create();return;}
        T5_DEBUGF(T5_LOG_MESH,"[T5-STORE] loaded %u/%u messages; oldest records evicted at capacity\n",header_.count,header_.capacity);
    }
    size_t count()const{return header_.count;}
    const StoredMessage& at(size_t logical)const{return records_[(header_.head+logical)%MAX_STORED_MESSAGES];}
    StoredMessage* append(MessageKind kind,const uint8_t* key,size_t key_len,const char* text,uint32_t timestamp,UiMessageState state,uint32_t ack=0){
        uint16_t physical;
        if(header_.count<MAX_STORED_MESSAGES){physical=(header_.head+header_.count)%MAX_STORED_MESSAGES;header_.count++;}
        else{physical=header_.head;header_.head=(header_.head+1)%MAX_STORED_MESSAGES;T5_DEBUGLN(T5_LOG_MESH,"[T5-STORE] capacity reached; evicting oldest message");}
        StoredMessage& item=records_[physical];memset(&item,0,sizeof(item));item.sequence=++header_.sequence;item.timestamp=timestamp;
        item.ack=ack;item.kind=(uint8_t)kind;item.state=(uint8_t)state;memcpy(item.key,key,min(key_len,sizeof(item.key)));strncpy(item.text,text,sizeof(item.text)-1);
        write_record(physical);write_header();return &item;
    }
    void update_state(uint32_t sequence,UiMessageState state){
        for(size_t i=0;i<header_.count;++i){uint16_t p=(header_.head+i)%MAX_STORED_MESSAGES;if(records_[p].sequence==sequence){records_[p].state=(uint8_t)state;write_record(p);return;}}
    }
};

class MeshCoreUiProvider final:public UiDataProvider{
    ListStorage contacts_[MAX_UI_CONTACTS]{},channels_[MAX_UI_CHANNELS]{},conversations_[MAX_UI_CONTACTS+MAX_UI_CHANNELS]{},adverts_[MAX_UI_ADVERTS]{};
    MessageView active_messages_[MAX_STORED_MESSAGES]{};
    UiMapNode map_nodes_[MAX_MAP_NODES]{};
    size_t map_node_count_=0;
    size_t contact_count_=0,channel_count_=0,conversation_count_=0,advert_count_=0,active_count_=0;
    bool active_channel_=false;uint8_t active_key_[7]{};char active_title_[34]="MESSAGES";uint32_t refreshed_at_=0;
    UnreadPeer direct_unread_[MAX_UI_CONTACTS]{};
    uint8_t channel_unread_[MAX_UI_CHANNELS]{};
    DiscoveredContact discovered_[MAX_UI_ADVERTS]{};
    ContactInfo detail_contact_{};bool detail_valid_=false;bool detail_saved_=false;
    // Successful info replies are live observations, not new adverts.
    // Keep their timestamp and optional GPS in RAM without rewriting MeshCore
    // contact storage or falsely updating last_advert_timestamp.
    struct RecentInfo {
        uint8_t key[PUB_KEY_SIZE]{};
        uint32_t reply_millis=0;
        uint32_t gps_reply_millis=0; // local receipt, not the remote fix time
        int32_t lat=0,lon=0;
        bool heard=false,has_gps=false;
    } recent_info_{};
    char detail_identity_[24]{},detail_seen_[72]{},detail_advert_age_[72]{};
    char detail_position_source_[72]{},detail_route_[40]{},detail_position_[64]{};
    char detail_status_[80]="NOT REQUESTED",detail_telemetry_[120]="NOT REQUESTED",detail_path_[64]="NOT REQUESTED";
    bool detail_request_active_=false,request_gps_received_=false;UiNodeInfoRequest detail_request_type_=UiNodeInfoRequest::None;int32_t detail_lat_=0,detail_lon_=0;
    uint8_t detail_frame_[192]{};uint8_t detail_frame_len_=0;
    MessageStore store_;
    static void bind(ListStorage& item){item.entry.title=item.title;item.entry.subtitle=item.subtitle;item.entry.time=item.time;}
    static void format_short_age(uint32_t seconds,char* out,size_t len){
        if(seconds<60)snprintf(out,len,"JUST NOW");
        else if(seconds<3600)snprintf(out,len,"%luM AGO",(unsigned long)(seconds/60));
        else if(seconds<86400)snprintf(out,len,"%luH AGO",(unsigned long)(seconds/3600));
        else snprintf(out,len,"%luD AGO",(unsigned long)(seconds/86400));
    }
    static void bind(MessageView& item){item.entry.text=item.text;item.entry.time=item.time;}
    bool matches(const StoredMessage& m)const{return m.kind==(uint8_t)(active_channel_?MessageKind::Channel:MessageKind::Direct)&&memcmp(m.key,active_key_,active_channel_?1:6)==0;}
    bool has_recent_info(const uint8_t* full_key)const {
        return recent_info_.heard&&memcmp(recent_info_.key,full_key,PUB_KEY_SIZE)==0;
    }
    void note_info_reply() {
        // Only a matched reply (not a request, send acknowledgement or timeout)
        // establishes that the remote node was heard.
        if(!has_recent_info(detail_contact_.id.pub_key)){
            recent_info_={};
            memcpy(recent_info_.key,detail_contact_.id.pub_key,PUB_KEY_SIZE);
        }
        recent_info_.heard=true;
        recent_info_.reply_millis=millis();
    }
    const StoredMessage* last_for(const uint8_t* key,bool channel)const{
        for(size_t n=store_.count();n>0;--n){const auto& m=store_.at(n-1);if(m.kind==(uint8_t)(channel?MessageKind::Channel:MessageKind::Direct)&&memcmp(m.key,key,channel?1:6)==0)return &m;}return nullptr;
    }
    uint8_t& direct_unread(const uint8_t* key){
        for(auto& item:direct_unread_)if(item.used&&!memcmp(item.key,key,6))return item.count;
        for(auto& item:direct_unread_)if(!item.used){item.used=true;memcpy(item.key,key,6);item.count=0;return item.count;}
        return direct_unread_[0].count;
    }
    void rebuild_active(){
        active_count_=0;
        for(size_t i=0;i<store_.count()&&active_count_<MAX_STORED_MESSAGES;++i){const auto& m=store_.at(i);if(!matches(m))continue;
            auto& view=active_messages_[active_count_++];memset(&view,0,sizeof(view));bind(view);strncpy(view.text,m.text,sizeof(view.text)-1);format_time(m.timestamp,view.time);
            view.entry.outgoing=m.state!=(uint8_t)UiMessageState::Received;view.entry.state=(UiMessageState)m.state;
        }
    }
    bool activate(const ListStorage& item,bool channel){active_channel_=channel;detail_valid_=false;detail_frame_len_=0;detail_request_active_=false;detail_request_type_=UiNodeInfoRequest::None;request_gps_received_=false;strcpy(detail_status_,"NOT REQUESTED");strcpy(detail_telemetry_,"NOT REQUESTED");strcpy(detail_path_,"NOT REQUESTED");memcpy(active_key_,item.key,sizeof(active_key_));strncpy(active_title_,item.title,sizeof(active_title_)-1);rebuild_active();return true;}
public:
    MeshCoreUiProvider(){for(auto& i:contacts_)bind(i);for(auto& i:channels_)bind(i);for(auto& i:conversations_)bind(i);for(auto& i:adverts_)bind(i);for(auto& i:active_messages_)bind(i);}
    void begin(){store_.begin();refresh(true);}
    void refresh(bool force=false){
        const uint32_t interval=ui_is_standby()?60000:10000;
        if(!force&&millis()-refreshed_at_<interval)return;
        refreshed_at_=millis();
#if T5_LOG_POWER
        const uint32_t started=micros();
#endif
        contact_count_=channel_count_=conversation_count_=advert_count_=0;
        ContactInfo contact{};auto iterator=t5_mesh().startContactsIterator();
        while(contact_count_<MAX_UI_CONTACTS&&iterator.hasNext(&t5_mesh(),contact)){
            auto& item=contacts_[contact_count_++];memset(&item,0,sizeof(item));bind(item);strncpy(item.title,contact.name[0]?contact.name:"UNNAMED NODE",sizeof(item.title)-1);
            format_last_seen(contact.last_advert_timestamp,item.subtitle);format_time(contact.last_advert_timestamp,item.time);memcpy(item.key,contact.id.pub_key,7);item.entry.unread=direct_unread(item.key);
            if(const auto* last=last_for(item.key,false)){auto& c=conversations_[conversation_count_++];memset(&c,0,sizeof(c));bind(c);strncpy(c.title,item.title,sizeof(c.title)-1);strncpy(c.subtitle,last->text,sizeof(c.subtitle)-1);format_time(last->timestamp,c.time);memcpy(c.key,item.key,7);c.entry.unread=direct_unread(item.key);}
        }
        for(int i=0;i<MAX_GROUP_CHANNELS&&channel_count_<MAX_UI_CHANNELS;++i){ChannelDetails ch{};if(!t5_mesh().getChannel(i,ch)||!ch.name[0])continue;
            auto& item=channels_[channel_count_++];memset(&item,0,sizeof(item));bind(item);snprintf(item.title,sizeof(item.title),"# %s",ch.name);strncpy(item.subtitle,"MESHCORE CHANNEL",sizeof(item.subtitle)-1);item.key[0]=i;item.channel_index=i;item.entry.unread=i<MAX_UI_CHANNELS?channel_unread_[i]:0;
        }
        AdvertPath heard[MAX_UI_ADVERTS]{};const int heard_count=t5_mesh().getRecentlyHeard(heard,MAX_UI_ADVERTS);
        for(int i=0;i<heard_count&&advert_count_<MAX_UI_ADVERTS;++i){if(!heard[i].recv_timestamp||!heard[i].name[0])continue;auto& item=adverts_[advert_count_++];memset(&item,0,sizeof(item));bind(item);
            const uint8_t hops=heard[i].path_len&63;
            strncpy(item.title,heard[i].name,sizeof(item.title)-1);snprintf(item.subtitle,sizeof(item.subtitle),hops?"RECEIVED ADVERT  %u HOP%s":"RECEIVED ADVERT  ZERO HOP",hops,hops==1?"":"S");format_time(heard[i].recv_timestamp,item.time);memcpy(item.key,heard[i].pubkey_prefix,7);
            T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] advert '%s' path=0x%02x hops=%u\n",item.title,heard[i].path_len,hops);
        }
        // MeshCore's saved contacts are the single source of last-known GPS.
        // Traverse independently of the 16-contact list screen limit.
        map_node_count_=0;
        ContactInfo positioned{};auto positions=t5_mesh().startContactsIterator();
        while(map_node_count_<MAX_MAP_NODES&&positions.hasNext(&t5_mesh(),positioned)) {
            if((!positioned.gps_lat&&!positioned.gps_lon)||
               positioned.gps_lat < -85051100 || positioned.gps_lat > 85051100 ||
               positioned.gps_lon < -180000000 || positioned.gps_lon > 180000000)continue;
            UiMapNode& item=map_nodes_[map_node_count_++];memset(&item,0,sizeof(item));
            strncpy(item.name,positioned.name[0]?positioned.name:"UNNAMED",sizeof(item.name)-1);
            memcpy(item.key,positioned.id.pub_key,sizeof(item.key));
            item.latitude=positioned.gps_lat;item.longitude=positioned.gps_lon;
            item.advertised_at=positioned.last_advert_timestamp;
        }
        // A node with no saved advert GPS may still have returned valid GPS
        // telemetry. Let it appear on the map for this session.
        if(recent_info_.heard&&recent_info_.has_gps){
            bool found=false;
            for(size_t i=0;i<map_node_count_;++i)
                if(memcmp(map_nodes_[i].key,recent_info_.key,
                          sizeof(map_nodes_[i].key))==0){found=true;break;}
            if(!found&&map_node_count_<MAX_MAP_NODES){
                UiMapNode& item=map_nodes_[map_node_count_++];item={};
                if(const ContactInfo* contact=t5_mesh().lookupContactByPubKey(
                        recent_info_.key,PUB_KEY_SIZE)){
                    strncpy(item.name,contact->name[0]?contact->name:"UNNAMED",
                            sizeof(item.name)-1);
                    item.advertised_at=contact->last_advert_timestamp;
                    memcpy(item.key,recent_info_.key,sizeof(item.key));
                    item.latitude=recent_info_.lat;
                    item.longitude=recent_info_.lon;
                }else --map_node_count_;
            }
        }
        rebuild_active();
#if T5_LOG_POWER
        static uint32_t last_report=0;
        if(force||millis()-last_report>=60000){last_report=millis();T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] model refresh=%luus interval=%lums contacts=%u channels=%u adverts=%u standby=%d\n",(unsigned long)(micros()-started),(unsigned long)interval,(unsigned)contact_count_,(unsigned)channel_count_,(unsigned)advert_count_,ui_is_standby());}
#endif
    }
    void received_direct(const uint8_t* key,uint32_t timestamp,const char* text){auto& unread=direct_unread(key);if(unread<255)unread++;store_.append(MessageKind::Direct,key,6,text,timestamp,UiMessageState::Received);refresh(true);ui_notify_message_received(false);}
    void received_channel(uint8_t channel,uint32_t timestamp,const char* text){if(channel<MAX_UI_CHANNELS&&channel_unread_[channel]<255)channel_unread_[channel]++;store_.append(MessageKind::Channel,&channel,1,text,timestamp,UiMessageState::Received);refresh(true);ui_notify_message_received(true);}
    uint32_t sent(const char* text,uint32_t timestamp,uint32_t ack){auto* m=store_.append(active_channel_?MessageKind::Channel:MessageKind::Direct,active_key_,active_channel_?1:6,text,timestamp,UiMessageState::Sent,ack);rebuild_active();return m->sequence;}
    uint32_t queue_direct(const char* text,uint32_t timestamp){auto* m=store_.append(MessageKind::Direct,active_key_,6,text,timestamp,UiMessageState::Sending);rebuild_active();return m->sequence;}
    void update_message(uint32_t sequence,UiMessageState state){store_.update_state(sequence,state);rebuild_active();ui_request_data_refresh("message-state");}
    size_t map_node_count() const override {return map_node_count_;}
    bool map_node(size_t index,UiMapNode& out) const override {
        if(index>=map_node_count_)return false;
        out=map_nodes_[index];
        // Display receipt age of GPS telemetry separately from advert age.
        if(recent_info_.heard&&recent_info_.has_gps&&
           memcmp(recent_info_.key,out.key,sizeof(out.key))==0){
            out.latitude=recent_info_.lat;
            out.longitude=recent_info_.lon;
            out.gps_from_reply=true;
            out.gps_received_millis=recent_info_.gps_reply_millis;
        }
        return true;
    }
    bool open_map_node(size_t index) override {
        if(index>=map_node_count_)return false;
        ListStorage item{};bind(item);strncpy(item.title,map_nodes_[index].name,sizeof(item.title)-1);
        memcpy(item.key,map_nodes_[index].key,sizeof(item.key));return activate(item,false);
    }
    size_t conversation_count()const override{return conversation_count_;}const UiListEntry& conversation(size_t i)const override{return conversations_[i].entry;}
    bool open_conversation(size_t i)override{if(i>=conversation_count_)return false;direct_unread(conversations_[i].key)=0;conversations_[i].entry.unread=0;return activate(conversations_[i],false);}
    size_t contact_count()const override{return contact_count_;}const UiListEntry& contact(size_t i)const override{return contacts_[i].entry;}bool open_contact(size_t i)override{if(i>=contact_count_)return false;direct_unread(contacts_[i].key)=0;contacts_[i].entry.unread=0;return activate(contacts_[i],false);}
    size_t channel_count()const override{return channel_count_;}const UiListEntry& channel(size_t i)const override{return channels_[i].entry;}bool open_channel(size_t i)override{if(i>=channel_count_)return false;if(channels_[i].channel_index<MAX_UI_CHANNELS)channel_unread_[channels_[i].channel_index]=0;channels_[i].entry.unread=0;return activate(channels_[i],true);}
    size_t advert_count()const override{return advert_count_;}const UiListEntry& advert(size_t i)const override{return adverts_[i].entry;}
    void cache_discovered(const uint8_t* frame,size_t len){
        if(!frame||len<36||len>sizeof(discovered_[0].frame))return;DiscoveredContact* slot=nullptr;
        for(auto& item:discovered_)if(item.len&&!memcmp(item.prefix,frame+1,7)){slot=&item;break;}
        if(!slot)for(auto& item:discovered_)if(!item.len){slot=&item;break;}if(!slot)slot=&discovered_[0];
        memcpy(slot->prefix,frame+1,7);memcpy(slot->frame,frame,len);slot->len=(uint8_t)len;
    }
    bool open_advert(size_t i)override{
        if(i>=advert_count_)return false;detail_valid_=false;detail_saved_=false;detail_frame_len_=0;
        if(auto* saved=t5_mesh().lookupContactByPubKey(adverts_[i].key,7)){detail_contact_=*saved;detail_valid_=detail_saved_=true;return true;}
        for(const auto& item:discovered_)if(item.len&&!memcmp(item.prefix,adverts_[i].key,7)){
            memcpy(detail_frame_,item.frame,item.len);detail_frame_len_=item.len;size_t p=1;
            memcpy(detail_contact_.id.pub_key,item.frame+p,PUB_KEY_SIZE);p+=PUB_KEY_SIZE;detail_contact_.type=item.frame[p++];detail_contact_.flags=item.frame[p++];detail_contact_.out_path_len=item.frame[p++];
            memcpy(detail_contact_.out_path,item.frame+p,MAX_PATH_SIZE);p+=MAX_PATH_SIZE;memcpy(detail_contact_.name,item.frame+p,32);detail_contact_.name[31]=0;p+=32;
            memcpy(&detail_contact_.last_advert_timestamp,item.frame+p,4);p+=4;if(item.len>=p+8){memcpy(&detail_contact_.gps_lat,item.frame+p,4);p+=4;memcpy(&detail_contact_.gps_lon,item.frame+p,4);}detail_valid_=true;return true;
        }return false;
    }
    bool active_node_details(UiNodeDetails& out)const override{
        auto* self=const_cast<MeshCoreUiProvider*>(this);if(!detail_valid_){ContactInfo contact{};if(!active_contact(contact))return false;self->detail_contact_=contact;self->detail_valid_=self->detail_saved_=true;}
        // Re-read the contact on each UI render: later advertisements may
        // update its stored position and advert timestamp while Info is open.
        if(detail_saved_) {
            if(const ContactInfo* current=t5_mesh().lookupContactByPubKey(
                    detail_contact_.id.pub_key,PUB_KEY_SIZE))
                self->detail_contact_=*current;
        }
        snprintf(self->detail_identity_,sizeof(self->detail_identity_),"%02X%02X%02X%02X...%02X%02X",detail_contact_.id.pub_key[0],detail_contact_.id.pub_key[1],detail_contact_.id.pub_key[2],detail_contact_.id.pub_key[3],detail_contact_.id.pub_key[30],detail_contact_.id.pub_key[31]);
        const uint32_t now=(uint32_t)time(nullptr);
        const uint32_t advert_age=detail_contact_.last_advert_timestamp&&
            now>=detail_contact_.last_advert_timestamp?
            now-detail_contact_.last_advert_timestamp:0U;
        if(detail_contact_.last_advert_timestamp)
            format_short_age(advert_age,self->detail_advert_age_,
                             sizeof(self->detail_advert_age_));
        else strcpy(self->detail_advert_age_,"UNKNOWN");
        format_last_seen(detail_contact_.last_advert_timestamp,self->detail_seen_);
        const bool recent=has_recent_info(detail_contact_.id.pub_key);
        if(recent){
            char age[24];
            format_short_age((uint32_t)(millis()-recent_info_.reply_millis)/1000U,
                             age,sizeof(age));
            snprintf(self->detail_seen_,sizeof(self->detail_seen_),
                     "INFO REPLY %s",age);
        }
        const uint8_t hops=detail_contact_.out_path_len&0x3F;
        if(detail_contact_.out_path_len==OUT_PATH_UNKNOWN)
            strcpy(self->detail_route_,"FLOOD / UNKNOWN");
        else snprintf(self->detail_route_,sizeof(self->detail_route_),
                      hops?"%u HOP%s":"ZERO HOP",hops,hops==1?"":"S");
        const bool reported_gps=recent&&recent_info_.has_gps;
        // Keep last-known coordinates when the latest request has no GPS.
        // LAST HEARD can advance independently of the position's provenance.
        self->detail_lat_=reported_gps?recent_info_.lat:detail_contact_.gps_lat;
        self->detail_lon_=reported_gps?recent_info_.lon:detail_contact_.gps_lon;
        if(reported_gps||self->detail_lat_||self->detail_lon_){
            const long alat=labs((long)self->detail_lat_),alon=labs((long)self->detail_lon_);
            snprintf(self->detail_position_,sizeof(self->detail_position_),
                "%c%ld.%06ld  %c%ld.%06ld",
                self->detail_lat_<0?'-':'+',alat/1000000,alat%1000000,
                self->detail_lon_<0?'-':'+',alon/1000000,alon%1000000);
            if(reported_gps){
                char age[24];
                format_short_age((uint32_t)(millis()-recent_info_.gps_reply_millis)/1000U,
                                 age,sizeof(age));
                snprintf(self->detail_position_source_,
                         sizeof(self->detail_position_source_),"GPS REPLY %s",age);
            }else snprintf(self->detail_position_source_,
                          sizeof(self->detail_position_source_),"SAVED ADVERT %s",
                          self->detail_advert_age_);
        }else{
            strcpy(self->detail_position_,"NO SAVED POSITION");
            strcpy(self->detail_position_source_,"NO GPS REPORTED");
        }
        out={detail_contact_.name,self->detail_identity_,self->detail_seen_,
             self->detail_route_,self->detail_position_,self->detail_status_,
             self->detail_telemetry_,self->detail_path_,self->detail_lat_,
             self->detail_lon_,detail_request_active_,detail_request_type_,detail_saved_,
             self->detail_advert_age_,self->detail_position_source_};
        return true;
    }
    bool add_active_node()override{if(!detail_valid_||detail_saved_||!detail_frame_len_)return false;detail_frame_[0]=9;if(!local_mesh_enqueue_command(detail_frame_,detail_frame_len_))return false;detail_saved_=true;return true;}
    bool remove_active_contact()override{if(!detail_valid_||!detail_saved_)return false;uint8_t command[1+PUB_KEY_SIZE]{15};memcpy(command+1,detail_contact_.id.pub_key,PUB_KEY_SIZE);if(!local_mesh_enqueue_command(command,sizeof(command)))return false;detail_saved_=false;return true;}
    bool request_active_node_info(UiNodeInfoRequest request)override;
    const uint8_t* detail_key()const{return detail_contact_.id.pub_key;}
    void request_state(bool active,UiNodeInfoRequest request=UiNodeInfoRequest::None){detail_request_active_=active;detail_request_type_=active?request:UiNodeInfoRequest::None;ui_request_data_refresh("node-info");}
    void request_timeout(UiNodeInfoRequest request){
        char* out=request==UiNodeInfoRequest::Status?detail_status_:request==UiNodeInfoRequest::Telemetry?detail_telemetry_:detail_path_;
        strcpy(out,"NO RESPONSE / NOT ALLOWED");
        if(request==UiNodeInfoRequest::Telemetry)ui_notify_node_position_unavailable();
    }
    void status_response(const uint8_t* data,size_t len){note_info_reply();snprintf(detail_status_,sizeof(detail_status_),"RECEIVED  %u BYTES",(unsigned)len);T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] node info: status reply received");}
    void path_response(const uint8_t* data,size_t len){note_info_reply();if(!len){strcpy(detail_path_,"NO PATH DATA");return;}const uint8_t hops=data[0]&0x3F;snprintf(detail_path_,sizeof(detail_path_),hops?"OUTBOUND %u HOP%s":"DIRECT / ZERO HOP",hops,hops==1?"":"S");T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] node info: path reply received");}
    void telemetry_response(const uint8_t* data,size_t len){
        note_info_reply();
        LPPReader reader(data,(uint8_t)min(len,(size_t)255));uint8_t channel=0,type=0;char* cursor=detail_telemetry_;size_t left=sizeof(detail_telemetry_);cursor[0]=0;
        while(reader.readHeader(channel,type)&&left>12){float v=0;char item[48]{};switch(type){case LPP_GPS:{float lat,lon,alt;if(reader.readGPS(lat,lon,alt)){
            if(isfinite(lat)&&isfinite(lon)&&lat>=-85.0511f&&lat<=85.0511f&&lon>=-180.0f&&lon<=180.0f){
                recent_info_.lat=(int32_t)(lat*1000000.0f);
                recent_info_.lon=(int32_t)(lon*1000000.0f);
                recent_info_.has_gps=true;
                recent_info_.gps_reply_millis=millis();
                request_gps_received_=true;
                detail_lat_=recent_info_.lat;detail_lon_=recent_info_.lon;
                snprintf(item,sizeof(item),"GPS %.4f %.4f",lat,lon);
            }
        }break;}case LPP_VOLTAGE:reader.readVoltage(v);snprintf(item,sizeof(item),"%.2fV",v);break;case LPP_CURRENT:reader.readCurrent(v);snprintf(item,sizeof(item),"%.3fA",v);break;case LPP_TEMPERATURE:reader.readTemperature(v);snprintf(item,sizeof(item),"%.1fC",v);break;case LPP_RELATIVE_HUMIDITY:reader.readRelativeHumidity(v);snprintf(item,sizeof(item),"%.1f%% RH",v);break;case LPP_BAROMETRIC_PRESSURE:reader.readPressure(v);snprintf(item,sizeof(item),"%.1f HPA",v);break;default:reader.skipData(type);break;}if(item[0]){const int n=snprintf(cursor,left,"%s%s",cursor==detail_telemetry_?"":"  ",item);if(n<0||(size_t)n>=left)break;cursor+=n;left-=n;}}
        if(!detail_telemetry_[0])strcpy(detail_telemetry_,"NO TELEMETRY RETURNED");
        if(request_gps_received_)refresh(true);
        else ui_notify_node_position_unavailable();
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] node info: telemetry reply received bytes=%u gps=%d\n",
                      (unsigned)len,request_gps_received_);
    }
    const char* active_title()const override{return active_title_;}bool active_is_channel()const override{return active_channel_;}size_t active_message_count()const override{return active_count_;}const UiMessage& active_message(size_t i)const override{return active_messages_[i].entry;}
    bool active_contact(ContactInfo& out)const{if(active_channel_)return false;auto* found=t5_mesh().lookupContactByPubKey(active_key_,6);if(!found)return false;out=*found;return true;}
    bool active_channel(ChannelDetails& out)const{return active_channel_&&t5_mesh().getChannel(active_key_[0],out);}
    uint16_t direct_unread_total()const{uint16_t total=0;for(const auto& item:direct_unread_)total+=item.count;return total;}
    uint16_t channel_unread_total()const{uint16_t total=0;for(const auto count:channel_unread_)total+=count;return total;}
};

MeshCoreUiProvider provider;char radio_summary[44]{};char setting_value[20]{};
struct PendingDirect{bool active=false;bool waiting_response=false;uint8_t retry=0;uint32_t sequence=0,timestamp=0,ack=0,deadline=0;uint8_t key[6]{};char text[145]{};} pending_direct;
struct PendingInfo{bool active=false;bool waiting_sent=false;UiNodeInfoRequest request=UiNodeInfoRequest::None;uint32_t deadline=0;uint8_t key[PUB_KEY_SIZE]{};} pending_info;
int8_t pending_advert=-1;
static bool enqueue_direct_attempt(){
    uint8_t frame[MAX_FRAME_SIZE+1]{};size_t p=0;frame[p++]=2;frame[p++]=0;frame[p++]=pending_direct.retry;memcpy(frame+p,&pending_direct.timestamp,4);p+=4;memcpy(frame+p,pending_direct.key,6);p+=6;const size_t n=min(strlen(pending_direct.text),(size_t)MAX_TEXT_LEN);memcpy(frame+p,pending_direct.text,n);p+=n;
    if(!local_mesh_enqueue_command(frame,p))return false;pending_direct.waiting_response=true;T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct attempt=%u queued sequence=%lu\n",pending_direct.retry,(unsigned long)pending_direct.sequence);return true;
}
static bool enqueue_info_request(){
    uint8_t frame[4+PUB_KEY_SIZE]{};size_t len=0;
    if(pending_info.request==UiNodeInfoRequest::Status){frame[0]=27;memcpy(frame+1,pending_info.key,PUB_KEY_SIZE);len=1+PUB_KEY_SIZE;}
    else if(pending_info.request==UiNodeInfoRequest::Telemetry){frame[0]=39;memcpy(frame+4,pending_info.key,PUB_KEY_SIZE);len=4+PUB_KEY_SIZE;}
    else if(pending_info.request==UiNodeInfoRequest::Path){frame[0]=52;frame[1]=0;memcpy(frame+2,pending_info.key,PUB_KEY_SIZE);len=2+PUB_KEY_SIZE;}
    else return false;
    if(!local_mesh_enqueue_command(frame,len))return false;
    pending_info.waiting_sent=true;pending_info.deadline=millis()+30000;return true;
}
static void finish_info(){pending_info.active=false;pending_info.waiting_sent=false;pending_info.request=UiNodeInfoRequest::None;provider.request_state(false);}

bool MeshCoreUiProvider::request_active_node_info(UiNodeInfoRequest request){
    if(active_channel_||pending_info.active||request==UiNodeInfoRequest::None)return false;
    ContactInfo contact{};if(!active_contact(contact))return false;
    pending_info={};pending_info.active=true;pending_info.request=request;
    memcpy(pending_info.key,contact.id.pub_key,PUB_KEY_SIZE);
    if(request==UiNodeInfoRequest::Telemetry)request_gps_received_=false;
    char* target=request==UiNodeInfoRequest::Status?detail_status_:request==UiNodeInfoRequest::Telemetry?detail_telemetry_:detail_path_;
    strcpy(target,"REQUESTING");
    request_state(true,request);
    if(!enqueue_info_request()){finish_info();strcpy(target,"REQUEST FAILED");return false;}
    return true;
}
}

UiDataProvider* local_mesh_provider(){return &provider;}
void local_mesh_on_frame(const uint8_t* frame,size_t len){
    if(!frame||!len)return;char message[150]{};
    if(frame[0]==0x8A){provider.cache_discovered(frame,len);provider.refresh(true);ui_request_data_refresh("new-advert");T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] discovered advert cached with full MeshCore identity");}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Status&&len>=8&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x87){provider.status_response(frame+8,len-8);finish_info();}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Telemetry&&len>=8&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x8B){provider.telemetry_response(frame+8,len-8);finish_info();}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Path&&len>=9&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x8D){provider.path_response(frame+8,len-8);finish_info();}
    else if(frame[0]==6&&len>=10&&pending_info.active&&pending_info.waiting_sent){uint32_t timeout=0;memcpy(&timeout,frame+6,4);pending_info.deadline=millis()+max((uint32_t)3000,timeout+2000);pending_info.waiting_sent=false;}
    else if(frame[0]==1&&pending_info.active){provider.request_timeout(pending_info.request);finish_info();}
    else if(frame[0]==6&&len>=10&&pending_direct.active){memcpy(&pending_direct.ack,frame+2,4);uint32_t timeout=0;memcpy(&timeout,frame+6,4);pending_direct.deadline=millis()+max((uint32_t)500,timeout);pending_direct.waiting_response=false;provider.update_message(pending_direct.sequence,pending_direct.retry?((UiMessageState)((uint8_t)UiMessageState::Retrying1+pending_direct.retry-1)):UiMessageState::Sent);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct attempt=%u transmitted ack=%08lx timeout=%lu\n",pending_direct.retry,(unsigned long)pending_direct.ack,(unsigned long)timeout);}
    else if(frame[0]==0x82&&len>=5&&pending_direct.active){uint32_t ack=0;memcpy(&ack,frame+1,4);if(ack==pending_direct.ack){provider.update_message(pending_direct.sequence,UiMessageState::Delivered);pending_direct.active=false;T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct delivered ack=%08lx\n",(unsigned long)ack);}}
    else if(frame[0]==1&&pending_direct.active&&pending_direct.waiting_response){provider.update_message(pending_direct.sequence,UiMessageState::Failed);pending_direct.active=false;T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct command failed error=%u\n",len>1?frame[1]:0);}
    else if((frame[0]==0||frame[0]==1)&&pending_advert>=0){const bool flood=pending_advert==1;ui_notify_advert_result(flood,frame[0]==0);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] %s advert action result=%s\n",flood?"flood":"zero-hop",frame[0]==0?"OK":"FAILED");pending_advert=-1;}
    else if((frame[0]==7||frame[0]==16)&&len>13){uint32_t timestamp=0;memcpy(&timestamp,&frame[9],4);const size_t start=frame[8]==2?17:13;if(len<=start)return;memcpy(message,&frame[start],min(sizeof(message)-1,len-start));provider.received_direct(&frame[1],timestamp,message);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct message received bytes=%u\n",(unsigned)(len-start));}
    else if((frame[0]==8||frame[0]==17)&&len>8){uint32_t timestamp=0;memcpy(&timestamp,&frame[4],4);memcpy(message,&frame[8],min(sizeof(message)-1,len-8));provider.received_channel(frame[1],timestamp,message);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] channel %u message received\n",frame[1]);}
}
void local_mesh_runtime_begin(){provider.begin();}
void local_mesh_loop(){
    t5_mesh().loop();provider.refresh();
    const uint32_t gps_now=millis();
    const bool gps_enabled=t5_mesh().getNodePrefs()->gps_enabled!=0;
    const uint32_t gps_interval=t5_mesh().getNodePrefs()->gps_interval;
    auto* gps_location=sensors.getLocationProvider();

    if(gps_duty_reset){
        gps_duty_reset=false;
        gps_duty_next_wake=0;
        if(gps_enabled&&gps_duty_sleeping){
            sensors.setSettingValue("gps","1");gps_duty_sleeping=false;
        }
    }
    if(!gps_enabled){
        if(!gps_duty_sleeping){sensors.setSettingValue("gps","0");gps_duty_sleeping=true;}
    }else if(gps_interval==0){
        if(gps_duty_sleeping){sensors.setSettingValue("gps","1");gps_duty_sleeping=false;}
    }else if(gps_duty_sleeping){
        if((int32_t)(gps_now-gps_duty_next_wake)>=0){
            gps_duty_wake_stamp=gps_location?(uint32_t)gps_location->getTimestamp():0;
            sensors.setSettingValue("gps","1");gps_duty_sleeping=false;gps_duty_awake_since=gps_now;
            T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] duty wake interval=%lus previous_stamp=%lu\n",(unsigned long)gps_interval,(unsigned long)gps_duty_wake_stamp);
        }
    }else if(gps_location&&gps_location->isValid()&&
             (!gps_duty_awake_since||
              (gps_now-gps_duty_awake_since>=1000&&
               (uint32_t)gps_location->getTimestamp()!=gps_duty_wake_stamp))){
        // Require a newly observed GPS timestamp after a scheduled wake so a
        // cached fix cannot immediately put the receiver back to sleep.
        gps_duty_next_wake=gps_now+gps_interval*1000UL;
        gps_duty_awake_since=0;gps_duty_wake_stamp=0;
        sensors.setSettingValue("gps","0");gps_duty_sleeping=true;
        T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] duty sleep after fresh fix; next wake in %lus\n",(unsigned long)gps_interval);
    }
    sensors.loop();
    t5_gps_power_probe_tick(); // executes even when MeshCore has stopped the GPS provider
    rtc_clock.tick();
    if(pending_info.active&&(int32_t)(millis()-pending_info.deadline)>=0){provider.request_timeout(pending_info.request);finish_info();}
    if(pending_direct.active&&!pending_direct.waiting_response&&pending_direct.deadline&&(int32_t)(millis()-pending_direct.deadline)>=0){
        if(pending_direct.retry>=5){provider.update_message(pending_direct.sequence,UiMessageState::Failed);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct failed after 5 retries sequence=%lu\n",(unsigned long)pending_direct.sequence);pending_direct.active=false;}
        else{pending_direct.retry++;provider.update_message(pending_direct.sequence,(UiMessageState)((uint8_t)UiMessageState::Retrying1+pending_direct.retry-1));if(!enqueue_direct_attempt()){provider.update_message(pending_direct.sequence,UiMessageState::Failed);pending_direct.active=false;}}
    }
    auto* location=sensors.getLocationProvider();
    static uint32_t next_ui_gps=0,candidate_since=0;static bool stable_fix=false,candidate_fix=false;static int stable_sats=0;static long stable_lat=0,stable_lon=0;static uint32_t stable_stamp=0;const uint32_t now=millis();if((int32_t)(now-next_ui_gps)>=0){next_ui_gps=now+(ui_is_standby()?10000:1000);const bool enabled=local_mesh_gps_enabled();const bool raw_fix=location&&location->isValid();if(raw_fix!=candidate_fix){candidate_fix=raw_fix;candidate_since=now;}if(raw_fix==stable_fix||now-candidate_since>=3000){stable_fix=raw_fix;if(raw_fix){stable_sats=(int)location->satellitesCount();stable_lat=location->getLatitude();stable_lon=location->getLongitude();stable_stamp=(uint32_t)location->getTimestamp();}}ui_status_set_gps(enabled,stable_fix,stable_sats,stable_lat,stable_lon,stable_stamp);}
#if T5_LOG_GPS
    static bool was_waiting=true;
    if(location){const bool waiting=location->waitingTimeSync();if(was_waiting&&!waiting)T5_DEBUGF(T5_LOG_GPS,"[T5-RTC] GPS provider finished sync request UTC=%lu; hardware RTC write may have been skipped (see [T5] rtc log)\n",(unsigned long)location->getTimestamp());was_waiting=waiting;}
#endif
#if T5_LOG_POWER
    static uint32_t radio_report_at=0;
    if(millis()-radio_report_at>=60000){radio_report_at=millis();T5_DEBUGF(T5_LOG_POWER,"[T5-POWER] radio continuous-rx=%d received=%lu errors=%lu sent=%lu boosted=%d (duty cycle intentionally disabled)\n",radio_driver.isInRecvMode(),(unsigned long)radio_driver.getPacketsRecv(),(unsigned long)radio_driver.getPacketsRecvErrors(),(unsigned long)radio_driver.getPacketsSent(),radio_driver.getRxBoostedGainMode());}
#endif
}
bool local_mesh_send_active(const char* text){
    if(!text||!text[0])return false;const uint32_t now=time(nullptr);
    if(provider.active_is_channel()){ChannelDetails channel{};if(!provider.active_channel(channel))return false;const bool ok=t5_mesh().sendGroupMessage(now,channel.channel,t5_mesh().getNodeName(),text,strlen(text));if(ok)provider.sent(text,now,0);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] channel send result=%d\n",ok);return ok;}
    ContactInfo contact{};if(!provider.active_contact(contact)||pending_direct.active)return false;pending_direct={};pending_direct.active=true;pending_direct.timestamp=now;memcpy(pending_direct.key,contact.id.pub_key,6);strncpy(pending_direct.text,text,sizeof(pending_direct.text)-1);pending_direct.sequence=provider.queue_direct(text,now);if(!enqueue_direct_attempt()){provider.update_message(pending_direct.sequence,UiMessageState::Failed);pending_direct.active=false;return false;}return true;
}
bool local_mesh_send_direct(size_t index,const char* text){if(!provider.open_contact(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_channel(size_t index,const char* text){if(!provider.open_channel(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_advert(bool flood){if(pending_advert>=0)return false;const uint8_t command[2]={7,(uint8_t)(flood?1:0)};if(!local_mesh_enqueue_command(command,sizeof(command)))return false;pending_advert=flood?1:0;return true;}
bool local_mesh_apply_radio(float freq,float bw,uint8_t sf,uint8_t cr,uint8_t path_hash_mode){auto* p=t5_mesh().getNodePrefs();if(freq<=0||bw<7||sf<5||sf>12||cr<5||cr>8)return false;p->freq=freq;p->bw=bw;p->sf=sf;p->cr=cr;p->path_hash_mode=min((uint8_t)2,path_hash_mode);t5_mesh().savePrefs();radio_driver.setParams(freq,bw,sf,cr);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] radio preset applied %.3f SF%u BW%.1f CR%u hash=%u\n",freq,sf,bw,cr,p->path_hash_mode);return true;}
void local_mesh_apply_name(const char* name){auto* p=t5_mesh().getNodePrefs();strncpy(p->node_name,name,sizeof(p->node_name)-1);p->node_name[sizeof(p->node_name)-1]=0;t5_mesh().savePrefs();}
void local_mesh_apply_gps(bool enabled){auto* p=t5_mesh().getNodePrefs();p->gps_enabled=enabled?1:0;t5_mesh().savePrefs();t5_mesh().applyGpsPrefs();gps_duty_sleeping=!enabled;reset_gps_duty_cycle();}
bool local_mesh_gps_enabled(){return t5_mesh().getNodePrefs()->gps_enabled!=0;}bool local_mesh_gps_fix(){auto* location=sensors.getLocationProvider();return location&&location->isValid();}
uint32_t local_mesh_gps_interval(){return t5_mesh().getNodePrefs()->gps_interval;}
bool local_mesh_gps_advert_location(){return t5_mesh().getNodePrefs()->advert_loc_policy!=0;}
uint8_t local_mesh_gps_constellation_mode(){return t5_gps_constellation_mode();}
bool local_mesh_gps_set_constellation_mode(uint8_t mode){return t5_gps_set_constellation_mode(mode);}

void local_mesh_cycle_gps_interval(){static constexpr uint32_t values[]={0,60,300,900,1800};auto* p=t5_mesh().getNodePrefs();size_t i=0;while(i<4&&p->gps_interval!=values[i])++i;p->gps_interval=values[(i+1)%5];t5_mesh().savePrefs();t5_mesh().applyGpsPrefs();gps_duty_sleeping=false;reset_gps_duty_cycle();}
void local_mesh_toggle_gps_advert_location(){auto* p=t5_mesh().getNodePrefs();p->advert_loc_policy=p->advert_loc_policy?0:1;t5_mesh().savePrefs();}
uint32_t local_mesh_current_time(){return rtc_clock.getCurrentTime();}
bool local_mesh_time_valid(){return rtc_clock.isValid();}
const char* local_mesh_node_name(){return t5_mesh().getNodeName();}
const char* local_mesh_radio_summary(){auto* p=t5_mesh().getNodePrefs();snprintf(radio_summary,sizeof(radio_summary),"%.3f SF%u BW%.1f CR%u",p->freq,p->sf,p->bw,p->cr);return radio_summary;}
const char* local_mesh_privacy_value(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:return p->autoadd_config?"ENABLED":"DISABLED";case 1:if(!p->autoadd_max_hops)return "NO LIMIT";if(p->autoadd_max_hops==1)return "DIRECT ONLY";snprintf(setting_value,sizeof(setting_value),"UP TO %u HOPS",p->autoadd_max_hops-1);return setting_value;case 2:return p->advert_loc_policy?"SHARE":"HIDDEN";case 3:return p->telemetry_mode_base==0?"DENY":p->telemetry_mode_base==1?"CONTACT FLAGS":"ALLOW ALL";case 4:return p->telemetry_mode_loc==0?"DENY":p->telemetry_mode_loc==1?"CONTACT FLAGS":"ALLOW ALL";default:return p->isRepeatEn()?"ENABLED":"DISABLED";}}
void local_mesh_toggle_privacy(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:p->autoadd_config=p->autoadd_config?0:0x1E;break;case 1:p->autoadd_max_hops=(p->autoadd_max_hops+1)%6;break;case 2:p->advert_loc_policy=p->advert_loc_policy?0:1;break;case 3:p->telemetry_mode_base=(p->telemetry_mode_base+1)%3;break;case 4:p->telemetry_mode_loc=(p->telemetry_mode_loc+1)%3;break;default:p->setRepeatEn(!p->isRepeatEn());break;}t5_mesh().savePrefs();}
void local_mesh_cycle_path_hash(){auto* p=t5_mesh().getNodePrefs();p->path_hash_mode=(p->path_hash_mode+1)%3;t5_mesh().savePrefs();}
uint8_t local_mesh_path_hash_mode(){return min((uint8_t)2,t5_mesh().getNodePrefs()->path_hash_mode);}
void local_mesh_prepare_shutdown(){
    T5_DEBUGLN(T5_LOG_MESH,"[T5-SHUTDOWN] stopping MeshCore peripherals");
    radio_driver.powerOff();
    if(auto* location=sensors.getLocationProvider())location->stop();
    Serial1.end();
    Serial.println("[T5-SHUTDOWN] SX1262 sleep requested; GPS stopped");
}
uint16_t local_mesh_direct_unread_total(){return provider.direct_unread_total();}
uint16_t local_mesh_channel_unread_total(){return provider.channel_unread_total();}
