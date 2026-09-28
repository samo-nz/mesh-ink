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

    // Shared setup/detail geometry.
    int form_margin;
    int form_width;
    int form_text_x;
    int detail_value_x;

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
    return {
        width,
        height,

        48,                 // status_height
        60,                 // bottom_nav_height
        height-60,          // bottom_nav_top
        48,                 // map_top
        height-60,          // map_bottom
        width/2,            // map_centre_x
        (48+height-60)/2,   // map_centre_y
        width/4,            // tab_width

        12,                 // outer_margin
        width-24,           // outer_width
        24,                 // section_margin
        width-48,           // section_width
        16,                 // text_inset
        28,                 // content_text_x
        width-12,           // content_right
        width-46,           // settings_arrow_x

        30,                 // form_margin
        width-60,           // form_width
        48,                 // form_text_x
        width/2-40,         // detail_value_x

        58,                 // header_top
        58,                 // header_button_width
        48,                 // header_button_height
        12,                 // header_back_x
        width-70,           // header_action_x
        64,                 // header_title_y
        70,                 // header_text_y

        120,                // list_top
        142,                // list_row_height
        150,                // list_row_stride
        height-85,          // list_footer_y
        112                 // settings_row_height
    };
}

// Reusable horizontal split helpers. Gaps preserve the current T5 visual
// proportions but widths adapt to the logical viewport.
constexpr int meshink_form_pair_width(const MeshInkUiLayout& layout) {
    return (layout.form_width-40)/2;
}
constexpr int meshink_form_pair_right(const MeshInkUiLayout& layout) {
    return layout.width-layout.form_margin-meshink_form_pair_width(layout);
}
constexpr int meshink_section_pair_width(const MeshInkUiLayout& layout) {
    return (layout.section_width-52)/2;
}
constexpr int meshink_section_pair_right(const MeshInkUiLayout& layout) {
    return layout.width-layout.section_margin-meshink_section_pair_width(layout);
}
constexpr int meshink_pager_button_width(const MeshInkUiLayout& layout) {
    return (layout.section_width-132)/2;
}
constexpr int meshink_pager_right(const MeshInkUiLayout& layout) {
    return layout.width-layout.section_margin-meshink_pager_button_width(layout);
}
constexpr int meshink_slider_left(const MeshInkUiLayout& layout) {
    return layout.section_margin+38;
}
constexpr int meshink_slider_width(const MeshInkUiLayout& layout) {
    return layout.width-2*meshink_slider_left(layout);
}

// Display & Power action geometry. Night Timer mode needs two actions below
// the final 112 px settings row, so use compact 52 px buttons with 8 px gaps.
constexpr int meshink_shutdown_top(const MeshInkUiLayout& layout) {
    return layout.bottom_nav_top-110;
}
constexpr int meshink_night_action_height() {
    return 52;
}
constexpr int meshink_night_schedule_top(const MeshInkUiLayout& layout) {
    return layout.bottom_nav_top-124;
}
constexpr int meshink_night_shutdown_top(const MeshInkUiLayout& layout) {
    return layout.bottom_nav_top-64;
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
static_assert(MESHINK_T5_REFERENCE_LAYOUT.form_margin==30 &&
              MESHINK_T5_REFERENCE_LAYOUT.form_width==480 &&
              MESHINK_T5_REFERENCE_LAYOUT.form_text_x==48,
              "T5 setup form geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.detail_value_x==230,
              "T5 detail value column changed");
static_assert(meshink_form_pair_width(MESHINK_T5_REFERENCE_LAYOUT)==220 &&
              meshink_form_pair_right(MESHINK_T5_REFERENCE_LAYOUT)==290,
              "T5 confirmation button geometry changed");
static_assert(meshink_section_pair_width(MESHINK_T5_REFERENCE_LAYOUT)==220 &&
              meshink_section_pair_right(MESHINK_T5_REFERENCE_LAYOUT)==296,
              "T5 section pair geometry changed");
static_assert(meshink_pager_button_width(MESHINK_T5_REFERENCE_LAYOUT)==180 &&
              meshink_pager_right(MESHINK_T5_REFERENCE_LAYOUT)==336,
              "T5 pager button geometry changed");
static_assert(meshink_slider_left(MESHINK_T5_REFERENCE_LAYOUT)==62 &&
              meshink_slider_width(MESHINK_T5_REFERENCE_LAYOUT)==416,
              "T5 slider geometry changed");
static_assert(meshink_shutdown_top(MESHINK_T5_REFERENCE_LAYOUT)==790,
              "T5 normal shutdown position changed");
static_assert(meshink_night_schedule_top(MESHINK_T5_REFERENCE_LAYOUT)==776 &&
              meshink_night_shutdown_top(MESHINK_T5_REFERENCE_LAYOUT)==836 &&
              meshink_night_action_height()==52,
              "T5 Night Timer action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.header_action_x==470,
              "T5 header action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.list_top==120 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_height==142 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_stride==150 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_footer_y==875,
              "T5 list geometry changed");
