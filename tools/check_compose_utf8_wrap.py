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
assert "draw_toast();" in portrait and "draw_keyboard_picker();" in portrait, "Portrait picker must be rendered"\nassert portrait.rindex("draw_keyboard_picker();") > portrait.rindex("draw_toast();"), "Portrait picker must overlay chat, toast and keyboard"
