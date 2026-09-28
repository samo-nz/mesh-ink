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

    MeshInkUiRect r=meshink_welcome_name_rect(t5);
    assert(r.x==30&&r.y==180&&r.width==480&&r.height==64);
    r=meshink_map_control_rect(t5,0);
    assert(r.x==462&&r.y==58&&r.width==66&&r.height==66);
    r=meshink_map_control_rect(t5,2);
    assert(r.x==462&&r.y==208&&r.width==66&&r.height==66);
    r=meshink_quick_minus_rect(t5);
    assert(r.x==24&&r.y==256&&r.width==112&&r.height==70);
    r=meshink_quick_advert_rect(t5);
    assert(r.x==24&&r.y==410&&r.width==238&&r.height==100);
    r=meshink_node_left_action_rect(t5);
    assert(r.x==24&&r.y==808&&r.width==240&&r.height==70);
    r=meshink_node_right_action_rect(t5);
    assert(r.x==276&&r.y==808&&r.width==240&&r.height==70);
    r=meshink_display_slider_track_rect(t5);
    assert(r.x==62&&r.y==464&&r.width==416&&r.height==5);
    r=meshink_night_minus_rect(t5);
    assert(r.x==24&&r.y==460&&r.width==220&&r.height==76);

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
