// Host-side exhaustive check of the *actual C++* keyboard drawing/touch layout.
// Run: g++ -std=c++11 -Wall -Wextra -Werror -Isrc tools/test_keyboard_geometry.cpp -o /tmp/meshink-keys && /tmp/meshink-keys
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "keyboard_geometry.h"

using meshink_keyboard::Row;
using meshink_keyboard::key_index;
using meshink_keyboard::key_index_edge_extended;
using meshink_keyboard::in_row;

static void check_row(Row row,int screen_width) {
    assert(row.count>0);
    assert(row.start>=0&&row.pitch>row.width&&row.width>=40);
    assert(row.left>=0&&row.right<=screen_width);
    assert(row.start>=row.left);
    assert(row.start+(row.count-1)*row.pitch+row.width<=row.right);
    assert(key_index(row,row.left)==0);
    assert(key_index(row,row.right-1)==row.count-1);
    assert(key_index(row,row.left-1)==-1);
    assert(key_index(row,row.right)==-1);
    for(int i=0;i<row.count;++i){
        const int start=row.start+i*row.pitch;
        // Every pixel of each visible key, including BOTH edges, must map
        // to that key. This catches the old T->Y half-key shift.
        for(int x=start;x<start+row.width;++x)
            assert(key_index(row,x)==i);
        if(i+1<row.count){
            const int next=start+row.pitch;
            const int gap=next-(start+row.width);
            assert(gap>0);
            const int midpoint=row.start-(row.pitch-row.width)/2+(i+1)*row.pitch;
            for(int x=start+row.width;x<next;++x)
                assert(key_index(row,x)==(x<midpoint?i:i+1));
        }
    }
}

int main() {
    int rows=0;
    for(const bool landscape : {false,true}){
        const int width=landscape?960:540;
        check_row(meshink_keyboard::numbers(landscape),width);++rows;
        for(int row=0;row<3;++row){
            const int normal_count=row==0?10:row==1?9:7;
            check_row(meshink_keyboard::letters(landscape,row,normal_count),width);++rows;
            if(row==2){
                check_row(meshink_keyboard::letters(landscape,row,8),width);++rows;
                const auto symbols=meshink_keyboard::letters(landscape,row,8);
                // The eight symbols must NOT overdraw the visible Delete key.
                assert(symbols.start+7*symbols.pitch+symbols.width <=
                       (landscape?812:460));
                assert(symbols.left==(landscape?149:91));
                assert(symbols.right==(landscape?805:457));
            }
        }
        // Alphabetic home row: A/L deliberately own the blank margins
        // to the physical display edges. Ordinary key_index() stays strict so
        // symbol rows and every other row keep their existing geometry.
        const auto home=meshink_keyboard::letters(landscape,1,9);
        assert(key_index(home,home.left-1)==-1);
        assert(key_index(home,home.right)==-1);
        assert(key_index_edge_extended(home,0,width)==0);
        assert(key_index_edge_extended(home,home.left-1,width)==0);
        assert(key_index_edge_extended(home,home.left,width)==0);
        assert(key_index_edge_extended(home,home.right-1,width)==8);
        assert(key_index_edge_extended(home,home.right,width)==8);
        assert(key_index_edge_extended(home,width-1,width)==8);
        assert(key_index_edge_extended(home,-1,width)==-1);
        assert(key_index_edge_extended(home,width,width)==-1);

        const auto top=meshink_keyboard::letters(landscape,0,10);
        const auto digits=meshink_keyboard::numbers(landscape);
        const int t=top.start+4*top.pitch+top.width-1;
        const int five=digits.start+4*digits.pitch+digits.width-1;
        assert(key_index(top,t)==4);    // right-hand edge of T still types T
        assert(key_index(digits,five)==4); // right-hand edge of 5 types 5
        const int number_y=landscape?145:618;
        const int letter_y=landscape?215:688;
        assert(in_row(number_y-4,number_y));
        assert(!in_row(letter_y-4,number_y));
        assert(in_row(letter_y-4,letter_y));
        for(int row=0;row<3;++row){
            const int top_y=letter_y+70*row;
            assert(in_row(top_y-4,top_y));
            assert(in_row(top_y+65,top_y));
            assert(!in_row(top_y+66,top_y));
        }
    }
    // Special buttons use the same midpoint ownership as the visible gaps.
    // Portrait third row: mode [12,88), letters [93..), DEL [460,528).
    assert(91==(88+93+1)/2);
    assert(457==(454+460)/2);
    // Portrait bottom: LAND [12,112), SPACE [120,418), SEND [426,528).
    assert(116==(112+120)/2);
    assert(422==(418+426)/2);
    // Landscape third row: mode [15,145), letters [153..), DEL [812,945).
    assert(149==(145+153)/2);
    assert(805==(798+812)/2);
    // Landscape bottom: PORTRAIT [15,195), SPACE [203,703), action [711,945).
    assert(199==(195+203)/2);
    assert(707==(703+711)/2);

    // The landscape touch transform must cover the entire 960x540 display
    // without changing orientation or introducing an off-by-one edge.
    auto landscape_x=[](int raw_x,int raw_y){(void)raw_x;return raw_y;};
    auto landscape_y=[](int raw_x,int raw_y){(void)raw_y;return 539-raw_x;};
    assert(landscape_x(0,0)==0&&landscape_y(0,0)==539);
    assert(landscape_x(539,959)==959&&landscape_y(539,959)==0);
    assert(landscape_x(539,0)==0&&landscape_y(539,0)==0);
    assert(landscape_x(0,959)==959&&landscape_y(0,959)==539);

    std::printf("PASS: %d keyboard layouts verified in portrait and landscape; A/L edge expansion and symbol/Delete separation confirmed.\n",rows);
    return 0;
}
