#define MESHINK_TOUCH_BACKEND_HEADER "touch_mock_backend.h"
#include "hardware/touch.h"

#include <cassert>
#include <cstring>
#include <iostream>

int main() {
    static_assert(sizeof(MeshInkTouchPoint)==4,
                  "touch point stays a compact board-independent coordinate pair");

    assert(std::strcmp(meshink_touch_backend_name(),"mock")==0);

    meshink_touch_set_power(false);
    assert(!meshink_touch_mock_powered);
    meshink_touch_set_power(true);
    assert(meshink_touch_mock_powered);

    const unsigned before=meshink_touch_mock_resets;
    meshink_touch_reset_tracking();
    assert(meshink_touch_mock_resets==before+1);

    const MeshInkTouchPrimarySample primary=meshink_touch_read_primary();
    assert(primary.pressed&&!primary.home);
    assert(primary.x==123&&primary.y==456);

    MeshInkTouchContacts contacts{};
    assert(meshink_touch_read_contacts(contacts));
    assert(contacts.count==2&&!contacts.home);
    assert(contacts.points[0].x==10&&contacts.points[0].y==20);
    assert(contacts.points[1].x==30&&contacts.points[1].y==40);

    std::cout << "PASS: generic touch contract compiles and runs with a non-GT911 backend.\n";
    return 0;
}
