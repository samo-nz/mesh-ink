#pragma once
#include <stddef.h>
#include <stdint.h>

enum class UiMessageState : uint8_t { Received=0, Sending, Sent, Delivered, Failed, Retrying1, Retrying2, Retrying3, Retrying4, Retrying5 };

struct UiListEntry {
    const char* title;
    const char* subtitle;
    const char* time;
    uint8_t unread;
};

struct UiMessage {
    const char* text;
    const char* time;
    bool outgoing;
    UiMessageState state;
};

struct UiNodeDetails {
    const char* name;
    const char* identity;
    const char* last_seen;
    const char* route;
    const char* position;
    const char* status;
    const char* telemetry;
    const char* path;
    int32_t latitude;
    int32_t longitude;
    bool request_active;
    bool saved_contact;
};

class UiDataProvider {
public:
    virtual ~UiDataProvider() = default;
    virtual size_t conversation_count() const = 0;
    virtual const UiListEntry& conversation(size_t index) const = 0;
    virtual bool open_conversation(size_t index) = 0;
    virtual size_t contact_count() const = 0;
    virtual const UiListEntry& contact(size_t index) const = 0;
    virtual bool open_contact(size_t index) = 0;
    virtual size_t channel_count() const = 0;
    virtual const UiListEntry& channel(size_t index) const = 0;
    virtual bool open_channel(size_t index) = 0;
    virtual size_t advert_count() const = 0;
    virtual const UiListEntry& advert(size_t index) const = 0;
    virtual bool open_advert(size_t index) = 0;
    virtual bool active_node_details(UiNodeDetails& out) const = 0;
    virtual bool add_active_node() = 0;
    virtual bool remove_active_contact() = 0;
    virtual bool request_active_node_info() = 0;
    virtual const char* active_title() const = 0;
    virtual bool active_is_channel() const = 0;
    virtual size_t active_message_count() const = 0;
    virtual const UiMessage& active_message(size_t index) const = 0;
};
