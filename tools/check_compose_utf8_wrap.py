#!/usr/bin/env python3
"""Guard against byte-wise UTF-8 wrapping in MeshInk's compose entry."""
from pathlib import Path

source = Path("src/ui_onboarding.cpp").read_text()
start = source.index("static int ui_compose_wrap_lines(")
end = source.index("static void ui_draw_compose_tail(", start)
wrap = source[start:end]
assert "meshink_utf8::next(scan,strlen(scan))" in wrap, "Composer must decode a whole Unicode codepoint"
assert "scan+=ch.bytes;" in wrap, "Composer must advance by the UTF-8 codepoint length"
assert "ui_char_advance(ch.codepoint,scale)" in wrap, "Composer width must measure Unicode codepoints"
assert "++scan;" not in wrap, "Byte-wise compose wrapping splits multibyte characters"

natural_start = source.index("static int ui_text_max_line_width(")
natural_end = source.index("static size_t ui_wrap_take(", natural_start)
natural = source[natural_start:natural_end]
assert "meshink_utf8::next(p,strlen(p))" in natural, "Bubble width must decode complete UTF-8 codepoints"
assert "ui_char_advance(ch.codepoint,scale)" in natural, "Bubble sizing must use glyph pixel advance"
assert "p+=ch.bytes;" in natural, "Bubble sizing must not advance one UTF-8 byte at a time"

print("PASS: Unicode-aware composer wrapping and bubble width measurement")

# The picker must be painted on top of the portrait chat after all other UI.
portrait = source[source.index("static void draw_screen() {"):source.index("static void refresh(MeshInkRefreshMode mode,bool wake_light=true) {")]
assert "draw_toast();" in portrait and "draw_keyboard_picker();" in portrait, "Portrait picker must be rendered"
assert portrait.rindex("draw_keyboard_picker();") > portrait.rindex("draw_toast();"), "Portrait picker must overlay chat, toast and keyboard"

# An incremental chat redraw must explicitly erase popup pixels on dismissal.
assert "static void keyboard_picker_clear_pixels()" in source
handler = source[source.index("int choice=keyboard_picker_option"): ]
assert handler.count("keyboard_picker_clear_pixels();") >= 3
assert "Repaint the erased history overlay after the key tap." in handler

# Release-candidate guards for complete-codepoint label truncation and
# touch-state continuity (source contracts, not a substitute for hardware tests).
centred = source[source.index("static void ui_centred_fit("):source.index("static int ui_text_max_line_width(")]
assert "meshink_utf8::next(value+out,value_bytes-out)" in centred
assert "ui_char_advance(cp.codepoint,scale)" in centred
assert "memcpy(clipped+out,value+out,cp.bytes)" in centred
assert "out+=cp.bytes" in centred
assert "clipped[out]=value[out]" not in centred

picker = source[source.index("static void keyboard_picker_start("):source.index("static void draw_keyboard()")]
assert "if(keyboard_landscape)for(uint32_t shown:MESHINK_KEYBOARD_FREQUENT_EMOJI)" in picker
assert "if(!frequent)picker_chars[picker_count++]=cp;" in picker
assert "const int columns=keyboard_landscape?12:8;" in picker
assert "return index<picker_count?index:-1;" in picker

touch = source[source.index("// Show optional characters while the finger is still held."):
               source.index("if(keyboard_delete_hold&&!quick_panel_active",source.index("// Show optional characters while the finger is still held."))]
assert "millis()-pressed_at>=450" in touch
assert "keyboard_message_mode&&(keyboard_visible||keyboard_landscape)" in touch
assert "if(queue_touch_event(hold))keyboard_long_emitted=true;" in touch
assert "!keyboard_delete_hold" in touch

picker_events = source[source.index("int choice=keyboard_picker_option"):source.index("if(!handled_picker)handle_tap(tap.x,tap.y);")]
assert "if(keyboard_landscape&&picker_emoji&&" in picker_events
assert "selected=MESHINK_KEYBOARD_FREQUENT_EMOJI[index];" in picker_events
assert "picker_open=false;picker_pending_key=0;" in picker_events
assert "if(picker_pending_key&&!same_key)append(picker_pending_key);" in picker_events
assert "if(!handled_picker&&tap.hold_ms>=450)" in picker_events
print("PASS: UTF-8 centred clipping and keyboard picker interaction contracts")
