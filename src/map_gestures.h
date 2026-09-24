#pragma once
#include <stdint.h>
#include <math.h>

// Pure map-only geometry shared by UI and host regression tests.
// All coordinates are portrait screen pixels in the existing 540x960 view.
namespace meshink_map_gestures {
constexpr int MAP_TOP=48, MAP_BOTTOM=900, MAP_CENTRE_Y=474;
constexpr int MIN_ZOOM=8, MAX_ZOOM=18;
constexpr uint32_t TAP_WINDOW_MS=350;
constexpr int TAP_RADIUS_PX=60;
constexpr int TAP_SLOP_PX=16;
constexpr uint32_t TAP_MAX_HOLD_MS=260;

inline bool terrain_point(int x,int y) {
    return x>=0 && x<540 && y>=MAP_TOP && y<MAP_BOTTOM &&
        !(x>=456 && y<281); // reserve +, -, and GPS target controls
}
inline bool tap_candidate(int dx,int dy,uint32_t held_ms) {
    return dx>=-TAP_SLOP_PX && dx<=TAP_SLOP_PX &&
        dy>=-TAP_SLOP_PX && dy<=TAP_SLOP_PX &&
        held_ms<=TAP_MAX_HOLD_MS;
}
inline bool same_tap_area(int x0,int y0,int x1,int y1) {
    const int32_t dx=x1-x0,dy=y1-y0;
    return dx*dx+dy*dy<=TAP_RADIUS_PX*TAP_RADIUS_PX;
}
inline int32_t distance_squared(int x0,int y0,int x1,int y1) {
    const int32_t dx=x1-x0,dy=y1-y0;
    return dx*dx+dy*dy;
}
// A 30% fingers-apart change is an intentional pinch, not touch jitter;
// larger pinches can request up to three discrete levels in one e-paper update.
inline int pinch_zoom_steps(int32_t initial_squared,int32_t final_squared) {
    if(initial_squared<2500 || final_squared<0)return 0;
    const int64_t start=initial_squared,finish=final_squared;
    if(finish>=start*9)return 3;
    if(finish>=start*4)return 2;
    if(finish*100>=start*169)return 1;
    if(finish*9<=start)return -3;
    if(finish*4<=start)return -2;
    if(finish*169<=start*100)return -1;
    return 0;
}
struct Centre {double latitude,longitude;};
// Keep the original screen coordinate anchored at the same geographic
// location as zoom changes, including across the +/-180 degree meridian.
inline Centre zoom_about(double latitude,double longitude,int old_zoom,
                         int new_zoom,int anchor_x,int anchor_y) {
    constexpr double PI_=3.14159265358979323846;
    const double old_world=256.0*(1u<<old_zoom);
    const double new_world=256.0*(1u<<new_zoom);
    const double lat=latitude<-85.0511?-85.0511:
                     latitude>85.0511?85.0511:latitude;
    const double rad=lat*PI_/180.0;
    const double old_x=(longitude+180.0)/360.0*old_world;
    const double old_y=(1.0-log(tan(rad)+1.0/cos(rad))/PI_)*old_world/2.0;
    const double dx=anchor_x-270.0,dy=anchor_y-MAP_CENTRE_Y;
    double new_x=(old_x+dx)*(new_world/old_world)-dx;
    new_x=fmod(fmod(new_x,new_world)+new_world,new_world);
    double new_y=(old_y+dy)*(new_world/old_world)-dy;
    if(new_y<0)new_y=0;
    if(new_y>new_world)new_y=new_world;
    return {atan(sinh(PI_*(1.0-2.0*new_y/new_world)))*180.0/PI_,
            new_x/new_world*360.0-180.0};
}
} // namespace meshink_map_gestures
