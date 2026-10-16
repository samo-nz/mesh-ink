#include "message_store.h"
#include "hardware/performance.h"
#include "protocol/mesh_protocol.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
constexpr uint32_t STORE_MAGIC=0x354D3554; // T5M5
constexpr uint16_t CORE_STORE_VERSION=3;
constexpr uint16_t MESHTASTIC_STORE_VERSION=4;
constexpr char LEGACY_STORE_PATH[]="/ui_messages.bin";
constexpr char CORE_STORE_PATH[]="/meshcore_messages.bin";
constexpr char CORE_INVALID_PATH[]="/meshcore_messages.invalid.bak";
constexpr char MESHTASTIC_STORE_PATH[]="/meshtastic_messages.bin";
// SPIFFS_OBJ_NAME_LEN is 32 bytes INCLUDING NUL. The former
// "/meshtastic_messages.invalid.bak" is 32 visible bytes and cannot be
// created, leaving a first-use partial journal stuck in recovery forever.
constexpr char MESHTASTIC_INVALID_PATH[]="/meshtastic_messages.bad";
constexpr char CORE_RECOVERY_PATH[]="/mc_messages_recovery.bin";
constexpr char MESHTASTIC_RECOVERY_PATH[]="/mt_messages_recovery.bin";
constexpr char LEGACY_CORE_RECOVERY_PATH[]="/ui_messages.recovery.bin";
constexpr size_t SPIFFS_PATH_CAPACITY=32;
static_assert(sizeof(CORE_INVALID_PATH)<=SPIFFS_PATH_CAPACITY,
              "MeshCore recovery filename exceeds SPIFFS name limit");
static_assert(sizeof(MESHTASTIC_INVALID_PATH)<=SPIFFS_PATH_CAPACITY,
              "Meshtastic recovery filename exceeds SPIFFS name limit");
static_assert(sizeof(CORE_RECOVERY_PATH)<=SPIFFS_PATH_CAPACITY,"MeshCore recovery filename too long");
static_assert(sizeof(MESHTASTIC_RECOVERY_PATH)<=SPIFFS_PATH_CAPACITY,"Meshtastic recovery filename too long");

static_assert(sizeof(MeshInkMessageStoreHeader)==16,
              "journal header layout changed; bump store version explicitly");
// Legacy disk records remain exactly 188 bytes for existing MeshCore users.
struct MeshInkStoredMessageV3 {
    uint32_t sequence=0,timestamp=0,ack=0;
    uint8_t kind=0,state=0,key[7]{};
    char text[MESHINK_MESSAGE_TEXT_BYTES]{};
    int8_t snr_q4=0,repeat_snr_q4=0;
    uint8_t path_len=MESHINK_MESSAGE_PATH_UNKNOWN,repeats=0,flags=0,origin=0;
};
static_assert(sizeof(MeshInkStoredMessageV3)==188,"MeshCore v3 disk format changed");
static_assert(sizeof(MeshInkStoredMessage)==264,"Meshtastic v4 disk format changed");

MeshInkStoredMessage expand_v3(const MeshInkStoredMessageV3& old){
    MeshInkStoredMessage v{};
    v.sequence=old.sequence;v.timestamp=old.timestamp;v.ack=old.ack;
    v.kind=old.kind;v.state=old.state;
    memcpy(v.key,old.key,sizeof(v.key));memcpy(v.text,old.text,sizeof(old.text));
    v.snr_q4=old.snr_q4;v.repeat_snr_q4=old.repeat_snr_q4;
    v.path_len=old.path_len;v.repeats=old.repeats;
    v.flags=old.flags;v.origin=old.origin;
    return v;
}
MeshInkStoredMessageV3 shrink_v3(const MeshInkStoredMessage& v){
    MeshInkStoredMessageV3 old{};
    old.sequence=v.sequence;old.timestamp=v.timestamp;old.ack=v.ack;
    old.kind=v.kind;old.state=v.state;
    memcpy(old.key,v.key,sizeof(old.key));memcpy(old.text,v.text,sizeof(old.text));
    old.snr_q4=v.snr_q4;old.repeat_snr_q4=v.repeat_snr_q4;
    old.path_len=v.path_len;old.repeats=v.repeats;
    old.flags=v.flags;old.origin=v.origin;
    return old;
}

constexpr uint32_t STORE_FLASH_CPU_MHZ=240;

struct StoreCpuBoostScope {
    uint32_t previous_mhz=0;
    bool restore=false;

    explicit StoreCpuBoostScope(bool enabled=true){
        if(!enabled)return;
        previous_mhz=meshink_performance_cpu_mhz();
        if(previous_mhz<STORE_FLASH_CPU_MHZ)
            restore=meshink_performance_set_cpu_mhz(STORE_FLASH_CPU_MHZ);
    }
    ~StoreCpuBoostScope(){
        if(restore)meshink_performance_set_cpu_mhz(previous_mhz);
    }
};

MeshInkMessageStore meshcore_journal(CORE_STORE_PATH,CORE_INVALID_PATH);
MeshInkMessageStore meshtastic_journal(MESHTASTIC_STORE_PATH,MESHTASTIC_INVALID_PATH,true);
// Old single-protocol installations wrote only the MeshCore journal here.
static bool copy_legacy_meshcore_journal(){
    const char* source_path=SPIFFS.exists(LEGACY_CORE_RECOVERY_PATH)
        ?LEGACY_CORE_RECOVERY_PATH:LEGACY_STORE_PATH;
    if(SPIFFS.exists(CORE_STORE_PATH)||SPIFFS.exists(CORE_RECOVERY_PATH)||
       !SPIFFS.exists(source_path))return true;
    constexpr char STAGING[]="/mc_migrate.tmp";
    SPIFFS.remove(STAGING);
    File source=SPIFFS.open(source_path,"r");
    File target=SPIFFS.open(STAGING,"w");
    if(!source||!target){
        if(source)source.close();if(target)target.close();
        SPIFFS.remove(STAGING);return false;
    }
    const size_t expected=source.size();
    uint8_t block[512];size_t copied=0;bool ok=true;
    while(ok&&copied<expected){
        const size_t wanted=min(sizeof(block),expected-copied);
        const size_t got=source.read(block,wanted);
        ok=got==wanted&&target.write(block,got)==got;
        copied+=got;
    }
    target.flush();source.close();target.close();
    if(!ok||copied!=expected){SPIFFS.remove(STAGING);return false;}
    File from=SPIFFS.open(source_path,"r");
    File to=SPIFFS.open(STAGING,"r");
    ok=from&&to&&from.size()==expected&&to.size()==expected;
    uint8_t left[512],right[512];size_t verified=0;
    while(ok&&verified<expected){
        const size_t wanted=min(sizeof(left),expected-verified);
        ok=from.read(left,wanted)==wanted&&to.read(right,wanted)==wanted&&
           memcmp(left,right,wanted)==0;
        verified+=wanted;
    }
    if(from)from.close();if(to)to.close();
    if(!ok){SPIFFS.remove(STAGING);return false;}
    if(!SPIFFS.rename(STAGING,CORE_STORE_PATH)){
        SPIFFS.remove(STAGING);return false;
    }
    Serial.printf("[T5-STORE] legacy journal migrated and verified %u bytes from %s; original retained\n",
                  (unsigned)expected,source_path);
    return true;
}

static size_t record_offset(uint16_t physical,bool extended){
    return sizeof(MeshInkMessageStoreHeader)+
           (size_t)physical*(extended?sizeof(MeshInkStoredMessage)
                                     :sizeof(MeshInkStoredMessageV3));
}
static bool read_record(File& f,uint16_t physical,MeshInkStoredMessage& out,bool extended){
    if(!f.seek(record_offset(physical,extended)))return false;
    if(extended)return f.read((uint8_t*)&out,sizeof(out))==sizeof(out);
    MeshInkStoredMessageV3 old{};
    if(f.read((uint8_t*)&old,sizeof(old))!=sizeof(old))return false;
    out=expand_v3(old);
    return true;
}

static bool incomplete_direct_state(uint8_t state){
    switch((UiMessageState)state){
        case UiMessageState::Sending:
        case UiMessageState::Sent:
        case UiMessageState::Retrying1:
        case UiMessageState::Retrying2:
        case UiMessageState::Retrying3:
        case UiMessageState::Retrying4:
        case UiMessageState::Retrying5:
            return true;
        default:
            return false;
    }
}

static bool write_record_to(File& f,uint16_t physical,const MeshInkStoredMessage& record,
                            bool extended){
    if(!f.seek(record_offset(physical,extended)))return false;
    if(extended)return f.write((const uint8_t*)&record,sizeof(record))==sizeof(record);
    const MeshInkStoredMessageV3 old=shrink_v3(record);
    return f.write((const uint8_t*)&old,sizeof(old))==sizeof(old;

}
}

const char* MeshInkMessageStore::recovery_path() const {
    return expanded_records_?MESHTASTIC_RECOVERY_PATH:CORE_RECOVERY_PATH;
}
size_t MeshInkMessageStore::disk_record_size() const {
    return expanded_records_?sizeof(MeshInkStoredMessage):sizeof(MeshInkStoredMessageV3);
}
uint16_t MeshInkMessageStore::disk_version() const {
    return expanded_records_?MESHTASTIC_STORE_VERSION:CORE_STORE_VERSION;
}

MeshInkMessageStore& meshink_message_store(){
    // The selected protocol is fixed for the entire boot session. MeshCore BLE
    // companion and local mode always resolve to the same flash-backed journal.
    return mesh_protocol_descriptor().id==2?meshtastic_journal:meshcore_journal;
}

bool MeshInkMessageStore::ensure_cache(){
    if(records_)return true;
    const size_t bytes=MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage);
    records_=(MeshInkStoredMessage*)heap_caps_malloc(bytes,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT);
    cache_in_psram_=records_!=nullptr;
    if(!records_)records_=(MeshInkStoredMessage*)heap_caps_malloc(bytes,MALLOC_CAP_8BIT);
    if(!records_){
        Serial.printf("[T5-STORE] WARN message cache allocation failed bytes=%u\n",(unsigned)bytes);
        return false;
    }
    for(size_t i=0;i<MESHINK_MESSAGE_CAPACITY;++i)
        records_[i]=MeshInkStoredMessage{};
    return true;
}

bool MeshInkMessageStore::load_cache(File& source){
    if(!ensure_cache())return false;
    StoreCpuBoostScope cpu_boost;
    bool ok=source.seek(sizeof(MeshInkMessageStoreHeader));
    if(expanded_records_){
        const size_t bytes=MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage);
        ok=ok&&source.read((uint8_t*)records_,bytes)==bytes;
    }else{
        for(size_t i=0;ok&&i<MESHINK_MESSAGE_CAPACITY;++i){
            MeshInkStoredMessageV3 v3{};
            ok=source.read((uint8_t*)&v3,sizeof(v3))==sizeof(v3);
            if(ok)records_[i]=expand_v3(v3);
        }
    }
    if(!ok){
        heap_caps_free(records_);records_=nullptr;cache_in_psram_=false;
        return false;
    }
    return true;
}

bool MeshInkMessageStore::create_empty(){
    StoreCpuBoostScope cpu_boost;
    header_={STORE_MAGIC,disk_version(),(uint16_t)MESHINK_MESSAGE_CAPACITY,0,0,0};
    File f=SPIFFS.open(active_path_,"w");
    if(!f){
        Serial.printf("[T5-STORE] ERROR cannot create new journal path=%s\n",path_);
        return false;
    }
    const size_t hw=f.write((const uint8_t*)&header_,sizeof(header_));
    MeshInkStoredMessage blank{};
    bool ok=hw==sizeof(header_);
    for(size_t i=0;ok&&i<MESHINK_MESSAGE_CAPACITY;++i)
        ok=write_record_to(f,(uint16_t)i,blank,expanded_records_);
    f.close();
    if(!ok){
        Serial.printf("[T5-STORE] ERROR creating v%u journal path=%s; removing incomplete new file\n",
                      (unsigned)disk_version(),path_);
        SPIFFS.remove(active_path_); // Only reached after opening this path for a NEW journal.
        return false;
    }
    // Check the newly created file after closing it, not just the write counts.
    // Incomplete first-use creation must never look like a previous journal.
    const size_t expected_bytes=sizeof(header_)+MESHINK_MESSAGE_CAPACITY*disk_record_size();
    File verify=SPIFFS.open(active_path_,"r");
    MeshInkMessageStoreHeader written{};
    const bool verified=verify&&verify.size()==expected_bytes&&
        verify.read((uint8_t*)&written,sizeof(written))==sizeof(written)&&
        memcmp(&written,&header_,sizeof(written))==0;
    if(verify)verify.close();
    if(!verified){
        Serial.printf("[T5-STORE] ERROR verifying newly created journal path=%s; removing incomplete new file\n",
                      path_);
        SPIFFS.remove(active_path_);
        return false;
    }
    file_=SPIFFS.open(active_path_,"r+");
    if(!file_){
        Serial.printf("[T5-STORE] ERROR reopening newly created journal path=%s; will retry on next start\n",
                      path_);
        return false;
    }
    if(ensure_cache())
        for(size_t i=0;i<MESHINK_MESSAGE_CAPACITY;++i)
            records_[i]=MeshInkStoredMessage{};
    Serial.printf("[T5-STORE] created flash-backed v%u journal: %u messages, %u bytes cache=%s\n",
                  (unsigned)disk_version(),(unsigned)MESHINK_MESSAGE_CAPACITY,
                  (unsigned)(sizeof(header_)+MESHINK_MESSAGE_CAPACITY*disk_record_size()),
                  records_?(cache_in_psram_?"PSRAM":"RAM"):"NONE");
    return true;
}

void MeshInkMessageStore::write_header(){
    if(!initialized_||!file_)return;
    StoreCpuBoostScope cpu_boost;
    const bool seek_ok=file_.seek(0);
    const size_t written=seek_ok?file_.write((const uint8_t*)&header_,sizeof(header_)):0;
    file_.flush();
    if(!seek_ok||written!=sizeof(header_))
        Serial.printf("[T5-STORE] ERROR writing journal header bytes=%u/%u\n",
                      (unsigned)written,(unsigned)sizeof(header_));
}

bool MeshInkMessageStore::write_record(uint16_t physical,const MeshInkStoredMessage& record){
    if(!initialized_||!file_||physical>=MESHINK_MESSAGE_CAPACITY)return false;
    StoreCpuBoostScope cpu_boost;
    const bool ok=write_record_to(file_,physical,record,expanded_records_);
    if(ok){
        file_.flush();
    }
    if(!ok){
        Serial.printf("[T5-STORE] ERROR writing journal record=%u\n",(unsigned)physical);
        return false;
    }
    if(records_)records_[physical]=record;
    return true;
}

void MeshInkMessageStore::prepare_for_restore(){
    if(file_){file_.flush();file_.close();}
    initialized_=false;
    if(records_){heap_caps_free(records_);records_=nullptr;cache_in_psram_=false;}
}

bool MeshInkMessageStore::begin(){
    if(initialized_)return true;
    StoreCpuBoostScope cpu_boost;
    // Once a protocol moves to recovery, the recovery journal takes
    // precedence on all later boots. Never reopen a damaged primary.
    active_path_=SPIFFS.exists(recovery_path())?recovery_path():path_;
    Serial.printf("[T5-STORE] protocol=%s active journal=%s\n",
                  expanded_records_?"Meshtastic":"MeshCore",active_path_);
    // Import the complete, old MeshCore-only physical journal, without
    // decoding or altering any records, and retain the source as a fallback.
    if(path_==CORE_STORE_PATH&&!copy_legacy_meshcore_journal()){
        Serial.println("[T5-STORE] legacy journal migration failed; refusing empty replacement");
        return false;
    }


    // A missing journal is normal the FIRST time a protocol is selected.
    // Do not treat an absent Meshtastic journal as a corrupt legacy file.
    // An open failure is NOT evidence the file does not exist: never
    // overwrite a journal that the filesystem reports as present.
    if(!SPIFFS.exists(active_path_)){
        Serial.printf("[T5-STORE] journal absent path=%s; creating fresh v%u journal\n",
                      active_path_,(unsigned)disk_version());
        const bool ok=create_empty();
        initialized_=ok;
        return ok;
    }
    File f=SPIFFS.open(active_path_,"r");
    if(!f){
        if(active_path_==path_&&!SPIFFS.exists(recovery_path())){
            // Match the main-branch hotfix: never truncate an unreadable
            // existing journal. Establish a durable alternative path.
            Serial.printf("[T5-STORE] unreadable primary %s retained; creating separate recovery journal\n",path_);
            active_path_=recovery_path();
            const bool ok=create_empty();
            initialized_=ok;
            return ok;
        }
        Serial.printf("[T5-STORE] ERROR recovery journal %s unreadable; original retained\n",
                      active_path_);
        return false;
    }

    MeshInkMessageStoreHeader disk{};
    const bool header_ok=f.read((uint8_t*)&disk,sizeof(disk))==sizeof(disk);
    const size_t file_size=f.size();f.close();

    const size_t expected_size=sizeof(MeshInkMessageStoreHeader)+
                               MESHINK_MESSAGE_CAPACITY*disk_record_size();
    // The previous Meshtastic journal contains development-only test messages.
    // Discard ONLY an identified 188-byte Meshtastic v3 file. Never wipe the
    // independent MeshCore journal or unknown files.
    const size_t old_size=sizeof(MeshInkMessageStoreHeader)+
                          MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessageV3);
    if(expanded_records_&&header_ok&&disk.magic==STORE_MAGIC&&
       disk.version==CORE_STORE_VERSION&&disk.capacity==MESHINK_MESSAGE_CAPACITY&&
       file_size==old_size){
        Serial.println("[MT-JOURNAL] discarding development v3 history for new v4");
        if(!SPIFFS.remove(active_path_))return false;
        const bool ok=create_empty();
        initialized_=ok;
        return ok;
    }
    if(header_ok&&disk.magic==STORE_MAGIC&&disk.version==disk_version()&&
       disk.capacity==MESHINK_MESSAGE_CAPACITY&&disk.head<MESHINK_MESSAGE_CAPACITY&&
       disk.count<=MESHINK_MESSAGE_CAPACITY&&file_size==expected_size){
        header_=disk;
        File cache_source=SPIFFS.open(active_path_,"r");
        const bool cache_loaded=cache_source&&load_cache(cache_source);
        if(cache_source)cache_source.close();
        file_=SPIFFS.open(active_path_,"r+");if(!file_)return false;initialized_=true;

        // No in-flight direct-send runtime survives a reboot. Any journal
        // record still in a transient sending/retry state is therefore stale
        // and must become a durable FAILED record before history is exposed.
        size_t recovered_failed=0;
        size_t recovery_errors=0;
        for(size_t logical=0;logical<header_.count;++logical){
            const uint16_t physical=(header_.head+(uint16_t)logical)%MESHINK_MESSAGE_CAPACITY;
            MeshInkStoredMessage item{};
            if(records_)item=records_[physical];
            else if(!read_record(file_,physical,item,expanded_records_)){++recovery_errors;continue;}
            if(item.kind!=(uint8_t)MeshInkMessageKind::Direct||
               !incomplete_direct_state(item.state))continue;
            item.state=(uint8_t)UiMessageState::Failed;
            if(write_record(physical,item))++recovered_failed;
            else ++recovery_errors;
        }

        Serial.printf("[T5-STORE] loaded flash-backed v%u journal %u/%u messages; record-cache=%s header=%uB recovered-failed=%u errors=%u\n",
                      (unsigned)disk_version(),(unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY,
                      cache_loaded?(cache_in_psram_?"PSRAM":"RAM"):"NONE",
                      (unsigned)sizeof(header_),(unsigned)recovered_failed,
                      (unsigned)recovery_errors);
        return true;
    }

    Serial.printf("[T5-STORE] existing journal invalid path=%s size=%u expected=%u header=%u magic=%08lx version=%u capacity=%u; preserving before recreate\n",
                  path_,(unsigned)file_size,(unsigned)expected_size,header_ok?1U:0U,
                  (unsigned long)disk.magic,(unsigned)disk.version,(unsigned)disk.capacity);
    // Never delete a previous recovery copy; it may be the user's only
    // copy of older messages. Use numbered short paths for subsequent faults.
    char archived[SPIFFS_PATH_CAPACITY]{};
    bool archive_available=false;
    const bool meshtastic=strcmp(path_,MESHTASTIC_STORE_PATH)==0;
    for(unsigned attempt=0;attempt<=16;++attempt){
        if(attempt==0){
            snprintf(archived,sizeof(archived),"%s",invalid_path_);
        }else{
            snprintf(archived,sizeof(archived),
                     meshtastic?"/mt_journal_bad.%u":"/mc_journal_bad.%u",attempt);
        }
        if(!SPIFFS.exists(archived)){archive_available=true;break;}
    }
    if(!archive_available){
        Serial.println("[T5-STORE] ERROR no unused recovery filename; original retained");
        return false;
    }
    if(!SPIFFS.rename(active_path_,archived)){
        // Main-branch journal hotfix, applied to BOTH independent journals:
        // If SPIFFS refuses to rename the damaged primary, keep it untouched
        // and start an alternative journal, then prefer that path at reboot.
        if(active_path_==path_&&!SPIFFS.exists(recovery_path())){
            Serial.printf("[T5-STORE] rename failed for %s; primary retained, starting %s\n",
                          path_,recovery_path());
            active_path_=recovery_path();
            const bool recovered=create_empty();
            initialized_=recovered;
            return recovered;
        }
        Serial.printf("[T5-STORE] ERROR cannot preserve invalid recovery journal %s; original retained\n",
                      active_path_);
        return false;
    }
    Serial.printf("[T5-STORE] unsupported journal preserved at %s; initializing fresh v%u journal\n",
                  archived,(unsigned)disk_version());
    const bool ok=create_empty();
    if(!ok){
        SPIFFS.remove(path_); // only an incomplete newly created file
        if(!SPIFFS.rename(archived,active_path_))
            Serial.printf("[T5-STORE] ERROR restoring preserved journal %s; archive retained\n",
                          archived);
        return false;
    }
    initialized_=true;
    return true;
}

bool MeshInkMessageStore::read(size_t logical,MeshInkStoredMessage& out) const{
    if(!initialized_||logical>=header_.count)return false;
    const uint16_t physical=(header_.head+(uint16_t)logical)%MESHINK_MESSAGE_CAPACITY;
    if(records_){
        out=records_[physical];
        return true;
    }
    if(!file_)return false;
    StoreCpuBoostScope cpu_boost;
    const bool ok=read_record(file_,physical,out,expanded_records_);
    return ok;
}

bool MeshInkMessageStore::find_physical(uint32_t sequence,uint16_t& physical) const{
    if(!initialized_||!sequence)return false;
    if(records_){
        for(size_t n=header_.count;n>0;--n){
            const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
            if(records_[p].sequence==sequence){physical=p;return true;}
        }
        return false;
    }
    if(!file_)return false;
    StoreCpuBoostScope cpu_boost;
    MeshInkStoredMessage item{};
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(!read_record(file_,p,item,expanded_records_))break;
        if(item.sequence==sequence){physical=p;return true;}
    }
    return false;
}

uint32_t MeshInkMessageStore::append(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        const char* text,uint32_t timestamp,UiMessageState state,
        uint32_t ack,MeshInkMessageOrigin origin,
        bool has_rx,int8_t snr_q4,uint8_t path_len,bool unread,uint8_t protocol_id){
    // Reject oversized text rather than silently truncating the record. Core
    // retains its 160-byte v3 limit; Meshtastic v4 holds all 233 payload bytes.
    const size_t max_text=expanded_records_?MESHINK_MESHTASTIC_MESSAGE_TEXT_MAX
                                           :MESHINK_MESSAGE_TEXT_MAX;
    if(text && strnlen(text,max_text+2)>max_text){
        Serial.printf("[T5-STORE] ERROR text exceeds %u-byte journal limit\n",
                      (unsigned)max_text);
        return 0;
    }
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
    item.path_len=has_rx?path_len:MESHINK_MESSAGE_PATH_UNKNOWN;
    item.origin=(uint8_t)(
        ((protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)<<MESHINK_MESSAGE_PROTOCOL_SHIFT) |
        ((uint8_t)origin&MESHINK_MESSAGE_ORIGIN_MASK));
    if(has_rx){
        item.snr_q4=snr_q4;
        item.flags|=MESHINK_MESSAGE_HAS_RX;
    }
    if(unread)item.flags|=MESHINK_MESSAGE_UNREAD;

    if(!file_)return 0;
    StoreCpuBoostScope cpu_boost;
    const bool record_ok=write_record_to(file_,physical,item);
    bool header_ok=false;
    if(record_ok){
        const bool header_seek_ok=file_.seek(0);
        if(header_seek_ok){
            header_ok=file_.write((const uint8_t*)&next,sizeof(next))==sizeof(next);
        }
    }
    if(record_ok&&header_ok){
        file_.flush();
    }
    if(!record_ok||!header_ok){
        Serial.println("[T5-STORE] ERROR appending journal record");
        return 0;
    }
    if(records_)records_[physical]=item;
    header_=next;
    return item.sequence;
}

bool MeshInkMessageStore::erase_direct_peer(uint32_t node_id,uint8_t protocol_id){
    if(!initialized_||!file_||!node_id)return false;
    const uint8_t key[4]={(uint8_t)node_id,(uint8_t)(node_id>>8),
                          (uint8_t)(node_id>>16),(uint8_t)(node_id>>24)};
    // Explicit user deletion. Never modify shared-channel history or
    // messages owned by a different protocol.
    StoreCpuBoostScope cpu_boost;
    MeshInkStoredMessage item{};
    for(size_t n=0;n<header_.count;++n){
        const uint16_t p=(header_.head+n)%MESHINK_MESSAGE_CAPACITY;
        if(records_)item=records_[p];
        else if(!read_record(file_,p,item,expanded_records_))return false;
        if(!item.sequence||item.kind!=(uint8_t)MeshInkMessageKind::Direct||
           meshink_message_protocol(item)!=(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)||
           memcmp(item.key,key,sizeof(key))!=0)continue;
        // Synchronously overwrite the complete physical record, including
        // old message text. The store mirror only changes after flash succeeds.
        if(!write_record(p,MeshInkStoredMessage{}))return false;
    }
    file_.flush();
    return true;
}

bool MeshInkMessageStore::update_state(uint32_t sequence,UiMessageState state){
    uint16_t p;
    if(!find_physical(sequence,p)||!file_){
        Serial.printf("[T5-STORE] ERROR state update missing sequence=%lu\n",
                      (unsigned long)sequence);
        return false;
    }
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return false;
    if(item.state==(uint8_t)state)return true;
    item.state=(uint8_t)state;
    return write_record(p,item);
}

bool MeshInkMessageStore::mark_read_through(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,uint8_t protocol_id){
    if(!initialized_&&!begin())return false;
    MeshInkStoredMessage item{};
    if(!file_||!key||!key_len||key_len>sizeof(item.key))return false;

    // A single marker on the newest record for this conversation means every
    // older record for the same peer/channel is read. This keeps "mark read"
    // to one record write regardless of how many unread messages accumulated.
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+(uint16_t)n-1)%MESHINK_MESSAGE_CAPACITY;
        if(records_)item=records_[p];
        else if(!read_record(file_,p,item,expanded_records_))return false;
        if(item.kind!=(uint8_t)kind||
           meshink_message_protocol(item)!=(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)||
           memcmp(item.key,key,key_len))continue;

        const uint8_t next_flags=(uint8_t)(
            (item.flags|MESHINK_MESSAGE_READ_THROUGH)&
            (uint8_t)~MESHINK_MESSAGE_UNREAD);
        if(next_flags==item.flags)return true;
        item.flags=next_flags;
        return write_record(p,item);
    }
    return true;
}

bool MeshInkMessageStore::mark_matching_received_read(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        uint32_t timestamp,const char* text,uint8_t protocol_id){
    if(!initialized_&&!begin())return false;
    MeshInkStoredMessage item{};
    if(!file_||!key||!key_len||key_len>sizeof(item.key)||!text)return false;

    // The companion queue is FIFO, so clear the oldest unread journal record
    // matching the frame actually handed to the app. Do not create a
    // READ_THROUGH marker: a newer message may still be waiting in MeshCore.
    for(size_t logical=0;logical<header_.count;++logical){
        const uint16_t p=(header_.head+(uint16_t)logical)%MESHINK_MESSAGE_CAPACITY;
        if(records_)item=records_[p];
        else if(!read_record(file_,p,item,expanded_records_))return false;
        if(item.sequence==0||
           meshink_message_protocol(item)!=(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)||
           item.kind!=(uint8_t)kind||
           item.state!=(uint8_t)UiMessageState::Received||
           !(item.flags&MESHINK_MESSAGE_UNREAD)||
           item.timestamp!=timestamp||
           memcmp(item.key,key,key_len)||
           strncmp(item.text,text,sizeof(item.text)))continue;
        item.flags&=(uint8_t)~MESHINK_MESSAGE_UNREAD;
        return write_record(p,item);
    }
    return false;
}

void MeshInkMessageStore::update_ack(uint32_t sequence,uint32_t ack){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return;
    if(item.ack==ack)return;
    item.ack=ack;write_record(p,item);
}

void MeshInkMessageStore::update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return;
    if(item.snr_q4==snr_q4&&item.path_len==path_len&&(item.flags&MESHINK_MESSAGE_HAS_RX))return;
    item.snr_q4=snr_q4;item.path_len=path_len;item.flags|=MESHINK_MESSAGE_HAS_RX;
    write_record(p,item);
}

void MeshInkMessageStore::update_route(uint32_t sequence,bool flood){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return;
    const uint8_t before=item.flags;
    item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
    if(flood)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
    else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
    if(item.flags==before)return;
    write_record(p,item);
}

void MeshInkMessageStore::update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return;
    if(item.repeats==repeats&&item.repeat_snr_q4==snr_q4)return;
    item.repeats=repeats;item.repeat_snr_q4=snr_q4;write_record(p,item);
}

void MeshInkMessageStore::update_outgoing(
        uint32_t sequence,UiMessageState state,uint32_t ack,bool route_flood){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item,expanded_records_))return;
    const MeshInkStoredMessage before=item;
    item.state=(uint8_t)state;
    item.ack=ack;
    item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
    if(route_flood)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
    else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
    if(!memcmp(&before,&item,sizeof(item)))return;
    write_record(p,item);
}

bool MeshInkMessageStore::sync_and_verify_for_deep_sleep(
        uint32_t& disk_sequence,size_t& disk_count){
    disk_sequence=0;
    disk_count=0;
    if(!initialized_||!file_){
        Serial.println("[T5-STORE] deep-sleep verify failed: journal not open");
        return false;
    }

    StoreCpuBoostScope cpu_boost;
    const MeshInkMessageStoreHeader expected=header_;

    // Force all stdio/VFS buffers out, then close the writer handle. Reopening
    // from SPIFFS makes this a session-boundary durability check rather than
    // trusting the still-live File object's in-memory state.
    file_.flush();
    file_.close();

    File verify=SPIFFS.open(active_path_,"r");
    MeshInkMessageStoreHeader disk{};
    bool header_ok=false;
    bool tail_ok=true;
    uint32_t tail_sequence=0;

    if(verify){
        header_ok=verify.read((uint8_t*)&disk,sizeof(disk))==sizeof(disk) &&
                  disk.magic==STORE_MAGIC &&
                  disk.version==disk_version() &&
                  disk.capacity==MESHINK_MESSAGE_CAPACITY &&
                  disk.head<MESHINK_MESSAGE_CAPACITY &&
                  disk.count<=MESHINK_MESSAGE_CAPACITY;
        if(header_ok&&disk.count){
            const uint16_t physical=
                (uint16_t)((disk.head+disk.count-1)%MESHINK_MESSAGE_CAPACITY);
            MeshInkStoredMessage tail{};
            tail_ok=read_record(verify,physical,tail,expanded_records_);
            if(tail_ok)tail_sequence=tail.sequence;
        }
        verify.close();
    }

    disk_sequence=header_ok?disk.sequence:0;
    disk_count=header_ok?disk.count:0;

    // Keep the journal usable if board-level sleep is refused after this check.
    file_=SPIFFS.open(active_path_,"r+");
    const bool reopen_ok=(bool)file_;

    const bool header_matches=
        header_ok &&
        disk.magic==expected.magic &&
        disk.version==expected.version &&
        disk.capacity==expected.capacity &&
        disk.head==expected.head &&
        disk.count==expected.count &&
        disk.sequence==expected.sequence;
    const bool tail_matches=
        !expected.count || (tail_ok&&tail_sequence==expected.sequence);
    const bool ok=header_matches&&tail_matches&&reopen_ok;

    Serial.printf(
        "[T5-STORE] deep-sleep verify expected seq=%lu count=%u; disk seq=%lu count=%u tail=%lu header=%u tailok=%u reopen=%u result=%s\n",
        (unsigned long)expected.sequence,(unsigned)expected.count,
        (unsigned long)disk_sequence,(unsigned)disk_count,
        (unsigned long)tail_sequence,header_ok?1U:0U,tail_ok?1U:0U,
        reopen_ok?1U:0U,ok?"OK":"FAIL");

    return ok;
}

bool MeshInkMessageStore::mark_delivered_by_ack(uint32_t ack,uint8_t protocol_id){
    if(!initialized_||!file_||!ack)return false;
    // Normal operation scans the PSRAM/RAM journal mirror without changing
    // CPU frequency. Only the cache-allocation fallback touches flash here.
    StoreCpuBoostScope flash_boost(!records_);
    MeshInkStoredMessage item{};
    bool delivered=false;
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(records_)item=records_[p];
        else if(!read_record(file_,p,item,expanded_records_))break;
        if(meshink_message_protocol(item)==(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)&&
           item.ack==ack&&item.state!=(uint8_t)UiMessageState::Received){
            if(item.state==(uint8_t)UiMessageState::Delivered)delivered=true;
            else{
                item.state=(uint8_t)UiMessageState::Delivered;
                delivered=write_record(p,item);
            }
            break;
        }
    }
    return delivered;
}

uint32_t MeshInkMessageStore::find_matching_outgoing(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        uint32_t timestamp,const char* text,uint8_t protocol_id) const{
    if(!initialized_||!file_||!key||!text)return 0;
    if(records_){
        for(size_t n=header_.count;n>0;--n){
            const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
            const MeshInkStoredMessage& item=records_[p];
            if(meshink_message_protocol(item)!=(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)||
               item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
               item.timestamp!=timestamp||memcmp(item.key,key,min(key_len,sizeof(item.key)))||
               strncmp(item.text,text,sizeof(item.text)))continue;
            return item.sequence;
        }
        return 0;
    }
    StoreCpuBoostScope cpu_boost;
    MeshInkStoredMessage item{};
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(!read_record(file_,p,item,expanded_records_))break;
        if(meshink_message_protocol(item)!=(protocol_id&MESHINK_MESSAGE_PROTOCOL_MASK)||
           item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
           item.timestamp!=timestamp||memcmp(item.key,key,min(key_len,sizeof(item.key)))||
           strncmp(item.text,text,sizeof(item.text)))continue;
        return item.sequence;
    }
    return 0;
}
