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

static size_t record_offset(uint16_t physical){
    return sizeof(MeshInkMessageStoreHeader)+
           (size_t)physical*sizeof(MeshInkStoredMessage);
}

static bool read_record(File& f,uint16_t physical,MeshInkStoredMessage& out){
    if(!f.seek(record_offset(physical)))return false;
    return f.read((uint8_t*)&out,sizeof(out))==sizeof(out);
}

static bool write_record_to(File& f,uint16_t physical,const MeshInkStoredMessage& record){
    if(!f.seek(record_offset(physical)))return false;
    return f.write((const uint8_t*)&record,sizeof(record))==sizeof(record);
}
}

MeshInkMessageStore& meshink_message_store(){return journal;}

bool MeshInkMessageStore::create_empty(){
    header_={STORE_MAGIC,STORE_VERSION,(uint16_t)MESHINK_MESSAGE_CAPACITY,0,0,0};
    File f=SPIFFS.open(STORE_PATH,"w");
    if(!f)return false;
    const size_t hw=f.write((const uint8_t*)&header_,sizeof(header_));
    MeshInkStoredMessage blank{};
    bool ok=hw==sizeof(header_);
    for(size_t i=0;ok&&i<MESHINK_MESSAGE_CAPACITY;++i)
        ok=f.write((const uint8_t*)&blank,sizeof(blank))==sizeof(blank);
    f.close();
    if(!ok){
        Serial.println("[T5-STORE] ERROR creating v2 message journal");
        return false;
    }
    Serial.printf("[T5-STORE] created flash-backed v2 journal: %u messages, %u bytes\n",
                  (unsigned)MESHINK_MESSAGE_CAPACITY,
                  (unsigned)(sizeof(header_)+
                    MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage)));
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

bool MeshInkMessageStore::write_record(uint16_t physical,const MeshInkStoredMessage& record){
    if(!initialized_||physical>=MESHINK_MESSAGE_CAPACITY)return false;
    File f=SPIFFS.open(STORE_PATH,"r+");
    if(!f)return false;
    const bool ok=write_record_to(f,physical,record);
    f.close();
    if(!ok)
        Serial.printf("[T5-STORE] ERROR writing journal record=%u\n",(unsigned)physical);
    return ok;
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

    MeshInkMessageStoreHeader migrated={
        STORE_MAGIC,STORE_VERSION,(uint16_t)MESHINK_MESSAGE_CAPACITY,0,
        (uint16_t)min((size_t)legacy_header.count,MESHINK_MESSAGE_CAPACITY),
        legacy_header.sequence
    };

    SPIFFS.remove(STORE_TEMP_PATH);
    File fresh=SPIFFS.open(STORE_TEMP_PATH,"w");
    if(!fresh){old.close();return false;}
    bool ok=fresh.write((const uint8_t*)&migrated,sizeof(migrated))==sizeof(migrated);

    for(uint16_t logical=0;ok&&logical<MESHINK_MESSAGE_CAPACITY;++logical){
        MeshInkStoredMessage current{};
        if(logical<migrated.count){
            const uint16_t physical=(legacy_header.head+logical)%legacy_header.capacity;
            const size_t offset=sizeof(MeshInkMessageStoreHeader)+
                                (size_t)physical*sizeof(LegacyStoredMessageV1);
            if(!old.seek(offset)){ok=false;break;}
            LegacyStoredMessageV1 legacy{};
            if(old.read((uint8_t*)&legacy,sizeof(legacy))!=sizeof(legacy)){
                ok=false;break;
            }
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
        ok=fresh.write((const uint8_t*)&current,sizeof(current))==sizeof(current);
    }
    old.close();fresh.close();

    if(!ok){
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
    header_=migrated;
    Serial.printf("[T5-STORE] migrated v1 history: %u messages -> flash-backed v2 capacity %u\n",
                  (unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY);
    return true;
}

bool MeshInkMessageStore::begin(){
    if(initialized_)return true;

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

    const size_t expected_v2=sizeof(MeshInkMessageStoreHeader)+
                             MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage);
    if(header_ok&&disk.magic==STORE_MAGIC&&
       disk.version==STORE_VERSION&&
       disk.capacity==MESHINK_MESSAGE_CAPACITY&&
       disk.head<MESHINK_MESSAGE_CAPACITY&&disk.count<=MESHINK_MESSAGE_CAPACITY&&
       file_size==expected_v2){
        header_=disk;
        initialized_=true;
        SPIFFS.remove(STORE_BACKUP_PATH);
        SPIFFS.remove(STORE_TEMP_PATH);
        Serial.printf("[T5-STORE] loaded flash-backed v2 journal %u/%u messages; RAM=%u-byte header only\n",
                      (unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY,
                      (unsigned)sizeof(header_));
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

bool MeshInkMessageStore::read(size_t logical,MeshInkStoredMessage& out) const{
    if(!initialized_||logical>=header_.count)return false;
    const uint16_t physical=(header_.head+(uint16_t)logical)%MESHINK_MESSAGE_CAPACITY;
    File f=SPIFFS.open(STORE_PATH,"r");
    if(!f)return false;
    const bool ok=read_record(f,physical,out);
    f.close();
    return ok;
}

bool MeshInkMessageStore::find_physical(uint32_t sequence,uint16_t& physical) const{
    if(!initialized_||!sequence)return false;
    File f=SPIFFS.open(STORE_PATH,"r");
    if(!f)return false;
    MeshInkStoredMessage item{};
    bool found=false;
    for(size_t i=0;i<header_.count;++i){
        const uint16_t p=(header_.head+i)%MESHINK_MESSAGE_CAPACITY;
        if(!read_record(f,p,item))break;
        if(item.sequence==sequence){physical=p;found=true;break;}
    }
    f.close();
    return found;
}

uint32_t MeshInkMessageStore::append(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        const char* text,uint32_t timestamp,UiMessageState state,
        uint32_t ack,MeshInkMessageOrigin origin){
    if(!initialized_&&!begin())return 0;

    uint16_t physical;
    MeshInkMessageStoreHeader next=header_;
    if(next.count<MESHINK_MESSAGE_CAPACITY){
        physical=(next.head+next.count)%MESHINK_MESSAGE_CAPACITY;
        next.count++;
    }else{
        physical=next.head;
        next.head=(next.head+1)%MESHINK_MESSAGE_CAPACITY;
    }

    MeshInkStoredMessage item{};
    item.sequence=++next.sequence;
    item.timestamp=timestamp;
    item.ack=ack;
    item.kind=(uint8_t)kind;
    item.state=(uint8_t)state;
    if(key&&key_len)memcpy(item.key,key,min(key_len,sizeof(item.key)));
    if(text)strncpy(item.text,text,sizeof(item.text)-1);
    item.path_len=MESHINK_MESSAGE_PATH_UNKNOWN;
    item.origin=(uint8_t)origin;

    File f=SPIFFS.open(STORE_PATH,"r+");
    if(!f)return 0;
    const bool record_ok=write_record_to(f,physical,item);
    bool header_ok=false;
    if(record_ok&&f.seek(0))
        header_ok=f.write((const uint8_t*)&next,sizeof(next))==sizeof(next);
    f.close();
    if(!record_ok||!header_ok){
        Serial.println("[T5-STORE] ERROR appending journal record");
        return 0;
    }
    header_=next;
    return item.sequence;
}

void MeshInkMessageStore::update_state(uint32_t sequence,UiMessageState state){
    uint16_t p; if(!find_physical(sequence,p))return;
    MeshInkStoredMessage item{};File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;
    if(read_record(f,p,item)){item.state=(uint8_t)state;write_record_to(f,p,item);}f.close();
}

void MeshInkMessageStore::update_ack(uint32_t sequence,uint32_t ack){
    uint16_t p; if(!find_physical(sequence,p))return;
    MeshInkStoredMessage item{};File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;
    if(read_record(f,p,item)){item.ack=ack;write_record_to(f,p,item);}f.close();
}

void MeshInkMessageStore::update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len){
    uint16_t p; if(!find_physical(sequence,p))return;
    MeshInkStoredMessage item{};File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;
    if(read_record(f,p,item)){
        item.snr_q4=snr_q4;item.path_len=path_len;item.flags|=MESHINK_MESSAGE_HAS_RX;
        write_record_to(f,p,item);
    }f.close();
}

void MeshInkMessageStore::update_route(uint32_t sequence,bool flood){
    uint16_t p; if(!find_physical(sequence,p))return;
    MeshInkStoredMessage item{};File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;
    if(read_record(f,p,item)){
        item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
        if(flood)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
        else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
        write_record_to(f,p,item);
    }f.close();
}

void MeshInkMessageStore::update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4){
    uint16_t p; if(!find_physical(sequence,p))return;
    MeshInkStoredMessage item{};File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return;
    if(read_record(f,p,item)){
        item.repeats=repeats;item.repeat_snr_q4=snr_q4;
        write_record_to(f,p,item);
    }f.close();
}

bool MeshInkMessageStore::mark_delivered_by_ack(uint32_t ack){
    if(!initialized_||!ack)return false;
    File f=SPIFFS.open(STORE_PATH,"r+");if(!f)return false;
    MeshInkStoredMessage item{};
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(!read_record(f,p,item))break;
        if(item.ack==ack&&item.state!=(uint8_t)UiMessageState::Received){
            item.state=(uint8_t)UiMessageState::Delivered;
            const bool ok=write_record_to(f,p,item);f.close();return ok;
        }
    }
    f.close();return false;
}

uint32_t MeshInkMessageStore::find_matching_outgoing(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        uint32_t timestamp,const char* text) const{
    if(!initialized_||!key||!text)return 0;
    File f=SPIFFS.open(STORE_PATH,"r");if(!f)return 0;
    MeshInkStoredMessage item{};
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(!read_record(f,p,item))break;
        if(item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
           item.timestamp!=timestamp||memcmp(item.key,key,min(key_len,sizeof(item.key)))||
           strncmp(item.text,text,sizeof(item.text)))continue;
        f.close();return item.sequence;
    }
    f.close();return 0;
}
