#include "meshtastic_official_phoneapi.h"
#include "../../lib/Meshtastic/src/mesh/PhoneAPI.h"
#include <pb_encode.h>
#include <new>

// Reuses the actual upstream PhoneAPI and protobuf state machine.
// NOTE: this translation unit is deliberately not in the current firmware
// build source filter: official core/router initialization comes first.
namespace {
class MeshInkInProcessPhoneAPI final : public PhoneAPI {
  protected:
    bool checkIsConnected() override { return true; }
  public:
    MeshInkInProcessPhoneAPI() { api_type = TYPE_PACKET; }
};
MeshInkInProcessPhoneAPI* api = nullptr;
}

extern "C" bool meshink_official_phoneapi_open(uint32_t nonce) {
    if (!api) {
        api = new(std::nothrow) MeshInkInProcessPhoneAPI();
        if (!api) return false;
    }
    meshtastic_ToRadio request = meshtastic_ToRadio_init_zero;
    request.which_payload_variant = meshtastic_ToRadio_want_config_id_tag;
    request.want_config_id = nonce;
    uint8_t bytes[meshtastic_ToRadio_size]{};
    pb_ostream_t stream = pb_ostream_from_buffer(bytes, sizeof(bytes));
    if (!pb_encode(&stream, meshtastic_ToRadio_fields, &request)) return false;
    // handleToRadio() returns "packet queued"; handshakes enqueue no RF.
    api->handleToRadio(bytes, stream.bytes_written);
    return api->isConnected();
}

extern "C" bool meshink_official_phoneapi_submit(const uint8_t* data, size_t len) {
    return api && data && len && len <= MAX_TO_FROM_RADIO_SIZE &&
           api->handleToRadio(data, len);
}
extern "C" bool meshink_official_phoneapi_has_data() {
    return api && api->available();
}
extern "C" size_t meshink_official_phoneapi_receive(uint8_t* out, size_t capacity) {
    if (!api || !out || capacity < meshtastic_FromRadio_size || !api->available())
        return 0;
    return api->getFromRadio(out);
}
extern "C" void meshink_official_phoneapi_close() {
    if (api) {
        api->close();
        delete api;
        api = nullptr;
    }
}
