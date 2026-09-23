// Host-side exhaustive check of the *actual C++* keyboard drawing/touch layout.
// Run: g++ -std=c++11 -Wall -Wextra -Werror -Isrc tools/test_keyboard_geometry.cpp -o /tmp/meshink-keys && /tmp/meshink-keys
#include <cassert>
#include <cstdio>
#include <initializer_list>
#include "keyboard_geometry.h"

using meshink_keyboard::Row;
using meshink_keyboard::key_index;
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
    std::printf("PASS: %d keyboard layouts, every key pixel and gap midpoint verified in portrait and landscape; no symbol/Delete overlap.\n",rows);
    return 0;
}
