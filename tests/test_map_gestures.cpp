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
                         int sx,int sy) {
    const double old_world=256.0*(1u<<old_zoom);
    const double new_world=256.0*(1u<<new_zoom);
    const double scale=new_world/old_world;
    const Centre centre=zoom_about(lat,lon,old_zoom,new_zoom,sx,sy);
    assert(std::isfinite(centre.latitude));
    assert(std::isfinite(centre.longitude));
    assert(centre.longitude>=-180.0&&centre.longitude<180.0);
    assert(centre.latitude>=-85.0512&&centre.latitude<=85.0512);
    const double dx=sx-270.0,dy=sy-MAP_CENTRE_Y;
    const double wanted_x=(world_x(lon,old_zoom)+dx)*scale;
    const double actual_x=world_x(centre.longitude,new_zoom)+dx;
    const double wrapped=std::remainder(actual_x-wanted_x,new_world);
    assert(std::fabs(wrapped)<0.00001);
    const double wanted_y=(world_y(lat,old_zoom)+dy)*scale;
    const double actual_y=world_y(centre.latitude,new_zoom)+dy;
    assert(std::fabs(actual_y-wanted_y)<0.0001);
}
int main() {
    // Gestures are strictly map-viewport only, not status, nav or controls.
    assert(terrain_point(100,100));
    assert(terrain_point(270,MAP_CENTRE_Y));
    assert(terrain_point(500,300));
    assert(!terrain_point(470,100)); // +, -, GPS target
    assert(!terrain_point(100,47));
    assert(!terrain_point(100,900));
    assert(!terrain_point(540,500));

    assert(tap_candidate(0,0,40));
    assert(tap_candidate(16,-16,260));
    assert(!tap_candidate(17,0,40));
    assert(!tap_candidate(0,0,261));
    assert(same_tap_area(100,100,135,135));
    assert(!same_tap_area(100,100,161,100));
    assert(TAP_WINDOW_MS>=300&&TAP_WINDOW_MS<=450);

    // Symmetric under swapping contact order. Reject tiny pinches and noise.
    assert(distance_squared(10,20,110,20)==distance_squared(110,20,10,20));
    assert(pinch_zoom_steps(100*100,110*110)==0);
    assert(pinch_zoom_steps(100*100,130*130)==1);
    assert(pinch_zoom_steps(100*100,60*60)==-1);
    assert(pinch_zoom_steps(100*100,210*210)==2);
    assert(pinch_zoom_steps(100*100,45*45)==-2);
    assert(pinch_zoom_steps(100*100,310*310)==3);
    assert(pinch_zoom_steps(100*100,30*30)==-3);
    assert(pinch_zoom_steps(20*20,200*200)==0);

    for(int z=9;z<=17;++z) {
        check_anchor(-41.2,174.7,z,z+1,130,690);
        check_anchor(-41.2,174.7,z,z+1,270,MAP_CENTRE_Y);
        check_anchor(-41.2,174.7,z,z-1,450,350);
        check_anchor(0.5,179.999,z,z+1,480,500);
        check_anchor(-0.5,-179.999,z,z-1,80,500);
    }
    std::puts("Map pinch/tap and Mercator anchor host tests passed");
}
