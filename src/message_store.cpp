#include "message_store.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>

namespace {
constexpr uint32_t STORE_MAGIC=0x354D3554; // T5M5
constexpr uint16_t STORE_VERSION=2;
constexpr uint16_t LEGACY_STORE_VERSION=1;
constexpr uint16_t LEGACY_STORE_CAPACITY=96;
constexpr char STORE_PATH[]="/ui_messages.bin";
constexpr char STORE_TEMP_PATH[]="/ui_messages.v2.tmp";
constexpr char STORE_BACKUP_PATH[]="/ui_messages.v1.bak";

struct LegacyStoredMessageV1 {
    uint32_t sequence;
    uint32_t timestamp;
    uint32_t ack;
    uint8_t kind;
    uint8_t state;
    uint8_t key[7];
    char text[145];
};

static_assert(sizeof(MeshInkMessageStoreHeader)==16,
              "journal header layout changed; update migration explicitly");
static_assert(sizeof(LegacyStoredMessageV1)==168,
              "v1 message layout must match the historical testing store");
static_assert(sizeof(MeshInkStoredMessage)==172,
              "v2 message layout changed; bump journal version explicitly");

MeshInkMessageStore journal;
}

MeshInkMessageStore& meshink_message_store(){return journal;}

bool MeshInkMessageStore::write_full(const char* path){
    File f=SPIFFS.open(path,"w");
    if(!f)return false;
    const size_t hw=f.write((const uint8_t*)&header_,sizeof(header_));
    const size_t rw=f.write((const uint8_t*)records_,sizeof(records_));
    f.close();
    return hw==sizeof(header_)&&rw==sizeof(records_);
}

bool MeshInkMessageStore::create_empty(){
    header_={STORE_MAGIC,STORE_VERSION,(uint16_t)MESHINK_MESSAGE_CAPACITY,0,0,0};
    memset(records_,0,sizeof(records_));
    if(!write_full(STORE_PATH)){
        Serial.println("[T5-STORE] ERROR creating v2 message journal");
        return false;
    }
    Serial.printf("[T5-STORE] created v2 journal: %u messages, %u bytes\n",
                  (unsigned)MESHINK_MESSAGE_CAPACITY,
                  (unsigned)(sizeof(header_)+sizeof(records_)));
    return true;
}

void MeshInkMessageStore::write_header(){
    if(!initialized_)return;
    File f=SPIFFS.open(STORE_PATH,"r+");
    if(!f){Serial.println("[T5-STORE] ERROR opening journal header");return;}
    const bool seek_ok=f.seek(0);
    const size_t written=seek_ok?f.write((const uint8_t*)&header_,sizeof(header_)):0;
    f.close();
    if(!seek_ok||written!=sizeof(header_))
        Serial.printf("[T5-STORE] ERROR writing journal header bytes=%u/%u\n",
                      (unsigned)written,(unsigned)sizeof(header_));
}

void MeshInkMessageStore::write_record(uint16_t physical){
    if(!initialized_||physical>=MESHINK_MESSAGE_CAPACITY)return;
    File f=SPIFFS.open(STORE_PATH,"r+");
    if(!f){Serial.println("[T5-STORE] ERROR opening journal record");return;}
    const size_t offset=sizeof(MeshInkMessageStoreHeader)+
                        (size_t)physical*sizeof(MeshInkStoredMessage);
    const bool seek_ok=f.seek(offset);
    const size_t written=seek_ok?
        f.write((const uint8_t*)&records_[physical],sizeof(MeshInkStoredMessage)):0;
    f.close();
    if(!seek_ok||written!=sizeof(MeshInkStoredMessage))
        Serial.printf("[T5-STORE] ERROR writing journal record=%u bytes=%u/%u\n",
                      (unsigned)physical,(unsigned)written,
                      (unsigned)sizeof(MeshInkStoredMessage));
}

bool MeshInkMessageStore::migrate_v1(const MeshInkMessageStoreHeader& legacy_header){
    if(legacy_header.capacity!=LEGACY_STORE_CAPACITY||
       legacy_header.head>=legacy_header.capacity||
       legacy_header.count>legacy_header.capacity)return false;

    File old=SPIFFS.open(STORE_PATH,"r");
    if(!old)return false;
    const size_t expected=sizeof(MeshInkMessageStoreHeader)+
                          (size_t)legacy_header.capacity*sizeof(LegacyStoredMessageV1);
    if((size_t)old.size()!=expected){old.close();return false;}

    header_={STORE_MAGIC,STORE_VERSION,(uint16_t)MESHINK_MESSAGE_CAPACITY,0,
             (uint16_t)min((size_t)legacy_header.count,MESHINK_MESSAGE_CAPACITY),
             legacy_header.sequence};
    memset(records_,0,sizeof(records_));

    for(uint16_t logical=0;logical<header_.count;++logical){
        const uint16_t physical=(legacy_header.head+logical)%legacy_header.capacity;
        const size_t offset=sizeof(MeshInkMessageStoreHeader)+
                            (size_t)physical*sizeof(LegacyStoredMessageV1);
        if(!old.seek(offset)){old.close();return false;}
        LegacyStoredMessageV1 legacy{};
        if(old.read((uint8_t*)&legacy,sizeof(legacy))!=sizeof(legacy)){
            old.close();return false;
        }
        MeshInkStoredMessage& current=records_[logical];
        current.sequence=legacy.sequence;
        current.timestamp=legacy.timestamp;
        current.ack=legacy.ack;
        current.kind=legacy.kind;
        current.state=legacy.state;
        memcpy(current.key,legacy.key,sizeof(current.key));
        memcpy(current.text,legacy.text,sizeof(current.text));
        current.path_len=MESHINK_MESSAGE_PATH_UNKNOWN;
        current.origin=(uint8_t)MeshInkMessageOrigin::LocalUi;
    }
    old.close();

    SPIFFS.remove(STORE_TEMP_PATH);
    if(!write_full(STORE_TEMP_PATH)){
        SPIFFS.remove(STORE_TEMP_PATH);
        return false;
    }

    SPIFFS.remove(STORE_BACKUP_PATH);
    if(!SPIFFS.rename(STORE_PATH,STORE_BACKUP_PATH)){
        SPIFFS.remove(STORE_TEMP_PATH);
        return false;
    }
    if(!SPIFFS.rename(STORE_TEMP_PATH,STORE_PATH)){
        SPIFFS.rename(STORE_BACKUP_PATH,STORE_PATH);
        SPIFFS.remove(STORE_TEMP_PATH);
        return false;
    }
    SPIFFS.remove(STORE_BACKUP_PATH);
    Serial.printf("[T5-STORE] migrated v1 history: %u messages -> v2 capacity %u\n",
                  (unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY);
    return true;
}

bool MeshInkMessageStore::begin(){
    if(initialized_)return true;

    // If power was lost after the old v1 file was renamed to the migration
    // backup but before the completed v2 temp file became live, restore the
    // original first and retry migration. Never interpret that state as a
    // missing history file and create an empty journal over it.
    if(!SPIFFS.exists(STORE_PATH)&&SPIFFS.exists(STORE_BACKUP_PATH)){
        SPIFFS.remove(STORE_TEMP_PATH);
        if(!SPIFFS.rename(STORE_BACKUP_PATH,STORE_PATH)){
            Serial.println("[T5-STORE] ERROR restoring v1 migration backup");
            return false;
        }
        Serial.println("[T5-STORE] recovered interrupted v1 migration");
    }

    File f=SPIFFS.open(STORE_PATH,"r");
    if(!f){
        const bool ok=create_empty();
        initialized_=ok;
        return ok;
    }

    MeshInkMessageStoreHeader disk{};
    const bool header_ok=f.read((uint8_t*)&disk,sizeof(disk))==sizeof(disk);
    const size_t file_size=f.size();
    f.close();

    if(header_ok&&disk.magic==STORE_MAGIC&&
       disk.version==STORE_VERSION&&
       disk.capacity==MESHINK_MESSAGE_CAPACITY&&
       disk.head<MESHINK_MESSAGE_CAPACITY&&disk.count<=MESHINK_MESSAGE_CAPACITY&&
       file_size==sizeof(MeshInkMessageStoreHeader)+sizeof(records_)){
        File current=SPIFFS.open(STORE_PATH,"r");
        if(!current)return false;
        const size_t hr=current.read((uint8_t*)&header_,sizeof(header_));
        const size_t rr=current.read((uint8_t*)records_,sizeof(records_));
        current.close();
        if(hr!=sizeof(header_)||rr!=sizeof(records_))return false;
        initialized_=true;
        // A power cut after the v2 file became live but before backup cleanup
        // can leave the old v1 backup behind. The validated v2 file wins.
        SPIFFS.remove(STORE_BACKUP_PATH);
        SPIFFS.remove(STORE_TEMP_PATH);
        Serial.printf("[T5-STORE] loaded v2 journal %u/%u messages\n",
                      (unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY);
        return true;
    }

    if(header_ok&&disk.magic==STORE_MAGIC&&disk.version==LEGACY_STORE_VERSION){
        if(migrate_v1(disk)){
            initialized_=true;
            return true;
        }
        Serial.println("[T5-STORE] ERROR v1 journal migration failed; original retained");
        return false;
    }

    Serial.printf("[T5-STORE] journal incompatible magic=%08lx version=%u capacity=%u; recreating\n",
                  (unsigned long)disk.magic,(unsigned)disk.version,(unsigned)disk.capacity);
    const bool ok=create_empty();
    initialized_=ok;
    return ok;
}

bool MeshInkMessageStore::find_physical(uint32_t sequence,uint16_t& physical) const{
    for(size_t i=0;i<header_.count;++i){
        const uint16_t p=(header_.head+i)%MESHINK_MESSAGE_CAPACITY;
        if(records_[p].sequence==sequence){physical=p;return true;}
    }
    return false;
}

MeshInkStoredMessage* MeshInkMessageStore::append(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        const char* text,uint32_t timestamp,UiMessageState state,
        uint32_t ack,MeshInkMessageOrigin origin){
    if(!initialized_&&!begin())return nullptr;
    uint16_t physical;
    if(header_.count<MESHINK_MESSAGE_CAPACITY){
        physical=(header_.head+header_.count)%MESHINK_MESSAGE_CAPACITY;
        header_.count++;
    }else{
        physical=header_.head;
        header_.head=(header_.head+1)%MESHINK_MESSAGE_CAPACITY;
    }
    MeshInkStoredMessage& item=records_[physical];
    memset(&item,0,sizeof(item));
    item.sequence=++header_.sequence;
    item.timestamp=timestamp;
    item.ack=ack;
    item.kind=(uint8_t)kind;
    item.state=(uint8_t)state;
    if(key&&key_len)memcpy(item.key,key,min(key_len,sizeof(item.key)));
    if(text)strncpy(item.text,text,sizeof(item.text)-1);
    item.path_len=MESHINK_MESSAGE_PATH_UNKNOWN;
    item.origin=(uint8_t)origin;
    write_record(physical);
    write_header();
    return &item;
}

void MeshInkMessageStore::update_state(uint32_t sequence,UiMessageState state){
    uint16_t p;
    if(!find_physical(sequence,p))return;
    records_[p].state=(uint8_t)state;
    write_record(p);
}

void MeshInkMessageStore::update_ack(uint32_t sequence,uint32_t ack){
    uint16_t p;
    if(!find_physical(sequence,p))return;
    records_[p].ack=ack;
    write_record(p);
}

void MeshInkMessageStore::update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len){
    uint16_t p;
    if(!find_physical(sequence,p))return;
    records_[p].snr_q4=snr_q4;
    records_[p].path_len=path_len;
    records_[p].flags|=MESHINK_MESSAGE_HAS_RX;
    write_record(p);
}

void MeshInkMessageStore::update_route(uint32_t sequence,bool flood){
    uint16_t p;
    if(!find_physical(sequence,p))return;
    records_[p].flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
    if(flood)records_[p].flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
    else records_[p].flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
    write_record(p);
}

void MeshInkMessageStore::update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4){
    uint16_t p;
    if(!find_physical(sequence,p))return;
    records_[p].repeats=repeats;
    records_[p].repeat_snr_q4=snr_q4;
    write_record(p);
}

bool MeshInkMessageStore::mark_delivered_by_ack(uint32_t ack){
    if(!ack)return false;
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(records_[p].ack==ack&&records_[p].state!=(uint8_t)UiMessageState::Received){
            records_[p].state=(uint8_t)UiMessageState::Delivered;
            write_record(p);
            return true;
        }
    }
    return false;
}

uint32_t MeshInkMessageStore::find_matching_outgoing(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        uint32_t timestamp,const char* text) const{
    if(!key||!text)return 0;
    for(size_t n=header_.count;n>0;--n){
        const auto& item=at(n-1);
        if(item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
           item.timestamp!=timestamp||memcmp(item.key,key,min(key_len,sizeof(item.key)))||
           strncmp(item.text,text,sizeof(item.text)))continue;
        return item.sequence;
    }
    return 0;
}
