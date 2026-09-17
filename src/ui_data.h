#pragma once
#include <stddef.h>
#include <stdint.h>

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
};

class UiDataProvider {
public:
    virtual ~UiDataProvider() = default;
    virtual size_t contact_count() const = 0;
    virtual const UiListEntry& contact(size_t index) const = 0;
    virtual size_t channel_count() const = 0;
    virtual const UiListEntry& channel(size_t index) const = 0;
    virtual size_t direct_message_count() const = 0;
    virtual const UiMessage& direct_message(size_t index) const = 0;
    virtual size_t channel_message_count() const = 0;
    virtual const UiMessage& channel_message(size_t index) const = 0;
};

static constexpr UiListEntry MOCK_CONTACTS[] = {
        {"ALICE", "Repeater signal is strong here", "12:42", 2},
        {"WEST COAST RELAY", "Advert received nearby", "11:18", 0},
        {"JAMES", "I will check the track tomorrow", "MON", 0},
        {"KOKATAHI BASE", "Weather clearing from the west", "SUN", 1},
};
static constexpr UiListEntry MOCK_CHANNELS[] = {
        {"# GENERAL", "Morning all - radio check", "12:35", 3},
        {"# WEST COAST", "Road open past the bridge", "10:06", 1},
        {"# EMERGENCY", "No active incidents", "FRI", 0},
};
static constexpr UiMessage MOCK_DIRECT_MESSAGES[] = {
        {"Are you still heading west today?", "12:31", false},
        {"Yes, leaving after lunch.", "12:34", true},
        {"Great. Repeater signal is strong here.", "12:42", false},
};
static constexpr UiMessage MOCK_CHANNEL_MESSAGES[] = {
        {"ALICE: Morning all - radio check.", "12:30", false},
        {"Signal is clear at Kokatahi.", "12:33", true},
        {"JAMES: Reading you five by five.", "12:35", false},
};

class MockUiDataProvider final : public UiDataProvider {
public:
    size_t contact_count() const override { return sizeof(MOCK_CONTACTS)/sizeof(MOCK_CONTACTS[0]); }
    const UiListEntry& contact(size_t index) const override { return MOCK_CONTACTS[index]; }
    size_t channel_count() const override { return sizeof(MOCK_CHANNELS)/sizeof(MOCK_CHANNELS[0]); }
    const UiListEntry& channel(size_t index) const override { return MOCK_CHANNELS[index]; }
    size_t direct_message_count() const override { return sizeof(MOCK_DIRECT_MESSAGES)/sizeof(MOCK_DIRECT_MESSAGES[0]); }
    const UiMessage& direct_message(size_t index) const override { return MOCK_DIRECT_MESSAGES[index]; }
    size_t channel_message_count() const override { return sizeof(MOCK_CHANNEL_MESSAGES)/sizeof(MOCK_CHANNEL_MESSAGES[0]); }
    const UiMessage& channel_message(size_t index) const override { return MOCK_CHANNEL_MESSAGES[index]; }
};
