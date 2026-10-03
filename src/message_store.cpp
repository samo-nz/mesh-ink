#include "message_store.h"
#include "t5_timing.h"

#include <Arduino.h>
#include <SPIFFS.h>
#include <esp_heap_caps.h>
#include <string.h>

namespace {
constexpr uint32_t STORE_MAGIC=0x354D3554; // T5M5
constexpr uint16_t STORE_VERSION=3;
constexpr char STORE_PATH[]="/ui_messages.bin";
constexpr char STORE_INVALID_PATH[]="/ui_messages.invalid.bak";

static_assert(sizeof(MeshInkMessageStoreHeader)==16,
              "journal header layout changed; bump store version explicitly");
static_assert(sizeof(MeshInkStoredMessage)==188,
              "message record layout changed; bump store version explicitly");

constexpr uint32_t STORE_FLASH_CPU_MHZ=240;

struct StoreCpuBoostScope {
    uint32_t previous_mhz=0;
    bool restore=false;

    StoreCpuBoostScope(){
        previous_mhz=getCpuFrequencyMhz();
        if(previous_mhz<STORE_FLASH_CPU_MHZ)
            restore=setCpuFrequencyMhz(STORE_FLASH_CPU_MHZ);
    }
    ~StoreCpuBoostScope(){
        if(restore)setCpuFrequencyMhz(previous_mhz);
    }
};

MeshInkMessageStore journal;
#if T5_TIMING_DIAGNOSTICS
uint32_t perf_reads=0;
uint32_t perf_read_us=0;
uint32_t perf_read_worst_us=0;
uint32_t perf_cache_reads=0;
uint32_t perf_writes=0;
uint32_t perf_write_us=0;
uint32_t perf_write_worst_us=0;
#endif

static size_t record_offset(uint16_t physical){
    return sizeof(MeshInkMessageStoreHeader)+
           (size_t)physical*sizeof(MeshInkStoredMessage);
}

static bool read_record(File& f,uint16_t physical,MeshInkStoredMessage& out){
    if(!f.seek(record_offset(physical)))return false;
    return f.read((uint8_t*)&out,sizeof(out))==sizeof(out);
}

static bool write_record_to(File& f,uint16_t physical,const MeshInkStoredMessage& record,
                            uint32_t* seek_us=nullptr,uint32_t* write_us=nullptr){
#if T5_TIMING_DIAGNOSTICS
    const uint32_t seek_started=micros();
#endif
    const bool seek_ok=f.seek(record_offset(physical));
#if T5_TIMING_DIAGNOSTICS
    if(seek_us)*seek_us=(uint32_t)(micros()-seek_started);
#else
    (void)seek_us;
#endif
    if(!seek_ok)return false;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t write_started=micros();
#endif
    const bool ok=f.write((const uint8_t*)&record,sizeof(record))==sizeof(record);
#if T5_TIMING_DIAGNOSTICS
    if(write_us)*write_us=(uint32_t)(micros()-write_started);
#else
    (void)write_us;
#endif
    return ok;
}
}

MeshInkMessageStore& meshink_message_store(){return journal;}

void meshink_message_store_perf_snapshot(MeshInkMessageStorePerf& out){
#if T5_TIMING_DIAGNOSTICS
    out.reads=perf_reads;
    out.read_us=perf_read_us;
    out.read_worst_us=perf_read_worst_us;
    out.cache_reads=perf_cache_reads;
    out.writes=perf_writes;
    out.write_us=perf_write_us;
    out.write_worst_us=perf_write_worst_us;
#else
    out=MeshInkMessageStorePerf{};
#endif
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
    memset(records_,0,bytes);
    return true;
}

bool MeshInkMessageStore::load_cache(File& source){
    if(!ensure_cache())return false;
    StoreCpuBoostScope cpu_boost;
    const size_t bytes=MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage);
    if(!source.seek(sizeof(MeshInkMessageStoreHeader)))return false;
    const uint32_t started=micros();
    const size_t got=source.read((uint8_t*)records_,bytes);
    const uint32_t elapsed=(uint32_t)(micros()-started);
#if T5_TIMING_DIAGNOSTICS
    ++perf_reads;perf_read_us+=elapsed;
    if(elapsed>perf_read_worst_us)perf_read_worst_us=elapsed;
#endif
    if(got!=bytes){
        heap_caps_free(records_);records_=nullptr;cache_in_psram_=false;
        return false;
    }
    Serial.printf("[T5-STORE] cache-load=%lu.%01lums storage=%s bytes=%u cpu=%luMHz\n",
                  (unsigned long)(elapsed/1000UL),
                  (unsigned long)((elapsed%1000UL)/100UL),
                  cache_in_psram_?"PSRAM":"RAM",(unsigned)bytes,
                  (unsigned long)getCpuFrequencyMhz());
    return true;
}

bool MeshInkMessageStore::create_empty(){
    StoreCpuBoostScope cpu_boost;
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
        Serial.println("[T5-STORE] ERROR creating v3 message journal");
        return false;
    }
    file_=SPIFFS.open(STORE_PATH,"r+");
    if(!file_){
        Serial.println("[T5-STORE] ERROR reopening new message journal");
        return false;
    }
    if(ensure_cache())
        memset(records_,0,MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage));
    Serial.printf("[T5-STORE] created flash-backed v3 journal: %u messages, %u bytes cache=%s\n",
                  (unsigned)MESHINK_MESSAGE_CAPACITY,
                  (unsigned)(sizeof(header_)+
                    MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage)),
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

bool MeshInkMessageStore::write_record(uint16_t physical,const MeshInkStoredMessage& record,const char* operation){
    if(!initialized_||!file_||physical>=MESHINK_MESSAGE_CAPACITY)return false;
    StoreCpuBoostScope cpu_boost;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t started=micros();
    uint32_t seek_us=0,write_us=0,flush_us=0;
#endif
    const bool ok=write_record_to(file_,physical,record,
#if T5_TIMING_DIAGNOSTICS
                                  &seek_us,&write_us
#else
                                  nullptr,nullptr
#endif
    );
    if(ok){
#if T5_TIMING_DIAGNOSTICS
        const uint32_t flush_started=micros();
#endif
        file_.flush();
#if T5_TIMING_DIAGNOSTICS
        flush_us=(uint32_t)(micros()-flush_started);
#endif
    }
#if T5_TIMING_DIAGNOSTICS
    const uint32_t elapsed=(uint32_t)(micros()-started);
    ++perf_writes;perf_write_us+=elapsed;
    if(elapsed>perf_write_worst_us)perf_write_worst_us=elapsed;
    Serial.printf("[T5-STOREPERF] op=%s record=%u seek=%lu.%01lums write=%lu.%01lums flush=%lu.%01lums total=%lu.%01lums cpu=%luMHz ok=%u\n",
                  operation?operation:"update",(unsigned)physical,
                  (unsigned long)(seek_us/1000UL),(unsigned long)((seek_us%1000UL)/100UL),
                  (unsigned long)(write_us/1000UL),(unsigned long)((write_us%1000UL)/100UL),
                  (unsigned long)(flush_us/1000UL),(unsigned long)((flush_us%1000UL)/100UL),
                  (unsigned long)(elapsed/1000UL),(unsigned long)((elapsed%1000UL)/100UL),
                  (unsigned long)getCpuFrequencyMhz(),ok?1U:0U);
#endif
    if(!ok){
        Serial.printf("[T5-STORE] ERROR writing journal record=%u\n",(unsigned)physical);
        return false;
    }
    if(records_)records_[physical]=record;
    return true;
}

bool MeshInkMessageStore::begin(){
    if(initialized_)return true;
    StoreCpuBoostScope cpu_boost;

    File f=SPIFFS.open(STORE_PATH,"r");
    if(!f){const bool ok=create_empty();initialized_=ok;return ok;}

    MeshInkMessageStoreHeader disk{};
    const bool header_ok=f.read((uint8_t*)&disk,sizeof(disk))==sizeof(disk);
    const size_t file_size=f.size();f.close();

    const size_t expected_v3=sizeof(MeshInkMessageStoreHeader)+
                             MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage);
    if(header_ok&&disk.magic==STORE_MAGIC&&disk.version==STORE_VERSION&&
       disk.capacity==MESHINK_MESSAGE_CAPACITY&&disk.head<MESHINK_MESSAGE_CAPACITY&&
       disk.count<=MESHINK_MESSAGE_CAPACITY&&file_size==expected_v3){
        header_=disk;
        File cache_source=SPIFFS.open(STORE_PATH,"r");
        const bool cache_loaded=cache_source&&load_cache(cache_source);
        if(cache_source)cache_source.close();
        file_=SPIFFS.open(STORE_PATH,"r+");if(!file_)return false;initialized_=true;
        Serial.printf("[T5-STORE] loaded flash-backed v3 journal %u/%u messages; record-cache=%s header=%uB\n",
                      (unsigned)header_.count,(unsigned)MESHINK_MESSAGE_CAPACITY,
                      cache_loaded?(cache_in_psram_?"PSRAM":"RAM"):"NONE",
                      (unsigned)sizeof(header_));
        return true;
    }

    Serial.printf("[T5-STORE] journal unsupported magic=%08lx version=%u capacity=%u; preserving before recreate\n",
                  (unsigned long)disk.magic,(unsigned)disk.version,(unsigned)disk.capacity);
    SPIFFS.remove(STORE_INVALID_PATH);
    if(!SPIFFS.rename(STORE_PATH,STORE_INVALID_PATH)){
        Serial.println("[T5-STORE] ERROR preserving unsupported message journal");
        return false;
    }
    const bool ok=create_empty();
    if(!ok){
        SPIFFS.remove(STORE_PATH);
        SPIFFS.rename(STORE_INVALID_PATH,STORE_PATH);
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
#if T5_TIMING_DIAGNOSTICS
        ++perf_cache_reads;
#endif
        return true;
    }
    if(!file_)return false;
    StoreCpuBoostScope cpu_boost;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t started=micros();
#endif
    const bool ok=read_record(file_,physical,out);
#if T5_TIMING_DIAGNOSTICS
    const uint32_t elapsed=(uint32_t)(micros()-started);
    ++perf_reads;perf_read_us+=elapsed;
    if(elapsed>perf_read_worst_us)perf_read_worst_us=elapsed;
#endif
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
        if(!read_record(file_,p,item))break;
        if(item.sequence==sequence){physical=p;return true;}
    }
    return false;
}

uint32_t MeshInkMessageStore::append(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        const char* text,uint32_t timestamp,UiMessageState state,
        uint32_t ack,MeshInkMessageOrigin origin,
        bool has_rx,int8_t snr_q4,uint8_t path_len){
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
    item.origin=(uint8_t)origin;
    if(has_rx){
        item.snr_q4=snr_q4;
        item.flags|=MESHINK_MESSAGE_HAS_RX;
    }

    if(!file_)return 0;
    StoreCpuBoostScope cpu_boost;
#if T5_TIMING_DIAGNOSTICS
    const uint32_t started=micros();
    uint32_t record_seek_us=0,record_write_us=0,header_seek_us=0,header_write_us=0,flush_us=0;
#endif
    const bool record_ok=write_record_to(file_,physical,item,
#if T5_TIMING_DIAGNOSTICS
                                         &record_seek_us,&record_write_us
#else
                                         nullptr,nullptr
#endif
    );
    bool header_ok=false;
    if(record_ok){
#if T5_TIMING_DIAGNOSTICS
        const uint32_t hs=micros();
#endif
        const bool header_seek_ok=file_.seek(0);
#if T5_TIMING_DIAGNOSTICS
        header_seek_us=(uint32_t)(micros()-hs);
#endif
        if(header_seek_ok){
#if T5_TIMING_DIAGNOSTICS
            const uint32_t hw=micros();
#endif
            header_ok=file_.write((const uint8_t*)&next,sizeof(next))==sizeof(next);
#if T5_TIMING_DIAGNOSTICS
            header_write_us=(uint32_t)(micros()-hw);
#endif
        }
    }
    if(record_ok&&header_ok){
#if T5_TIMING_DIAGNOSTICS
        const uint32_t fs=micros();
#endif
        file_.flush();
#if T5_TIMING_DIAGNOSTICS
        flush_us=(uint32_t)(micros()-fs);
#endif
    }
#if T5_TIMING_DIAGNOSTICS
    const uint32_t elapsed=(uint32_t)(micros()-started);
    perf_writes+=2;perf_write_us+=elapsed;
    if(elapsed>perf_write_worst_us)perf_write_worst_us=elapsed;
    Serial.printf("[T5-STOREPERF] op=append record=%u rseek=%lu.%01lums rwrite=%lu.%01lums hseek=%lu.%01lums hwrite=%lu.%01lums flush=%lu.%01lums total=%lu.%01lums cpu=%luMHz ok=%u\n",
                  (unsigned)physical,
                  (unsigned long)(record_seek_us/1000UL),(unsigned long)((record_seek_us%1000UL)/100UL),
                  (unsigned long)(record_write_us/1000UL),(unsigned long)((record_write_us%1000UL)/100UL),
                  (unsigned long)(header_seek_us/1000UL),(unsigned long)((header_seek_us%1000UL)/100UL),
                  (unsigned long)(header_write_us/1000UL),(unsigned long)((header_write_us%1000UL)/100UL),
                  (unsigned long)(flush_us/1000UL),(unsigned long)((flush_us%1000UL)/100UL),
                  (unsigned long)(elapsed/1000UL),(unsigned long)((elapsed%1000UL)/100UL),
                  (unsigned long)getCpuFrequencyMhz(),
                  (record_ok&&header_ok)?1U:0U);
#endif
    if(!record_ok||!header_ok){
        Serial.println("[T5-STORE] ERROR appending journal record");
        return 0;
    }
    if(records_)records_[physical]=item;
    header_=next;
    return item.sequence;
}

void MeshInkMessageStore::update_state(uint32_t sequence,UiMessageState state){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    if(item.state==(uint8_t)state)return;
    item.state=(uint8_t)state;write_record(p,item,"state");
}

void MeshInkMessageStore::update_ack(uint32_t sequence,uint32_t ack){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    if(item.ack==ack)return;
    item.ack=ack;write_record(p,item,"ack");
}

void MeshInkMessageStore::update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    if(item.snr_q4==snr_q4&&item.path_len==path_len&&(item.flags&MESHINK_MESSAGE_HAS_RX))return;
    item.snr_q4=snr_q4;item.path_len=path_len;item.flags|=MESHINK_MESSAGE_HAS_RX;
    write_record(p,item,"rx");
}

void MeshInkMessageStore::update_route(uint32_t sequence,bool flood){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    const uint8_t before=item.flags;
    item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
    if(flood)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
    else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
    if(item.flags==before)return;
    write_record(p,item,"route");
}

void MeshInkMessageStore::update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    if(item.repeats==repeats&&item.repeat_snr_q4==snr_q4)return;
    item.repeats=repeats;item.repeat_snr_q4=snr_q4;write_record(p,item,"repeat");
}

void MeshInkMessageStore::update_outgoing(
        uint32_t sequence,UiMessageState state,uint32_t ack,bool route_flood){
    uint16_t p;if(!find_physical(sequence,p)||!file_)return;
    MeshInkStoredMessage item{};
    if(records_)item=records_[p];
    else if(!read_record(file_,p,item))return;
    const MeshInkStoredMessage before=item;
    item.state=(uint8_t)state;
    item.ack=ack;
    item.flags|=MESHINK_MESSAGE_ROUTE_KNOWN;
    if(route_flood)item.flags|=MESHINK_MESSAGE_ROUTE_FLOOD;
    else item.flags&=(uint8_t)~MESHINK_MESSAGE_ROUTE_FLOOD;
    if(!memcmp(&before,&item,sizeof(item)))return;
    write_record(p,item,"outgoing");
}

bool MeshInkMessageStore::mark_delivered_by_ack(uint32_t ack){
    if(!initialized_||!file_||!ack)return false;
    // Normal operation scans the PSRAM/RAM journal mirror without changing
    // CPU frequency. Only the cache-allocation fallback touches flash here.
    StoreCpuBoostScope* flash_boost=nullptr;
    if(!records_)flash_boost=new StoreCpuBoostScope();
    MeshInkStoredMessage item{};
    bool delivered=false;
    for(size_t n=header_.count;n>0;--n){
        const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
        if(records_)item=records_[p];
        else if(!read_record(file_,p,item))break;
        if(item.ack==ack&&item.state!=(uint8_t)UiMessageState::Received){
            if(item.state==(uint8_t)UiMessageState::Delivered)delivered=true;
            else{
                item.state=(uint8_t)UiMessageState::Delivered;
                delivered=write_record(p,item,"delivered");
            }
            break;
        }
    }
    delete flash_boost;
    return delivered;
}

uint32_t MeshInkMessageStore::find_matching_outgoing(
        MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
        uint32_t timestamp,const char* text) const{
    if(!initialized_||!file_||!key||!text)return 0;
    if(records_){
        for(size_t n=header_.count;n>0;--n){
            const uint16_t p=(header_.head+n-1)%MESHINK_MESSAGE_CAPACITY;
            const MeshInkStoredMessage& item=records_[p];
            if(item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
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
        if(!read_record(file_,p,item))break;
        if(item.kind!=(uint8_t)kind||item.state==(uint8_t)UiMessageState::Received||
           item.timestamp!=timestamp||memcmp(item.key,key,min(key_len,sizeof(item.key)))||
           strncmp(item.text,text,sizeof(item.text)))continue;
        return item.sequence;
    }
    return 0;
}
