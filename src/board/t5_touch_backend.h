#pragma once

#include <Arduino.h>
#include <driver/i2c.h>
#include "board_profile.h"
#include "../hardware/touch_types.h"

#ifndef MESHINK_TOUCH_DIAGNOSTICS
#define MESHINK_TOUCH_DIAGNOSTICS 0
#endif

// LILYGO T5 H752/H752-01 GT911 backend.
//
// Preserve the field-tested MeshInk semantics:
// - the ordinary single-contact reader keeps the last pressed point until the
//   controller reports release;
// - the contact reader keeps the previous frame while no new GT911 status
//   frame is ready;
// - >2 contacts are surfaced as count=3 so Maps suppresses unsupported
//   gestures until all fingers lift;
// - reset/INT sequencing selects the existing 0x5D GT911 address.
//
// Higher-level gesture interpretation, keyboard stabilization and UI routing
// deliberately remain outside this backend.
namespace meshink_t5_touch_detail {

static constexpr uint8_t GT911_ADDR = 0x5D;
static constexpr uint16_t GT911_PRODUCT_ID = 0x8140;
static constexpr uint16_t GT911_CONFIG_VERSION = 0x8047;
static constexpr uint16_t GT911_STATUS = 0x814E;
static constexpr uint16_t GT911_FIRST_POINT = 0x814F;

struct State {
    bool was_pressed = false;
    int16_t cached_x = 0;
    int16_t cached_y = 0;
    uint8_t last_count = 0;
    int16_t last_x0 = 0;
    int16_t last_y0 = 0;
    int16_t last_x1 = 0;
    int16_t last_y1 = 0;
    uint32_t i2c_errors = 0;
    uint32_t last_error_report_ms = 0;
    MeshInkOrientation orientation = MeshInkOrientation::Portrait;
};

inline State& state() {
    static State value{};
    return value;
}

inline void report_i2c_error(const char* operation,uint16_t reg,size_t len) {
    State& s=state();
    ++s.i2c_errors;
    const uint32_t now=millis();
    if(s.i2c_errors==1||now-s.last_error_report_ms>=2000) {
        s.last_error_report_ms=now;
        Serial.printf("[T5-TOUCH] ERROR GT911 %s reg=0x%04X len=%u count=%lu\n",
                      operation,(unsigned)reg,(unsigned)len,
                      (unsigned long)s.i2c_errors);
    }
}

inline bool read(uint16_t reg,uint8_t* data,size_t len) {
    uint8_t address[2]={(uint8_t)(reg>>8),(uint8_t)reg};
    const bool ok=i2c_master_write_read_device(
        I2C_NUM_0,GT911_ADDR,address,sizeof(address),data,len,
        pdMS_TO_TICKS(20))==ESP_OK;
    if(!ok)report_i2c_error("read",reg,len);
    return ok;
}

inline bool write_status_clear() {
    const uint8_t data[3]={(uint8_t)(GT911_STATUS>>8),
                           (uint8_t)GT911_STATUS,0};
    const bool ok=i2c_master_write_to_device(
        I2C_NUM_0,GT911_ADDR,data,sizeof(data),pdMS_TO_TICKS(20))==ESP_OK;
    if(!ok)report_i2c_error("clear",GT911_STATUS,1);
    return ok;
}

inline void reset_tracking() {
    State& s=state();
    s.was_pressed=false;
    s.last_count=0;
    s.last_x0=s.last_y0=s.last_x1=s.last_y1=0;
}

inline void select_address_reset_sequence() {
    pinMode((gpio_num_t)T5_PIN_TOUCH_RST,OUTPUT);
    digitalWrite((gpio_num_t)T5_PIN_TOUCH_RST,LOW);
    pinMode((gpio_num_t)T5_PIN_TOUCH_INT,OUTPUT);
    digitalWrite((gpio_num_t)T5_PIN_TOUCH_INT,LOW);
}

inline void release_reset_sequence() {
    delay(10);
    digitalWrite((gpio_num_t)T5_PIN_TOUCH_RST,HIGH);
    delay(60);
    pinMode((gpio_num_t)T5_PIN_TOUCH_INT,INPUT);
}

static constexpr int16_t TOUCH_PORTRAIT_WIDTH = 540;

inline MeshInkTouchPoint logical_point(int16_t raw_x,int16_t raw_y) {
    MeshInkTouchPoint point{};
    if(state().orientation==MeshInkOrientation::Landscape) {
        point.x=raw_y;
        point.y=(int16_t)(TOUCH_PORTRAIT_WIDTH-1-raw_x);
    } else {
        point.x=raw_x;
        point.y=raw_y;
    }
    return point;
}

} // namespace meshink_t5_touch_detail

inline const char* meshink_touch_backend_name() { return "GT911"; }

inline void meshink_touch_prepare_boot() {
    meshink_t5_touch_detail::reset_tracking();
    meshink_t5_touch_detail::select_address_reset_sequence();
}

inline void meshink_touch_finish_boot() {
    using namespace meshink_t5_touch_detail;
    release_reset_sequence();
#if MESHINK_TOUCH_DIAGNOSTICS
    // Test-build identity probe. Do not block startup if the controller is
    // absent/transiently unavailable; normal reads retain legacy retry
    // behavior and operation errors remain rate-limited below.
    uint8_t product[4]{};
    uint8_t config=0;
    const bool product_ok=read(GT911_PRODUCT_ID,product,sizeof(product));
    const bool config_ok=read(GT911_CONFIG_VERSION,&config,1);
    if(product_ok) {
        if(config_ok) {
            Serial.printf("[T5-TOUCH] backend=GT911 address=0x%02X product=%02X%02X%02X%02X config=0x%02X ready\n",
                          (unsigned)GT911_ADDR,
                          (unsigned)product[0],(unsigned)product[1],
                          (unsigned)product[2],(unsigned)product[3],
                          (unsigned)config);
        } else {
            Serial.printf("[T5-TOUCH] backend=GT911 address=0x%02X product=%02X%02X%02X%02X config=? ready\n",
                          (unsigned)GT911_ADDR,
                          (unsigned)product[0],(unsigned)product[1],
                          (unsigned)product[2],(unsigned)product[3]);
        }
    } else {
        Serial.printf("[T5-TOUCH] backend=GT911 address=0x%02X identity probe unavailable; runtime retry active\n",
                      (unsigned)GT911_ADDR);
    }
#endif
}

inline bool meshink_touch_clear() {
    return meshink_t5_touch_detail::write_status_clear();
}

inline void meshink_touch_reset_tracking() {
    meshink_t5_touch_detail::reset_tracking();
}

inline void meshink_touch_set_orientation(MeshInkOrientation orientation) {
    using namespace meshink_t5_touch_detail;
    State& s=state();
    if(s.orientation==orientation)return;
    s.orientation=orientation;
    reset_tracking();
}

inline void meshink_touch_set_power(bool enabled) {
    using namespace meshink_t5_touch_detail;
    reset_tracking();
    if(enabled) {
        select_address_reset_sequence();
        release_reset_sequence();
        write_status_clear();
    } else {
        pinMode((gpio_num_t)T5_PIN_TOUCH_RST,OUTPUT);
        digitalWrite((gpio_num_t)T5_PIN_TOUCH_RST,LOW);
    }
}

inline MeshInkTouchPrimarySample meshink_touch_read_primary() {
    using namespace meshink_t5_touch_detail;
    State& s=state();
    MeshInkTouchPrimarySample sample{};

    uint8_t status=0;
    if(!read(GT911_STATUS,&status,1) || !(status&0x80)) {
        const MeshInkTouchPoint point=logical_point(s.cached_x,s.cached_y);
        sample.x=point.x;
        sample.y=point.y;
        sample.pressed=s.was_pressed;
        return sample;
    }
    if(status&0x10) {
        sample.home=true;
        sample.pressed=true;
        write_status_clear();
        s.was_pressed=false;
        return sample;
    }

    const uint8_t count=status&0x0F;
    if(!count||count>5) {
        const MeshInkTouchPoint point=logical_point(s.cached_x,s.cached_y);
        sample.x=point.x;
        sample.y=point.y;
        write_status_clear();
        s.was_pressed=false;
        return sample;
    }

    uint8_t point[8]{};
    if(!read(GT911_FIRST_POINT,point,sizeof(point))) {
        sample.pressed=s.was_pressed;
        return sample;
    }

    s.cached_x=(int16_t)(point[1]|((uint16_t)point[2]<<8));
    s.cached_y=(int16_t)(point[3]|((uint16_t)point[4]<<8));
    const MeshInkTouchPoint logical=logical_point(s.cached_x,s.cached_y);
    sample.x=logical.x;
    sample.y=logical.y;
    sample.pressed=true;
    write_status_clear();
    s.was_pressed=true;
    return sample;
}

inline bool meshink_touch_read_contacts(MeshInkTouchContacts& contacts) {
    using namespace meshink_t5_touch_detail;
    State& s=state();
    contacts={};

    uint8_t status=0;
    if(!read(GT911_STATUS,&status,1))return false;
    if(!(status&0x80)) {
        contacts.count=s.last_count;
        contacts.points[0]=logical_point(s.last_x0,s.last_y0);
        contacts.points[1]=logical_point(s.last_x1,s.last_y1);
        return true;
    }
    if(status&0x10) {
        contacts.home=true;
        s.last_count=0;
        write_status_clear();
        return true;
    }

    const uint8_t reported=status&0x0F;
    if(reported>5||reported==0) {
        s.last_count=0;
        write_status_clear();
        return true;
    }
    if(reported>2) {
        s.last_count=3;
        contacts.count=3;
        write_status_clear();
        return true;
    }

    uint8_t points[16]{};
    if(!read(GT911_FIRST_POINT,points,reported*8))return false;

    s.last_x0=(int16_t)(points[1]|((uint16_t)points[2]<<8));
    s.last_y0=(int16_t)(points[3]|((uint16_t)points[4]<<8));
    if(reported==2) {
        s.last_x1=(int16_t)(points[9]|((uint16_t)points[10]<<8));
        s.last_y1=(int16_t)(points[11]|((uint16_t)points[12]<<8));
    }
    s.last_count=reported;
    contacts.count=reported;
    contacts.points[0]=logical_point(s.last_x0,s.last_y0);
    contacts.points[1]=logical_point(s.last_x1,s.last_y1);
    write_status_clear();
    return true;
}
