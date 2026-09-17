#include <Arduino.h>
#include <Mesh.h>
#include <time.h>
#include "local_mesh_runtime.h"
#include "ui_onboarding.h"
#include "board/target.h"
#include "../lib/MeshCore/examples/companion_radio/MyMesh.h"

extern MyMesh& t5_mesh();

namespace {
constexpr size_t MAX_UI_CONTACTS = 12;
constexpr size_t MAX_UI_CHANNELS = 8;
constexpr size_t MAX_UI_MESSAGES = 12;

struct ListStorage { UiListEntry entry{}; char title[34]{}; char subtitle[56]{}; char time[10]{}; uint8_t key[6]{}; };
struct MessageStorage { UiMessage entry{}; char text[150]{}; char time[10]{}; };

static void format_time(uint32_t timestamp, char out[10]) {
    time_t raw = timestamp ? (time_t)timestamp : time(nullptr);
    struct tm value{};
    localtime_r(&raw, &value);
    snprintf(out, 10, "%02d:%02d", value.tm_hour, value.tm_min);
}

class MeshCoreUiProvider final : public UiDataProvider {
    ListStorage contacts_[MAX_UI_CONTACTS]{};
    ListStorage channels_[MAX_UI_CHANNELS]{};
    MessageStorage direct_[MAX_UI_MESSAGES]{};
    MessageStorage channel_messages_[MAX_UI_MESSAGES]{};
    size_t contact_count_ = 0, channel_count_ = 0, direct_count_ = 0, channel_message_count_ = 0;
    uint32_t refreshed_at_ = 0;

    static void bind(ListStorage& item) {
        item.entry.title=item.title; item.entry.subtitle=item.subtitle; item.entry.time=item.time;
    }
    static void bind(MessageStorage& item) { item.entry.text=item.text; item.entry.time=item.time; }
    void push(MessageStorage* list, size_t& count, const char* text, uint32_t timestamp, bool outgoing) {
        if (count == MAX_UI_MESSAGES) { memmove(&list[0], &list[1], sizeof(MessageStorage)*(MAX_UI_MESSAGES-1)); count--; }
        MessageStorage& item=list[count++]; memset(&item,0,sizeof(item)); bind(item);
        strncpy(item.text,text,sizeof(item.text)-1); format_time(timestamp,item.time); item.entry.outgoing=outgoing;
    }
public:
    MeshCoreUiProvider() { for(auto& i:contacts_)bind(i);for(auto& i:channels_)bind(i);for(auto& i:direct_)bind(i);for(auto& i:channel_messages_)bind(i); }
    void refresh(bool force=false) {
        if(!force && millis()-refreshed_at_<2000)return; refreshed_at_=millis();
        contact_count_=0;
        ContactInfo contact{}; auto iterator=t5_mesh().startContactsIterator();
        while(contact_count_<MAX_UI_CONTACTS && iterator.hasNext(&t5_mesh(),contact)){
            auto& item=contacts_[contact_count_++]; memset(&item,0,sizeof(item)); bind(item);
            strncpy(item.title,contact.name[0]?contact.name:"UNNAMED NODE",sizeof(item.title)-1);
            snprintf(item.subtitle,sizeof(item.subtitle),"%s  %s",contact.out_path_len==0?"DIRECT":"MESH ROUTE",contact.type==ADV_TYPE_REPEATER?"REPEATER":"CHAT NODE");
            format_time(contact.last_advert_timestamp,item.time); memcpy(item.key,contact.id.pub_key,6);
        }
        channel_count_=0;
        for(int i=0;i<MAX_GROUP_CHANNELS && channel_count_<MAX_UI_CHANNELS;++i){
            ChannelDetails ch{}; if(!t5_mesh().getChannel(i,ch)||!ch.name[0])continue;
            auto& item=channels_[channel_count_++]; memset(&item,0,sizeof(item)); bind(item);
            snprintf(item.title,sizeof(item.title),"# %s",ch.name); strncpy(item.subtitle,"MESHCORE CHANNEL",sizeof(item.subtitle)-1);
            memcpy(item.key,&i,sizeof(uint8_t));
        }
        ui_status_set_unread(0); ui_status_set_channel_unread(0);
    }
    void received_direct(const ContactInfo& from,uint32_t timestamp,const char* text){ push(direct_,direct_count_,text,timestamp,false); refresh(true); }
    void received_channel(const mesh::GroupChannel&,uint32_t timestamp,const char* text){ push(channel_messages_,channel_message_count_,text,timestamp,false); }
    void sent_direct(const char* text){ push(direct_,direct_count_,text,time(nullptr),true); }
    void sent_channel(const char* text){ push(channel_messages_,channel_message_count_,text,time(nullptr),true); }
    size_t contact_count()const override{return contact_count_;} const UiListEntry& contact(size_t i)const override{return contacts_[i].entry;}
    size_t channel_count()const override{return channel_count_;} const UiListEntry& channel(size_t i)const override{return channels_[i].entry;}
    size_t direct_message_count()const override{return direct_count_;} const UiMessage& direct_message(size_t i)const override{return direct_[i].entry;}
    size_t channel_message_count()const override{return channel_message_count_;} const UiMessage& channel_message(size_t i)const override{return channel_messages_[i].entry;}
    bool contact_at(size_t ui_index,ContactInfo& result)const { if(ui_index>=contact_count_)return false; ContactInfo* found=t5_mesh().lookupContactByPubKey(contacts_[ui_index].key,6);if(!found)return false;result=*found;return true; }
    bool channel_at(size_t ui_index,ChannelDetails& result)const { if(ui_index>=channel_count_)return false; return t5_mesh().getChannel(channels_[ui_index].key[0],result); }
};
MeshCoreUiProvider provider;
char radio_summary[44]{};
char setting_value[20]{};
}

UiDataProvider* local_mesh_provider(){return &provider;}
void local_mesh_on_direct(const ContactInfo& from,uint32_t timestamp,const char* text){provider.received_direct(from,timestamp,text);}
void local_mesh_on_channel(const mesh::GroupChannel& ch,uint32_t timestamp,const char* text){provider.received_channel(ch,timestamp,text);}

void local_mesh_loop(){ provider.refresh(); sensors.loop(); rtc_clock.tick(); ui_status_set_gps(local_mesh_gps_enabled(),local_mesh_gps_fix()); }
bool local_mesh_send_direct(size_t index,const char* text){
    ContactInfo contact{}; if(!provider.contact_at(index,contact))return false;
    uint32_t ack=0,timeout=0; const int result=t5_mesh().sendMessage(contact,time(nullptr),0,text,ack,timeout);
    if(result>=0)provider.sent_direct(text); Serial.printf("[T5-MESH] direct send result=%d timeout=%lu\n",result,(unsigned long)timeout); return result>=0;
}
bool local_mesh_send_channel(size_t index,const char* text){
    ChannelDetails channel{}; if(!provider.channel_at(index,channel))return false;
    bool ok=t5_mesh().sendGroupMessage(time(nullptr),channel.channel,t5_mesh().getNodeName(),text,strlen(text));
    if(ok)provider.sent_channel(text); Serial.printf("[T5-MESH] channel send result=%d\n",ok); return ok;
}
void local_mesh_apply_name(const char* name){auto* p=t5_mesh().getNodePrefs();strncpy(p->node_name,name,sizeof(p->node_name)-1);p->node_name[sizeof(p->node_name)-1]=0;t5_mesh().savePrefs();}
void local_mesh_apply_gps(bool enabled){auto* p=t5_mesh().getNodePrefs();p->gps_enabled=enabled?1:0;t5_mesh().savePrefs();t5_mesh().applyGpsPrefs();}
bool local_mesh_gps_enabled(){return t5_mesh().getNodePrefs()->gps_enabled!=0;}
bool local_mesh_gps_fix(){auto* location=sensors.getLocationProvider();return location&&location->isValid();}
const char* local_mesh_node_name(){return t5_mesh().getNodeName();}
const char* local_mesh_radio_summary(){auto* p=t5_mesh().getNodePrefs();snprintf(radio_summary,sizeof(radio_summary),"%.3f SF%u BW%.1f CR%u",p->freq,p->sf,p->bw,p->cr);return radio_summary;}
const char* local_mesh_privacy_value(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:return p->autoadd_config?"ENABLED":"DISABLED";case 1:snprintf(setting_value,sizeof(setting_value),"%u HOPS",p->autoadd_max_hops);return setting_value;case 2:return p->advert_loc_policy?"SHARE":"HIDDEN";case 3:return p->telemetry_mode_base?"ALLOW":"DENY";case 4:return p->telemetry_mode_loc?"ALLOW":"DENY";default:return p->isRepeatEn()?"ENABLED":"DISABLED";}}
void local_mesh_toggle_privacy(uint8_t item){auto* p=t5_mesh().getNodePrefs();switch(item){case 0:p->autoadd_config=p->autoadd_config?0:0xFF;break;case 1:p->autoadd_max_hops=(p->autoadd_max_hops+1)%6;break;case 2:p->advert_loc_policy=p->advert_loc_policy?0:1;break;case 3:p->telemetry_mode_base=p->telemetry_mode_base?0:2;break;case 4:p->telemetry_mode_loc=p->telemetry_mode_loc?0:2;break;default:p->setRepeatEn(!p->isRepeatEn());break;}t5_mesh().savePrefs();}
void local_mesh_cycle_path_hash(){auto* p=t5_mesh().getNodePrefs();p->path_hash_mode=(p->path_hash_mode+1)%4;t5_mesh().savePrefs();}
