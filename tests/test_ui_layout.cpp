#include "ui_layout.h"
#include <cassert>
#include <cstdio>

static bool contains(MeshInkUiRect outer,MeshInkUiRect inner) {
    return inner.x>=outer.x && inner.y>=outer.y &&
           inner.x+inner.width<=outer.x+outer.width &&
           inner.y+inner.height<=outer.y+outer.height;
}

static bool in_bounds(const MeshInkUiLayout& layout,MeshInkUiRect rect) {
    return rect.x>=0 && rect.y>=0 && rect.width>0 && rect.height>0 &&
           rect.x+rect.width<=layout.width &&
           rect.y+rect.height<=layout.height;
}

static void expect_rect(MeshInkUiRect rect,int x,int y,int width,int height) {
    assert(rect.x==x);
    assert(rect.y==y);
    assert(rect.width==width);
    assert(rect.height==height);
}

static void check_layout(const MeshInkUiLayout& layout) {
    assert(layout.status_height>0);
    assert(layout.bottom_nav_height>0);
    assert(layout.bottom_nav_top+layout.bottom_nav_height==layout.height);
    assert(layout.map_top==layout.status_height);
    assert(layout.map_bottom==layout.bottom_nav_top);

    assert(in_bounds(layout,meshink_header_back_rect(layout)));
    assert(in_bounds(layout,meshink_header_action_rect(layout)));
    assert(contains(meshink_header_back_touch_rect(layout),
                    meshink_header_back_rect(layout)));
    assert(contains(meshink_header_action_touch_rect(layout),
                    meshink_header_action_rect(layout)));

    assert(in_bounds(layout,meshink_welcome_name_rect(layout)));
    assert(in_bounds(layout,meshink_welcome_preset_rect(layout)));
    assert(in_bounds(layout,meshink_welcome_companion_rect(layout)));
    assert(in_bounds(layout,meshink_welcome_show_keyboard_rect(layout)));

    for(int row=0;row<5;++row)
        assert(in_bounds(layout,meshink_preset_row_rect(layout,row)));
    assert(in_bounds(layout,meshink_preset_prev_rect(layout)));
    assert(in_bounds(layout,meshink_preset_next_rect(layout)));

    const MeshInkUiRect node_left=meshink_node_left_action_rect(layout);
    const MeshInkUiRect node_right=meshink_node_right_action_rect(layout);
    assert(in_bounds(layout,node_left));
    assert(in_bounds(layout,node_right));
    assert(node_left.x+node_left.width<node_right.x);
    assert(in_bounds(layout,meshink_node_action_rect(layout)));
    assert(in_bounds(layout,meshink_node_map_rect(layout)));

    for(int i=0;i<3;++i)
        assert(in_bounds(layout,meshink_map_control_rect(layout,i)));

    const MeshInkUiRect quick_slider=meshink_quick_slider_track_rect(layout);
    const MeshInkUiRect quick_touch=meshink_quick_slider_touch_rect(layout);
    assert(in_bounds(layout,quick_slider));
    assert(in_bounds(layout,quick_touch));
    assert(contains(quick_touch,quick_slider));
    assert(in_bounds(layout,meshink_quick_minus_rect(layout)));
    assert(in_bounds(layout,meshink_quick_plus_rect(layout)));
    assert(in_bounds(layout,meshink_quick_advert_rect(layout)));
    assert(in_bounds(layout,meshink_quick_power_rect(layout)));

    const MeshInkUiRect display_slider=meshink_display_slider_track_rect(layout);
    const MeshInkUiRect display_touch=meshink_display_slider_touch_rect(layout);
    assert(in_bounds(layout,display_slider));
    assert(in_bounds(layout,display_touch));
    assert(contains(display_touch,display_slider));
    assert(in_bounds(layout,meshink_display_brightness_rect(layout)));
    assert(in_bounds(layout,meshink_shutdown_rect(layout)));

    assert(in_bounds(layout,meshink_night_start_rect(layout)));
    assert(in_bounds(layout,meshink_night_end_rect(layout)));
    assert(in_bounds(layout,meshink_night_minus_rect(layout)));
    assert(in_bounds(layout,meshink_night_plus_rect(layout)));
    assert(in_bounds(layout,meshink_night_save_rect(layout)));
}

int main() {
    const MeshInkUiLayout t5=meshink_make_ui_layout(540,960);
    check_layout(t5);

    // Field-tested T5 geometry must stay pixel-for-pixel unchanged.
    assert(t5.status_height==48);
    assert(t5.bottom_nav_top==900);
    assert(t5.outer_margin==12&&t5.outer_width==516);
    assert(t5.section_margin==24&&t5.section_width==492);
    assert(t5.form_margin==30&&t5.form_width==480);
    assert(t5.header_top==58&&t5.header_action_x==470);

    expect_rect(meshink_header_back_rect(t5),12,58,58,48);
    expect_rect(meshink_header_back_touch_rect(t5),0,48,110,70);
    expect_rect(meshink_header_action_rect(t5),470,58,58,48);
    expect_rect(meshink_header_action_touch_rect(t5),450,48,90,70);

    expect_rect(meshink_welcome_name_rect(t5),30,180,480,64);
    expect_rect(meshink_welcome_preset_rect(t5),24,292,492,88);
    expect_rect(meshink_welcome_companion_rect(t5),30,402,480,52);
    expect_rect(meshink_welcome_show_keyboard_rect(t5),30,840,480,64);

    expect_rect(meshink_preset_back_rect(t5),12,48,110,84);
    expect_rect(meshink_preset_row_rect(t5,0),12,132,516,112);
    expect_rect(meshink_preset_row_rect(t5,4),12,644,516,112);
    expect_rect(meshink_preset_prev_rect(t5),24,800,180,62);
    expect_rect(meshink_preset_next_rect(t5),336,800,180,62);

    expect_rect(meshink_confirm_left_rect(t5,500),30,500,220,72);
    expect_rect(meshink_confirm_right_rect(t5,500),290,500,220,72);
    expect_rect(meshink_confirm_left_rect(t5,650),30,650,220,72);
    expect_rect(meshink_confirm_right_rect(t5,650),290,650,220,72);

    expect_rect(meshink_node_map_rect(t5),24,590,492,62);
    expect_rect(meshink_node_action_rect(t5),24,808,492,70);
    expect_rect(meshink_node_left_action_rect(t5),24,808,240,70);
    expect_rect(meshink_node_right_action_rect(t5),276,808,240,70);
    expect_rect(meshink_password_save_rect(t5),20,496,260,46);

    expect_rect(meshink_map_control_rect(t5,0),462,58,66,66);
    expect_rect(meshink_map_control_rect(t5,1),462,133,66,66);
    expect_rect(meshink_map_control_rect(t5,2),462,208,66,66);

    expect_rect(meshink_quick_slider_track_rect(t5),44,182,452,16);
    expect_rect(meshink_quick_slider_touch_rect(t5),28,146,484,80);
    expect_rect(meshink_quick_minus_rect(t5),24,256,112,70);
    expect_rect(meshink_quick_plus_rect(t5),404,256,112,70);
    expect_rect(meshink_quick_advert_rect(t5),24,410,238,100);
    expect_rect(meshink_quick_power_rect(t5),278,410,238,100);

    expect_rect(meshink_settings_inline_action_rect(t5,118),374,146,154,56);
    expect_rect(meshink_display_brightness_rect(t5),12,358,516,160);
    expect_rect(meshink_display_slider_track_rect(t5),62,464,416,5);
    expect_rect(meshink_display_slider_touch_rect(t5),40,420,460,100);
    expect_rect(meshink_shutdown_rect(t5),24,790,492,70);

    expect_rect(meshink_night_start_rect(t5),24,150,492,112);
    expect_rect(meshink_night_end_rect(t5),24,286,492,112);
    expect_rect(meshink_night_minus_rect(t5),24,460,220,76);
    expect_rect(meshink_night_plus_rect(t5),296,460,220,76);
    expect_rect(meshink_night_save_rect(t5),24,600,492,76);


    // Synthetic displays prove both axes scale rather than retaining T5 pixels.
    const MeshInkUiLayout compact=meshink_make_ui_layout(480,800);
    check_layout(compact);
    assert(compact.status_height==40);
    assert(compact.bottom_nav_top==750);
    assert(compact.outer_margin==11&&compact.outer_width==458);
    assert(compact.section_margin==21&&compact.section_width==438);
    assert(compact.form_margin==27&&compact.form_width==426);
    assert(meshink_map_control_rect(compact,0).x==411);
    assert(meshink_map_control_rect(compact,0).y==48);

    const MeshInkUiLayout large=meshink_make_ui_layout(720,1280);
    check_layout(large);
    assert(large.status_height>t5.status_height);
    assert(large.outer_margin>t5.outer_margin);
    assert(meshink_welcome_name_rect(large).height>
           meshink_welcome_name_rect(t5).height);
    assert(meshink_map_control_rect(large,0).width>
           meshink_map_control_rect(t5,0).width);

    std::puts("PASS: shared UI drawing/touch geometry preserves T5 and scales on both axes.");
    return 0;
}
