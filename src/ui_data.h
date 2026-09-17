#pragma once
#include <stddef.h>
#include <stdint.h>

enum class UiMessageState : uint8_t { Received=0, Sending, Sent, Delivered, Failed };

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
    virtual const char* active_title() const = 0;
    virtual bool active_is_channel() const = 0;
    virtual size_t active_message_count() const = 0;
    virtual const UiMessage& active_message(size_t index) const = 0;
};
