#include <Arduino.h>
#include <Mesh.h>
#include <Utils.h>
#include <SPIFFS.h>
#include <Preferences.h>
#include <time.h>
#include "local_mesh_runtime.h"
#include "message_store.h"
#include "companion_runtime.h"
#include "ui_onboarding.h"
#include "hardware/gps.h"
#include "hardware/rtc.h"
#include "hardware/radio.h"
#include "t5_logging.h"
#include <helpers/sensors/LPPDataHelpers.h>
#include "meshcore_adapter.h"
#include "channel_key.h"
#include <esp_system.h>


namespace {
constexpr size_t MAX_UI_CONTACTS=16;
constexpr size_t MAX_UNREAD_PEERS=MAX_CONTACTS;
constexpr size_t MAX_MAP_NODES=50;
constexpr size_t MAX_UI_CHANNELS=8;
constexpr size_t MAX_UI_ADVERTS=16;
constexpr size_t MAX_STORED_MESSAGES=MESHINK_MESSAGE_CAPACITY;
constexpr uint8_t DIRECT_RETRY_LIMIT=3;
static_assert(MESHINK_MESSAGE_TEXT_MAX==MAX_TEXT_LEN,
              "MeshInk message limit must track MeshCore MAX_TEXT_LEN");
static_assert(13+MESHINK_MESSAGE_TEXT_MAX<=MAX_FRAME_SIZE,
              "full direct-message command must fit the MeshCore companion frame");
constexpr uint32_t CREDENTIAL_MAGIC=0x3143524D; // MRC1
constexpr size_t MAX_SAVED_CREDENTIALS=16;
struct SavedCredential{uint8_t key[PUB_KEY_SIZE]{};char password[16]{};bool valid=false;};
struct CredentialBlob{uint32_t magic=CREDENTIAL_MAGIC;SavedCredential entries[MAX_SAVED_CREDENTIALS]{};};

static bool load_credentials(CredentialBlob& blob){
    Preferences prefs;if(!prefs.begin("mesh-auth",true))return false;
    const size_t len=prefs.getBytesLength("credentials");
    const bool ok=len==sizeof(blob)&&prefs.getBytes("credentials",&blob,sizeof(blob))==sizeof(blob)&&blob.magic==CREDENTIAL_MAGIC;
    prefs.end();if(!ok)blob=CredentialBlob{};return ok;
}
static bool write_credentials(const CredentialBlob& blob){
    Preferences prefs;if(!prefs.begin("mesh-auth",false))return false;
    const bool ok=prefs.putBytes("credentials",&blob,sizeof(blob))==sizeof(blob);prefs.end();return ok;
}
static bool load_saved_password(const uint8_t* key,char* out,size_t len){
    if(!key||!out||!len)return false;
    CredentialBlob blob{};
    load_credentials(blob);
    for(const auto& entry:blob.entries)if(entry.valid&&!memcmp(entry.key,key,PUB_KEY_SIZE)){
        strncpy(out,entry.password,len-1);
        out[len-1]=0;
        return true;
    }
    out[0]=0;
    return false;
}
static bool save_password_for(const uint8_t* key,const char* password){
    if(!key||!password)return false;
    CredentialBlob blob{};
    load_credentials(blob);
    SavedCredential* slot=nullptr;
    for(auto& entry:blob.entries)if(entry.valid&&!memcmp(entry.key,key,PUB_KEY_SIZE)){slot=&entry;break;}
    if(!slot)for(auto& entry:blob.entries)if(!entry.valid){slot=&entry;break;}
    if(!slot)slot=&blob.entries[0];
    *slot=SavedCredential{};
    slot->valid=true;
    memcpy(slot->key,key,PUB_KEY_SIZE);
    strncpy(slot->password,password,sizeof(slot->password)-1);
    return write_credentials(blob);
}
static bool clear_saved_password(const uint8_t* key){
    if(!key)return false;
    CredentialBlob blob{};
    load_credentials(blob);
    bool changed=false;
    for(auto& entry:blob.entries)if(entry.valid&&!memcmp(entry.key,key,PUB_KEY_SIZE)){
        entry=SavedCredential{};
        changed=true;
    }
    return !changed||write_credentials(blob);
}

// gps_interval in upstream MeshCore controls how often coordinates are copied,
// not receiver power. Timed modes here pause the SOFTWARE GPS provider after
// a fix; board-specific receiver power and tuning remain behind hardware/gps.h.
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

using MessageKind=MeshInkMessageKind;
using StoredMessage=MeshInkStoredMessage;
struct ListStorage{UiListEntry entry{};char title[34]{};char subtitle[72]{};char time[10]{};uint8_t key[7]{};uint8_t channel_index=0;};
struct MessageView{UiMessage entry{};char text[MESHINK_MESSAGE_TEXT_BYTES]{};char time[10]{};char network[52]{};};
struct UnreadPeer{uint8_t key[6]{};uint8_t count=0;bool used=false;};
constexpr size_t DISCOVERED_CONTACT_CACHE_BYTES=192;
struct DiscoveredContact{uint8_t prefix[7]{};uint8_t frame[DISCOVERED_CONTACT_CACHE_BYTES]{};uint8_t len=0;};
constexpr size_t DISCOVERED_CONTACT_BASE_LEN=
    1+PUB_KEY_SIZE+3+MAX_PATH_SIZE+32+4; // through last_advert_timestamp
static_assert(DISCOVERED_CONTACT_BASE_LEN<=DISCOVERED_CONTACT_CACHE_BYTES,
              "discovered contact base frame must fit cache");

static void format_time(uint32_t timestamp,char out[10]){
    time_t raw=timestamp?(time_t)timestamp:time(nullptr);struct tm value{};localtime_r(&raw,&value);
    snprintf(out,10,"%02d:%02d",value.tm_hour,value.tm_min);
}
static UiNodeRole meshcore_ui_role(uint8_t type){
    switch(type){
        case ADV_TYPE_CHAT:return UiNodeRole::Client;
        case ADV_TYPE_REPEATER:return UiNodeRole::Relay;
        case ADV_TYPE_ROOM:return UiNodeRole::Service;
        case ADV_TYPE_SENSOR:return UiNodeRole::Sensor;
        default:return UiNodeRole::Unknown;
    }
}

static void format_node_role(uint8_t type,char* out,size_t len){
    switch(type){
        case ADV_TYPE_CHAT:strncpy(out,"CHAT",len);break;
        case ADV_TYPE_REPEATER:strncpy(out,"REPEATER",len);break;
        case ADV_TYPE_ROOM:strncpy(out,"ROOM SERVER",len);break;
        case ADV_TYPE_SENSOR:strncpy(out,"SENSOR",len);break;
        case ADV_TYPE_NONE:strncpy(out,"UNKNOWN",len);break;
        default:snprintf(out,len,"TYPE %u",(unsigned)type);break;
    }
    if(len)out[len-1]=0;
}
static const char* state_text(UiMessageState state){
    switch(state){case UiMessageState::Sending:return "SENDING";case UiMessageState::Sent:return "SENT";
        case UiMessageState::Delivered:return "DELIVERED";case UiMessageState::Failed:return "FAILED";
        case UiMessageState::Retrying1:return "RETRYING 1/2";case UiMessageState::Retrying2:return "RETRYING 2/2";
        case UiMessageState::Retrying3:return "SENDING";case UiMessageState::Retrying4:return "RETRYING 4/5";
        case UiMessageState::Retrying5:return "RETRYING 5/5";default:return "";}
}
static void format_last_heard(uint32_t timestamp,char out[72]){
    if(!timestamp){strcpy(out,"UNKNOWN");return;}
    const uint32_t now=meshink_rtc_current_time(),age=now>timestamp?now-timestamp:0;
    if(age<60)strcpy(out,"JUST NOW");
    else if(age<3600)snprintf(out,72,"%lu MIN AGO",(unsigned long)(age/60));
    else if(age<86400)snprintf(out,72,"%lu HOUR%s AGO",(unsigned long)(age/3600),age/3600==1?"":"S");
    else snprintf(out,72,"%lu DAY%s AGO",(unsigned long)(age/86400),age/86400==1?"":"S");
}

class MeshCoreUiProvider final:public UiDataProvider{
    ListStorage contacts_[MAX_UI_CONTACTS]{},channels_[MAX_UI_CHANNELS]{},conversations_[MAX_UI_CONTACTS+MAX_UI_CHANNELS]{},adverts_[MAX_UI_ADVERTS]{};
    uint16_t active_indices_[MESHINK_MESSAGE_CAPACITY]{};
    mutable MessageView active_message_view_{};
    UiMapNode map_nodes_[MAX_MAP_NODES]{};
    size_t map_node_count_=0;
    size_t contact_count_=0,channel_count_=0,conversation_count_=0,advert_count_=0,active_count_=0;
    uint32_t active_revision_=1;
    // One direct send can be in flight at a time. Retry/status presentation is
    // session-only UI state: never write it to the flash-backed journal.
    uint32_t transient_direct_sequence_=0;
    UiMessageState transient_direct_state_=UiMessageState::Sending;
    bool transient_direct_route_known_=false;
    bool transient_direct_route_flood_=false;
    bool active_channel_=false;uint8_t active_key_[7]{};char active_title_[34]="MESSAGES";uint32_t refreshed_at_=0;
    uint32_t conversation_store_revision_=0xFFFFFFFFUL;
    uint32_t conversation_contacts_signature_=0;
    // Unread cache is tiny (one entry per configured MeshCore contact) and
    // contains only peers that are currently unread. Historical read-neutral
    // journal records never consume a slot.
    UnreadPeer direct_unread_[MAX_UNREAD_PEERS]{};
    uint8_t channel_unread_[MAX_UI_CHANNELS]{};
    DiscoveredContact discovered_[MAX_UI_ADVERTS]{};
    ContactInfo detail_contact_{};bool detail_valid_=false;bool detail_saved_=false;
    // Keep transient GPS telemetry provenance separate from MeshCore's contact
    // timestamps. LAST HEARD comes from ContactInfo::lastmod; LAST ADVERT stays
    // ContactInfo::last_advert_timestamp.
    struct RecentInfo {
        uint8_t key[PUB_KEY_SIZE]{};
        uint32_t gps_reply_millis=0; // local receipt, not the remote fix time
        int32_t lat=0,lon=0;
        bool heard=false,has_gps=false;
    } recent_info_{};
    char detail_identity_[24]{},detail_seen_[72]{},detail_advert_age_[72]{};
    char detail_position_source_[72]{},detail_route_[40]{},detail_position_[64]{};
    char detail_access_[20]="NOT LOGGED IN";
    char detail_role_[20]="UNKNOWN";
    char detail_status_[320]="NOT REQUESTED",detail_telemetry_[120]="NOT REQUESTED",detail_path_[64]="NOT REQUESTED",detail_trace_[240]="NOT REQUESTED";
    bool detail_request_active_=false,detail_login_active_=false,detail_authenticated_=false,request_gps_received_=false;
    UiNodeInfoRequest detail_request_type_=UiNodeInfoRequest::None;int32_t detail_lat_=0,detail_lon_=0;
    uint8_t detail_frame_[192]{};uint8_t detail_frame_len_=0;
    MeshInkMessageStore& store_=meshink_message_store();
    static void bind(ListStorage& item){item.entry.title=item.title;item.entry.subtitle=item.subtitle;item.entry.time=item.time;}
    static void format_short_age(uint32_t seconds,char* out,size_t len){
        if(seconds<60)snprintf(out,len,"JUST NOW");
        else if(seconds<3600)snprintf(out,len,"%luM AGO",(unsigned long)(seconds/60));
        else if(seconds<86400)snprintf(out,len,"%luH AGO",(unsigned long)(seconds/3600));
        else snprintf(out,len,"%luD AGO",(unsigned long)(seconds/86400));
    }
    static void bind(MessageView& item){item.entry.text=item.text;item.entry.time=item.time;item.entry.network=item.network;}
    void format_message_network(const StoredMessage& stored,char* out,size_t len)const{
        if(!out||!len)return;
        out[0]=0;
        const UiMessageState state=(UiMessageState)stored.state;
        if(state!=UiMessageState::Received){
            if(stored.kind==(uint8_t)MessageKind::Channel&&stored.repeats){
                snprintf(out,len,"HEARD %u REPEAT%s",(unsigned)stored.repeats,stored.repeats==1?"":"S");
                return;
            }
            const bool route_known=(stored.flags&MESHINK_MESSAGE_ROUTE_KNOWN)!=0;
            const char* route=(stored.flags&MESHINK_MESSAGE_ROUTE_FLOOD)?"FLOOD":"DIRECT";
            if(state==UiMessageState::Retrying1||state==UiMessageState::Retrying2){
                const unsigned retry=state==UiMessageState::Retrying1?1U:2U;
                if(route_known)snprintf(out,len,"RETRYING %s %u/2",route,retry);
                else snprintf(out,len,"RETRYING %u/2",retry);
                return;
            }
            if(state==UiMessageState::Retrying3){
                if(route_known)snprintf(out,len,"FINAL %s",route);
                else {strncpy(out,"FINAL",len-1);out[len-1]=0;}
                return;
            }
            const char* base=(stored.kind==(uint8_t)MessageKind::Direct&&
                              state==UiMessageState::Sent)?"SENDING":state_text(state);
            if(route_known&&base[0]&&state!=UiMessageState::Failed)
                snprintf(out,len,"%s %s",base,route);
            else if(base[0]){strncpy(out,base,len-1);out[len-1]=0;}
            return;
        }
        if(stored.flags&MESHINK_MESSAGE_HAS_RX){
            const uint8_t hops=stored.path_len&0x3F;
            if(stored.path_len==OUT_PATH_UNKNOWN)
                snprintf(out,len,"SNR %.1f DB  DIRECT",stored.snr_q4/4.0f);
            else
                snprintf(out,len,"SNR %.1f DB  %u HOP%s",stored.snr_q4/4.0f,
                         (unsigned)hops,hops==1?"":"S");
        }
    }
    bool matches(const StoredMessage& m)const{return meshink_message_protocol(m)==0&&m.kind==(uint8_t)(active_channel_?MessageKind::Channel:MessageKind::Direct)&&memcmp(m.key,active_key_,active_channel_?1:6)==0;}
    bool has_recent_info(const uint8_t* full_key)const {
        return recent_info_.heard&&memcmp(recent_info_.key,full_key,PUB_KEY_SIZE)==0;
    }
    void note_heard(const uint8_t* key,size_t key_len) {
        if(!key||!key_len)return;
        ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(key,key_len);
        if(!contact)return;
        const uint32_t heard=meshink_rtc_current_time();
        if(!heard)return;
        contact->lastmod=heard; // our clock: this T5 positively heard this contact
        local_mesh_schedule_contacts_save();
        if(detail_valid_&&!memcmp(detail_contact_.id.pub_key,contact->id.pub_key,PUB_KEY_SIZE))
            detail_contact_.lastmod=heard;
    }
    void note_info_reply() {
        // Only a matched response proves that this node transmitted back to us.
        // Mirror MeshCore's lastmod semantics without touching LAST ADVERT.
        note_heard(detail_contact_.id.pub_key,PUB_KEY_SIZE);
        if(!has_recent_info(detail_contact_.id.pub_key)){
            recent_info_={};
            memcpy(recent_info_.key,detail_contact_.id.pub_key,PUB_KEY_SIZE);
        }
        recent_info_.heard=true;
    }
    uint32_t contacts_signature()const{
        uint32_t hash=2166136261UL;
        for(size_t i=0;i<contact_count_;++i){
            for(uint8_t b:contacts_[i].key){hash^=b;hash*=16777619UL;}
            for(const char* p=contacts_[i].title;*p;++p){hash^=(uint8_t)*p;hash*=16777619UL;}
            hash^=(uint8_t)contacts_[i].entry.role;hash*=16777619UL;
        }
        hash^=(uint32_t)contact_count_;hash*=16777619UL;
        return hash;
    }
    void rebuild_conversations(uint32_t contact_signature){
        int16_t latest[MAX_UI_CONTACTS];
        for(auto& index:latest)index=-1;
        StoredMessage item{};

        // One linear journal pass finds the newest direct record for every
        // visible contact. This replaces the old N-contacts x N-messages scan.
        for(size_t logical=0;logical<store_.count();++logical){
            if(!store_.read(logical,item)||
               meshink_message_protocol(item)!=0||
               item.kind!=(uint8_t)MessageKind::Direct||item.sequence==0)continue;
            for(size_t contact=0;contact<contact_count_;++contact){
                if(!memcmp(item.key,contacts_[contact].key,6)){
                    latest[contact]=(int16_t)logical;
                    break;
                }
            }
        }

        conversation_count_=0;
        for(size_t contact=0;contact<contact_count_&&
                conversation_count_<MAX_UI_CONTACTS+MAX_UI_CHANNELS;++contact){
            if(latest[contact]<0||!store_.read((size_t)latest[contact],item))continue;
            auto& summary=conversations_[conversation_count_++];
            summary=ListStorage{};bind(summary);
            strncpy(summary.title,contacts_[contact].title,sizeof(summary.title)-1);
            strncpy(summary.subtitle,item.text,sizeof(summary.subtitle)-1);
            format_time(item.timestamp,summary.time);
            memcpy(summary.key,contacts_[contact].key,sizeof(summary.key));
            summary.entry.unread=direct_unread_count(summary.key);
            summary.entry.role=contacts_[contact].entry.role;
        }
        conversation_store_revision_=store_.revision();
        conversation_contacts_signature_=contact_signature;
    }
    void refresh_conversation_labels(){
        for(size_t i=0;i<conversation_count_;++i){
            conversations_[i].entry.unread=direct_unread_count(conversations_[i].key);
            for(size_t contact=0;contact<contact_count_;++contact){
                if(memcmp(conversations_[i].key,contacts_[contact].key,6))continue;
                strncpy(conversations_[i].title,contacts_[contact].title,
                        sizeof(conversations_[i].title)-1);
                conversations_[i].entry.role=contacts_[contact].entry.role;
                break;
            }
        }
    }
    uint8_t direct_unread_count(const uint8_t* key)const{
        if(!key)return 0;
        for(const auto& item:direct_unread_)
            if(item.used&&!memcmp(item.key,key,6))return item.count;
        return 0;
    }
    UnreadPeer* direct_unread_peer(const uint8_t* key,bool create){
        if(!key)return nullptr;
        for(auto& item:direct_unread_)
            if(item.used&&!memcmp(item.key,key,6))return &item;
        if(!create)return nullptr;
        for(auto& item:direct_unread_)if(!item.used){
            item=UnreadPeer{};
            item.used=true;
            memcpy(item.key,key,6);
            return &item;
        }
        Serial.println("[T5-STORE] WARNING unread peer cache full");
        return nullptr;
    }
    void sync_unread_status(){
        uint16_t direct_total=0,channel_total=0;
        for(const auto& item:direct_unread_)direct_total+=item.count;
        for(const auto count:channel_unread_)channel_total+=count;
        ui_status_set_unread(direct_total);
        ui_status_set_channel_unread(channel_total);
    }
    void rebuild_unread_from_journal(){
        memset(direct_unread_,0,sizeof(direct_unread_));
        memset(channel_unread_,0,sizeof(channel_unread_));
        StoredMessage item{};
        for(size_t i=0;i<store_.count();++i){
            if(!store_.read(i,item)||item.sequence==0||meshink_message_protocol(item)!=0)continue;
            const bool read_through=(item.flags&MESHINK_MESSAGE_READ_THROUGH)!=0;
            const bool unread=(item.flags&MESHINK_MESSAGE_UNREAD)!=0;
            if(item.kind==(uint8_t)MessageKind::Direct){
                // Old build-39 records have neither bit and are deliberately
                // treated as read. Never allocate cache slots for them.
                if(read_through){
                    if(auto* peer=direct_unread_peer(item.key,false))*peer=UnreadPeer{};
                }else if(unread){
                    if(auto* peer=direct_unread_peer(item.key,true))
                        if(peer->count<255)++peer->count;
                }
            }else if(item.kind==(uint8_t)MessageKind::Channel){
                const uint8_t channel=item.key[0];
                if(channel>=MAX_UI_CHANNELS)continue;
                if(read_through)channel_unread_[channel]=0;
                else if(unread&&channel_unread_[channel]<255)++channel_unread_[channel];
            }
        }
        for(size_t i=0;i<contact_count_;++i)
            contacts_[i].entry.unread=direct_unread_count(contacts_[i].key);
        for(size_t i=0;i<conversation_count_;++i)
            conversations_[i].entry.unread=direct_unread_count(conversations_[i].key);
        for(size_t i=0;i<channel_count_;++i){
            const uint8_t channel=channels_[i].channel_index;
            channels_[i].entry.unread=channel<MAX_UI_CHANNELS?channel_unread_[channel]:0;
        }
        // The journal-derived totals are the UI truth as well: status bar,
        // bottom-nav dots and standby summary all follow the same counters.
        sync_unread_status();
    }
    void mark_read(MessageKind kind,const uint8_t* key,size_t key_len){
        const uint8_t pending=kind==MessageKind::Direct
            ?direct_unread_count(key)
            :(key&&key[0]<MAX_UI_CHANNELS?channel_unread_[key[0]]:0);
        if(!pending)return;
        if(!store_.mark_read_through(kind,key,key_len))
            Serial.println("[T5-STORE] ERROR persisting unread read-through marker");
        rebuild_unread_from_journal();
    }
    void rebuild_active(){
        active_count_=0;
        StoredMessage item{};
        for(size_t i=0;i<store_.count()&&active_count_<MESHINK_MESSAGE_CAPACITY;++i){
            if(!store_.read(i,item)||item.sequence==0||!matches(item))continue;
            active_indices_[active_count_++]=(uint16_t)i;
        }
        ++active_revision_;
    }
    bool activate(const ListStorage& item,bool channel){active_channel_=channel;detail_valid_=false;detail_frame_len_=0;detail_request_active_=false;detail_login_active_=false;detail_authenticated_=false;detail_request_type_=UiNodeInfoRequest::None;request_gps_received_=false;strcpy(detail_status_,"NOT REQUESTED");strcpy(detail_telemetry_,"NOT REQUESTED");strcpy(detail_path_,"NOT REQUESTED");strcpy(detail_trace_,"NOT REQUESTED");memcpy(active_key_,item.key,sizeof(active_key_));strncpy(active_title_,item.title,sizeof(active_title_)-1);rebuild_active();return true;}
public:
    MeshCoreUiProvider(){
        for(auto& i:contacts_)bind(i);
        for(auto& i:channels_)bind(i);
        for(auto& i:conversations_)bind(i);
        for(auto& i:adverts_)bind(i);
        bind(active_message_view_);
    }
    void begin(){
        if(!store_.begin())return;
        rebuild_unread_from_journal();
        Serial.printf("[T5-STORE] unread restored direct=%u channel=%u\n",
                      (unsigned)direct_unread_total(),
                      (unsigned)channel_unread_total());
        refresh(true);
    }
    void heard(const uint8_t* key,size_t key_len){note_heard(key,key_len);}
    void refresh(bool force=false){
        const uint32_t interval=ui_is_standby()?60000:10000;
        if(!force&&millis()-refreshed_at_<interval)return;
        refreshed_at_=millis();
        contact_count_=channel_count_=advert_count_=0;
        ContactInfo contact{};auto iterator=meshink_meshcore().startContactsIterator();
        while(contact_count_<MAX_UI_CONTACTS&&iterator.hasNext(&meshink_meshcore(),contact)){
            auto& item=contacts_[contact_count_++];
            item=ListStorage{};
            bind(item);
            strncpy(item.title,contact.name[0]?contact.name:"UNNAMED NODE",sizeof(item.title)-1);
            char role[20]{},heard[72]{};
            format_node_role(contact.type,role,sizeof(role));
            format_last_heard(contact.lastmod,heard);
            snprintf(item.subtitle,sizeof(item.subtitle),"%s  HEARD %.43s",role,heard);
            if(contact.lastmod)format_time(contact.lastmod,item.time);
            else strcpy(item.time,"--:--");
            memcpy(item.key,contact.id.pub_key,7);
            item.entry.unread=direct_unread_count(item.key);
            item.entry.role=meshcore_ui_role(contact.type);

        }
        const uint32_t contact_signature=contacts_signature();
        if(conversation_store_revision_!=store_.revision()||
           conversation_contacts_signature_!=contact_signature)
            rebuild_conversations(contact_signature);
        else refresh_conversation_labels();

        for(int i=0;i<MAX_GROUP_CHANNELS&&channel_count_<MAX_UI_CHANNELS;++i){ChannelDetails ch{};if(!meshink_meshcore().getChannel(i,ch)||!ch.name[0])continue;
            auto& item=channels_[channel_count_++];
            item=ListStorage{};
            bind(item);
            snprintf(item.title,sizeof(item.title),"# %s",ch.name);
            strncpy(item.subtitle,"MESHCORE CHANNEL",sizeof(item.subtitle)-1);
            item.key[0]=i;
            item.channel_index=i;
            item.entry.unread=i<MAX_UI_CHANNELS?channel_unread_[i]:0;
        }
        AdvertPath heard[MAX_UI_ADVERTS]{};const int heard_count=meshink_meshcore().getRecentlyHeard(heard,MAX_UI_ADVERTS);
        for(int i=0;i<heard_count&&advert_count_<MAX_UI_ADVERTS;++i){if(!heard[i].recv_timestamp||!heard[i].name[0])continue;auto& item=adverts_[advert_count_++];item=ListStorage{};bind(item);
            const uint8_t hops=heard[i].path_len&63;uint8_t node_type=ADV_TYPE_NONE;
            if(const auto* saved=meshink_meshcore().lookupContactByPubKey(heard[i].pubkey_prefix,7))node_type=saved->type;
            else for(const auto& discovered:discovered_)if(discovered.len&&!memcmp(discovered.prefix,heard[i].pubkey_prefix,7)){const size_t type_offset=1+PUB_KEY_SIZE;if(discovered.len>type_offset)node_type=discovered.frame[type_offset];break;}
            char role[20]{};format_node_role(node_type,role,sizeof(role));
            strncpy(item.title,heard[i].name,sizeof(item.title)-1);snprintf(item.subtitle,sizeof(item.subtitle),hops?"%s  ADVERT  %u HOP%s":"%s  ADVERT  ZERO HOP",role,hops,hops==1?"":"S");format_time(heard[i].recv_timestamp,item.time);memcpy(item.key,heard[i].pubkey_prefix,7);item.entry.role=meshcore_ui_role(node_type);
            T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] advert '%s' type=%u path=0x%02x hops=%u\n",item.title,node_type,heard[i].path_len,hops);
        }
        // MeshCore's saved contacts are the single source of last-known GPS.
        // Traverse independently of the 16-contact list screen limit.
        map_node_count_=0;
        ContactInfo positioned{};auto positions=meshink_meshcore().startContactsIterator();
        while(map_node_count_<MAX_MAP_NODES&&positions.hasNext(&meshink_meshcore(),positioned)) {
            if((!positioned.gps_lat&&!positioned.gps_lon)||
               positioned.gps_lat < -85051100 || positioned.gps_lat > 85051100 ||
               positioned.gps_lon < -180000000 || positioned.gps_lon > 180000000)continue;
            UiMapNode& item=map_nodes_[map_node_count_++];item=UiMapNode{};
            strncpy(item.name,positioned.name[0]?positioned.name:"UNNAMED",sizeof(item.name)-1);
            memcpy(item.key,positioned.id.pub_key,sizeof(item.key));
            item.latitude=positioned.gps_lat;item.longitude=positioned.gps_lon;
            item.role=meshcore_ui_role(positioned.type);
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
                if(const ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(
                        recent_info_.key,PUB_KEY_SIZE)){
                    strncpy(item.name,contact->name[0]?contact->name:"UNNAMED",
                            sizeof(item.name)-1);
                    item.role=meshcore_ui_role(contact->type);
                    item.advertised_at=contact->last_advert_timestamp;
                    memcpy(item.key,recent_info_.key,sizeof(item.key));
                    item.latitude=recent_info_.lat;
                    item.longitude=recent_info_.lon;
                }else --map_node_count_;
            }
        }
    }
    void received_direct(const uint8_t* key,uint32_t timestamp,const char* text,bool has_rf=false,int8_t snr_q4=0,uint8_t path_len=OUT_PATH_UNKNOWN){
        note_heard(key,6); // includes CLI/direct payloads that upstream does not bump
        const bool already_seen=ui_chat_is_visible(false)&&!active_channel_&&!memcmp(active_key_,key,6);
        const bool unread=!already_seen;
        const bool journal_full=store_.count()>=MESHINK_MESSAGE_CAPACITY;
        const uint32_t sequence=store_.append(
            MessageKind::Direct,key,6,text,timestamp,UiMessageState::Received,0,
            MeshInkMessageOrigin::LocalUi,has_rf,snr_q4,path_len,unread);
        Serial.printf("[T5-STORE] RX direct journal seq=%lu count=%u ts=%lu unread=%u result=%s\n",
                      (unsigned long)sequence,(unsigned)store_.count(),
                      (unsigned long)timestamp,unread?1U:0U,sequence?"OK":"FAIL");
        if(sequence){
            if(journal_full)rebuild_unread_from_journal();
            else{
                if(unread){
                    if(auto* peer=direct_unread_peer(key,true))
                        if(peer->count<255)++peer->count;
                }
                sync_unread_status();
            }
            if(!active_channel_&&!memcmp(active_key_,key,6))rebuild_active();
        }
        refresh(true);ui_notify_message_received(false);
    }
    void received_channel(uint8_t channel,uint32_t timestamp,const char* text,bool has_rf=false,int8_t snr_q4=0,uint8_t path_len=OUT_PATH_UNKNOWN){
        const bool already_seen=ui_chat_is_visible(true)&&active_channel_&&active_key_[0]==channel;
        const bool unread=!already_seen;
        const bool journal_full=store_.count()>=MESHINK_MESSAGE_CAPACITY;
        const uint32_t sequence=store_.append(
            MessageKind::Channel,&channel,1,text,timestamp,UiMessageState::Received,0,
            MeshInkMessageOrigin::LocalUi,has_rf,snr_q4,path_len,unread);
        Serial.printf("[T5-STORE] RX channel journal seq=%lu count=%u ts=%lu unread=%u result=%s\n",
                      (unsigned long)sequence,(unsigned)store_.count(),
                      (unsigned long)timestamp,unread?1U:0U,sequence?"OK":"FAIL");
        if(sequence){
            if(journal_full)rebuild_unread_from_journal();
            else{
                if(unread&&channel<MAX_UI_CHANNELS&&channel_unread_[channel]<255)
                    ++channel_unread_[channel];
                sync_unread_status();
            }
            if(active_channel_&&active_key_[0]==channel)rebuild_active();
        }
        refresh(true);ui_notify_message_received(true);
    }
    uint32_t sent(const char* text,uint32_t timestamp,uint32_t ack){
        const bool journal_full=store_.count()>=MESHINK_MESSAGE_CAPACITY;
        const uint32_t sequence=store_.append(
            active_channel_?MessageKind::Channel:MessageKind::Direct,
            active_key_,active_channel_?1:6,text,timestamp,UiMessageState::Sent,ack);
        if(sequence&&journal_full)rebuild_unread_from_journal();
        rebuild_active();
        return sequence;
    }
    uint32_t queue_direct(const char* text,uint32_t timestamp){
        const bool journal_full=store_.count()>=MESHINK_MESSAGE_CAPACITY;
        const uint32_t sequence=store_.append(
            MessageKind::Direct,active_key_,6,text,timestamp,UiMessageState::Sending);
        if(sequence&&journal_full)rebuild_unread_from_journal();
        transient_direct_sequence_=sequence;
        transient_direct_state_=UiMessageState::Sending;
        transient_direct_route_known_=false;
        transient_direct_route_flood_=false;
        rebuild_active();
        return sequence;
    }
    void transient_direct_status(uint32_t sequence,UiMessageState state,
                                 bool route_known=false,bool route_flood=false){
        if(!sequence||sequence!=transient_direct_sequence_)return;
        const bool changed=transient_direct_state_!=state||
                           transient_direct_route_known_!=route_known||
                           (route_known&&transient_direct_route_flood_!=route_flood);
        transient_direct_state_=state;
        transient_direct_route_known_=route_known;
        transient_direct_route_flood_=route_flood;
        if(changed)++active_revision_;
        ui_request_data_refresh("message-transient");
    }
    void clear_transient_direct(uint32_t sequence){
        if(!sequence||sequence!=transient_direct_sequence_)return;
        transient_direct_sequence_=0;
        transient_direct_state_=UiMessageState::Sending;
        transient_direct_route_known_=false;
        transient_direct_route_flood_=false;
    }
    bool update_message(uint32_t sequence,UiMessageState state){
        if(!sequence||!store_.update_state(sequence,state))return false;
        clear_transient_direct(sequence);
        ++active_revision_;
        ui_request_data_refresh("message-state");
        return true;
    }
    void confirm_direct_send(uint32_t sequence,uint32_t ack,bool flood,UiMessageState state){
        if(sequence){
            store_.update_outgoing(sequence,state,ack,flood);
            clear_transient_direct(sequence);
            ++active_revision_;
        }
        ui_request_data_refresh("message-route");
    }
    void note_channel_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4){
        if(sequence){store_.update_repeat(sequence,repeats,snr_q4);++active_revision_;}ui_request_data_refresh("channel-repeat");
    }
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
    bool open_conversation(size_t i)override{if(i>=conversation_count_)return false;mark_read(MessageKind::Direct,conversations_[i].key,6);return activate(conversations_[i],false);}
    size_t contact_count()const override{return contact_count_;}const UiListEntry& contact(size_t i)const override{return contacts_[i].entry;}bool open_contact(size_t i)override{if(i>=contact_count_)return false;mark_read(MessageKind::Direct,contacts_[i].key,6);return activate(contacts_[i],false);}
    size_t channel_count()const override{return channel_count_;}
    const UiListEntry& channel(size_t i)const override{
        static UiListEntry empty{};
        return i<channel_count_?channels_[i].entry:empty;
    }
    bool open_channel(size_t i)override{
        if(i>=channel_count_)return false;
        mark_read(MessageKind::Channel,&channels_[i].channel_index,1);
        return activate(channels_[i],true);
    }
    size_t channel_name_limit()const override{return sizeof(ChannelDetails::name)-1;}
    size_t channel_capacity()const override{return MAX_GROUP_CHANNELS;}
    bool channel_management_available()const override{return true;}
    bool channel_removable(size_t index)const override{
        return index<channel_count_&&channels_[index].channel_index!=0;
    }
    bool create_channel(const char* name,const char* key_hex)override{
        if(!meshink_channel_key::valid_name(name,channel_name_limit())||!key_hex||
           (key_hex[0]&&strlen(key_hex)!=32))return false;
        int free_slot=-1;
        // Slot 0 is reserved for the public channel, even if not initialized.
        for(int i=1;i<MAX_GROUP_CHANNELS;++i){
            ChannelDetails existing{};
            if(!meshink_meshcore().getChannel(i,existing))return false;
            if(existing.name[0]&&!strcmp(existing.name,name))return false;
            if(free_slot<0&&!existing.name[0])free_slot=i;
        }
        if(free_slot<0)return false;
        ChannelDetails created{};
        strncpy(created.name,name,sizeof(created.name)-1);
        size_t key_len=0;
        if(!key_hex[0]){
            // Use a 128-bit group key for MeshCore companion compatibility.
            // Keep the unused high 16 bytes zero, matching its 128-bit format.
            esp_fill_random(created.channel.secret,16);
        }else{
            if(!meshink_channel_key::parse_hex(key_hex,created.channel.secret,key_len))
                return false;
        }
        if(!meshink_meshcore().setChannel(free_slot,created))return false;
        local_mesh_save_channels_now();
        refresh(true);
        return true;
    }
    bool delete_channel(size_t index)override{
        if(index>=channel_count_)return false;
        const uint8_t slot=channels_[index].channel_index;
        if(slot==0)return false; // Never remove the default public channel.
        ChannelDetails erased{};
        if(!meshink_meshcore().setChannel(slot,erased))return false;
        local_mesh_save_channels_now();
        if(active_channel_&&active_key_[0]==slot){
            active_channel_=false;
            active_count_=0;
            ++active_revision_;
        }
        refresh(true);
        return true;
    }
    size_t advert_count()const override{return advert_count_;}const UiListEntry& advert(size_t i)const override{return adverts_[i].entry;}
    bool cache_discovered(const uint8_t* frame,size_t len){
        if(!frame||len<DISCOVERED_CONTACT_BASE_LEN||len>sizeof(discovered_[0].frame)){
            Serial.printf("[T5-MESH] rejected malformed new-advert frame bytes=%u expected=%u..%u\n",
                          (unsigned)len,(unsigned)DISCOVERED_CONTACT_BASE_LEN,
                          (unsigned)sizeof(discovered_[0].frame));
            return false;
        }
        DiscoveredContact* slot=nullptr;
        for(auto& item:discovered_)if(item.len&&!memcmp(item.prefix,frame+1,7)){slot=&item;break;}
        if(!slot)for(auto& item:discovered_)if(!item.len){slot=&item;break;}
        if(!slot)slot=&discovered_[0];
        *slot=DiscoveredContact{};
        memcpy(slot->prefix,frame+1,7);
        memcpy(slot->frame,frame,len);
        slot->len=(uint8_t)len;
        return true;
    }
    bool open_advert(size_t i)override{
        if(i>=advert_count_)return false;
        detail_valid_=false;
        detail_saved_=false;
        detail_frame_len_=0;
        if(auto* saved=meshink_meshcore().lookupContactByPubKey(adverts_[i].key,7)){detail_contact_=*saved;detail_valid_=detail_saved_=true;return true;}
        for(const auto& item:discovered_)if(item.len&&!memcmp(item.prefix,adverts_[i].key,7)){
            if(item.len<DISCOVERED_CONTACT_BASE_LEN){
                Serial.printf("[T5-MESH] cached advert rejected at open bytes=%u expected>=%u\n",
                              (unsigned)item.len,(unsigned)DISCOVERED_CONTACT_BASE_LEN);
                return false;
            }
            detail_contact_=ContactInfo{};
            memcpy(detail_frame_,item.frame,item.len);detail_frame_len_=item.len;size_t p=1;
            memcpy(detail_contact_.id.pub_key,item.frame+p,PUB_KEY_SIZE);p+=PUB_KEY_SIZE;detail_contact_.type=item.frame[p++];detail_contact_.flags=item.frame[p++];detail_contact_.out_path_len=item.frame[p++];
            memcpy(detail_contact_.out_path,item.frame+p,MAX_PATH_SIZE);p+=MAX_PATH_SIZE;memcpy(detail_contact_.name,item.frame+p,32);detail_contact_.name[31]=0;p+=32;
            memcpy(&detail_contact_.last_advert_timestamp,item.frame+p,4);p+=4;
            if(item.len>=p+8){memcpy(&detail_contact_.gps_lat,item.frame+p,4);p+=4;memcpy(&detail_contact_.gps_lon,item.frame+p,4);p+=4;if(item.len>=p+4)memcpy(&detail_contact_.lastmod,item.frame+p,4);}
            detail_valid_=true;return true;
        }return false;
    }
    bool active_node_details(UiNodeDetails& out)const override{
        auto* self=const_cast<MeshCoreUiProvider*>(this);if(!detail_valid_){ContactInfo contact{};if(!active_contact(contact))return false;self->detail_contact_=contact;self->detail_valid_=self->detail_saved_=true;}
        // Re-read the contact on each UI render: adverts, messages, paths and
        // matched replies may update its stored position/timestamps while Info is open.
        if(detail_saved_) {
            if(const ContactInfo* current=meshink_meshcore().lookupContactByPubKey(
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
        format_last_heard(detail_contact_.lastmod,self->detail_seen_);
        const bool recent=has_recent_info(detail_contact_.id.pub_key);
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
                         sizeof(self->detail_position_source_),"TELEMETRY POSITION %s",age);
            }else strcpy(self->detail_position_source_,"SAVED POSITION");
        }else{
            strcpy(self->detail_position_,"NO SAVED POSITION");
            strcpy(self->detail_position_source_,"NO GPS REPORTED");
        }
        format_node_role(detail_contact_.type,self->detail_role_,sizeof(self->detail_role_));
        uint32_t capabilities=UI_NODE_CAP_TELEMETRY|UI_NODE_CAP_PATH|UI_NODE_CAP_TRACE;
        if(detail_contact_.type==ADV_TYPE_REPEATER||detail_contact_.type==ADV_TYPE_ROOM)
            capabilities|=UI_NODE_CAP_STATUS|UI_NODE_CAP_LOGIN;
        out={detail_contact_.name,self->detail_identity_,self->detail_seen_,
             self->detail_route_,self->detail_position_,self->detail_status_,
             self->detail_telemetry_,self->detail_path_,self->detail_trace_,self->detail_lat_,
             self->detail_lon_,detail_request_active_,detail_request_type_,
             detail_login_active_,detail_authenticated_,self->detail_access_,self->detail_role_,
             meshcore_ui_role(detail_contact_.type),capabilities,detail_saved_,
             self->detail_advert_age_,self->detail_position_source_};
        return true;
    }
    bool add_active_node()override{if(!detail_valid_||detail_saved_||!detail_frame_len_)return false;detail_frame_[0]=9;if(!local_mesh_enqueue_command(detail_frame_,detail_frame_len_))return false;detail_saved_=true;return true;}
    bool remove_active_contact()override{if(!detail_valid_||!detail_saved_)return false;uint8_t command[1+PUB_KEY_SIZE]{15};memcpy(command+1,detail_contact_.id.pub_key,PUB_KEY_SIZE);if(!local_mesh_enqueue_command(command,sizeof(command)))return false;detail_saved_=false;return true;}
    bool request_active_node_info(UiNodeInfoRequest request)override;
    bool login_active_node(const char* password,bool save_password)override;
    bool active_node_saved_password(char* out,size_t len)const override{return detail_valid_&&load_saved_password(detail_contact_.id.pub_key,out,len);}
    const uint8_t* detail_key()const{return detail_contact_.id.pub_key;}
    void request_state(bool active,UiNodeInfoRequest request=UiNodeInfoRequest::None){detail_request_active_=active;detail_request_type_=active?request:UiNodeInfoRequest::None;ui_request_data_refresh("node-info");}
    void request_timeout(UiNodeInfoRequest request){
        char* out=request==UiNodeInfoRequest::Status?detail_status_:
                  request==UiNodeInfoRequest::Telemetry?detail_telemetry_:
                  request==UiNodeInfoRequest::Trace?detail_trace_:detail_path_;
        strcpy(out,"NO RESPONSE / NOT ALLOWED");
        if(request==UiNodeInfoRequest::Telemetry)ui_notify_node_position_unavailable();
    }
    void login_state(bool active){detail_login_active_=active;ui_request_data_refresh("node-login");}
    void login_result(bool success,uint8_t permissions=0,bool role_known=false){
        detail_login_active_=false;detail_authenticated_=success;
        if(success){
            if(role_known){
                switch(permissions&0x03){
                    case 0:strcpy(detail_access_,"GUEST");break;
                    case 1:strcpy(detail_access_,"READ ONLY");break;
                    case 2:strcpy(detail_access_,"READ/WRITE");break;
                    case 3:strcpy(detail_access_,"ADMIN");break;
                }
            }else strcpy(detail_access_,"LEGACY LOGIN");
        }else{strcpy(detail_access_,"NOT LOGGED IN");strcpy(detail_status_,"LOGIN FAILED");}
        ui_request_data_refresh("node-login");
    }
    void status_response(const uint8_t* data,size_t len){
        note_info_reply();
        const bool repeater=detail_contact_.type==ADV_TYPE_REPEATER;
        const bool room=detail_contact_.type==ADV_TYPE_ROOM;
        if((repeater&&len>=56)||(room&&len>=52)){
            uint16_t batt=0,queue=0,errors=0,direct_dups=0,flood_dups=0;int16_t noise=0,rssi=0,snr4=0;
            uint32_t rx=0,tx=0,tx_air=0,uptime=0,sent_flood=0,sent_direct=0,recv_flood=0,recv_direct=0;
            memcpy(&batt,data+0,2);memcpy(&queue,data+2,2);memcpy(&noise,data+4,2);memcpy(&rssi,data+6,2);
            memcpy(&rx,data+8,4);memcpy(&tx,data+12,4);memcpy(&tx_air,data+16,4);memcpy(&uptime,data+20,4);
            memcpy(&sent_flood,data+24,4);memcpy(&sent_direct,data+28,4);memcpy(&recv_flood,data+32,4);memcpy(&recv_direct,data+36,4);
            memcpy(&errors,data+40,2);memcpy(&snr4,data+42,2);memcpy(&direct_dups,data+44,2);memcpy(&flood_dups,data+46,2);
            const uint32_t days=uptime/86400U,hours=(uptime%86400U)/3600U;
            if(repeater){
                uint32_t rx_air=0,rx_errors=0;memcpy(&rx_air,data+48,4);memcpy(&rx_errors,data+52,4);
                snprintf(detail_status_,sizeof(detail_status_),
                         "BATTERY %.2f V\nUPTIME %luD %luH\nTX QUEUE %u\nNOISE %d DBM\nLAST RSSI %d DBM\nLAST SNR %.1f DB\nPACKETS RX/TX %lu / %lu\nFLOOD RX/TX %lu / %lu\nDIRECT RX/TX %lu / %lu\nRX ERRORS %lu\nDUPLICATES D/F %u / %u\nAIRTIME TX/RX %luS / %luS\nERROR FLAGS 0X%04X",
                         batt/1000.0f,(unsigned long)days,(unsigned long)hours,(unsigned)queue,(int)noise,(int)rssi,snr4/4.0f,
                         (unsigned long)rx,(unsigned long)tx,(unsigned long)recv_flood,(unsigned long)sent_flood,
                         (unsigned long)recv_direct,(unsigned long)sent_direct,(unsigned long)rx_errors,
                         (unsigned)direct_dups,(unsigned)flood_dups,(unsigned long)tx_air,(unsigned long)rx_air,(unsigned)errors);
            }else{
                uint16_t posted=0,pushed=0;memcpy(&posted,data+48,2);memcpy(&pushed,data+50,2);
                snprintf(detail_status_,sizeof(detail_status_),
                         "BATTERY %.2f V\nUPTIME %luD %luH\nTX QUEUE %u\nNOISE %d DBM\nLAST RSSI %d DBM\nLAST SNR %.1f DB\nPACKETS RX/TX %lu / %lu\nFLOOD RX/TX %lu / %lu\nDIRECT RX/TX %lu / %lu\nDUPLICATES D/F %u / %u\nAIRTIME TX %luS\nPOSTS %u\nPUSHES %u\nERROR FLAGS 0X%04X",
                         batt/1000.0f,(unsigned long)days,(unsigned long)hours,(unsigned)queue,(int)noise,(int)rssi,snr4/4.0f,
                         (unsigned long)rx,(unsigned long)tx,(unsigned long)recv_flood,(unsigned long)sent_flood,
                         (unsigned long)recv_direct,(unsigned long)sent_direct,(unsigned)direct_dups,(unsigned)flood_dups,
                         (unsigned long)tx_air,(unsigned)posted,(unsigned)pushed,(unsigned)errors);
            }
        }else snprintf(detail_status_,sizeof(detail_status_),"STATUS RESPONSE  %u BYTES",(unsigned)len);
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] node info: status reply received bytes=%u type=%u\n",(unsigned)len,detail_contact_.type);
    }
    void path_response(const uint8_t* data,size_t len){note_info_reply();if(!len){strcpy(detail_path_,"NO PATH DATA");return;}const uint8_t hops=data[0]&0x3F;snprintf(detail_path_,sizeof(detail_path_),hops?"OUTBOUND %u HOP%s":"DIRECT / ZERO HOP",hops,hops==1?"":"S");T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] node info: path reply received");}
    void trace_response(const uint8_t* frame,size_t len){
        note_info_reply();
        if(!frame||len<13){strcpy(detail_trace_,"INVALID TRACE RESPONSE");return;}
        const uint8_t path_bytes=frame[2],shift=frame[3]&0x03;
        const uint8_t hash_size=1U<<shift;
        if(!hash_size||path_bytes%hash_size){strcpy(detail_trace_,"INVALID TRACE PATH");return;}
        const uint8_t hops=path_bytes>>shift;
        const size_t needed=12U+path_bytes+hops+1U;
        if(len<needed){strcpy(detail_trace_,"SHORT TRACE RESPONSE");return;}
        const uint8_t* hashes=frame+12;
        const int8_t* snrs=(const int8_t*)(frame+12+path_bytes);
        char* cursor=detail_trace_;size_t left=sizeof(detail_trace_);cursor[0]=0;
        int n=snprintf(cursor,left,"TRACE %u HOP%s",(unsigned)hops,hops==1?"":"S");
        if(n<0||(size_t)n>=left)return;
        cursor+=n;left-=n;
        for(uint8_t i=0;i<hops&&left>24;++i){
            char hash[18]{};char* hp=hash;
            for(uint8_t j=0;j<hash_size&&j<8;++j){snprintf(hp,3,"%02X",hashes[i*hash_size+j]);hp+=2;}
            n=snprintf(cursor,left,"\n%u %s  %.1f DB",(unsigned)(i+1),hash,snrs[i]/4.0f);
            if(n<0||(size_t)n>=left)break;
            cursor+=n;left-=n;
        }
        const int8_t final_snr=snrs[hops];
        if(left>20)snprintf(cursor,left,"\nDEST  %.1f DB",final_snr/4.0f);
        ui_request_data_refresh("trace-result");
    }
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
                // A matched GPS telemetry response is the node's latest known
                // position. Store it in MeshCore's ContactInfo so the same
                // coordinates survive reboot and appear in Maps/Node Info.
                if(ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(
                        detail_contact_.id.pub_key,PUB_KEY_SIZE)){
                    contact->gps_lat=recent_info_.lat;
                    contact->gps_lon=recent_info_.lon;
                    detail_contact_.gps_lat=recent_info_.lat;
                    detail_contact_.gps_lon=recent_info_.lon;
                    local_mesh_schedule_contacts_save();
                }
                snprintf(item,sizeof(item),"GPS %.4f %.4f",lat,lon);
            }
        }break;}case LPP_VOLTAGE:reader.readVoltage(v);snprintf(item,sizeof(item),"%.2fV",v);break;case LPP_CURRENT:reader.readCurrent(v);snprintf(item,sizeof(item),"%.3fA",v);break;case LPP_TEMPERATURE:reader.readTemperature(v);snprintf(item,sizeof(item),"%.1fC",v);break;case LPP_RELATIVE_HUMIDITY:reader.readRelativeHumidity(v);snprintf(item,sizeof(item),"%.1f%% RH",v);break;case LPP_BAROMETRIC_PRESSURE:reader.readPressure(v);snprintf(item,sizeof(item),"%.1f HPA",v);break;default:reader.skipData(type);break;}if(item[0]){const int n=snprintf(cursor,left,"%s%s",cursor==detail_telemetry_?"":"  ",item);if(n<0||(size_t)n>=left)break;cursor+=n;left-=n;}}
        if(!detail_telemetry_[0])strcpy(detail_telemetry_,"NO TELEMETRY RETURNED");
        if(request_gps_received_)refresh(true);
        else ui_notify_node_position_unavailable();
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] node info: telemetry reply received bytes=%u gps=%d\n",
                      (unsigned)len,request_gps_received_);
    }
    const char* active_title()const override{return active_title_;}
    bool active_is_channel()const override{return active_channel_;}
    size_t active_message_count()const override{return active_count_;}
    const UiMessage& active_message(size_t i)const override{
        active_message_view_=MessageView{};bind(active_message_view_);
        if(i>=active_count_)return active_message_view_.entry;
        StoredMessage item{};
        if(!store_.read(active_indices_[i],item))return active_message_view_.entry;
        if(item.sequence==transient_direct_sequence_&&
           item.kind==(uint8_t)MessageKind::Direct) {
            item.state=(uint8_t)transient_direct_state_;
            if(transient_direct_route_known_) {
                item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
                if(transient_direct_route_flood_)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
                else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
            } else {
                item.flags&=(uint8_t)~(MESHINK_MESSAGE_ROUTE_KNOWN|
                                      MESHINK_MESSAGE_ROUTE_FLOOD);
            }
        }
        strncpy(active_message_view_.text,item.text,sizeof(active_message_view_.text)-1);
        format_time(item.timestamp,active_message_view_.time);
        active_message_view_.entry.outgoing=item.state!=(uint8_t)UiMessageState::Received;
        active_message_view_.entry.state=(UiMessageState)item.state;
        format_message_network(item,active_message_view_.network,sizeof(active_message_view_.network));
        return active_message_view_.entry;
    }
    uint32_t active_message_revision()const override{return active_revision_;}
    bool active_contact(ContactInfo& out)const{if(active_channel_)return false;auto* found=meshink_meshcore().lookupContactByPubKey(active_key_,6);if(!found)return false;out=*found;return true;}
    uint8_t active_channel_index()const{return active_key_[0];}
    bool active_channel(ChannelDetails& out)const{return active_channel_&&meshink_meshcore().getChannel(active_key_[0],out);}
    uint16_t direct_unread_total()const override{uint16_t total=0;for(const auto& item:direct_unread_)total+=item.count;return total;}
    uint16_t channel_unread_total()const override{uint16_t total=0;for(const auto count:channel_unread_)total+=count;return total;}
};

MeshCoreUiProvider provider;char radio_summary[44]{};char setting_value[20]{};
struct PendingDirect{bool active=false;bool waiting_response=false;bool finalizing_failure=false;uint8_t retry=0;uint32_t sequence=0,timestamp=0,deadline=0;uint32_t acks[DIRECT_RETRY_LIMIT+1]{};bool route_flood[DIRECT_RETRY_LIMIT+1]{};uint8_t key[6]{};char text[MESHINK_MESSAGE_TEXT_BYTES]{};} pending_direct;
struct PendingInfo{bool active=false;bool waiting_sent=false;UiNodeInfoRequest request=UiNodeInfoRequest::None;uint32_t deadline=0,tag=0;uint8_t key[PUB_KEY_SIZE]{};} pending_info;
struct PendingLogin{bool active=false;bool waiting_sent=false;bool save_password=false;uint32_t deadline=0;uint8_t key[PUB_KEY_SIZE]{};char password[16]{};} pending_login;
struct RecentChannelSend{
    bool active=false;
    uint8_t channel=0,repeats=0;
    uint32_t sequence=0,timestamp=0,expires=0;
    char wire_text[MESHINK_MESSAGE_TEXT_BYTES]{};
};
constexpr size_t MAX_RECENT_CHANNEL_SENDS=4;
RecentChannelSend recent_channel_sends[MAX_RECENT_CHANNEL_SENDS]{};
struct PendingStats{bool active=false;uint8_t type=0;uint32_t deadline=0;} pending_stats;
char diagnostics_core[160]="NOT REQUESTED";
char diagnostics_radio[160]="NOT REQUESTED";
char diagnostics_packets[200]="NOT REQUESTED";
int8_t pending_advert=-1;
static bool enqueue_direct_attempt(){
    uint8_t frame[MAX_FRAME_SIZE+1]{};size_t p=0;frame[p++]=2;frame[p++]=0;frame[p++]=pending_direct.retry;memcpy(frame+p,&pending_direct.timestamp,4);p+=4;memcpy(frame+p,pending_direct.key,6);p+=6;const size_t n=min(strlen(pending_direct.text),(size_t)MAX_TEXT_LEN);memcpy(frame+p,pending_direct.text,n);p+=n;
    if(!local_mesh_enqueue_command(frame,p))return false;
    pending_direct.waiting_response=true;
    T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct attempt=%u queued sequence=%lu\n",
              pending_direct.retry,(unsigned long)pending_direct.sequence);
    return true;
}
static bool force_pending_direct_flood(){
    ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(pending_direct.key,6);
    if(!contact)return false;
    contact->out_path_len=OUT_PATH_UNKNOWN;
    T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] direct retries exhausted; reset saved path for flood fallback");
    return true;
}
static bool pending_direct_route(bool& flood){
    ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(pending_direct.key,6);
    if(!contact)return false;
    flood=contact->out_path_len==OUT_PATH_UNKNOWN;
    return true;
}

static size_t channel_message_limit(const char* node_name){
    const size_t name_len=node_name?strlen(node_name):0;
    const size_t prefix_len=name_len+2; // MeshCore sends "<name>: <text>"
    return prefix_len<MAX_TEXT_LEN?(size_t)MAX_TEXT_LEN-prefix_len:0;
}

static bool pending_direct_is_visible_chat(){
    if(!ui_chat_is_visible(false))return false;
    ContactInfo active{};
    return provider.active_contact(active)&&memcmp(active.id.pub_key,pending_direct.key,6)==0;
}

static void fail_pending_direct(const char* reason){
    if(!pending_direct.active)return;
    pending_direct.finalizing_failure=true;
    pending_direct.waiting_response=false;
    if(!provider.update_message(pending_direct.sequence,UiMessageState::Failed)){
        pending_direct.deadline=millis()+250;
        T5_DEBUGF(T5_LOG_MESH,
                  "[T5-MESH] WARN failed-state journal write sequence=%lu; retrying finalization\n",
                  (unsigned long)pending_direct.sequence);
        return;
    }
    const bool restored=pending_direct_is_visible_chat()&&
                        ui_restore_failed_compose(pending_direct.text);
    T5_DEBUGF(T5_LOG_MESH,
              "[T5-MESH] direct failed sequence=%lu retry=%u reason=%s draft_restored=%d journal=FAILED\n",
              (unsigned long)pending_direct.sequence,(unsigned)pending_direct.retry,
              reason?reason:"unknown",restored?1:0);
    pending_direct={};
}
static bool enqueue_info_request(){
    uint8_t frame[MAX_FRAME_SIZE+1]{};size_t len=0;
    if(pending_info.request==UiNodeInfoRequest::Status){frame[0]=27;memcpy(frame+1,pending_info.key,PUB_KEY_SIZE);len=1+PUB_KEY_SIZE;}
    else if(pending_info.request==UiNodeInfoRequest::Telemetry){frame[0]=39;memcpy(frame+4,pending_info.key,PUB_KEY_SIZE);len=4+PUB_KEY_SIZE;}
    else if(pending_info.request==UiNodeInfoRequest::Path){frame[0]=52;frame[1]=0;memcpy(frame+2,pending_info.key,PUB_KEY_SIZE);len=2+PUB_KEY_SIZE;}
    else if(pending_info.request==UiNodeInfoRequest::Trace){
        ContactInfo* contact=meshink_meshcore().lookupContactByPubKey(pending_info.key,PUB_KEY_SIZE);
        if(!contact||contact->out_path_len==OUT_PATH_UNKNOWN)return false;
        const uint8_t hash_size=(contact->out_path_len>>6)+1;
        const uint8_t hash_count=contact->out_path_len&0x3F;
        if(hash_size>2||!hash_count)return false; // upstream TRACE command supports 1/2-byte saved paths here
        const uint8_t path_bytes=hash_count*hash_size;
        uint32_t tag=millis()^0x4D495348UL;
        for(uint8_t i=0;i<4;++i)tag^=((uint32_t)pending_info.key[i])<<(i*8);
        const uint32_t auth=0;
        frame[0]=36;memcpy(frame+1,&tag,4);memcpy(frame+5,&auth,4);
        frame[9]=hash_size==2?1:0;
        memcpy(frame+10,contact->out_path,path_bytes);len=10+path_bytes;
        pending_info.tag=tag;
    }else return false;
    if(!local_mesh_enqueue_command(frame,len))return false;
    pending_info.waiting_sent=true;pending_info.deadline=millis()+30000;return true;
}
static void finish_info(){pending_info.active=false;pending_info.waiting_sent=false;pending_info.request=UiNodeInfoRequest::None;provider.request_state(false);}

static void build_channel_wire_text(char* out,size_t len,const char* node_name,const char* text){
    if(!out||!len)return;
    out[0]=0;
    const int prefix=snprintf(out,len,"%s: ",node_name?node_name:"");
    if(prefix<0||(size_t)prefix>=len)return;
    const size_t max_total=min((size_t)MAX_TEXT_LEN,len-1);
    const size_t used=min((size_t)prefix,max_total);
    const size_t copy=max_total-used;
    strncat(out,text?text:"",copy);
    out[max_total]=0;
}
static void track_channel_send(uint8_t channel,uint32_t timestamp,uint32_t sequence,const char* text){
    const uint32_t now=millis();RecentChannelSend* slot=nullptr;
    for(auto& item:recent_channel_sends)if(!item.active||(int32_t)(now-item.expires)>=0){slot=&item;break;}
    if(!slot){slot=&recent_channel_sends[0];for(auto& item:recent_channel_sends)if((int32_t)(item.expires-slot->expires)<0)slot=&item;}
    *slot=RecentChannelSend{};slot->active=true;slot->channel=channel;slot->timestamp=timestamp;slot->sequence=sequence;slot->expires=now+120000UL;
    build_channel_wire_text(slot->wire_text,sizeof(slot->wire_text),meshink_meshcore().getNodeName(),text);
}
static void handle_raw_repeat(const uint8_t* frame,size_t len){
    if(!frame||len<=3)return;
    const uint32_t now=millis();bool any=false;
    for(auto& item:recent_channel_sends){
        if(item.active&&(int32_t)(now-item.expires)>=0)item.active=false;
        if(item.active)any=true;
    }
    if(!any)return;
    mesh::Packet packet;
    const size_t raw_len=len-3;if(raw_len>255||!packet.readFrom(frame+3,(uint8_t)raw_len)||packet.getPayloadType()!=PAYLOAD_TYPE_GRP_TXT||packet.payload_len<4)return;
    for(auto& item:recent_channel_sends){
        if(!item.active)continue;
        ChannelDetails channel{};
        if(!meshink_meshcore().getChannel(item.channel,channel)||channel.channel.hash[0]!=packet.payload[0])continue;
        uint8_t data[MAX_PACKET_PAYLOAD+1]{};
        const int plain=mesh::Utils::MACThenDecrypt(channel.channel.secret,data,&packet.payload[1],packet.payload_len-1);
        if(plain<5)continue;
        data[min(plain,(int)MAX_PACKET_PAYLOAD)]=0;
        uint32_t timestamp=0;memcpy(&timestamp,data,4);
        if(timestamp!=item.timestamp||data[4]!=0||strcmp((char*)&data[5],item.wire_text))continue;
        if(item.repeats<255)++item.repeats;
        provider.note_channel_repeat(item.sequence,item.repeats,(int8_t)frame[1]);
        break;
    }
}
static bool enqueue_stats_request(uint8_t type){
    const uint8_t frame[2]={56,type};
    if(!local_mesh_enqueue_command(frame,sizeof(frame)))return false;
    pending_stats.active=true;pending_stats.type=type;pending_stats.deadline=millis()+5000UL;return true;
}
static void finish_stats(bool failed=false){
    if(failed){
        if(pending_stats.type==0)strcpy(diagnostics_core,"REQUEST FAILED");
        else if(pending_stats.type==1)strcpy(diagnostics_radio,"REQUEST FAILED");
        else strcpy(diagnostics_packets,"REQUEST FAILED");
    }
    pending_stats.active=false;ui_request_data_refresh("mesh-stats");
}
static void handle_stats_response(const uint8_t* frame,size_t len){
    if(!pending_stats.active||!frame||len<2||frame[0]!=24||frame[1]!=pending_stats.type)return;
    const uint8_t type=frame[1];
    if(type==0&&len>=11){
        uint16_t batt=0,errors=0;uint32_t uptime=0;memcpy(&batt,frame+2,2);memcpy(&uptime,frame+4,4);memcpy(&errors,frame+8,2);const uint8_t queue=frame[10];
        snprintf(diagnostics_core,sizeof(diagnostics_core),"BATTERY %.2f V\nUPTIME %luD %luH\nTX QUEUE %u\nERROR FLAGS 0X%04X",batt/1000.0f,(unsigned long)(uptime/86400U),(unsigned long)((uptime%86400U)/3600U),(unsigned)queue,(unsigned)errors);
    }else if(type==1&&len>=14){
        int16_t noise=0;memcpy(&noise,frame+2,2);const int8_t rssi=(int8_t)frame[4],snr4=(int8_t)frame[5];uint32_t txair=0,rxair=0;memcpy(&txair,frame+6,4);memcpy(&rxair,frame+10,4);
        snprintf(diagnostics_radio,sizeof(diagnostics_radio),"NOISE %d DBM\nLAST RSSI %d DBM\nLAST SNR %.1f DB\nAIRTIME TX/RX %lu / %lu S",(int)noise,(int)rssi,snr4/4.0f,(unsigned long)txair,(unsigned long)rxair);
    }else if(type==2&&len>=30){
        uint32_t rx=0,tx=0,sf=0,sd=0,rf=0,rd=0,err=0;memcpy(&rx,frame+2,4);memcpy(&tx,frame+6,4);memcpy(&sf,frame+10,4);memcpy(&sd,frame+14,4);memcpy(&rf,frame+18,4);memcpy(&rd,frame+22,4);memcpy(&err,frame+26,4);
        snprintf(diagnostics_packets,sizeof(diagnostics_packets),"PACKETS RX/TX %lu / %lu\nFLOOD RX/TX %lu / %lu\nDIRECT RX/TX %lu / %lu\nRX ERRORS %lu",(unsigned long)rx,(unsigned long)tx,(unsigned long)rf,(unsigned long)sf,(unsigned long)rd,(unsigned long)sd,(unsigned long)err);
    }else{finish_stats(true);return;}
    if(type<2){if(!enqueue_stats_request(type+1))finish_stats(true);}
    else finish_stats(false);
}

bool MeshCoreUiProvider::request_active_node_info(UiNodeInfoRequest request){
    if(active_channel_||pending_info.active||pending_login.active||pending_direct.active||request==UiNodeInfoRequest::None)return false;
    ContactInfo contact{};if(!active_contact(contact))return false;
    const bool protected_server=contact.type==ADV_TYPE_REPEATER||contact.type==ADV_TYPE_ROOM;
    if(request==UiNodeInfoRequest::Status&&(!protected_server||!detail_authenticated_))return false;
    if(request==UiNodeInfoRequest::Telemetry&&protected_server&&!detail_authenticated_)return false;
    if(request==UiNodeInfoRequest::Trace){
        if(contact.out_path_len==OUT_PATH_UNKNOWN){strcpy(detail_trace_,"NO SAVED ROUTE\nREQUEST PATH FIRST");ui_request_data_refresh("trace-no-route");return true;}
        const uint8_t hash_size=(contact.out_path_len>>6)+1,hops=contact.out_path_len&0x3F;
        if(!hops){strcpy(detail_trace_,"DIRECT / ZERO HOP\nNO REPEATERS TO TRACE");ui_request_data_refresh("trace-direct");return true;}
        if(hash_size>2){strcpy(detail_trace_,"TRACE UNAVAILABLE FOR\n3-BYTE PATH HASHES");ui_request_data_refresh("trace-hash-size");return true;}
    }
    pending_info={};pending_info.active=true;pending_info.request=request;
    memcpy(pending_info.key,contact.id.pub_key,PUB_KEY_SIZE);
    if(request==UiNodeInfoRequest::Telemetry)request_gps_received_=false;
    char* target=request==UiNodeInfoRequest::Status?detail_status_:
                 request==UiNodeInfoRequest::Telemetry?detail_telemetry_:
                 request==UiNodeInfoRequest::Trace?detail_trace_:detail_path_;
    strcpy(target,"REQUESTING");
    request_state(true,request);
    if(!enqueue_info_request()){finish_info();strcpy(target,"REQUEST FAILED");return false;}
    return true;
}

bool MeshCoreUiProvider::login_active_node(const char* password,bool save_password){
    if(active_channel_||pending_login.active||pending_info.active||pending_direct.active||!password)return false;
    ContactInfo contact{};if(!active_contact(contact)||(contact.type!=ADV_TYPE_REPEATER&&contact.type!=ADV_TYPE_ROOM))return false;
    const size_t password_len=min(strlen(password),(size_t)15);
    uint8_t frame[1+PUB_KEY_SIZE+15]{};frame[0]=26;memcpy(frame+1,contact.id.pub_key,PUB_KEY_SIZE);memcpy(frame+1+PUB_KEY_SIZE,password,password_len);
    if(!local_mesh_enqueue_command(frame,1+PUB_KEY_SIZE+password_len))return false;
    pending_login={};pending_login.active=true;pending_login.waiting_sent=true;pending_login.save_password=save_password;pending_login.deadline=millis()+30000;
    memcpy(pending_login.key,contact.id.pub_key,PUB_KEY_SIZE);memcpy(pending_login.password,password,password_len);pending_login.password[password_len]=0;
    detail_authenticated_=false;strcpy(detail_access_,"LOGGING IN");login_state(true);return true;
}
}

UiDataProvider* local_mesh_provider(){return &provider;}
void local_mesh_refresh_ui_data(){provider.refresh(true);}
void local_mesh_receive_channel_from_core(
        uint8_t channel,uint32_t timestamp,const char* text,
        bool has_rf,int8_t snr_q4,uint8_t path_len){
    provider.received_channel(channel,timestamp,text,has_rf,snr_q4,path_len);
}
void local_mesh_on_frame(const uint8_t* frame,size_t len){
    if(!frame||!len)return;
    char message[MESHINK_MESSAGE_TEXT_BYTES]{};
    if(frame[0]==0x88){handle_raw_repeat(frame,len);return;}
    if(frame[0]==24&&pending_stats.active){handle_stats_response(frame,len);return;}
    if(frame[0]==0x81&&len>=1+PUB_KEY_SIZE){provider.refresh(true);ui_request_data_refresh("route-updated");return;}
    if(frame[0]==0x8A){
        if(provider.cache_discovered(frame,len)){
            provider.refresh(true);
            ui_request_data_refresh("new-advert");
            T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] discovered advert cached with full MeshCore identity");
        }
    }
    else if(pending_login.active&&len>=8&&!memcmp(frame+2,pending_login.key,6)&&frame[0]==0x85){
        PendingLogin completed=pending_login;pending_login={};
        const bool role_known=len>=13;const uint8_t permissions=role_known?frame[12]:(frame[1]?3:0);
        if(completed.save_password)save_password_for(completed.key,completed.password);else clear_saved_password(completed.key);
        memset(completed.password,0,sizeof(completed.password));provider.login_result(true,permissions,role_known);
        const bool status_requested=provider.request_active_node_info(UiNodeInfoRequest::Status);
        T5_DEBUGF(T5_LOG_MESH,
                  "[T5-MESH] server login succeeded role=%u known=%d auto-status=%u\n",
                  (unsigned)(permissions&3),role_known,status_requested?1U:0U);
    }
    else if(pending_login.active&&len>=8&&!memcmp(frame+2,pending_login.key,6)&&frame[0]==0x86){memset(pending_login.password,0,sizeof(pending_login.password));pending_login={};provider.login_result(false);T5_DEBUGLN(T5_LOG_MESH,"[T5-MESH] server login failed");}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Status&&len>=8&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x87){provider.status_response(frame+8,len-8);finish_info();}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Telemetry&&len>=8&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x8B){provider.telemetry_response(frame+8,len-8);finish_info();}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Path&&len>=9&&!memcmp(frame+2,pending_info.key,6)&&frame[0]==0x8D){provider.path_response(frame+8,len-8);finish_info();}
    else if(pending_info.active&&pending_info.request==UiNodeInfoRequest::Trace&&frame[0]==0x89&&len>=13){
        uint32_t tag=0;memcpy(&tag,frame+4,4);if(tag==pending_info.tag){provider.trace_response(frame,len);finish_info();}
    }
    else if(frame[0]==6&&len>=10&&pending_login.active&&pending_login.waiting_sent){uint32_t timeout=0;memcpy(&timeout,frame+6,4);pending_login.deadline=millis()+max((uint32_t)3000,timeout+2000);pending_login.waiting_sent=false;}
    else if(frame[0]==1&&pending_login.active){memset(pending_login.password,0,sizeof(pending_login.password));pending_login={};provider.login_result(false);}
    else if(frame[0]==6&&len>=10&&pending_info.active&&pending_info.waiting_sent){uint32_t timeout=0;memcpy(&timeout,frame+6,4);pending_info.deadline=millis()+max((uint32_t)3000,timeout+2000);pending_info.waiting_sent=false;}
    else if(frame[0]==1&&pending_info.active){provider.request_timeout(pending_info.request);finish_info();}
    else if(frame[0]==6&&len>=10&&pending_direct.active){
        uint32_t ack=0,timeout=0;memcpy(&ack,frame+2,4);memcpy(&timeout,frame+6,4);
        const uint8_t attempt=min(pending_direct.retry,DIRECT_RETRY_LIMIT);
        pending_direct.acks[attempt]=ack;
        pending_direct.route_flood[attempt]=frame[1]!=0;
        pending_direct.deadline=millis()+max((uint32_t)500,timeout);
        pending_direct.waiting_response=false;
        // ACK/route metadata and retry progress are transient UI state until
        // the direct message reaches a durable final outcome.
        const UiMessageState attempt_state=attempt==0?UiMessageState::Sending:
            (attempt==1?UiMessageState::Retrying1:
             (attempt==2?UiMessageState::Retrying2:UiMessageState::Retrying3));
        provider.transient_direct_status(
            pending_direct.sequence,attempt_state,true,
            pending_direct.route_flood[attempt]);
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct attempt=%u route=%s ack=%08lx timeout=%lu journal=unchanged\n",
                  attempt,pending_direct.route_flood[attempt]?"flood":"direct",
                  (unsigned long)ack,(unsigned long)timeout);
    }
    else if(frame[0]==0x82&&len>=5&&pending_direct.active){
        uint32_t ack=0;memcpy(&ack,frame+1,4);
        int8_t matched_attempt=-1;
        for(uint8_t attempt=0;attempt<=DIRECT_RETRY_LIMIT;++attempt){
            if(pending_direct.acks[attempt]&&pending_direct.acks[attempt]==ack){
                matched_attempt=(int8_t)attempt;break;
            }
        }
        if(matched_attempt>=0){
            const uint8_t attempt=(uint8_t)matched_attempt;
            provider.heard(pending_direct.key,6);
            provider.confirm_direct_send(
                pending_direct.sequence,ack,pending_direct.route_flood[attempt],
                UiMessageState::Delivered);
            T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct delivered attempt=%u ack=%08lx\n",
                      attempt,(unsigned long)ack);
            pending_direct={};
        }
    }
    else if(frame[0]==1&&pending_direct.active&&pending_direct.waiting_response){char reason[24]{};snprintf(reason,sizeof(reason),"command error %u",len>1?frame[1]:0);fail_pending_direct(reason);}
    else if(frame[0]==1&&pending_stats.active){finish_stats(true);}
    else if((frame[0]==0||frame[0]==1)&&pending_advert>=0){const bool flood=pending_advert==1;ui_notify_advert_result(flood,frame[0]==0);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] %s advert action result=%s\n",flood?"flood":"zero-hop",frame[0]==0?"OK":"FAILED");pending_advert=-1;}
    else if(frame[0]==7&&len>=13){
        const uint8_t* key=frame+1;const uint8_t path_len=frame[7],txt_type=frame[8];uint32_t timestamp=0;memcpy(&timestamp,frame+9,4);
        const size_t start=txt_type==2?17:13;
        if(len<=start)return;
        memcpy(message,frame+start,min(sizeof(message)-1,len-start));
        provider.received_direct(key,timestamp,message,false,0,path_len);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct legacy message received bytes=%u\n",(unsigned)(len-start));
    }
    else if(frame[0]==16&&len>=16){
        const int8_t snr_q4=(int8_t)frame[1];const uint8_t* key=frame+4;const uint8_t path_len=frame[10],txt_type=frame[11];uint32_t timestamp=0;memcpy(&timestamp,frame+12,4);
        const size_t start=txt_type==2?20:16;if(len<=start)return;
        memcpy(message,frame+start,min(sizeof(message)-1,len-start));
        provider.received_direct(key,timestamp,message,true,snr_q4,path_len);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] direct v3 message snr=%.1f path=0x%02x\n",snr_q4/4.0f,path_len);
    }
    else if(frame[0]==8&&len>=8){
        const uint8_t channel=frame[1],path_len=frame[2];uint32_t timestamp=0;memcpy(&timestamp,frame+4,4);
        if(len<=8)return;
        memcpy(message,frame+8,min(sizeof(message)-1,len-8));
        provider.received_channel(channel,timestamp,message,false,0,path_len);
    }
    else if(frame[0]==17&&len>=11){
        const int8_t snr_q4=(int8_t)frame[1];const uint8_t channel=frame[4],path_len=frame[5];uint32_t timestamp=0;memcpy(&timestamp,frame+7,4);
        if(len<=11)return;
        memcpy(message,frame+11,min(sizeof(message)-1,len-11));provider.received_channel(channel,timestamp,message,true,snr_q4,path_len);
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] channel %u v3 message snr=%.1f path=0x%02x\n",channel,snr_q4/4.0f,path_len);
    }
}
void local_mesh_runtime_begin(){provider.begin();}
void local_mesh_loop(){
    meshink_meshcore().loop();
    local_mesh_flush_contacts_save_if_due();
    provider.refresh();
#if ENV_INCLUDE_GPS == 1
    const uint32_t gps_now=millis();
    const bool gps_enabled=meshink_meshcore().getNodePrefs()->gps_enabled!=0;
    const uint32_t gps_interval=meshink_meshcore().getNodePrefs()->gps_interval;
    const MeshInkGpsStatus gps_location=meshink_gps_read_status();

    if(gps_duty_reset){
        gps_duty_reset=false;
        gps_duty_next_wake=0;
        if(gps_enabled&&gps_duty_sleeping){
            meshink_gps_set_provider_enabled(true);gps_duty_sleeping=false;
        }
    }
    if(!gps_enabled){
        if(!gps_duty_sleeping){meshink_gps_set_provider_enabled(false);gps_duty_sleeping=true;}
    }else if(gps_interval==0){
        if(gps_duty_sleeping){meshink_gps_set_provider_enabled(true);gps_duty_sleeping=false;}
    }else if(gps_duty_sleeping){
        if((int32_t)(gps_now-gps_duty_next_wake)>=0){
            gps_duty_wake_stamp=gps_location.available?gps_location.timestamp:0;
            meshink_gps_set_provider_enabled(true);gps_duty_sleeping=false;gps_duty_awake_since=gps_now;
            T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] duty wake interval=%lus previous_stamp=%lu\n",(unsigned long)gps_interval,(unsigned long)gps_duty_wake_stamp);
        }
    }else if(gps_location.valid&&
             (!gps_duty_awake_since||
              (gps_now-gps_duty_awake_since>=1000&&
               gps_location.timestamp!=gps_duty_wake_stamp))){
        // Require a newly observed GPS timestamp after a scheduled wake so a
        // cached fix cannot immediately put the receiver back to sleep.
        gps_duty_next_wake=gps_now+gps_interval*1000UL;
        gps_duty_awake_since=0;gps_duty_wake_stamp=0;
        meshink_gps_set_provider_enabled(false);gps_duty_sleeping=true;
        T5_DEBUGF(T5_LOG_GPS,"[T5-GPS] duty sleep after fresh fix; next wake in %lus\n",(unsigned long)gps_interval);
    }
#endif
    meshink_gps_service_loop();
#if ENV_INCLUDE_GPS == 1
    meshink_gps_background_tick(); // executes even when MeshCore has stopped the GPS provider
#endif
    meshink_rtc_tick();
    if(pending_login.active&&(int32_t)(millis()-pending_login.deadline)>=0){memset(pending_login.password,0,sizeof(pending_login.password));pending_login={};provider.login_result(false);}
    if(pending_info.active&&(int32_t)(millis()-pending_info.deadline)>=0){provider.request_timeout(pending_info.request);finish_info();}
    if(pending_stats.active&&(int32_t)(millis()-pending_stats.deadline)>=0)finish_stats(true);
    if(pending_direct.active&&pending_direct.finalizing_failure&&
       pending_direct.deadline&&(int32_t)(millis()-pending_direct.deadline)>=0){
        fail_pending_direct("persist retry");
    }else if(pending_direct.active&&!pending_direct.waiting_response&&pending_direct.deadline&&(int32_t)(millis()-pending_direct.deadline)>=0){
        if(pending_direct.retry>=DIRECT_RETRY_LIMIT)fail_pending_direct("retry limit");
        else{
            pending_direct.retry++;
            if(pending_direct.retry==DIRECT_RETRY_LIMIT&&!force_pending_direct_flood()){
                fail_pending_direct("flood fallback contact missing");
            }else{
                // Retry progress is runtime/UI state only. The journal remains
                // Sending until one final Delivered or Failed update.
                const UiMessageState retry_state=
                    (UiMessageState)((uint8_t)UiMessageState::Retrying1+
                                     pending_direct.retry-1);
                bool route_flood=false;
                const bool route_known=pending_direct_route(route_flood);
                provider.transient_direct_status(
                    pending_direct.sequence,retry_state,route_known,route_flood);
                if(!enqueue_direct_attempt())fail_pending_direct("retry queue busy");
            }
        }
    }
#if ENV_INCLUDE_GPS == 1
    const MeshInkGpsStatus location=meshink_gps_read_status();
    static uint32_t next_ui_gps=0,candidate_since=0;
    static bool stable_fix=false,candidate_fix=false;
    static int stable_sats=0;
    static long stable_lat=0,stable_lon=0;
    static uint32_t stable_stamp=0;
    const uint32_t now=millis();
    if((int32_t)(now-next_ui_gps)>=0){
        next_ui_gps=now+(ui_is_standby()?10000:1000);
        const bool enabled=meshink_gps_constellation_mode()!=MeshInkGpsConstellationMode::None;
        const bool gps_error=location.error!=MeshInkGpsError::None;
        const bool raw_fix=!gps_error&&location.valid;
        if(gps_error){
            // Faults are immediate user information; never hold a stale "FIXED"
            // state through the normal three-second fix/search debounce.
            stable_fix=false;
            candidate_fix=false;
            candidate_since=now;
        }else{
            if(raw_fix!=candidate_fix){candidate_fix=raw_fix;candidate_since=now;}
            if(raw_fix==stable_fix||now-candidate_since>=3000){
                stable_fix=raw_fix;
                if(raw_fix){
                    stable_sats=(int)location.satellites;
                    stable_lat=location.latitude;
                    stable_lon=location.longitude;
                    stable_stamp=location.timestamp;
                }
            }
        }
        ui_status_set_gps(enabled,stable_fix,stable_sats,stable_lat,stable_lon,stable_stamp,location.error);
    }
#if T5_LOG_GPS
    static bool was_waiting=true;
    if(location.available){const bool waiting=location.waiting_time_sync;if(was_waiting&&!waiting)T5_DEBUGF(T5_LOG_GPS,"[T5-RTC] GPS provider finished sync request UTC=%lu; hardware RTC write may have been skipped (see [T5] rtc log)\n",(unsigned long)location.timestamp);was_waiting=waiting;}
#endif
#endif
}
bool local_mesh_send_active(const char* text){
    if(!text||!text[0])return false;
    const uint32_t now=time(nullptr);
    if(provider.active_is_channel()){
        ChannelDetails channel{};if(!provider.active_channel(channel))return false;
        const size_t limit=channel_message_limit(meshink_meshcore().getNodeName());
        const size_t text_len=strlen(text);
        if(text_len>limit){
            T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] channel send rejected text=%u limit=%u (sender prefix consumes %u)\n",
                      (unsigned)text_len,(unsigned)limit,(unsigned)(MAX_TEXT_LEN-limit));
            return false;
        }
        const bool ok=meshink_meshcore().sendGroupMessage(now,channel.channel,meshink_meshcore().getNodeName(),text,text_len);
        if(ok){const uint32_t sequence=provider.sent(text,now,0);track_channel_send(provider.active_channel_index(),now,sequence,text);}
        T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] channel send result=%d\n",ok);return ok;
    }
    ContactInfo contact{};if(!provider.active_contact(contact)||pending_direct.active)return false;
    if(strlen(text)>MESHINK_MESSAGE_TEXT_MAX)return false;
    pending_direct={};pending_direct.active=true;pending_direct.timestamp=now;
    memcpy(pending_direct.key,contact.id.pub_key,6);
    strncpy(pending_direct.text,text,sizeof(pending_direct.text)-1);
    pending_direct.sequence=provider.queue_direct(text,now);
    // MeshCore chooses direct vs flood from the contact's current path. Show
    // that route immediately; RESP_CODE_SENT will confirm the actual choice.
    provider.transient_direct_status(
        pending_direct.sequence,UiMessageState::Sending,true,
        contact.out_path_len==OUT_PATH_UNKNOWN);
    if(!enqueue_direct_attempt()){fail_pending_direct("initial queue busy");return false;}
    return true;
}
bool local_mesh_send_direct(size_t index,const char* text){if(!provider.open_contact(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_channel(size_t index,const char* text){if(!provider.open_channel(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_advert(bool flood){if(pending_advert>=0)return false;const uint8_t command[2]={7,(uint8_t)(flood?1:0)};if(!local_mesh_enqueue_command(command,sizeof(command)))return false;pending_advert=flood?1:0;return true;}
uint8_t local_mesh_tx_power(){return meshink_meshcore().getNodePrefs()->tx_power_dbm;}
bool local_mesh_save_tx_power(uint8_t dbm){
    if(dbm<2||dbm>22)return false;
    auto* prefs=meshink_meshcore().getNodePrefs();
    prefs->tx_power_dbm=dbm;
    meshink_meshcore().savePrefs();
    // The setup wizard reboots after saving, so the radio driver picks this up at startup.
    return true;
}
bool local_mesh_apply_radio(float freq,float bw,uint8_t sf,uint8_t cr,uint8_t path_hash_mode){auto* p=meshink_meshcore().getNodePrefs();if(freq<=0||bw<7||sf<5||sf>12||cr<5||cr>8)return false;p->freq=freq;p->bw=bw;p->sf=sf;p->cr=cr;p->path_hash_mode=min((uint8_t)2,path_hash_mode);meshink_meshcore().savePrefs();meshink_radio_apply_params(freq,bw,sf,cr);T5_DEBUGF(T5_LOG_MESH,"[T5-MESH] radio preset applied %.3f SF%u BW%.1f CR%u hash=%u\n",freq,sf,bw,cr,p->path_hash_mode);return true;}
void local_mesh_apply_name(const char* name){auto* p=meshink_meshcore().getNodePrefs();strncpy(p->node_name,name,sizeof(p->node_name)-1);p->node_name[sizeof(p->node_name)-1]=0;meshink_meshcore().savePrefs();}
void local_mesh_sync_gps_mode(MeshInkGpsConstellationMode mode){
#if ENV_INCLUDE_GPS == 1
    const bool enabled=mode!=MeshInkGpsConstellationMode::None;
    auto* p=meshink_meshcore().getNodePrefs();
    p->gps_enabled=enabled?1:0;
    meshink_meshcore().savePrefs();
    meshink_meshcore().applyGpsPrefs();
    gps_duty_sleeping=!enabled;
    reset_gps_duty_cycle();
#else
    (void)mode;
#endif
}
#if ENV_INCLUDE_GPS == 1
uint32_t local_mesh_gps_interval(){return meshink_meshcore().getNodePrefs()->gps_interval;}
void local_mesh_cycle_gps_interval(){static constexpr uint32_t values[]={0,60,300,900,1800};auto* p=meshink_meshcore().getNodePrefs();size_t i=0;while(i<4&&p->gps_interval!=values[i])++i;p->gps_interval=values[(i+1)%5];meshink_meshcore().savePrefs();meshink_meshcore().applyGpsPrefs();gps_duty_sleeping=false;reset_gps_duty_cycle();}
#else
uint32_t local_mesh_gps_interval(){return 0;}
void local_mesh_cycle_gps_interval(){}
#endif
bool local_mesh_gps_advert_location(){return meshink_meshcore().getNodePrefs()->advert_loc_policy!=0;}
bool local_mesh_my_location(long& latitude,long& longitude){
    const auto* p=meshink_meshcore().getNodePrefs();
    if(!p||!isfinite(p->node_lat)||!isfinite(p->node_lon)||
       p->node_lat<-85.0511||p->node_lat>85.0511||p->node_lon<-180.0||p->node_lon>180.0||
       (fabs(p->node_lat)<0.0000005&&fabs(p->node_lon)<0.0000005))return false;
    latitude=(long)llround(p->node_lat*1000000.0);
    longitude=(long)llround(p->node_lon*1000000.0);
    return true;
}
void local_mesh_toggle_gps_advert_location(){auto* p=meshink_meshcore().getNodePrefs();p->advert_loc_policy=p->advert_loc_policy?0:1;meshink_meshcore().savePrefs();}
const char* local_mesh_radio_summary(){auto* p=meshink_meshcore().getNodePrefs();snprintf(radio_summary,sizeof(radio_summary),"%.3f / SF%u / BW%.1f / CR%u",p->freq,p->sf,p->bw,p->cr);return radio_summary;}
bool local_mesh_radio_matches(float frequency_mhz,float bandwidth_khz,uint8_t spreading_factor,uint8_t coding_rate,uint8_t path_hash_bytes){
    const auto* p=meshink_meshcore().getNodePrefs();
    if(!p||!path_hash_bytes)return false;
    return fabsf(p->freq-frequency_mhz)<0.0005f &&
           fabsf(p->bw-bandwidth_khz)<0.05f &&
           p->sf==spreading_factor && p->cr==coding_rate &&
           (uint8_t)(min((uint8_t)2,p->path_hash_mode)+1)==path_hash_bytes;
}
const char* local_mesh_privacy_value(uint8_t item){auto* p=meshink_meshcore().getNodePrefs();switch(item){case 0:return p->autoadd_config?"ENABLED":"DISABLED";case 1:if(!p->autoadd_max_hops)return "NO LIMIT";if(p->autoadd_max_hops==1)return "DIRECT ONLY";snprintf(setting_value,sizeof(setting_value),"UP TO %u HOPS",p->autoadd_max_hops-1);return setting_value;case 2:return p->advert_loc_policy?"SHARE":"HIDDEN";case 3:return p->telemetry_mode_base==0?"DENY":p->telemetry_mode_base==1?"CONTACT FLAGS":"ALLOW ALL";case 4:return p->telemetry_mode_loc==0?"DENY":p->telemetry_mode_loc==1?"CONTACT FLAGS":"ALLOW ALL";default:return p->isRepeatEn()?"ENABLED":"DISABLED";}}
void local_mesh_toggle_privacy(uint8_t item){auto* p=meshink_meshcore().getNodePrefs();switch(item){case 0:p->autoadd_config=p->autoadd_config?0:0x1E;break;case 1:p->autoadd_max_hops=(p->autoadd_max_hops+1)%6;break;case 2:p->advert_loc_policy=p->advert_loc_policy?0:1;break;case 3:p->telemetry_mode_base=(p->telemetry_mode_base+1)%3;break;case 4:p->telemetry_mode_loc=(p->telemetry_mode_loc+1)%3;break;default:p->setRepeatEn(!p->isRepeatEn());break;}meshink_meshcore().savePrefs();}
void local_mesh_cycle_path_hash(){auto* p=meshink_meshcore().getNodePrefs();p->path_hash_mode=(p->path_hash_mode+1)%3;meshink_meshcore().savePrefs();}
uint8_t local_mesh_path_hash_mode(){return min((uint8_t)2,meshink_meshcore().getNodePrefs()->path_hash_mode);}
void local_mesh_prepare_shutdown(){
    T5_DEBUGLN(T5_LOG_MESH,"[T5-SHUTDOWN] stopping MeshCore radio");
    meshink_radio_power_off();
    T5_DEBUGLN(T5_LOG_MESH,"[T5-SHUTDOWN] LoRa radio sleep requested");
}

bool local_mesh_request_diagnostics(){
    if(pending_stats.active||pending_info.active||pending_login.active||pending_direct.active||pending_advert>=0)return false;
    strcpy(diagnostics_core,"REQUESTING");strcpy(diagnostics_radio,"WAITING");strcpy(diagnostics_packets,"WAITING");
    if(!enqueue_stats_request(0)){strcpy(diagnostics_core,"REQUEST BUSY");return false;}
    ui_request_data_refresh("mesh-stats");return true;
}
bool local_mesh_diagnostics_busy(){return pending_stats.active;}
const char* local_mesh_diagnostics_core(){return diagnostics_core;}
const char* local_mesh_diagnostics_radio(){return diagnostics_radio;}
const char* local_mesh_diagnostics_packets(){return diagnostics_packets;}
