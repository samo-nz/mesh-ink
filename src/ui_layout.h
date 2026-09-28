#pragma once

// Board-independent logical screen metrics.
//
// This layer describes UI regions, not physical e-paper memory. The T5's
// existing 540x960 layout is retained exactly while screen-edge geometry is
// derived from the logical display size. Interior component layout can be
// made responsive incrementally without changing display backends.
struct MeshInkUiLayout {
    int width;
    int height;
    int status_height;
    int bottom_nav_height;
    int bottom_nav_top;
    int map_top;
    int map_bottom;
    int map_centre_x;
    int map_centre_y;
    int tab_width;
};

constexpr MeshInkUiLayout meshink_make_ui_layout(int width,int height) {
    return {
        width,
        height,
        48,
        60,
        height-60,
        48,
        height-60,
        width/2,
        (48+height-60)/2,
        width/4
    };
}

// Regression reference only. Application code should derive metrics from its
// selected display backend rather than assuming these dimensions.
constexpr MeshInkUiLayout MESHINK_T5_REFERENCE_LAYOUT=
    meshink_make_ui_layout(540,960);

static_assert(MESHINK_T5_REFERENCE_LAYOUT.status_height==48,
              "T5 status bar geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.bottom_nav_top==900,
              "T5 bottom navigation geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.map_centre_x==270,
              "T5 map horizontal centre changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.map_centre_y==474,
              "T5 map vertical centre changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.tab_width==135,
              "T5 navigation tab width changed");
