"""Static UI regression checks for MeshInk interaction and map geometry.

The firmware build catches C++ errors; these checks ensure the intended
screen/navigation behaviours and matching draw/touch targets remain wired.
They do not replace a physical GT911, GPS, or e-paper test.
"""
from pathlib import Path

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "ui_onboarding.cpp").read_text(encoding="utf-8")
runtime_source = (root / "src" / "local_mesh_runtime.cpp").read_text(encoding="utf-8")
data_source = (root / "src" / "ui_data.h").read_text(encoding="utf-8")
map_source = (root / "src" / "map_tiles.cpp").read_text(encoding="utf-8")
pmtiles_source = (root / "src" / "pmtiles_reader.cpp").read_text(encoding="utf-8")
pmtiles_header = (root / "src" / "pmtiles_reader.h").read_text(encoding="utf-8")

def contains(fragment, label):
    assert fragment in source, f"{label}: expected code is missing"

# First-install identity, setup text and guaranteed e-paper transitions.
contains('#include <esp_random.h>', "hardware-generated first-install identity")
contains('static char node_name[21] = "MeshInk-";', "default MeshInk identity prefix")
contains('static constexpr char alphabet[]="ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";', "new ID alphabet")
contains('for(int i=0;i<4;++i)node_name[8+i]=alphabet[esp_random()%36];', "four randomized characters")
contains('node_name[12]=0;', "generated name termination")
contains('initial_name.putString("name",node_name);', "generated name persisted before setup save")
contains('}else if(!setup_complete){', "existing completed identity is preserved")
assert 'centred("SETTINGS SAVED",760,2,0,true);' not in source, "obsolete saved line below setup keyboard"
assert 'show_toast(screen==Screen::Welcome?"SETTINGS SAVED"' not in source, "setup saved toast should not obscure first Contacts"
contains('fast_full_redraw("FIRST_SETUP_SCREEN",false);', "full e-paper redraw on first setup")
contains('fast_full_redraw("FIRST_CONTACTS_AFTER_SETUP",true);', "full e-paper redraw on first Contacts")
contains('landscape_key(keyboard_password_mode?"LOGIN":(keyboard_message_mode?"SEND":"DONE"),711,425,234);', "landscape keyboard preserves DONE for name entry and LOGIN for repeater auth")
assert 'if(was_setup)show_contacts_after_setup();' not in source, "landscape keyboard must not complete setup"
assert source.count('save_node_name();')==1, "only the portrait SAVE may persist setup"
contains('keyboard_visible=true;set_keyboard_orientation(false);\n        return true;', "landscape DONE returns to portrait without saving")
contains('if(was_setup){\n            show_contacts_after_setup();', "portrait setup SAVE opens Contacts")

contains("frontlight_brightness=30;", "new-device frontlight default")
contains('prefs.getUChar("light_level",30)', "first-install brightness load")
contains('if(frontlight_brightness>100)frontlight_brightness=30', "brightness fallback preserves valid OFF level")
assert 'frontlight_brightness<1||frontlight_brightness>100' not in source, "saved OFF brightness must survive reboot"
contains('const bool restore_landscape=keyboard_landscape||(quick_panel_active&&quick_panel_restore_landscape);', "standby preserves keyboard under quick settings")
contains('standby_restore_landscape=restore_landscape;', "standby stores resolved landscape restore state")
contains('draw_screen();fast_full_redraw("SHORT_BOOT_REFRESH",false);', "BOOT refresh without home navigation")
assert "SHORT_BOOT_HOME" not in source, "short BOOT still changes navigation"
# Keep non-Maps typing and Home release on the original single-touch path.
# Map gestures use a separate GT911 two-point parser and must not leak into
# the keyboard or other app screens. The old one-branch sampler text check
# predates the map-only split and would reject a working two-point sampler.
contains("bool held=false,home_held=false,map_previous=false;", "independent home touch latch")
contains("const bool on_map=screen==Screen::Maps&&!standby_active&&\n            !keyboard_landscape&&!quick_panel_active;", "Maps yields touch sampling to Quick Settings")
contains("if(!map_touch_points(count,x0,y0,x1,y1,home))", "Maps reads two touch points")
contains("r==1&&!keyboard_symbols", "alphabetic A/L edge expansion is isolated from symbols")
contains("key_index_edge_extended(", "A/L use edge-expanded home-row hit targets")
non_map_sampler = source.split("// Non-Maps keeps the legacy single-touch GT911 parser.", 1)[1].split(
    "vTaskDelay(pdMS_TO_TICKS(8));", 1
)[0]
assert "const bool pressed=touch_point(x,y,home);" in non_map_sampler, "non-Maps must keep the original single-touch parser"
assert "held=false;\n            }else if(home_held){\n                if(!pressed)home_held=false;" in non_map_sampler, "Home must not become ordinary non-Maps tap release"
assert "int16_t event_x=last_x,event_y=last_y;" in non_map_sampler, "ordinary non-keyboard UI release position remains the default"
assert "const bool keyboard_touch=!quick_panel_active&&" in non_map_sampler, "keyboard-only thumb-roll gate"
assert "constexpr int16_t KEYBOARD_TOUCH_SLOP=28;" in non_map_sampler, "bounded keyboard thumb-roll tolerance"
assert "event_x=start_x;" in non_map_sampler and "event_y=start_y;" in non_map_sampler, "small keyboard releases anchor to touch-down"
assert "QueuedTap tap{event_x,event_y,dx,dy,false};" in non_map_sampler, "stabilized non-Maps release event path"
assert "quick_slider_dragging=quick_panel_active&&" in non_map_sampler, "Quick Settings slider enters live-drag mode"
assert "frontlight_preview(quick_slider_preview);" in non_map_sampler, "Quick Settings slider previews brightness during movement"
contains("if(tap.map_sampled&&screen!=Screen::Maps)continue;", "discard stale Maps gestures after tab switch")
contains("if(touch_queue)xQueueReset(touch_queue);", "home clears previous-page touches")
contains("open_screen(setup_complete?Screen::Contacts:Screen::Welcome);", "home persists logical navigation")
contains("static constexpr size_t LIST_ITEMS_PER_PAGE = 5;", "contacts/channels use paged list rows")
contains("contacts_page*LIST_ITEMS_PER_PAGE", "Contacts taps and rendering address later pages")
contains("channels_page*LIST_ITEMS_PER_PAGE", "Channels taps and rendering address later pages")
contains("(screen==Screen::Contacts||screen==Screen::Channels)&&abs(tap.dy)>60", "Contacts/Channels vertical swipe changes pages")
contains("draw_list_page_footer(contacts_page,count);", "Contacts displays page count when multiple pages exist")
contains("draw_list_page_footer(channels_page,count);", "Channels displays page count when multiple pages exist")
contains("if(pages<=1)return;", "single-page Contacts/Channels hide the page footer")
contains("if(page>0)draw_page_arrow", "page indicator shows previous-page swipe-down arrow only when available")
contains("if(page+1<pages)draw_page_arrow", "page indicator shows next-page swipe-up arrow only when available")
contains("(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&!keyboard_visible&&abs(tap.dy)>60", "conversation history uses vertical swipe paging")
contains("draw_page_indicator(chat_page,pages,840);", "conversation history shows swipe page indicator")
assert 'text("OLDER"' not in source and 'text("NEWER"' not in source, "conversation paging buttons must stay removed"
contains("static uint8_t node_info_page_count(uint8_t type){return node_has_status(type)?4:3;}", "Node Info page count is role-aware")
contains("node_has_status(uint8_t type){return type==(uint8_t)UiNodeRole::Repeater||type==(uint8_t)UiNodeRole::Room;}", "Status is exposed for repeaters and room servers")
contains("screen==Screen::ContactDetails&&!keyboard_visible&&abs(tap.dy)>60", "Node Info pages use vertical swipe paging")
contains('UiNodeInfoRequest::Status,"REQUEST STATUS"', "Node Info exposes an individual status request")
contains('UiNodeInfoRequest::Telemetry,"REQUEST TELEMETRY"', "Node Info exposes an individual telemetry request")
contains('UiNodeInfoRequest::Path,"REQUEST PATH"', "Node Info exposes an individual path request")
assert "REQUEST ALL INFO" not in source, "Node Info must not send every remote request at once"
contains("draw_node_role_icon(item.node_type", "Contacts and Discovery show node role icons")
contains('case (uint8_t)UiNodeRole::Repeater:return "REPEATER";', "Repeater role label")
contains('case (uint8_t)UiNodeRole::Room:return "ROOM SERVER";', "Room Server role label")
contains('case (uint8_t)UiNodeRole::Sensor:return "SENSOR";', "Sensor role label")
contains('keyboard_password_mode?"LOGIN"', "protected-node password keyboard has a dedicated login action")
assert "login_active_node(const char* password, bool save_password)" in data_source, "UI provider exposes protected-node login with save option"
assert "frame[0]=26" in runtime_source, "protected-node login uses MeshCore CMD_SEND_LOGIN"
assert "frame[0]==0x85" in runtime_source and "frame[0]==0x86" in runtime_source, "protected-node login handles success and failure pushes"
assert "PERM_ACL_" not in source, "UI must display returned role without inventing ACL constants"
assert 'strcpy(detail_access_,"GUEST")' in runtime_source, "Guest ACL role is reported"
assert 'strcpy(detail_access_,"READ ONLY")' in runtime_source, "Read-only ACL role is reported"
assert 'strcpy(detail_access_,"READ/WRITE")' in runtime_source, "Read-write ACL role is reported"
assert 'strcpy(detail_access_,"ADMIN")' in runtime_source, "Admin ACL role is reported"
assert 'Preferences prefs;if(!prefs.begin("mesh-auth",false))return false;' in runtime_source, "saved remote passwords persist in NVS"
assert "uint8_t key[PUB_KEY_SIZE]" in runtime_source and "char password[16]" in runtime_source, "saved credentials are keyed to full node identity"
contains('text("SAVE PASSWORD"', "password screen has opt-in persistence checkbox")
contains("active_node_saved_password(remote_password,sizeof(remote_password))", "saved password is prefilled on later login")
contains("static void thick_line(int x1,int y1,int x2,int y2)", "role icons use thicker line primitives")
contains("static void thick_rect(int x,int y,int w,int h)", "role icons use thicker rectangle primitives")
contains("draw_wrapped(node.status,24,294,27,3,0,true,14);", "received status text is larger and bold")
contains("draw_wrapped(node.telemetry,24,270,27,3,0,true,4);", "received telemetry text is larger and bold")
contains("draw_wrapped(node.path,24,270,27,3,0,true,5);", "received path text is larger and bold")
contains('page==NodeInfoPage::Status&&hit(x,y,24,808,492,70)', "status action touch follows lowered button")
contains('page==NodeInfoPage::Telemetry&&hit(x,y,24,808,492,70)', "telemetry action touch follows lowered button")
contains('page==NodeInfoPage::Path&&hit(x,y,24,808,492,70)', "path action touch follows lowered button")
assert source.count("active_node_saved_password(remote_password,sizeof(remote_password))")>=2, "saved credentials should prefill from both Status and Telemetry login"

contains('epd_fill_rect({0,486,540,474},0xFF,fb);', "password keyboard clears the lower Node Info background")
contains('screen==Screen::ContactDetails&&!(keyboard_visible&&keyboard_password_mode)', "bottom navigation is hidden while password keyboard is open")
contains('text(remote_password[0]?remote_password:"REMOTE PASSWORD"', "portrait password entry shows plain text")
contains('const char* value=keyboard_password_mode?remote_password:', "landscape password entry shows plain text")
assert "char masked[16]" not in source, "password entry must not mask typed text on-device"


assert "contact.type==ADV_TYPE_REPEATER||contact.type==ADV_TYPE_ROOM" in runtime_source, "status capability includes repeater and room server"
assert "if(request==UiNodeInfoRequest::Status&&(!protected_server||!detail_authenticated_))return false;" in runtime_source, "status requests require authenticated protected server"
assert "BATTERY %.2f V" in runtime_source and "PACKETS RX/TX" in runtime_source, "server status payload is decoded into readable metrics"
assert "POSTS %u" in runtime_source and "PUSHES %u" in runtime_source, "room server status exposes room-specific counters"

assert "request_active_node_info(UiNodeInfoRequest request)" in data_source, "Node Info provider must accept one typed request"
assert "advance_info" not in runtime_source, "Node Info transport must not auto-chain requests"
assert "pending_info.stage" not in runtime_source, "Node Info transport must not retain staged request-all state"
for request in ("Status", "Telemetry", "Path"):
    assert f"pending_info.request==UiNodeInfoRequest::{request}" in runtime_source, f"{request} reply must match only its selected request"
assert "contact_count()&&i<5" not in source, "Contacts must not be hard-limited to the first five entries"
assert "channel_count()&&i<5" not in source, "Channels must not be hard-limited to the first five entries"
contains("text_refresh_pending=false;toast_visible=false;toast_opens_main=false;", "home cancels pending refreshes")
contains("for(int d=-3;d<=3;++d)line(x+2,y+2+d,x+27,y+27+d);", "bold GPS-off slash")
contains("epd_fill_rect({x,y+5,30,3},0,fb);", "bold envelope frame")
contains("for(int d=-1;d<=1;++d) {\n        line(x+3,y+8+d", "bold envelope flap")
for y in (58,133,208):
    contains(f"box(control_x,{y},66,66", f"draw 1.5x map button at y={y}")
    contains(f"if(hit(x,y,462,{y},66,66))", f"matching map touch target at y={y}")
assert 'text("ME",control_x+' not in source, "locate icon must not display text"
contains("box(control_x,208,66,66,true);", "black locate button matches zoom buttons")
contains("epd_fill_rect({target_x,target_y+21,45,5},0xFF,fb);", "large white locate crosshair horizontal")
contains("epd_fill_rect({target_x+21,target_y,5,45},0xFF,fb);", "large white locate crosshair vertical")
contains("draw_target_icon(sx-15,sy-15,false);", "device marker same icon as GPS fix")
contains("if(next==Screen::Maps&&screen!=Screen::Maps&&!preserve_map_centre)", "automatic map recenter")
contains("open_screen(Screen::Maps,true);", "explicit node position preserved")
contains('prefs.getBool("map_fix_saved",false)', "reload last known position")
contains('location_store.putBool("map_fix_saved",true)', "persist verified last known position")
contains("if(enabled&&has_fix&&latitude>=-85051100L", "never replace last fix with disabled/no-fix coordinates")
contains("centre_map_on_device();", "current or stale position recenter")
contains('show_toast(current_fix?"CENTRED ON DEVICE":"CENTRED ON LAST FIX")', "stale position explicitly indicated")
contains("!(tap.x>=456&&tap.y<281)", "larger map controls excluded from swipe")
contains("static constexpr int MAP_TOP=48;", "map starts below compact status bar")
contains("static constexpr int MAP_BOTTOM=900;", "map ends at bottom nav")
contains('epd_fill_rect({0,MAP_TOP,540,MAP_BOTTOM-MAP_TOP},0x00,fb);\n    draw_toast_message("Loading..");', "Maps loading refresh doubles as black contrast preparation")
contains('fast_full_redraw("MAP_BLACK_LOADING_COMPLETE",false);', "Maps reveal final frame after black loading preparation")
assert "transient black prep" not in source, "Maps no longer use a dedicated third black-prep refresh"
contains("static constexpr int MAP_CENTRE_Y=(MAP_TOP+MAP_BOTTOM)/2;", "map projection centre matches viewport")
contains("result=map_tiles_render(fb,0,MAP_TOP,540,MAP_BOTTOM-MAP_TOP,", "map fills entire viewport")
assert 'draw_app_header("MAPS")' not in source, "extra maps header must be removed"
tiles = (Path(__file__).resolve().parents[1] / "src" / "map_tiles.cpp").read_text(encoding="utf-8")
assert "max(48," in tiles and "max(118," not in tiles, "map tile clipping still leaves a header strip"
companion = (Path(__file__).resolve().parents[1] / "src" / "companion_runtime.cpp").read_text(encoding="utf-8")
assert 'initial_gps.getBool("complete",false)' in companion, "existing GPS settings must be preserved"
assert 'initial_gps.getBool("gps_default_v1",false)' in companion, "GPS defaults must only apply once"
assert "settings->gps_enabled=1;" in companion, "new setup GPS must default ON"
assert "settings->gps_interval=0;" in companion, "new setup GPS must default continuous"
assert 'initial_gps.putBool("gps_default_v1",true);' in companion, "GPS default marker missing"
# Portrait keyboard ergonomics: message entry uses a wide space bar with no
# adjacent HIDE key; Radio Settings name entry has a wide SAVE action and
# dismisses by tapping above the keyboard instead.
contains('key("SPACE",120,898,298);', "space-capable portrait keyboards use a wide space bar")
contains('key(keyboard_password_mode?"LOGIN":"SEND",426,898,102);', "message/password action remains isolated at far right")
contains('if(y<618){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MODE_DU);return true;}', "message keyboard dismisses quickly by tapping above it")
assert source.count("if(x<422){append(' ');queue_text_refresh();return true;}")>=2, "message and password former HIDE regions belong to SPACE"
contains('key("SAVE",120,898,408);', "name entry uses a wide SAVE action instead of a dead space bar")
contains('if(screen==Screen::RadioSettings&&y<618){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MODE_DU);return true;}', "Radio Settings keyboard dismisses quickly by tapping above it")
assert 'key("HIDE",318,898,100);' not in source, "portrait HIDE key must be removed everywhere"

# 1.8.4 interaction-latency fixes and sentence-style message keyboard.
contains("static bool message_keyboard_case_dirty = false;", "message keyboard tracks one-time case redraw")
contains("if(n==0&&!keyboard_symbols&&keyboard_upper&&", "first message letter triggers lowercase")
contains("keyboard_upper=false;", "auto lowercase transition")
contains("if(!compose_text[0]){keyboard_symbols=false;keyboard_upper=true;", "fresh messages reopen uppercase")
contains("if(text_refresh_pending)return;", "typing refresh is throttled/coalesced instead of indefinitely debounced")
contains("static void draw_message_entry_fast()", "message typing avoids full chat redraw")
contains("static void draw_radio_name_fast()", "Radio Settings name typing avoids full settings redraw")
contains("replace_name_on_type=false;keyboard_message_mode=false;keyboard_visible=true", "Radio Settings preserves the existing node name when editing")

contains("(settings_page&&!(screen==Screen::RadioSettings&&keyboard_visible))", "bottom tabs are hidden behind Radio Settings keyboard")
contains("const bool text_refresh_due=text_refresh_pending", "text refresh is staged for coalescing")
contains("if(status_dirty&&!message_alert_active)", "status redraw has priority for coalescing")
contains("else if(text_refresh_due)", "text refresh runs only if status did not already redraw")
contains("draw_screen();refresh(MODE_DU);return true;", "same-page keyboard transitions use DU")

# 1.8.3 correlated timing instrumentation must remain wired without adding
# synchronous Serial writes to the touch producer.
contains("uint32_t queued_at_ms=0;", "queued touch events carry enqueue timestamps")
contains("T5InputTimingScope timing_input", "UI measures touch event queue age and handler time")
contains("t5_timing_note_ui_draw", "framebuffer draw timing hook")
contains("t5_timing_note_chat_draw", "chat history/keyboard render split")
contains("t5_timing_note_text_wait", "text debounce timing hook")
contains("T5UiAction::StatusPoll", "status-poll timing attribution")
contains("T5UiAction::TextRefresh", "text-refresh timing attribution")
assert "[T5-TOUCH] input queue full" not in source, "touch producer must never print queue overflow synchronously"

# 1.8.10: full-screen framebuffer composition may use a short 240 MHz burst,
# but must restore the previous clock immediately afterwards.
contains('T5CpuBoostScope draw_cpu_boost(!standby_active,"ui-draw");', "full UI drawing temporarily boosts CPU")
contains('set_cpu_target(previous_mhz,"ui-draw-complete",false);', "UI draw boost restores previous CPU clock")
contains("navigation_touch_cutoff_ms=millis();", "full-screen page navigation records a stale-touch cutoff")
contains("const bool stale_navigation_tap=", "UI filters touch releases queued during blocking navigation")
contains("!keyboard_visible&&!keyboard_landscape&&!quick_panel_active&&", "stale-touch filter excludes keyboard and Quick Settings")
contains("screen!=Screen::Maps;", "stale-touch filter excludes Maps gestures")
assert "native=%u parent=%u src=%u-%u loose=%u pmtiles=%u" in map_source, "map logs native/parent and source zoom/type"
assert "parent-edge=%u parent-full=%u parent-px=%lu" in map_source, "map logs whether fallback tiles are clipped edges or fully visible"
assert "[T5-MAP-PERF]" in map_source, "map emits detailed PMTiles cold-path timing"
assert "preload-seek=%luus preload-read=%luus/%u preload-bytes=%lu" in map_source, "map separates PMTiles sequential preload timing"
assert "range-seek=%luus/%u range-read=%luus/%u range-bytes=%lu" in map_source, "map retains callback range timing for preload fallback"
assert "png.openRAM(pmt_png_buffer" in map_source, "PMTiles PNG payloads decode from reusable RAM preload"
assert "epd_get_rotation()!=EPD_ROT_INVERTED_PORTRAIT" in map_source, "direct map framebuffer path is guarded by portrait rotation"
assert "const int physical_y=physical_height-px-1;" in map_source, "direct map framebuffer path matches EPDiy inverted portrait transform"
assert "out_row[(unsigned)py>>1]=(uint8_t)(low|high);" in map_source, "direct map composition packs two 4-bit panel pixels per byte"
assert "draw_cached_epdiy(tile,draw);" in map_source, "direct map composition retains generic EPDiy fallback"
assert "alignas(4) uint8_t pmt_io_stage[4096]" in map_source, "PMTiles SD payload reads use aligned 4 KB internal staging"
assert "pmt-decode=%luus loose-decode=%luus compose=%luus sd-checks=%u" in map_source, "map separates decode and composition timing"
assert "struct PmtilesPerfStats" in pmtiles_header, "PMTiles reader exposes metadata performance counters"
assert "metadata_seek_us" in pmtiles_source and "metadata_read_us" in pmtiles_source, "PMTiles reader measures metadata I/O"
assert "Keep the active archive handle open across map renders" in pmtiles_source, "PMTiles keeps FAT fast-seek archive handle persistent"
assert "pmtiles_end_frame() {" in pmtiles_source and "frame_active = false;" in pmtiles_source, "PMTiles frame end stops lending without closing archive"
assert "inflate_us" in pmtiles_source and "index_parse_us" in pmtiles_source, "PMTiles reader measures inflate and index parsing"
assert "[T5-PMT] ready path=%s zoom=%u-%u" in pmtiles_source, "PMTiles logs archive zoom coverage"
contains('if(result.native_pmtiles&&result.native_loose)return "MIX";', "MIX badge is reserved for genuinely mixed native sources")
contains('if(result.native_pmtiles)return "PMT";', "native PMTiles wins over harmless parent fallback")
contains('if(result.native_loose)return "PNG";', "native loose PNG wins over harmless parent fallback")
contains('if(result.parent_pmtiles)return "E-M";', "parent-only PMTiles viewport gets enlarged-source badge")
contains('if(result.parent_loose)return "E-P";', "parent-only loose viewport gets enlarged-source badge")
contains('const uint32_t sample_ms=(keyboard_visible||keyboard_landscape)?4:8;', "keyboard touch sampler uses faster cadence for rapid repeated letters")
contains('"ZOOM %u (%s)"', "map displays compact source badge beside zoom")
assert 'has_pmtiles_magic' in map_source, "cache64 archive scan recognizes PMTiles v3 header"
assert 'archive-scan entry=%s dir=%u base=%s' in map_source, "archive scan logs cache64 directory enumeration"
assert 'archive-scan file=%s suffix=%u header=%u' in map_source, "archive scan reports suffix and PMTiles header detection"
board_source = (root / "src" / "board" / "target.cpp").read_text(encoding="utf-8")
timing_source = (root / "src" / "t5_timing.cpp").read_text(encoding="utf-8")
assert "class T5RadioHal final : public ArduinoHal" in board_source, "radio uses custom HAL to share EPDiy GPIO ISR service"
assert "gpio_isr_handler_add(" in board_source, "radio attaches DIO handler to existing IDF ISR service"
assert "ArduinoHal::attachInterrupt" in board_source, "radio HAL retains companion-mode Arduino interrupt fallback"
assert "constexpr uint32_t LEARN_MS=5000;" in timing_source, "timing diagnostics use 5 second warm-up"

print("PASS: UI behaviour, full-height map, monochrome controls and first-setup continuous GPS defaults")
print("PASS: 10 UI issue checks (icon strokes, controls, Home/BOOT, last GPS, brightness)")
