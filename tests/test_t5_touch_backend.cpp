#include <cassert>
#include <cstring>
#include <iostream>
#include <string>
#include <tuple>
#include <vector>

#include "board/t5_touch_backend.h"

using namespace meshink_t5_touch_detail;

static std::vector<uint8_t> point_frame(
    uint16_t x0,uint16_t y0,uint16_t x1=0,uint16_t y1=0,bool two=false) {
    std::vector<uint8_t> out(two?16:8,0);
    out[1]=(uint8_t)x0;out[2]=(uint8_t)(x0>>8);
    out[3]=(uint8_t)y0;out[4]=(uint8_t)(y0>>8);
    if(two) {
        out[9]=(uint8_t)x1;out[10]=(uint8_t)(x1>>8);
        out[11]=(uint8_t)y1;out[12]=(uint8_t)(y1>>8);
    }
    return out;
}

static bool log_contains(const char* needle) {
    for(const auto& line:touch_stub::serial_lines)
        if(line.find(needle)!=std::string::npos)return true;
    return false;
}

static void reset_all() {
    touch_stub::reset_arduino();
    touch_stub::reset_i2c();
    state()=State{};
}

int main() {
    // Boot/reset sequencing and identity diagnostics remain backend-owned.
    reset_all();
    touch_stub::queue_read(GT911_PRODUCT_ID,{'9','1','1',0});
    touch_stub::queue_read(GT911_CONFIG_VERSION,{0x42});
    meshink_touch_prepare_boot();
    assert(touch_stub::gpio_events.size()==4);
    assert(std::get<0>(touch_stub::gpio_events[0])==T5_PIN_TOUCH_RST);
    assert(std::get<1>(touch_stub::gpio_events[0])==OUTPUT);
    assert(std::get<0>(touch_stub::gpio_events[1])==T5_PIN_TOUCH_RST);
    assert(std::get<2>(touch_stub::gpio_events[1])==LOW);
    assert(std::get<0>(touch_stub::gpio_events[2])==T5_PIN_TOUCH_INT);
    assert(std::get<1>(touch_stub::gpio_events[2])==OUTPUT);
    assert(std::get<0>(touch_stub::gpio_events[3])==T5_PIN_TOUCH_INT);
    assert(std::get<2>(touch_stub::gpio_events[3])==LOW);
    meshink_touch_finish_boot();
    assert(touch_stub::delays.size()==2);
    assert(touch_stub::delays[0]==10&&touch_stub::delays[1]==60);
    assert(log_contains("backend=GT911"));
    assert(log_contains("config=0x42"));

    // Primary contact: new frame -> cached hold -> explicit release.
    reset_all();
    touch_stub::queue_read(GT911_STATUS,{0x81});
    touch_stub::queue_read_bytes(GT911_FIRST_POINT,point_frame(321,654));
    MeshInkTouchPrimarySample primary=meshink_touch_read_primary();
    assert(primary.pressed&&!primary.home);
    assert(primary.x==321&&primary.y==654);
    assert(state().was_pressed);
    assert(touch_stub::writes.size()==1);

    touch_stub::queue_read(GT911_STATUS,{0x00});
    primary=meshink_touch_read_primary();
    assert(primary.pressed&&!primary.home);
    assert(primary.x==321&&primary.y==654);

    touch_stub::queue_read(GT911_STATUS,{0x80});
    primary=meshink_touch_read_primary();
    assert(!primary.pressed&&!primary.home);
    assert(primary.x==321&&primary.y==654);
    assert(!state().was_pressed);

    // Home is a distinct press and must not turn into an ordinary held touch.
    touch_stub::queue_read(GT911_STATUS,{0x90});
    primary=meshink_touch_read_primary();
    assert(primary.home&&primary.pressed);
    assert(!state().was_pressed);
    touch_stub::queue_read(GT911_STATUS,{0x00});
    primary=meshink_touch_read_primary();
    assert(!primary.home&&!primary.pressed);

    // A transient status-read failure while pressed keeps the legacy held state.
    touch_stub::queue_read(GT911_STATUS,{0x81});
    touch_stub::queue_read_bytes(GT911_FIRST_POINT,point_frame(50,60));
    primary=meshink_touch_read_primary();
    assert(primary.pressed);
    touch_stub::queue_read_failure(GT911_STATUS);
    primary=meshink_touch_read_primary();
    assert(primary.pressed&&primary.x==50&&primary.y==60);
    assert(state().i2c_errors==1);
    assert(log_contains("ERROR GT911 read"));

    touch_stub::queue_read(GT911_STATUS,{0x81});
    touch_stub::queue_read_failure(GT911_FIRST_POINT);
    primary=meshink_touch_read_primary();
    assert(primary.pressed&&primary.x==0&&primary.y==0);

    // Maps contact path: one finger persists until release/new frame.
    reset_all();
    MeshInkTouchContacts contacts{};
    touch_stub::queue_read(GT911_STATUS,{0x81});
    touch_stub::queue_read_bytes(GT911_FIRST_POINT,point_frame(10,20));
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==1&&!contacts.home);
    assert(contacts.points[0].x==10&&contacts.points[0].y==20);

    touch_stub::queue_read(GT911_STATUS,{0x00});
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==1);
    assert(contacts.points[0].x==10&&contacts.points[0].y==20);

    // Two-finger frame is retained with both coordinate records intact.
    touch_stub::queue_read(GT911_STATUS,{0x82});
    touch_stub::queue_read_bytes(GT911_FIRST_POINT,point_frame(100,200,300,400,true));
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==2);
    assert(contacts.points[0].x==100&&contacts.points[0].y==200);
    assert(contacts.points[1].x==300&&contacts.points[1].y==400);

    // Unsupported >2 contacts deliberately surface as count=3 until release.
    touch_stub::queue_read(GT911_STATUS,{0x83});
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==3);
    touch_stub::queue_read(GT911_STATUS,{0x00});
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==3);
    touch_stub::queue_read(GT911_STATUS,{0x80});
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==0);

    // Map Home and I2C failure semantics.
    touch_stub::queue_read(GT911_STATUS,{0x90});
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.home&&contacts.count==0);
    touch_stub::queue_read_failure(GT911_STATUS);
    assert(!meshink_touch_read_contacts(contacts));

    // Standby/companion power control owns reset sequencing and clears tracking.
    reset_all();
    state().was_pressed=true;
    state().last_count=2;
    meshink_touch_set_power(false);
    assert(!state().was_pressed&&state().last_count==0);
    assert(!touch_stub::gpio_events.empty());
    assert(std::get<0>(touch_stub::gpio_events.back())==T5_PIN_TOUCH_RST);
    assert(std::get<2>(touch_stub::gpio_events.back())==LOW);

    touch_stub::reset_arduino();
    touch_stub::reset_i2c();
    meshink_touch_set_power(true);
    assert(touch_stub::delays.size()==2);
    assert(touch_stub::delays[0]==10&&touch_stub::delays[1]==60);
    assert(touch_stub::writes.size()==1); // status clear after reset

    std::cout << "PASS: GT911 backend preserves field-tested touch semantics.\n";
    return 0;
}
