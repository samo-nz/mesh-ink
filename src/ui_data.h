#pragma once
#include <stddef.h>
#include <stdint.h>

enum class UiMessageState : uint8_t { Received=0, Sending, Sent, Delivered, Failed, Retrying1, Retrying2, Retrying3, Retrying4, Retrying5 };
enum class UiNodeInfoRequest : uint8_t { Status=0, Telemetry=1, Path=2, Trace=3, None=255 };
enum class UiNodeRole : uint8_t { Unknown=0, Client=1, Relay=2, Service=3, Sensor=4 };

enum UiNodeCapability : uint32_t {
    UI_NODE_CAP_STATUS    = 1u << 0,
    UI_NODE_CAP_TELEMETRY = 1u << 1,
    UI_NODE_CAP_PATH      = 1u << 2,
    UI_NODE_CAP_TRACE     = 1u << 3,
    UI_NODE_CAP_LOGIN     = 1u << 4,
};

inline bool ui_node_has_capability(uint32_t capabilities, UiNodeCapability capability) {
    return (capabilities & (uint32_t)capability) != 0;
}

struct UiListEntry {
    const char* title;
    const char* subtitle;
    const char* time;
    uint8_t unread;
    UiNodeRole role;
};

struct UiMessage {
    const char* text;
    const char* time;
    bool outgoing;
    UiMessageState state;
    const char* network; // persisted RF/route metadata when available
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
    const char* trace;
    int32_t latitude;
    int32_t longitude;
    bool request_active;
    UiNodeInfoRequest request_type;
    bool login_active;
    bool authenticated;
    const char* access_level;
    const char* role_label; // Protocol-provided user-facing role name; shared role drives icons/behavior.
    UiNodeRole role;
    uint32_t capabilities; // Optional node-detail pages/actions exposed by the active protocol helper.
    bool saved_contact;
    const char* advert_age;       // time since the last saved advertisement
    const char* position_source;  // last advert vs GPS reply receipt age
};

struct UiMapNode {
    char name[32];
    uint8_t key[7];
    int32_t latitude;
    int32_t longitude;
    UiNodeRole role = UiNodeRole::Unknown;
    uint32_t advertised_at; // Protocol-reported advertisement/last-seen timestamp when available.
    uint32_t gps_received_millis=0; // local reception of GPS telemetry
    bool gps_from_reply=false;
};

class UiDataProvider {
public:
    virtual ~UiDataProvider() = default;
    virtual size_t conversation_count() const = 0;
    virtual const UiListEntry& conversation(size_t index) const = 0;
    virtual bool open_conversation(size_t index) = 0;
    virtual size_t map_node_count() const = 0;
    virtual bool map_node(size_t index, UiMapNode& out) const = 0;
    virtual bool open_map_node(size_t index) = 0;
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
    virtual bool request_active_node_info(UiNodeInfoRequest request) = 0;
    virtual bool login_active_node(const char* password, bool save_password) = 0;
    virtual bool active_node_saved_password(char* out, size_t len) const = 0;
    virtual const char* active_title() const = 0;
    virtual bool active_is_channel() const = 0;
    virtual size_t active_message_count() const = 0;
    virtual const UiMessage& active_message(size_t index) const = 0;
    // Unread totals are a shared MeshInk concept. Protocol providers supply
    // the counts from their translated message journal rather than adding
    // backend-specific unread callbacks to the protocol helper.
    virtual uint16_t direct_unread_total() const { return 0; }
    virtual uint16_t channel_unread_total() const { return 0; }
    // Changes whenever the active conversation or its displayed metadata
    // changes, allowing small UI layout caches to invalidate safely.
    virtual uint32_t active_message_revision() const { return 0; }
};
