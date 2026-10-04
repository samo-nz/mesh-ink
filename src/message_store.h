#pragma once

#include <stddef.h>
#include <stdint.h>
#include <FS.h>
#include "ui_data.h"
#include "message_limits.h"

constexpr size_t MESHINK_MESSAGE_CAPACITY=250;
constexpr uint8_t MESHINK_MESSAGE_PATH_UNKNOWN=0xFF;

enum class MeshInkMessageKind:uint8_t { Direct=0, Channel=1 };
enum class MeshInkMessageOrigin:uint8_t { LocalUi=0, CompanionApp=1 };

enum MeshInkMessageFlags:uint8_t {
    MESHINK_MESSAGE_HAS_RX       = 1U<<0,
    MESHINK_MESSAGE_ROUTE_KNOWN  = 1U<<1,
    MESHINK_MESSAGE_ROUTE_FLOOD  = 1U<<2,
    MESHINK_MESSAGE_UNREAD       = 1U<<3,
    MESHINK_MESSAGE_READ_THROUGH = 1U<<4
};

struct MeshInkStoredMessage {
    uint32_t sequence=0;
    uint32_t timestamp=0;
    uint32_t ack=0;
    uint8_t kind=0;
    uint8_t state=0;
    uint8_t key[7]{};
    char text[MESHINK_MESSAGE_TEXT_BYTES]{};
    int8_t snr_q4=0;
    int8_t repeat_snr_q4=0;
    uint8_t path_len=MESHINK_MESSAGE_PATH_UNKNOWN;
    uint8_t repeats=0;
    uint8_t flags=0;
    uint8_t origin=0;
};

struct MeshInkMessageStoreHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t capacity;
    uint16_t head;
    uint16_t count;
    uint32_t sequence;
};

// SPIFFS is the persistent authority. A full physical-record mirror is loaded
// once at startup and is the read path for the running session. Mutations are
// synchronous write-through and update RAM only after flash succeeds.
class MeshInkMessageStore {
    MeshInkMessageStoreHeader header_{};
    mutable File file_{};
    MeshInkStoredMessage* records_=nullptr;
    bool cache_in_psram_=false;
    bool initialized_=false;

    bool ensure_cache();
    bool load_cache(File& source);
    bool create_empty();
    void write_header();
    bool write_record(uint16_t physical,const MeshInkStoredMessage& record);
    bool find_physical(uint32_t sequence,uint16_t& physical) const;

public:
    bool begin();
    size_t count() const { return initialized_?header_.count:0; }
    uint32_t revision() const { return initialized_?header_.sequence:0; }
    bool read(size_t logical,MeshInkStoredMessage& out) const;

    uint32_t append(MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
                    const char* text,uint32_t timestamp,UiMessageState state,
                    uint32_t ack=0,
                    MeshInkMessageOrigin origin=MeshInkMessageOrigin::LocalUi,
                    bool has_rx=false,int8_t snr_q4=0,
                    uint8_t path_len=MESHINK_MESSAGE_PATH_UNKNOWN,
                    bool unread=false);
    bool update_state(uint32_t sequence,UiMessageState state);
    bool mark_read_through(MeshInkMessageKind kind,const uint8_t* key,size_t key_len);
    void update_ack(uint32_t sequence,uint32_t ack);
    void update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len);
    void update_route(uint32_t sequence,bool flood);
    void update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4);
    void update_outgoing(uint32_t sequence,UiMessageState state,uint32_t ack,bool route_flood);
    bool mark_delivered_by_ack(uint32_t ack);
    bool sync_and_verify_for_deep_sleep(uint32_t& disk_sequence,size_t& disk_count);
    uint32_t find_matching_outgoing(MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
                                    uint32_t timestamp,const char* text) const;
};

MeshInkMessageStore& meshink_message_store();
