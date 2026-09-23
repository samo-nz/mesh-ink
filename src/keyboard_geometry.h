#pragma once

// Shared drawing and touch geometry for the on-screen keyboard. Integer-only
// calculations allow this header to be tested on a host without Arduino.
// Horizontal hitboxes divide the small gaps at their midpoint, instead of
// shifting letters half a key to the right.
namespace meshink_keyboard {

struct Row {
    int start;
    int pitch;
    int width;
    int count;
    int left;
    int right; // exclusive
};

inline Row numbers(bool landscape) {
    return landscape ? Row{15,93,88,10,13,943}
                     : Row{15,52,49,10,14,534};
}

inline Row letters(bool landscape,int row,int count) {
    if(row==0)
        return landscape ? Row{15,93,88,count,13,943}
                         : Row{15,52,49,count,14,534};
    if(row==1)
        return landscape ? Row{60,93,88,count,58,894}
                         : Row{41,52,49,count,40,508};
    // Seven letter keys or eight symbol keys must fit between the mode
    // and Delete buttons. Both drawing and touch use the same pitch.
    if(landscape)
        return count==8 ? Row{153,81,76,count,149,805}
                        : Row{153,93,88,count,149,805};
    return count==8 ? Row{93,45,42,count,91,457}
                    : Row{93,52,49,count,91,457};
}

inline int key_index(Row row,int x) {
    if(row.count<=0||x<row.left||x>=row.right)return -1;
    const int first_boundary=row.start-(row.pitch-row.width)/2;
    int index=(x-first_boundary)/row.pitch;
    if(index<0)index=0;
    if(index>=row.count)index=row.count-1;
    return index;
}

inline bool in_row(int y,int top) {
    // Extend each 62-pixel key to the midpoint of the 8-pixel row gap.
    return y>=top-4&&y<top+66;
}

} // namespace meshink_keyboard
