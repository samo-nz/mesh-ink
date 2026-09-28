#include "map_gestures.h"
#include <cassert>
#include <cmath>
#include <cstdio>
using namespace meshink_map_gestures;

static double world_y(double lat,int zoom) {
    constexpr double pi=3.14159265358979323846;
    const double r=lat*pi/180.0;
    return (1.0-std::log(std::tan(r)+1.0/std::cos(r))/pi)*
           (256.0*(1u<<zoom))/2.0;
}
static double world_x(double lon,int zoom) {
    return (lon+180.0)/360.0*(256.0*(1u<<zoom));
}
static void check_anchor(double lat,double lon,int old_zoom,int new_zoom,
                         int sx,int sy,const MeshInkUiLayout& layout) {
    const double old_world=256.0*(1u<<old_zoom);
    const double new_world=256.0*(1u<<new_zoom);
    const double scale=new_world/old_world;
    const Centre centre=zoom_about(lat,lon,old_zoom,new_zoom,sx,sy,layout);
    assert(std::isfinite(centre.latitude));
    assert(std::isfinite(centre.longitude));
    assert(centre.longitude>=-180.0&&centre.longitude<180.0);
    assert(centre.latitude>=-85.0512&&centre.latitude<=85.0512);
    const double dx=sx-layout.map_centre_x;
    const double dy=sy-layout.map_centre_y;
    const double wanted_x=(world_x(lon,old_zoom)+dx)*scale;
    const double actual_x=world_x(centre.longitude,new_zoom)+dx;
    const double wrapped=std::remainder(actual_x-wanted_x,new_world);
    assert(std::fabs(wrapped)<0.00001);
    const double wanted_y=(world_y(lat,old_zoom)+dy)*scale;
    const double actual_y=world_y(centre.latitude,new_zoom)+dy;
    assert(std::fabs(actual_y-wanted_y)<0.0001);
}
int main() {
    const MeshInkUiLayout& t5=MESHINK_T5_REFERENCE_LAYOUT;

    // Gestures are strictly map-viewport only, not status, nav or controls.
    assert(terrain_point(100,100,t5));
    assert(terrain_point(t5.map_centre_x,t5.map_centre_y,t5));
    assert(terrain_point(500,300,t5));
    assert(!terrain_point(470,100,t5)); // +, -, GPS target
    assert(!terrain_point(100,t5.map_top-1,t5));
    assert(!terrain_point(100,t5.map_bottom,t5));
    assert(!terrain_point(t5.width,500,t5));

    // Prove the gesture geometry itself is not tied to 540x960. This is only a
    // host geometry test, not a claim of support for any particular 480x800
    // hardware.
    constexpr MeshInkUiLayout compact=meshink_make_ui_layout(480,800);
    static_assert(compact.bottom_nav_top==740,"compact nav edge");
    static_assert(compact.map_centre_x==240,"compact horizontal centre");
    static_assert(compact.map_centre_y==394,"compact vertical centre");
    static_assert(compact.tab_width==120,"compact tab width");
    static_assert(compact.outer_width==456,"compact outer card width");
    static_assert(compact.section_width==432,"compact section width");
    static_assert(compact.content_text_x==28,"compact text inset");
    static_assert(compact.header_action_x==410,"compact header action position");
    assert(terrain_point(100,100,compact));
    assert(terrain_point(350,300,compact));
    assert(!terrain_point(410,100,compact));
    assert(!terrain_point(100,compact.map_bottom,compact));

    assert(tap_candidate(0,0,40));
    assert(tap_candidate(16,-16,260));
    assert(!tap_candidate(17,0,40));
    assert(!tap_candidate(0,0,261));
    assert(same_tap_area(100,100,135,135));
    assert(!same_tap_area(100,100,161,100));
    assert(TAP_WINDOW_MS>=300&&TAP_WINDOW_MS<=450);
    assert(MIN_ZOOM==2);
    assert(MAX_ZOOM==18);

    // Symmetric under swapping contact order. Reject tiny pinches and noise.
    assert(distance_squared(10,20,110,20)==distance_squared(110,20,10,20));
    assert(pinch_zoom_steps(100*100,110*110)==0);
    assert(pinch_zoom_steps(100*100,130*130)==1);
    assert(pinch_zoom_steps(100*100,60*60)==-1);
    // Pinch magnitude must never skip zoom levels. A new gesture is required
    // for every additional level in either direction.
    assert(pinch_zoom_steps(100*100,210*210)==1);
    assert(pinch_zoom_steps(100*100,45*45)==-1);
    assert(pinch_zoom_steps(100*100,310*310)==1);
    assert(pinch_zoom_steps(100*100,30*30)==-1);
    assert(pinch_zoom_steps(20*20,200*200)==0);

    for(int z=3;z<=17;++z) {
        check_anchor(-41.2,174.7,z,z+1,130,690,t5);
        check_anchor(-41.2,174.7,z,z+1,t5.map_centre_x,t5.map_centre_y,t5);
        check_anchor(-41.2,174.7,z,z-1,450,350,t5);
        check_anchor(0.5,179.999,z,z+1,480,500,t5);
        check_anchor(-0.5,-179.999,z,z-1,80,500,t5);
        check_anchor(-41.2,174.7,z,z+1,compact.map_centre_x,
                     compact.map_centre_y,compact);
    }
    std::puts("Map pinch/tap and logical-layout anchor host tests passed");
}
