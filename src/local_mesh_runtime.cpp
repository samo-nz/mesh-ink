#include <Arduino.h>
#include <Mesh.h>
#include <SPIFFS.h>
#include <time.h>
#include "local_mesh_runtime.h"
#include "companion_runtime.h"
#include "ui_onboarding.h"
#include "board/target.h"
#include "../lib/MeshCore/examples/companion_radio/MyMesh.h"

extern MyMesh& t5_mesh();

namespace {
constexpr size_t MAX_UI_CONTACTS=16;
constexpr size_t MAX_UI_CHANNELS=8;
constexpr size_t MAX_UI_ADVERTS=16;
constexpr size_t MAX_STORED_MESSAGES=96;
constexpr uint32_t STORE_MAGIC=0x354D3554; // T5M5
constexpr uint16_t STORE_VERSION=1;
constexpr char STORE_PATH[]="/ui_messages.bin";

enum class MessageKind:uint8_t{Direct=0,Channel=1};
struct StoreHeader{uint32_t magic;uint16_t version;uint16_t capacity;uint16_t head;uint16_t count;uint32_t sequence;};
struct StoredMessage{uint32_t sequence;uint32_t timestamp;uint32_t ack;uint8_t kind;uint8_t state;uint8_t key[7];char text[145];};
struct ListStorage{UiListEntry entry{};char title[34]{};char subtitle[72]{};char time[10]{};uint8_t key[7]{};uint8_t channel_index=0;};
struct MessageView{UiMessage entry{};char text[145]{};char time[10]{};};

static void format_time(uint32_t timestamp,char out[10]){
    time_t raw=timestamp?(time_t)timestamp:time(nullptr);struct tm value{};localtime_r(&raw,&value);
    snprintf(out,10,"%02d:%02d",value.tm_hour,value.tm_min);
}
static const char* state_text(UiMessageState state){
    switch(state){case UiMessageState::Sending:return "SENDING";case UiMessageState::Sent:return "SENT";
        case UiMessageState::Delivered:return "DELIVERED";case UiMessageState::Failed:return "FAILED";default:return "";}
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
        Serial.printf("[T5-STORE] loaded %u/%u messages; oldest records evicted at capacity\n",header_.count,header_.capacity);
    }
    size_t count()const{return header_.count;}
    const StoredMessage& at(size_t logical)const{return records_[(header_.head+logical)%MAX_STORED_MESSAGES];}
    StoredMessage* append(MessageKind kind,const uint8_t* key,size_t key_len,const char* text,uint32_t timestamp,UiMessageState state,uint32_t ack=0){
        uint16_t physical;
        if(header_.count<MAX_STORED_MESSAGES){physical=(header_.head+header_.count)%MAX_STORED_MESSAGES;header_.count++;}
        else{physical=header_.head;header_.head=(header_.head+1)%MAX_STORED_MESSAGES;Serial.println("[T5-STORE] capacity reached; evicting oldest message");}
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
    size_t contact_count_=0,channel_count_=0,conversation_count_=0,advert_count_=0,active_count_=0;
    bool active_channel_=false;uint8_t active_key_[7]{};char active_title_[34]="MESSAGES";uint32_t refreshed_at_=0;
    MessageStore store_;
    static void bind(ListStorage& item){item.entry.title=item.title;item.entry.subtitle=item.subtitle;item.entry.time=item.time;}
    static void bind(MessageView& item){item.entry.text=item.text;item.entry.time=item.time;}
    bool matches(const StoredMessage& m)const{return m.kind==(uint8_t)(active_channel_?MessageKind::Channel:MessageKind::Direct)&&memcmp(m.key,active_key_,active_channel_?1:6)==0;}
    const StoredMessage* last_for(const uint8_t* key,bool channel)const{
        for(size_t n=store_.count();n>0;--n){const auto& m=store_.at(n-1);if(m.kind==(uint8_t)(channel?MessageKind::Channel:MessageKind::Direct)&&memcmp(m.key,key,channel?1:6)==0)return &m;}return nullptr;
    }
    void rebuild_active(){
        active_count_=0;
        for(size_t i=0;i<store_.count()&&active_count_<MAX_STORED_MESSAGES;++i){const auto& m=store_.at(i);if(!matches(m))continue;
            auto& view=active_messages_[active_count_++];memset(&view,0,sizeof(view));bind(view);strncpy(view.text,m.text,sizeof(view.text)-1);format_time(m.timestamp,view.time);
            view.entry.outgoing=m.state!=(uint8_t)UiMessageState::Received;view.entry.state=(UiMessageState)m.state;
        }
    }
    bool activate(const ListStorage& item,bool channel){active_channel_=channel;memcpy(active_key_,item.key,sizeof(active_key_));strncpy(active_title_,item.title,sizeof(active_title_)-1);rebuild_active();return true;}
public:
    MeshCoreUiProvider(){for(auto& i:contacts_)bind(i);for(auto& i:channels_)bind(i);for(auto& i:conversations_)bind(i);for(auto& i:adverts_)bind(i);for(auto& i:active_messages_)bind(i);}
    void begin(){store_.begin();refresh(true);}
    void refresh(bool force=false){
        const uint32_t interval=ui_is_standby()?60000:10000;
        if(!force&&millis()-refreshed_at_<interval)return;refreshed_at_=millis();const uint32_t started=micros();contact_count_=channel_count_=conversation_count_=advert_count_=0;
        ContactInfo contact{};auto iterator=t5_mesh().startContactsIterator();
        while(contact_count_<MAX_UI_CONTACTS&&iterator.hasNext(&t5_mesh(),contact)){
            auto& item=contacts_[contact_count_++];memset(&item,0,sizeof(item));bind(item);strncpy(item.title,contact.name[0]?contact.name:"UNNAMED NODE",sizeof(item.title)-1);
            snprintf(item.subtitle,sizeof(item.subtitle),"%s  %s",contact.out_path_len==0?"DIRECT":"MESH ROUTE",contact.type==ADV_TYPE_REPEATER?"REPEATER":"CHAT NODE");format_time(contact.last_advert_timestamp,item.time);memcpy(item.key,contact.id.pub_key,7);
            if(const auto* last=last_for(item.key,false)){auto& c=conversations_[conversation_count_++];memset(&c,0,sizeof(c));bind(c);strncpy(c.title,item.title,sizeof(c.title)-1);strncpy(c.subtitle,last->text,sizeof(c.subtitle)-1);format_time(last->timestamp,c.time);memcpy(c.key,item.key,7);}
        }
        for(int i=0;i<MAX_GROUP_CHANNELS&&channel_count_<MAX_UI_CHANNELS;++i){ChannelDetails ch{};if(!t5_mesh().getChannel(i,ch)||!ch.name[0])continue;
            auto& item=channels_[channel_count_++];memset(&item,0,sizeof(item));bind(item);snprintf(item.title,sizeof(item.title),"# %s",ch.name);strncpy(item.subtitle,"MESHCORE CHANNEL",sizeof(item.subtitle)-1);item.key[0]=i;item.channel_index=i;
        }
        AdvertPath heard[MAX_UI_ADVERTS]{};const int heard_count=t5_mesh().getRecentlyHeard(heard,MAX_UI_ADVERTS);
        for(int i=0;i<heard_count&&advert_count_<MAX_UI_ADVERTS;++i){if(!heard[i].recv_timestamp||!heard[i].name[0])continue;auto& item=adverts_[advert_count_++];memset(&item,0,sizeof(item));bind(item);
            const uint8_t hops=heard[i].path_len&63;
            strncpy(item.title,heard[i].name,sizeof(item.title)-1);snprintf(item.subtitle,sizeof(item.subtitle),hops?"RECEIVED ADVERT  %u HOP%s":"RECEIVED ADVERT  ZERO HOP",hops,hops==1?"":"S");format_time(heard[i].recv_timestamp,item.time);memcpy(item.key,heard[i].pubkey_prefix,7);
            Serial.printf("[T5-MESH] advert '%s' path=0x%02x hops=%u\n",item.title,heard[i].path_len,hops);
        }
        rebuild_active();
        static uint32_t last_report=0;if(force||millis()-last_report>=60000){last_report=millis();Serial.printf("[T5-POWER] model refresh=%luus interval=%lums contacts=%u channels=%u adverts=%u standby=%d\n",(unsigned long)(micros()-started),(unsigned long)interval,(unsigned)contact_count_,(unsigned)channel_count_,(unsigned)advert_count_,ui_is_standby());}
    }
    void received_direct(const uint8_t* key,uint32_t timestamp,const char* text){store_.append(MessageKind::Direct,key,6,text,timestamp,UiMessageState::Received);refresh(true);ui_notify_message_received(false);}
    void received_channel(uint8_t channel,uint32_t timestamp,const char* text){store_.append(MessageKind::Channel,&channel,1,text,timestamp,UiMessageState::Received);refresh(true);ui_notify_message_received(true);}
    uint32_t sent(const char* text,uint32_t timestamp,uint32_t ack){auto* m=store_.append(active_channel_?MessageKind::Channel:MessageKind::Direct,active_key_,active_channel_?1:6,text,timestamp,UiMessageState::Sent,ack);rebuild_active();return m->sequence;}
    size_t conversation_count()const override{return conversation_count_;}const UiListEntry& conversation(size_t i)const override{return conversations_[i].entry;}
    bool open_conversation(size_t i)override{return i<conversation_count_&&activate(conversations_[i],false);}
    size_t contact_count()const override{return contact_count_;}const UiListEntry& contact(size_t i)const override{return contacts_[i].entry;}bool open_contact(size_t i)override{return i<contact_count_&&activate(contacts_[i],false);}
    size_t channel_count()const override{return channel_count_;}const UiListEntry& channel(size_t i)const override{return channels_[i].entry;}bool open_channel(size_t i)override{return i<channel_count_&&activate(channels_[i],true);}
    size_t advert_count()const override{return advert_count_;}const UiListEntry& advert(size_t i)const override{return adverts_[i].entry;}
    const char* active_title()const override{return active_title_;}bool active_is_channel()const override{return active_channel_;}size_t active_message_count()const override{return active_count_;}const UiMessage& active_message(size_t i)const override{return active_messages_[i].entry;}
    bool active_contact(ContactInfo& out)const{if(active_channel_)return false;auto* found=t5_mesh().lookupContactByPubKey(active_key_,6);if(!found)return false;out=*found;return true;}
    bool active_channel(ChannelDetails& out)const{return active_channel_&&t5_mesh().getChannel(active_key_[0],out);}
};

MeshCoreUiProvider provider;char radio_summary[44]{};char setting_value[20]{};
}

UiDataProvider* local_mesh_provider(){return &provider;}
void local_mesh_on_frame(const uint8_t* frame,size_t len){
    if(!frame||!len)return;char message[150]{};
    if((frame[0]==7||frame[0]==16)&&len>13){uint32_t timestamp=0;memcpy(&timestamp,&frame[9],4);const size_t start=frame[8]==2?17:13;if(len<=start)return;memcpy(message,&frame[start],min(sizeof(message)-1,len-start));provider.received_direct(&frame[1],timestamp,message);Serial.printf("[T5-MESH] direct message received bytes=%u\n",(unsigned)(len-start));}
    else if((frame[0]==8||frame[0]==17)&&len>8){uint32_t timestamp=0;memcpy(&timestamp,&frame[4],4);memcpy(message,&frame[8],min(sizeof(message)-1,len-8));provider.received_channel(frame[1],timestamp,message);Serial.printf("[T5-MESH] channel %u message received\n",frame[1]);}
}
void local_mesh_runtime_begin(){provider.begin();}
void local_mesh_loop(){
    t5_mesh().loop();provider.refresh();sensors.loop();rtc_clock.tick();
    auto* location=sensors.getLocationProvider();
    static uint32_t next_ui_gps=0;const uint32_t now=millis();if((int32_t)(now-next_ui_gps)>=0){next_ui_gps=now+(ui_is_standby()?10000:1000);const bool enabled=local_mesh_gps_enabled();const bool fix=location&&location->isValid();const int sats=location?(int)location->satellitesCount():0;const long lat=location?location->getLatitude():0;const long lon=location?location->getLongitude():0;const uint32_t stamp=(fix&&location)?(uint32_t)location->getTimestamp():0;ui_status_set_gps(enabled,fix,sats,lat,lon,stamp);}
    static bool was_waiting=true;if(location){const bool waiting=location->waitingTimeSync();if(was_waiting&&!waiting)Serial.printf("[T5-RTC] synchronized from GPS UTC=%lu\n",(unsigned long)location->getTimestamp());was_waiting=waiting;}
    static uint32_t radio_report_at=0;if(millis()-radio_report_at>=60000){radio_report_at=millis();Serial.printf("[T5-POWER] radio continuous-rx=%d received=%lu errors=%lu sent=%lu boosted=%d (duty cycle intentionally disabled)\n",radio_driver.isInRecvMode(),(unsigned long)radio_driver.getPacketsRecv(),(unsigned long)radio_driver.getPacketsRecvErrors(),(unsigned long)radio_driver.getPacketsSent(),radio_driver.getRxBoostedGainMode());}
}
bool local_mesh_send_active(const char* text){
    if(!text||!text[0])return false;const uint32_t now=time(nullptr);
    if(provider.active_is_channel()){ChannelDetails channel{};if(!provider.active_channel(channel))return false;const bool ok=t5_mesh().sendGroupMessage(now,channel.channel,t5_mesh().getNodeName(),text,strlen(text));if(ok)provider.sent(text,now,0);Serial.printf("[T5-MESH] channel send result=%d\n",ok);return ok;}
    ContactInfo contact{};if(!provider.active_contact(contact))return false;uint32_t ack=0,timeout=0;const int result=t5_mesh().sendMessage(contact,now,0,text,ack,timeout);if(result>=0)provider.sent(text,now,ack);Serial.printf("[T5-MESH] direct send result=%d ack=%08lx timeout=%lu\n",result,(unsigned long)ack,(unsigned long)timeout);return result>=0;
}
bool local_mesh_send_direct(size_t index,const char* text){if(!provider.open_contact(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_channel(size_t index,const char* text){if(!provider.open_channel(index))return false;return local_mesh_send_active(text);}
bool local_mesh_send_advert(bool flood){if(!flood)return t5_mesh().advert();const uint8_t command[2]={7,1};return local_mesh_enqueue_command(command,sizeof(command));}
void local_mesh_apply_name(const char* name){auto* p=t5_mesh().getNodePrefs();strncpy(p->node_name,name,sizeof(p->node_name)-1);p->node_name[sizeof(p->node_name)-1]=0;t5_mesh().savePrefs();}
void local_mesh_apply_gps(bool enabled){auto* p=t5_mesh().getNodePrefs();p->gps_enabled=enabled?1:0;t5_mesh().savePrefs();t5_mesh().applyGpsPrefs();}
bool local_mesh_gps_enabled(){return t5_mesh().getNodePrefs()->gps_enabled!=0;}bool local_mesh_gps_fix(){auto* location=sensors.getLocationProvider();return location&&location->isValid();}
uint32_t local_mesh_gps_interval(){return t5_mesh().getNodePrefs()->gps_interval;}
bool local_mesh_gps_advert_location(){return t5_mesh().getNodePrefs()->advert_loc_policy!=0;}
void local_mesh_cycle_gps_interval(){static constexpr uint32_t values[]={0,60,300,900,1800};auto* p=t5_mesh().getNodePrefs();size_t i=0;while(i<4&&p->gps_interval!=values[i])++i;p->gps_interval=values[(i+1)%5];t5_mesh().savePrefs();t5_mesh().applyGpsPrefs();}
void local_mesh_toggle_gps_advert_location(){auto* p=t5_mesh().getNodePrefs();p->advert_loc_policy=p->advert_loc_policy?0:1;t5_mesh().savePrefs();}
uint32_t local_mesh_current_time(){return rtc_clock.getCurrentTime();}
const char* local_mesh_node_name(){return t5_mesh().getNodeName();}
const char* local_mesh_radio_summary(){auto* p=t5_mesh().getNodePrefs();snprintf(radio_summary,sizeof(radio_summary),"%.3f SF%u BW%.1f CR%u",p->freq,p->sf,p->bw,p->cr);return radio_summary;}
const char* local_mesh_privacy_value(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:return p->autoadd_config?"ENABLED":"DISABLED";case 1:snprintf(setting_value,sizeof(setting_value),"%u HOPS",p->autoadd_max_hops);return setting_value;case 2:return p->advert_loc_policy?"SHARE":"HIDDEN";case 3:return p->telemetry_mode_base?"ALLOW":"DENY";case 4:return p->telemetry_mode_loc?"ALLOW":"DENY";default:return p->isRepeatEn()?"ENABLED":"DISABLED";}}
void local_mesh_toggle_privacy(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:p->autoadd_config=p->autoadd_config?0:0xFF;break;case 1:p->autoadd_max_hops=(p->autoadd_max_hops+1)%6;break;case 2:p->advert_loc_policy=p->advert_loc_policy?0:1;break;case 3:p->telemetry_mode_base=p->telemetry_mode_base?0:2;break;case 4:p->telemetry_mode_loc=p->telemetry_mode_loc?0:2;break;default:p->setRepeatEn(!p->isRepeatEn());break;}t5_mesh().savePrefs();}
void local_mesh_cycle_path_hash(){auto* p=t5_mesh().getNodePrefs();p->path_hash_mode=(p->path_hash_mode+1)%4;t5_mesh().savePrefs();}
