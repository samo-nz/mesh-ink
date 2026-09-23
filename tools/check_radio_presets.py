"""Check that every MeshInk radio preset's displayed values equal its applied values.

This is intentionally a build-time/source-level consistency test: it does not
claim to measure RF performance or certify a regional frequency allocation.
"""
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
source = (ROOT / "src" / "ui_onboarding.cpp").read_text(encoding="utf-8")
table = re.search(r"static constexpr Preset PRESETS\[\] = \{(.*?)\n\};", source, re.S)
assert table, "Radio preset table not found"
rows = re.findall(
    r'\{"([^"]+)","([^"]+)",(\d+),([\d.]+)f,(\d+),(\d+),(\d+)\}',
    table.group(1),
)
assert len(rows) == 28, f"Expected 28 radio options, found {len(rows)}"
assert len({r[0] for r in rows}) == len(rows), "Duplicate radio option title"
assert rows[0] == ("KEEP CURRENT", "NO RADIO CHANGES", "0", "0", "0", "0", "0"), (
    "KEEP CURRENT must not change any radio setting"
)
assert rows[17][0] == "NZ NARROW", "Default index 17 is no longer NZ NARROW"
pattern = re.compile(
    r"(?P<freq>\d{3}\.\d{3}) / SF(?P<sf>\d+) / "
    r"BW(?P<bw>\d+(?:\.\d+)?) / CR(?P<cr>\d+)"
    r"(?: / (?P<bytes>[123])B)?"
)
for index, (title, description, freq_khz, bw, sf, cr, path_bytes) in enumerate(rows[1:], start=1):
    displayed = pattern.fullmatch(description)
    assert displayed, f"Preset {index}: malformed display settings: {title}"
    settings = (
        int(displayed["freq"].replace(".", "")),
        float(displayed["bw"]),
        int(displayed["sf"]),
        int(displayed["cr"]),
        int(displayed["bytes"] or 1),
    )
    applied = (int(freq_khz), float(bw), int(sf), int(cr), int(path_bytes))
    assert applied == settings, (
        f"Preset {index} {title}: display={settings} but applied={applied}"
    )
    frequency, bandwidth, spread, code, path = applied
    assert 400000 <= frequency <= 950000, f"{title}: frequency out of supported range"
    assert bandwidth in (62.5, 125.0, 250.0, 500.0), f"{title}: unexpected bandwidth"
    assert 5 <= spread <= 12, f"{title}: invalid SF"
    assert 5 <= code <= 8, f"{title}: invalid CR"
    assert 1 <= path <= 3, f"{title}: invalid path hash size"

assert 'if(setup_complete||selected_preset==0)return;' in source, (
    "Completed installations and KEEP CURRENT must remain unchanged at boot"
)
assert 'ui_apply_initial_radio_preset();' in (
    ROOT / "src" / "companion_runtime.cpp"
).read_text(encoding="utf-8"), "Fresh-setup radio synchronization is not wired into startup"

print(f"PASS: {len(rows)} radio options verified (27 configured + KEEP CURRENT); "
      "displayed values, applied values, first-boot hook and path hash agree")
