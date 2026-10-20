"""Protect byte-preserving message storage and offline backups during UTF-8 UI work.
This is a source-level regression guard, not a power-loss or hardware test.
"""
from pathlib import Path

root=Path(__file__).resolve().parents[1]
read=lambda p:(root/p).read_text(encoding="utf-8")
store=read("src/message_store.cpp")
meshtastic=read("src/protocol/meshtastic_ui_data.cpp")
core=read("src/local_mesh_runtime.cpp")
backup=read("src/backup_restore.cpp")
ui=read("src/ui_onboarding.cpp")
decoder=read("src/utf8_display.h")
assert "strnlen(text,max_text+2)>max_text" in store, "journal byte limit must reject oversize payloads"
assert "if(text)strncpy(item.text,text,sizeof(item.text)-1);" in store, "journal must copy original text bytes"
assert "memcpy(msg->text,p.decoded.payload.bytes,copied)" in meshtastic, "native text packet bytes must be preserved"
assert "MESHINK_MESSAGE_TEXT_MAX==MAX_TEXT_LEN" in core, "MeshCore byte limit mismatch"
assert "crc32(io,wanted,crc)" in backup and "crc32(io,bytes,crc)" in backup, "backup integrity must remain byte-based"
assert "meshink_utf8::next(" in ui, "decode text only at UI display"
assert "meshink_utf8::prior(" in ui, "clipping must respect UTF-8 boundaries"
assert "inline Char next(" in decoder, "display decoder missing"
assert "meshink_utf8::" not in store+meshtastic+core+backup, "display decoding leaked into persistence/protocol"
print("PASS: UTF-8 remains display-only; journal, wire bytes and archive checksums preserved")
