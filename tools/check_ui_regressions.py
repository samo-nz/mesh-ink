"""Static UI regression checks for MeshInk interaction and map geometry.

The firmware build catches C++ errors; these checks ensure the intended
screen/navigation behaviours and matching draw/touch targets remain wired.
They do not replace a physical GT911, GPS, or e-paper test.
"""
from pathlib import Path

source = (Path(__file__).resolve().parents[1] / "src" / "ui_onboarding.cpp").read_text(
    encoding="utf-8"
)

def contains(fragment, label):
    assert fragment in source, f"{label}: expected code is missing"

contains("frontlight_brightness=30;", "new-device frontlight default")
contains('prefs.getUChar("light_level",30)', "first-install brightness load")
contains('if(frontlight_brightness<1||frontlight_brightness>100)frontlight_brightness=30', "brightness fallback")
contains('draw_screen();fast_full_redraw("SHORT_BOOT_REFRESH",false);', "BOOT refresh without home navigation")
assert "SHORT_BOOT_HOME" not in source, "short BOOT still changes navigation"
contains("bool held=false,home_held=false;", "independent home touch latch")
contains("held=false;\n        }else if(home_held)", "home must not become ordinary tap release")
contains("if(touch_queue)xQueueReset(touch_queue);", "home clears previous-page touches")
contains("open_screen(setup_complete?Screen::Contacts:Screen::Welcome);", "home persists logical navigation")
contains("text_refresh_pending=false;toast_visible=false;toast_opens_main=false;", "home cancels pending refreshes")
contains("for(int d=-3;d<=3;++d)line(x+2,y+2+d,x+27,y+27+d);", "bold GPS-off slash")
contains("epd_fill_rect({x,y+5,30,3},0,fb);", "bold envelope frame")
contains("for(int d=-1;d<=1;++d) {\n        line(x+3,y+8+d", "bold envelope flap")
for y in (130,205,280):
    contains(f"box(control_x,{y},66,66", f"draw 1.5x map button at y={y}")
    contains(f"if(hit(x,y,462,{y},66,66))", f"matching map touch target at y={y}")
contains('text("ME",control_x+21,324,2,0,true);', "visible locate label")
contains("draw_target_icon(control_x+18,284,false);", "black-on-white locate crosshair")
contains("draw_target_icon(sx-15,sy-15,false);", "device marker same icon as GPS fix")
contains("if(next==Screen::Maps&&screen!=Screen::Maps&&!preserve_map_centre)", "automatic map recenter")
contains("open_screen(Screen::Maps,true);", "explicit node position preserved")
contains('prefs.getBool("map_fix_saved",false)', "reload last known position")
contains('location_store.putBool("map_fix_saved",true)', "persist verified last known position")
contains("if(enabled&&has_fix&&latitude>=-85051100L", "never replace last fix with disabled/no-fix coordinates")
contains("centre_map_on_device();", "current or stale position recenter")
contains('show_toast(current_fix?"CENTRED ON DEVICE":"CENTRED ON LAST FIX")', "stale position explicitly indicated")
contains("!(tap.x>=456&&tap.y<353)", "larger map controls excluded from swipe")
print("PASS: 10 UI issue checks (icon strokes, controls, Home/BOOT, last GPS, brightness)")
