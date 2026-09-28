#pragma once

inline bool meshink_touch_mock_powered = true;
inline unsigned meshink_touch_mock_resets = 0;

inline const char* meshink_touch_backend_name() { return "mock"; }
inline void meshink_touch_prepare_boot() {}
inline void meshink_touch_finish_boot() {}
inline bool meshink_touch_clear() { return true; }
inline void meshink_touch_reset_tracking() { ++meshink_touch_mock_resets; }
inline void meshink_touch_set_power(bool enabled) { meshink_touch_mock_powered=enabled; }
inline MeshInkTouchPrimarySample meshink_touch_read_primary() {
    MeshInkTouchPrimarySample sample{};
    sample.x=123;sample.y=456;sample.pressed=true;sample.home=false;
    return sample;
}
inline bool meshink_touch_read_contacts(MeshInkTouchContacts& contacts) {
    contacts={};
    contacts.count=2;
    contacts.points[0]={10,20};
    contacts.points[1]={30,40};
    return true;
}
