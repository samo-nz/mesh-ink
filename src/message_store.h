#pragma once

#include <stddef.h>
#include <stdint.h>
#include "ui_data.h"

constexpr size_t MESHINK_MESSAGE_CAPACITY=250;
constexpr uint8_t MESHINK_MESSAGE_PATH_UNKNOWN=0xFF;

enum class MeshInkMessageKind:uint8_t { Direct=0, Channel=1 };
enum class MeshInkMessageOrigin:uint8_t { LocalUi=0, CompanionApp=1 };

enum MeshInkMessageFlags:uint8_t {
    MESHINK_MESSAGE_HAS_RX      = 1U<<0,
    MESHINK_MESSAGE_ROUTE_KNOWN = 1U<<1,
    MESHINK_MESSAGE_ROUTE_FLOOD = 1U<<2
};

struct MeshInkStoredMessage {
    uint32_t sequence=0;
    uint32_t timestamp=0;
    uint32_t ack=0;
    uint8_t kind=0;
    uint8_t state=0;
    uint8_t key[7]{};
    char text[145]{};
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

class MeshInkMessageStore {
    MeshInkMessageStoreHeader header_{};
    MeshInkStoredMessage records_[MESHINK_MESSAGE_CAPACITY]{};
    bool initialized_=false;

    bool create_empty();
    bool write_full(const char* path);
    void write_header();
    void write_record(uint16_t physical);
    bool migrate_v1(const MeshInkMessageStoreHeader& legacy_header);
    bool find_physical(uint32_t sequence,uint16_t& physical) const;

public:
    bool begin();
    size_t count() const { return header_.count; }
    const MeshInkStoredMessage& at(size_t logical) const {
        return records_[(header_.head+logical)%MESHINK_MESSAGE_CAPACITY];
    }

    MeshInkStoredMessage* append(MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
                                 const char* text,uint32_t timestamp,UiMessageState state,
                                 uint32_t ack=0,
                                 MeshInkMessageOrigin origin=MeshInkMessageOrigin::LocalUi);
    void update_state(uint32_t sequence,UiMessageState state);
    void update_ack(uint32_t sequence,uint32_t ack);
    void update_rx(uint32_t sequence,int8_t snr_q4,uint8_t path_len);
    void update_route(uint32_t sequence,bool flood);
    void update_repeat(uint32_t sequence,uint8_t repeats,int8_t snr_q4);
    bool mark_delivered_by_ack(uint32_t ack);
    uint32_t find_matching_outgoing(MeshInkMessageKind kind,const uint8_t* key,size_t key_len,
                                    uint32_t timestamp,const char* text) const;
};

MeshInkMessageStore& meshink_message_store();
