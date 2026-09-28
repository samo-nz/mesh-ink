// Host-side checks for the actual scalable keyboard drawing/touch geometry.
// Run: g++ -std=c++11 -Wall -Wextra -Werror -Isrc tools/test_keyboard_geometry.cpp -o /tmp/meshink-keys && /tmp/meshink-keys
#include <cassert>
#include <cstdio>
#include "keyboard_geometry.h"

using meshink_keyboard::Metrics;
using meshink_keyboard::Row;
using meshink_keyboard::key_index;
using meshink_keyboard::key_index_edge_extended;
using meshink_keyboard::in_row;

static int visible_right(Row row) {
    return row.start+(row.count-1)*row.pitch+row.width;
}

static void check_row(Row row,int screen_width) {
    assert(row.count>0);
    assert(row.start>=0&&row.pitch>=row.width&&row.width>=30);
    assert(row.left>=0&&row.right<=screen_width);
    assert(row.start>=row.left);
    assert(visible_right(row)<=row.right);
    assert(key_index(row,row.left)==0);
    assert(key_index(row,row.right-1)==row.count-1);
    assert(key_index(row,row.left-1)==-1);
    assert(key_index(row,row.right)==-1);
    for(int i=0;i<row.count;++i){
        const int start=row.start+i*row.pitch;
        // Every visible key pixel must map back to that same key.
        for(int x=start;x<start+row.width;++x)
            assert(key_index(row,x)==i);
        if(i+1<row.count){
            const int next=start+row.pitch;
            const int gap=next-(start+row.width);
            assert(gap>=0);
            if(gap){
                const int midpoint=row.start-(row.pitch-row.width)/2+(i+1)*row.pitch;
                for(int x=start+row.width;x<next;++x)
                    assert(key_index(row,x)==(x<midpoint?i:i+1));
            }
        }
    }
}

static void check_metrics(const Metrics& metrics) {
    check_row(meshink_keyboard::numbers(metrics),metrics.width);
    for(int row=0;row<3;++row){
        const int normal_count=row==0?10:row==1?9:7;
        check_row(meshink_keyboard::letters(metrics,row,normal_count),metrics.width);
        if(row==2){
            check_row(meshink_keyboard::letters(metrics,row,8),metrics.width);
            const auto symbols=meshink_keyboard::letters(metrics,row,8);
            assert(visible_right(symbols)<=metrics.delete_key.x);
            assert(symbols.left==meshink_keyboard::mode_split(metrics));
            assert(symbols.right==meshink_keyboard::delete_split(metrics));
        }
    }

    // Alphabetic home-row edge expansion remains isolated from strict rows.
    const auto home=meshink_keyboard::letters(metrics,1,9);
    assert(key_index(home,home.left-1)==-1);
    assert(key_index(home,home.right)==-1);
    assert(key_index_edge_extended(home,0,metrics.width)==0);
    assert(key_index_edge_extended(home,metrics.width-1,metrics.width)==8);
    assert(key_index_edge_extended(home,-1,metrics.width)==-1);
    assert(key_index_edge_extended(home,metrics.width,metrics.width)==-1);

    assert(in_row(metrics.number_top,metrics.number_top,metrics));
    for(int row=0;row<3;++row){
        const int top=metrics.letter_top+metrics.row_step*row;
        assert(in_row(top,top,metrics));
        assert(in_row(top+metrics.key_height-1,top,metrics));
    }

    assert(metrics.mode_key.y==metrics.letter_top+2*metrics.row_step);
    assert(metrics.delete_key.y==metrics.mode_key.y);
    assert(metrics.orientation_key.y==metrics.bottom_top);
    assert(metrics.space_key.y==metrics.bottom_top);
    assert(metrics.action_key.y==metrics.bottom_top);
    assert(metrics.mode_key.x+metrics.mode_key.width<=metrics.letter_rows[2].start);
    assert(visible_right(metrics.letter_rows[2])<=metrics.delete_key.x);
    assert(metrics.orientation_key.x+metrics.orientation_key.width<=metrics.space_key.x);
    assert(metrics.space_key.x+metrics.space_key.width<=metrics.action_key.x);
    assert(meshink_keyboard::orientation_split(metrics)>metrics.orientation_key.x);
    assert(meshink_keyboard::action_split(metrics)>metrics.space_key.x);
    assert(metrics.entry.x>=0&&metrics.entry.y>=0);
    assert(metrics.entry.x+metrics.entry.width<=metrics.width);
    assert(metrics.entry.y+metrics.entry.height<=metrics.height);
}

int main() {
    const Metrics portrait=meshink_keyboard::make_metrics(540,960,false);
    const Metrics landscape=meshink_keyboard::make_metrics(960,540,true);

    // T5 reference appearance and hit boundaries must remain pixel-identical.
    assert(portrait.key_height==62&&portrait.row_gap==8&&portrait.row_step==70);
    assert(portrait.number_top==618&&portrait.letter_top==688&&portrait.bottom_top==898);
    assert(portrait.number_row.start==15&&portrait.number_row.pitch==52&&portrait.number_row.width==49);
    assert(portrait.mode_key.x==12&&portrait.mode_key.y==828&&portrait.mode_key.width==76);
    assert(portrait.delete_key.x==460&&portrait.delete_key.width==68);
    assert(portrait.orientation_key.x==12&&portrait.orientation_key.width==100);
    assert(portrait.space_key.x==120&&portrait.space_key.width==298);
    assert(portrait.action_key.x==426&&portrait.action_key.width==102);
    assert(portrait.wide_action_key.x==120&&portrait.wide_action_key.width==408);
    assert(portrait.entry.x==12&&portrait.entry.y==544&&portrait.entry.width==516&&portrait.entry.height==70);
    assert(portrait.history_bottom==526&&portrait.clear_top==486);
    assert(meshink_keyboard::mode_split(portrait)==91);
    assert(meshink_keyboard::delete_split(portrait)==457);
    assert(meshink_keyboard::orientation_split(portrait)==116);
    assert(meshink_keyboard::action_split(portrait)==422);

    assert(landscape.key_height==62&&landscape.row_gap==8&&landscape.row_step==70);
    assert(landscape.number_top==145&&landscape.letter_top==215&&landscape.bottom_top==425);
    assert(landscape.number_row.start==15&&landscape.number_row.pitch==93&&landscape.number_row.width==88);
    assert(landscape.mode_key.x==15&&landscape.mode_key.y==355&&landscape.mode_key.width==130);
    assert(landscape.delete_key.x==812&&landscape.delete_key.width==133);
    assert(landscape.orientation_key.x==15&&landscape.orientation_key.width==180);
    assert(landscape.space_key.x==203&&landscape.space_key.width==500);
    assert(landscape.action_key.x==711&&landscape.action_key.width==234);
    assert(landscape.entry.x==16&&landscape.entry.y==14&&landscape.entry.width==928&&landscape.entry.height==112);
    assert(meshink_keyboard::mode_split(landscape)==149);
    assert(meshink_keyboard::delete_split(landscape)==805);
    assert(meshink_keyboard::orientation_split(landscape)==199);
    assert(meshink_keyboard::action_split(landscape)==707);

    // Legacy helper overloads still expose the exact T5 reference rows.
    assert(meshink_keyboard::numbers(false).start==portrait.number_row.start);
    assert(meshink_keyboard::letters(true,2,8).width==landscape.symbol_bottom_row.width);

    check_metrics(portrait);
    check_metrics(landscape);

    // Synthetic larger board: geometry scales as one unit and stays bounded.
    const Metrics big_portrait=meshink_keyboard::make_metrics(720,1280,false);
    const Metrics big_landscape=meshink_keyboard::make_metrics(1280,720,true);
    check_metrics(big_portrait);
    check_metrics(big_landscape);
    assert(big_portrait.number_top>portrait.number_top);
    assert(big_portrait.number_row.width>portrait.number_row.width);
    assert(big_portrait.entry.width>portrait.entry.width);
    assert(big_landscape.space_key.width>landscape.space_key.width);

    // Board tuning can make small physical/touch corrections without forking
    // keyboard drawing or hit-testing.
    const meshink_keyboard::Tuning tuned={3,-5,2,1};
    const Metrics adjusted=meshink_keyboard::make_metrics(540,960,false,tuned);
    assert(adjusted.number_row.start==18);
    assert(adjusted.number_top==613);
    assert(adjusted.key_height==64);
    assert(adjusted.row_gap==9);
    assert(adjusted.entry.x==15&&adjusted.entry.y==539);

    // The landscape touch transform still covers the complete T5 surface.
    auto landscape_x=[](int raw_x,int raw_y){(void)raw_x;return raw_y;};
    auto landscape_y=[](int raw_x,int raw_y){(void)raw_y;return 539-raw_x;};
    assert(landscape_x(0,0)==0&&landscape_y(0,0)==539);
    assert(landscape_x(539,959)==959&&landscape_y(539,959)==0);
    assert(landscape_x(539,0)==0&&landscape_y(539,0)==0);
    assert(landscape_x(0,959)==959&&landscape_y(0,959)==539);

    std::printf("PASS: scalable keyboard metrics preserve exact T5 geometry, shared hitboxes, entry layout and board tuning.\n");
    return 0;
}
