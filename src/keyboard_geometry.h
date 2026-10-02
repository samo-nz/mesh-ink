#pragma once

// Shared drawing and touch geometry for the on-screen keyboard.
//
// The keyboard is described in the T5 reference coordinate system and then
// scaled onto the active logical display. This keeps the field-tested T5
// portrait layout pixel-for-pixel identical while allowing other displays to reuse the
// same keyboard implementation. Board profiles can supply small tuning
// offsets/deltas without forking keyboard code.
namespace meshink_keyboard {

struct Row {
    int start;
    int pitch;
    int width;
    int count;
    int left;
    int right; // exclusive touch boundary
};

struct Rect {
    int x;
    int y;
    int width;
    int height;
};

struct Tuning {
    int x_offset;
    int y_offset;
    int key_height_delta;
    int row_gap_delta;
};

struct Metrics {
    int width;
    int height;
    int key_height;
    int row_gap;
    int row_step;
    int number_top;
    int letter_top;
    int bottom_top;
    int dismiss_above;
    int history_bottom;
    int clear_top;
    Rect entry;
    Row number_row;
    Row letter_rows[3];
    Row symbol_bottom_row;
    Rect mode_key;
    Rect delete_key;
    Rect orientation_key;
    Rect space_key;
    Rect action_key;
    Rect wide_action_key;
};

inline int scale_axis(int value,int actual,int reference) {
    return (value*actual + reference/2)/reference;
}

inline Row scale_row(Row row,int width,int reference_width,int x_offset) {
    row.start=scale_axis(row.start,width,reference_width)+x_offset;
    row.pitch=scale_axis(row.pitch,width,reference_width);
    row.width=scale_axis(row.width,width,reference_width);
    row.left=scale_axis(row.left,width,reference_width)+x_offset;
    row.right=scale_axis(row.right,width,reference_width)+x_offset;
    return row;
}

inline Rect scale_rect(Rect rect,int width,int height,int reference_width,
                       int reference_height,const Tuning& tuning) {
    return {
        scale_axis(rect.x,width,reference_width)+tuning.x_offset,
        scale_axis(rect.y,height,reference_height)+tuning.y_offset,
        scale_axis(rect.width,width,reference_width),
        scale_axis(rect.height,height,reference_height)+tuning.key_height_delta
    };
}

inline Metrics make_metrics(int width,int height,bool landscape,
                            Tuning tuning={0,0,0,0}) {
    const int reference_width=landscape?960:540;
    const int reference_height=landscape?540:960;
    const int reference_number_top=landscape?198:618;

    const Row number_reference=landscape
        ? Row{15,93,88,10,13,943}
        : Row{15,52,49,10,14,534};
    const Row top_reference=number_reference;
    const Row home_reference=landscape
        ? Row{60,93,88,9,58,894}
        : Row{41,52,49,9,40,508};
    const Row bottom_reference=landscape
        ? Row{153,93,88,7,149,805}
        : Row{93,52,49,7,91,457};
    const Row symbol_bottom_reference=landscape
        ? Row{153,81,76,8,149,805}
        : Row{93,45,42,8,91,457};

    const Rect entry_reference=landscape
        ? Rect{16,14,928,165}
        : Rect{12,544,516,70};
    const int history_bottom_reference=landscape?0:526;
    const int clear_top_reference=landscape?0:486;

    const Rect mode_reference=landscape
        ? Rect{15,408,130,62}
        : Rect{12,828,76,62};
    const Rect delete_reference=landscape
        ? Rect{812,408,133,62}
        : Rect{460,828,68,62};
    const Rect orientation_reference=landscape
        ? Rect{15,478,180,62}
        : Rect{12,898,100,62};
    const Rect space_reference=landscape
        ? Rect{203,478,500,62}
        : Rect{120,898,298,62};
    const Rect action_reference=landscape
        ? Rect{711,478,234,62}
        : Rect{426,898,102,62};
    const Rect wide_action_reference=landscape
        ? action_reference
        : Rect{120,898,408,62};

    Metrics metrics{};
    metrics.width=width;
    metrics.height=height;
    metrics.key_height=scale_axis(62,height,reference_height)+tuning.key_height_delta;
    metrics.row_gap=scale_axis(8,height,reference_height)+tuning.row_gap_delta;
    if(metrics.row_gap<0)metrics.row_gap=0;
    metrics.row_step=metrics.key_height+metrics.row_gap;
    metrics.number_top=scale_axis(reference_number_top,height,reference_height)+tuning.y_offset;
    metrics.letter_top=metrics.number_top+metrics.row_step;
    metrics.bottom_top=metrics.letter_top+3*metrics.row_step;
    metrics.dismiss_above=metrics.number_top;
    metrics.history_bottom=history_bottom_reference?
        scale_axis(history_bottom_reference,height,reference_height)+tuning.y_offset:0;
    metrics.clear_top=clear_top_reference?
        scale_axis(clear_top_reference,height,reference_height)+tuning.y_offset:0;
    metrics.entry=scale_rect(entry_reference,width,height,reference_width,reference_height,tuning);
    metrics.entry.height=scale_axis(landscape?165:70,height,reference_height);
    metrics.number_row=scale_row(number_reference,width,reference_width,tuning.x_offset);
    metrics.letter_rows[0]=scale_row(top_reference,width,reference_width,tuning.x_offset);
    metrics.letter_rows[1]=scale_row(home_reference,width,reference_width,tuning.x_offset);
    metrics.letter_rows[2]=scale_row(bottom_reference,width,reference_width,tuning.x_offset);
    metrics.symbol_bottom_row=scale_row(symbol_bottom_reference,width,reference_width,tuning.x_offset);
    metrics.mode_key=scale_rect(mode_reference,width,height,reference_width,reference_height,tuning);
    metrics.delete_key=scale_rect(delete_reference,width,height,reference_width,reference_height,tuning);
    metrics.orientation_key=scale_rect(orientation_reference,width,height,reference_width,reference_height,tuning);
    metrics.space_key=scale_rect(space_reference,width,height,reference_width,reference_height,tuning);
    metrics.action_key=scale_rect(action_reference,width,height,reference_width,reference_height,tuning);
    metrics.wide_action_key=scale_rect(wide_action_reference,width,height,reference_width,reference_height,tuning);
    const int third_top=metrics.letter_top+2*metrics.row_step;
    metrics.mode_key.y=third_top;metrics.delete_key.y=third_top;
    metrics.orientation_key.y=metrics.bottom_top;metrics.space_key.y=metrics.bottom_top;
    metrics.action_key.y=metrics.bottom_top;metrics.wide_action_key.y=metrics.bottom_top;
    metrics.mode_key.height=metrics.key_height;metrics.delete_key.height=metrics.key_height;
    metrics.orientation_key.height=metrics.key_height;metrics.space_key.height=metrics.key_height;
    metrics.action_key.height=metrics.key_height;metrics.wide_action_key.height=metrics.key_height;
    return metrics;
}

inline Row numbers(const Metrics& metrics) {
    return metrics.number_row;
}

inline Row letters(const Metrics& metrics,int row,int count) {
    if(row<=0)return metrics.letter_rows[0];
    if(row==1)return metrics.letter_rows[1];
    return count==8?metrics.symbol_bottom_row:metrics.letter_rows[2];
}

// Compatibility helpers retain the exact field-tested T5 reference metrics.
inline Row numbers(bool landscape) {
    const Metrics metrics=make_metrics(landscape?960:540,landscape?540:960,landscape);
    return numbers(metrics);
}

inline Row letters(bool landscape,int row,int count) {
    const Metrics metrics=make_metrics(landscape?960:540,landscape?540:960,landscape);
    return letters(metrics,row,count);
}

inline int key_index(Row row,int x) {
    if(row.count<=0||x<row.left||x>=row.right)return -1;
    const int first_boundary=row.start-(row.pitch-row.width)/2;
    int index=(x-first_boundary)/row.pitch;
    if(index<0)index=0;
    if(index>=row.count)index=row.count-1;
    return index;
}

inline int key_index_edge_extended(Row row,int x,int screen_width) {
    if(row.count<=0||x<0||x>=screen_width)return -1;
    if(x<row.left)return 0;
    if(x>=row.right)return row.count-1;
    return key_index(row,x);
}

inline bool in_row(int y,int top,int key_height,int row_gap) {
    const int before=row_gap/2;
    const int after=row_gap-before;
    return y>=top-before&&y<top+key_height+after;
}

inline bool in_row(int y,int top,const Metrics& metrics) {
    return in_row(y,top,metrics.key_height,metrics.row_gap);
}

inline bool in_row(int y,int top) {
    return in_row(y,top,62,8);
}

inline int midpoint_between(const Rect& left,const Rect& right) {
    const int left_end=left.x+left.width;
    return (left_end+right.x+1)/2;
}

inline int mode_split(const Metrics& metrics) { return metrics.letter_rows[2].left; }
inline int delete_split(const Metrics& metrics) { return metrics.letter_rows[2].right; }
inline int orientation_split(const Metrics& metrics) {
    return midpoint_between(metrics.orientation_key,metrics.space_key);
}
inline int action_split(const Metrics& metrics) {
    return midpoint_between(metrics.space_key,metrics.action_key);
}

} // namespace meshink_keyboard
