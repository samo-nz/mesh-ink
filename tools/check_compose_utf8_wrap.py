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
print("PASS: compose wrapping advances by complete UTF-8 codepoints")
