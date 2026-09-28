#pragma once

// Board-independent logical screen metrics.
//
// This layer describes UI regions, not physical e-paper memory. The T5's
// existing 540x960 layout is retained exactly while both screen-edge and
// shared interior geometry are derived from the logical display size.
// Individual screen composition can become responsive incrementally without
// changing display backends or scattering board-name conditionals.
struct MeshInkUiLayout {
    int width;
    int height;

    // Screen edges.
    int status_height;
    int bottom_nav_height;
    int bottom_nav_top;
    int map_top;
    int map_bottom;
    int map_centre_x;
    int map_centre_y;
    int tab_width;

    // Shared interior/card geometry.
    int outer_margin;
    int outer_width;
    int section_margin;
    int section_width;
    int text_inset;
    int content_text_x;
    int content_right;
    int settings_arrow_x;

    // App header geometry.
    int header_top;
    int header_button_width;
    int header_button_height;
    int header_back_x;
    int header_action_x;
    int header_title_y;
    int header_text_y;

    // Reusable paged-list geometry.
    int list_top;
    int list_row_height;
    int list_row_stride;
    int list_footer_y;
    int settings_row_height;
};

constexpr MeshInkUiLayout meshink_make_ui_layout(int width,int height) {
    constexpr int status_height=48;
    constexpr int bottom_nav_height=60;
    constexpr int outer_margin=12;
    constexpr int section_margin=24;
    constexpr int text_inset=16;
    constexpr int header_button_width=58;
    constexpr int header_button_height=48;
    constexpr int list_row_height=142;
    constexpr int list_row_stride=150;
    return {
        width,
        height,

        status_height,
        bottom_nav_height,
        height-bottom_nav_height,
        status_height,
        height-bottom_nav_height,
        width/2,
        (status_height+height-bottom_nav_height)/2,
        width/4,

        outer_margin,
        width-2*outer_margin,
        section_margin,
        width-2*section_margin,
        text_inset,
        outer_margin+text_inset,
        width-outer_margin,
        width-46,

        58,
        header_button_width,
        header_button_height,
        outer_margin,
        width-outer_margin-header_button_width,
        64,
        70,

        120,
        list_row_height,
        list_row_stride,
        height-bottom_nav_height-25,
        112
    };
}

// Regression reference only. Application code derives metrics from its
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
static_assert(MESHINK_T5_REFERENCE_LAYOUT.outer_margin==12 &&
              MESHINK_T5_REFERENCE_LAYOUT.outer_width==516,
              "T5 outer card geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.section_margin==24 &&
              MESHINK_T5_REFERENCE_LAYOUT.section_width==492,
              "T5 section/action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.content_text_x==28 &&
              MESHINK_T5_REFERENCE_LAYOUT.settings_arrow_x==494,
              "T5 content inset geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.header_action_x==470,
              "T5 header action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.list_top==120 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_height==142 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_stride==150 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_footer_y==875,
              "T5 list geometry changed");
