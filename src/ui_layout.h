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


constexpr int meshink_form_pair_width(const MeshInkUiLayout& layout);
constexpr int meshink_form_pair_right(const MeshInkUiLayout& layout);
constexpr int meshink_section_pair_width(const MeshInkUiLayout& layout);
constexpr int meshink_section_pair_right(const MeshInkUiLayout& layout);
constexpr int meshink_pager_button_width(const MeshInkUiLayout& layout);
constexpr int meshink_pager_right(const MeshInkUiLayout& layout);

struct MeshInkUiRect {
    int x;
    int y;
    int width;
    int height;
};

constexpr int meshink_ui_ref_x(const MeshInkUiLayout& layout,int reference_x) {
    return (reference_x*layout.width+270)/540;
}
constexpr int meshink_ui_ref_y(const MeshInkUiLayout& layout,int reference_y) {
    return (reference_y*layout.height+480)/960;
}
constexpr int meshink_ui_ref_w(const MeshInkUiLayout& layout,int reference_w) {
    return (reference_w*layout.width+270)/540;
}
constexpr int meshink_ui_ref_h(const MeshInkUiLayout& layout,int reference_h) {
    return (reference_h*layout.height+480)/960;
}
constexpr MeshInkUiRect meshink_ui_ref_rect(
    const MeshInkUiLayout& layout,int x,int y,int width,int height) {
    return {
        meshink_ui_ref_x(layout,x),
        meshink_ui_ref_y(layout,y),
        meshink_ui_ref_w(layout,width),
        meshink_ui_ref_h(layout,height)
    };
}
constexpr MeshInkUiRect meshink_ui_expand_rect(
    MeshInkUiRect rect,int left,int top,int right,int bottom) {
    return {rect.x-left,rect.y-top,rect.width+left+right,rect.height+top+bottom};
}

// Shared interactive geometry. Drawing code and touch code both consume these
// rectangles so a board/display scaling change cannot move one without the
// other. The keyboard is intentionally separate: its visible rectangles are
// shared, while touch ownership extends into gaps/edges by design.
constexpr MeshInkUiRect meshink_header_back_rect(const MeshInkUiLayout& layout) {
    return {layout.header_back_x,layout.header_top,
            layout.header_button_width,layout.header_button_height};
}
constexpr MeshInkUiRect meshink_header_action_rect(const MeshInkUiLayout& layout) {
    return {layout.header_action_x,layout.header_top,
            layout.header_button_width,layout.header_button_height};
}
constexpr MeshInkUiRect meshink_header_back_touch_rect(const MeshInkUiLayout& layout) {
    const MeshInkUiRect visual=meshink_header_back_rect(layout);
    return meshink_ui_expand_rect(
        visual,
        meshink_ui_ref_w(layout,12),
        meshink_ui_ref_h(layout,10),
        meshink_ui_ref_w(layout,40),
        meshink_ui_ref_h(layout,12));
}
constexpr MeshInkUiRect meshink_outer_row_rect(
    const MeshInkUiLayout& layout,int reference_top,int reference_height=112) {
    return {layout.outer_margin,meshink_ui_ref_y(layout,reference_top),
            layout.outer_width,meshink_ui_ref_h(layout,reference_height)};
}
constexpr MeshInkUiRect meshink_section_row_rect(
    const MeshInkUiLayout& layout,int reference_top,int reference_height) {
    return {layout.section_margin,meshink_ui_ref_y(layout,reference_top),
            layout.section_width,meshink_ui_ref_h(layout,reference_height)};
}
constexpr MeshInkUiRect meshink_form_row_rect(
    const MeshInkUiLayout& layout,int reference_top,int reference_height) {
    return {layout.form_margin,meshink_ui_ref_y(layout,reference_top),
            layout.form_width,meshink_ui_ref_h(layout,reference_height)};
}

constexpr MeshInkUiRect meshink_welcome_name_rect(const MeshInkUiLayout& layout) {
    return meshink_form_row_rect(layout,180,64);
}
constexpr MeshInkUiRect meshink_welcome_preset_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,292,88);
}
constexpr MeshInkUiRect meshink_welcome_companion_rect(const MeshInkUiLayout& layout) {
    return meshink_form_row_rect(layout,402,52);
}
constexpr MeshInkUiRect meshink_welcome_show_keyboard_rect(const MeshInkUiLayout& layout) {
    return meshink_form_row_rect(layout,840,64);
}

constexpr MeshInkUiRect meshink_preset_back_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,12,48,110,84);
}
constexpr MeshInkUiRect meshink_preset_row_rect(const MeshInkUiLayout& layout,int row) {
    return meshink_outer_row_rect(layout,132+row*128,112);
}
constexpr MeshInkUiRect meshink_preset_prev_rect(const MeshInkUiLayout& layout) {
    return {layout.section_margin,meshink_ui_ref_y(layout,800),
            meshink_pager_button_width(layout),meshink_ui_ref_h(layout,62)};
}
constexpr MeshInkUiRect meshink_preset_next_rect(const MeshInkUiLayout& layout) {
    return {meshink_pager_right(layout),meshink_ui_ref_y(layout,800),
            meshink_pager_button_width(layout),meshink_ui_ref_h(layout,62)};
}

constexpr MeshInkUiRect meshink_confirm_left_rect(
    const MeshInkUiLayout& layout,int reference_top) {
    return {layout.form_margin,meshink_ui_ref_y(layout,reference_top),
            meshink_form_pair_width(layout),meshink_ui_ref_h(layout,72)};
}
constexpr MeshInkUiRect meshink_confirm_right_rect(
    const MeshInkUiLayout& layout,int reference_top) {
    return {meshink_form_pair_right(layout),meshink_ui_ref_y(layout,reference_top),
            meshink_form_pair_width(layout),meshink_ui_ref_h(layout,72)};
}

constexpr MeshInkUiRect meshink_node_map_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,590,62);
}
constexpr MeshInkUiRect meshink_node_action_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,808,70);
}
constexpr MeshInkUiRect meshink_node_left_action_rect(const MeshInkUiLayout& layout) {
    const int gap=meshink_ui_ref_w(layout,12);
    const int width=(layout.section_width-gap)/2;
    return {layout.section_margin,meshink_ui_ref_y(layout,808),width,
            meshink_ui_ref_h(layout,70)};
}
constexpr MeshInkUiRect meshink_node_right_action_rect(const MeshInkUiLayout& layout) {
    const MeshInkUiRect left=meshink_node_left_action_rect(layout);
    const int gap=meshink_ui_ref_w(layout,12);
    return {left.x+left.width+gap,left.y,left.width,left.height};
}

constexpr MeshInkUiRect meshink_map_control_rect(
    const MeshInkUiLayout& layout,int index) {
    return meshink_ui_ref_rect(layout,462,58+index*75,66,66);
}

constexpr MeshInkUiRect meshink_quick_slider_track_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,44,182,452,16);
}
constexpr MeshInkUiRect meshink_quick_slider_touch_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,28,146,484,80);
}
constexpr MeshInkUiRect meshink_quick_minus_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,24,256,112,70);
}
constexpr MeshInkUiRect meshink_quick_plus_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,404,256,112,70);
}
constexpr MeshInkUiRect meshink_quick_advert_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,24,410,238,100);
}
constexpr MeshInkUiRect meshink_quick_power_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,278,410,238,100);
}
constexpr int meshink_quick_panel_bottom(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_y(layout,620);
}

constexpr MeshInkUiRect meshink_display_brightness_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,12,358,516,160);
}
constexpr MeshInkUiRect meshink_display_slider_touch_rect(const MeshInkUiLayout& layout) {
    return meshink_ui_ref_rect(layout,40,420,460,100);
}
constexpr MeshInkUiRect meshink_night_start_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,150,112);
}
constexpr MeshInkUiRect meshink_night_end_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,286,112);
}
constexpr MeshInkUiRect meshink_night_minus_rect(const MeshInkUiLayout& layout) {
    return {layout.section_margin,meshink_ui_ref_y(layout,460),
            meshink_section_pair_width(layout),meshink_ui_ref_h(layout,76)};
}
constexpr MeshInkUiRect meshink_night_plus_rect(const MeshInkUiLayout& layout) {
    return {meshink_section_pair_right(layout),meshink_ui_ref_y(layout,460),
            meshink_section_pair_width(layout),meshink_ui_ref_h(layout,76)};
}
constexpr MeshInkUiRect meshink_night_save_rect(const MeshInkUiLayout& layout) {
    return meshink_section_row_rect(layout,600,76);
}

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

// Display & Power action geometry.
constexpr int meshink_shutdown_top(const MeshInkUiLayout& layout) {
    return layout.bottom_nav_top-110;
}
constexpr int meshink_settings_inline_action_width() {
    return 154;
}
constexpr int meshink_settings_inline_action_height() {
    return 56;
}
constexpr int meshink_settings_inline_action_x(const MeshInkUiLayout& layout) {
    return layout.width-layout.outer_margin-meshink_settings_inline_action_width();
}
constexpr int meshink_settings_inline_action_y(int row_top) {
    return row_top+(112-meshink_settings_inline_action_height())/2;
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
static_assert(meshink_settings_inline_action_x(MESHINK_T5_REFERENCE_LAYOUT)==374 &&
              meshink_settings_inline_action_y(118)==146 &&
              meshink_settings_inline_action_width()==154 &&
              meshink_settings_inline_action_height()==56,
              "T5 inline settings action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.header_action_x==470,
              "T5 header action geometry changed");
static_assert(MESHINK_T5_REFERENCE_LAYOUT.list_top==120 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_height==142 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_row_stride==150 &&
              MESHINK_T5_REFERENCE_LAYOUT.list_footer_y==875,
              "T5 list geometry changed");
