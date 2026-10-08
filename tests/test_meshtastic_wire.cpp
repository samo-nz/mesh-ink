#include "../src/protocol/meshtastic_wire.h"
#include <cassert>
#include <cstdio>
#include <cmath>
#include <cstring>
using namespace meshink_mt_wire;
int main(){
    uint8_t packet[32]{};
    const size_t n=encode_position(packet,-36484820,174779010);
    assert(n==10&&packet[0]==0x0d&&packet[5]==0x15);
    Position p=decode_position(packet,n);
    assert(p.valid&&p.lat_e6==-36484820&&p.lon_e6==174779010);
    assert(!decode_position(packet,n-1).valid);
    assert(!encode_position(packet,90000001,0));
    assert(!encode_position(packet,0,180000001));
    // An unrelated protobuf field may appear before valid coordinates.
    uint8_t extra[32]{0x20,0x03};
    memcpy(extra+2,packet,n);
    assert(decode_position(extra,n+2).valid);
    uint8_t no_lon[5]{0x0d,0,0,0,0};
    assert(!decode_position(no_lon,sizeof(no_lon)).valid);
    // Malformed length-delimited fields cannot read past the packet.
    uint8_t bad[]{0x1a,0x7f,0};
    assert(!decode_position(bad,sizeof(bad)).valid);
    const size_t m=encode_device_telemetry(packet,1760000000U,64,3.81f);
    assert(m==14&&packet[0]==0x0d&&packet[5]==0x12);
    DeviceTelemetry t=decode_device_telemetry(packet,m);
    assert(t.valid&&t.battery==64&&std::fabs(t.voltage-3.81f)<0.001f);
    assert(!decode_device_telemetry(packet,m-1).valid);
    assert(!encode_device_telemetry(packet,100,101,3.8f));
    std::puts("PASS: Meshtastic standard position/telemetry wire encoding and bounds");
    return 0;
}
