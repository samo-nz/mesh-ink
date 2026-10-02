"""Static UI regression checks for MeshInk interaction and map geometry.

The firmware build catches C++ errors; these checks ensure the intended
screen/navigation behaviours and matching draw/touch targets remain wired.
They do not replace a physical touch controller, GPS, or e-paper test.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "ui_onboarding.cpp").read_text(encoding="utf-8")
runtime_source = (root / "src" / "local_mesh_runtime.cpp").read_text(encoding="utf-8")
message_store_source = (root / "src" / "message_store.cpp").read_text(encoding="utf-8")
message_store_header = (root / "src" / "message_store.h").read_text(encoding="utf-8")
message_limits_source = (root / "src" / "message_limits.h").read_text(encoding="utf-8")
component_cmake_source = (root / "src" / "CMakeLists.txt").read_text(encoding="utf-8")
data_source = (root / "src" / "ui_data.h").read_text(encoding="utf-8")
map_source = (root / "src" / "map_tiles.cpp").read_text(encoding="utf-8")
pmtiles_source = (root / "src" / "pmtiles_reader.cpp").read_text(encoding="utf-8")
pmtiles_header = (root / "src" / "pmtiles_reader.h").read_text(encoding="utf-8")
unified_source = (root / "src" / "unified_main.cpp").read_text(encoding="utf-8")
standalone_source = (root / "src" / "ui_standalone_main.cpp").read_text(encoding="utf-8")
board_target_source = (root / "src" / "board" / "target.cpp").read_text(encoding="utf-8")
companion_source = (root / "src" / "companion_runtime.cpp").read_text(encoding="utf-8")
companion_notice_source = (root / "src" / "companion_notice.cpp").read_text(encoding="utf-8")
ui_layout_source = (root / "src" / "ui_layout.h").read_text(encoding="utf-8")
display_backend_source = (root / "src" / "board" / "t5_display_backend.h").read_text(encoding="utf-8")
display_types_source = (root / "src" / "hardware" / "display_types.h").read_text(encoding="utf-8")
wireless_selector_source = (root / "src" / "hardware" / "wireless.h").read_text(encoding="utf-8")
wireless_types_source = (root / "src" / "hardware" / "wireless_types.h").read_text(encoding="utf-8")
wireless_backend_source = (root / "src" / "board" / "t5_wireless_backend.h").read_text(encoding="utf-8")
power_selector_source = (root / "src" / "hardware" / "power.h").read_text(encoding="utf-8")
power_types_source = (root / "src" / "hardware" / "power_types.h").read_text(encoding="utf-8")
power_backend_header = (root / "src" / "board" / "t5_power_backend.h").read_text(encoding="utf-8")
power_backend_source = (root / "src" / "board" / "t5_power_backend.cpp").read_text(encoding="utf-8")
buttons_selector_source = (root / "src" / "hardware" / "buttons.h").read_text(encoding="utf-8")
buttons_backend_header = (root / "src" / "board" / "t5_buttons_backend.h").read_text(encoding="utf-8")
buttons_backend_source = (root / "src" / "board" / "t5_buttons_backend.cpp").read_text(encoding="utf-8")
radio_selector_source = (root / "src" / "hardware" / "radio.h").read_text(encoding="utf-8")
radio_types_source = (root / "src" / "hardware" / "radio_types.h").read_text(encoding="utf-8")
radio_backend_header = (root / "src" / "board" / "t5_radio_backend.h").read_text(encoding="utf-8")
radio_backend_source = (root / "src" / "board" / "t5_radio_backend.cpp").read_text(encoding="utf-8")
storage_selector_source = (root / "src" / "hardware" / "storage.h").read_text(encoding="utf-8")
storage_backend_header = (root / "src" / "board" / "t5_storage_backend.h").read_text(encoding="utf-8")
storage_backend_source = (root / "src" / "board" / "t5_storage_backend.cpp").read_text(encoding="utf-8")
board_selector_source = (root / "src" / "hardware" / "board.h").read_text(encoding="utf-8")
board_backend_source = (root / "src" / "board" / "t5_board_backend.h").read_text(encoding="utf-8")
touch_selector_source = (root / "src" / "hardware" / "touch.h").read_text(encoding="utf-8")
touch_types_source = (root / "src" / "hardware" / "touch_types.h").read_text(encoding="utf-8")
touch_backend_source = (root / "src" / "board" / "t5_touch_backend.h").read_text(encoding="utf-8")
platformio_source = (root / "platformio.ini").read_text(encoding="utf-8")
cache64_build_flags = platformio_source.split("[env:t5-unified-cache64]", 1)[1].split("; Generic portability", 1)[0]

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
contains('landscape_key(keyboard_password_mode?"LOGIN":(keyboard_message_mode?"SEND":"DONE"),metrics.action_key);', "landscape keyboard preserves DONE for name entry and LOGIN for repeater auth")
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
contains('draw_screen();fast_full_redraw("SHORT_BUTTON_REFRESH",true);', "primary-button refresh also wakes frontlight")
contains('last_user_activity=millis();\n            draw_screen();fast_full_redraw("SHORT_BUTTON_REFRESH",true);', "primary-button short press counts as user activity")
contains('fast_full_redraw("CONTACTS_AFTER_BOOT",false);\n        // Startup can take longer than the saved light timeout.', "Contacts boot refresh completes before frontlight timer reset")
contains('timeout only after Contacts is actually visible.\n        frontlight_event();', "Contacts starts a fresh frontlight timeout after splash")
assert "SHORT_BOOT_HOME" not in source, "obsolete BOOT-specific home navigation remains absent"
# Keep the application gesture/event layer independent from the physical touch
# controller. Maps consumes multi-contact frames; other screens consume the
# legacy primary-contact semantics supplied by the selected touch backend.
contains('#include "hardware/touch.h"', "UI includes generic touch hardware boundary")
contains("bool held=false,home_held=false,map_previous=false;", "independent home touch latch")
contains("const bool on_map=screen==Screen::Maps&&!standby_active&&\n            !keyboard_landscape&&!quick_panel_active;", "Maps yields touch sampling to Quick Settings")
contains("MeshInkTouchContacts contacts{};", "Maps consumes board-independent contact frames")
contains("if(!meshink_touch_read_contacts(contacts))", "Maps reads multi-contact touch backend")
contains("const MeshInkTouchPrimarySample sample=meshink_touch_read_primary();", "non-Maps consumes primary touch backend")
contains("const bool owns_screen_edges=r<2;", "number/top/home keyboard rows use full screen-edge ownership")
contains("key_index_edge_extended(", "outer keyboard rows use edge-expanded hit targets")
non_map_sampler = source.split("// Non-Maps keeps the legacy single-contact semantics supplied by the touch", 1)[1].split(
    "vTaskDelay(pdMS_TO_TICKS(8));", 1
)[0]
assert "const bool home=sample.home,pressed=sample.pressed;" in non_map_sampler, "non-Maps preserves primary press/Home semantics"
assert "held=false;\n            }else if(home_held){\n                if(!pressed)home_held=false;" in non_map_sampler, "Home must not become ordinary non-Maps tap release"
assert "int16_t event_x=last_x,event_y=last_y;" in non_map_sampler, "ordinary non-keyboard UI release position remains the default"
assert "const bool keyboard_touch=!quick_panel_active&&" in non_map_sampler, "keyboard-only thumb-roll gate"
assert "constexpr int16_t KEYBOARD_TOUCH_SLOP=28;" in non_map_sampler, "bounded keyboard thumb-roll tolerance"
assert "event_x=start_x;" in non_map_sampler and "event_y=start_y;" in non_map_sampler, "small keyboard releases anchor to touch-down"
assert "QueuedTap tap{event_x,event_y,dx,dy,false};" in non_map_sampler, "stabilized non-Maps release event path"
assert "quick_slider_dragging=quick_panel_active&&" in non_map_sampler, "Quick Settings slider enters live-drag mode"
assert "frontlight_preview(quick_slider_preview);" in non_map_sampler, "Quick Settings slider previews brightness during movement"

# GT911 belongs entirely to the T5 hardware backend. Application/UI code must
# not regain controller registers, address selection, reset/INT pins or cached
# controller state.
for controller_detail in (
    "GT911", "0x814E", "0x814F", "T5_PIN_TOUCH_RST", "T5_PIN_TOUCH_INT",
    "cached_touch_x", "map_last_count", "was_pressed"
):
    assert controller_detail not in source, f"UI leaked touch-controller detail: {controller_detail}"
assert 'MESHINK_TOUCH_BACKEND_HEADER' in touch_selector_source, "touch selector must support a replaceable board backend"
assert 'struct MeshInkTouchPrimarySample' in touch_types_source, "generic primary touch sample type"
assert 'struct MeshInkTouchContacts' in touch_types_source, "generic multi-contact touch type"
for backend_detail in (
    "GT911_ADDR", "GT911_STATUS", "GT911_FIRST_POINT",
    "T5_PIN_TOUCH_RST", "T5_PIN_TOUCH_INT",
    "meshink_touch_prepare_boot", "meshink_touch_finish_boot",
    "meshink_touch_set_power", "meshink_touch_read_primary",
    "meshink_touch_read_contacts"
):
    assert backend_detail in touch_backend_source, f"T5 touch backend missing {backend_detail}"
assert "meshink_touch_prepare_boot();" in source and "meshink_touch_finish_boot();" in source, "UI delegates boot touch sequencing"
assert "meshink_touch_set_power(enabled);" in source, "UI delegates touch power sequencing"
assert "meshink_touch_reset_tracking();" in source, "UI resets backend tracking on sampling-mode changes"
assert "meshink_touch_set_power(false);" in board_target_source, "companion mode delegates touch disable to backend"
assert "GT911" not in board_target_source, "board runtime must not name the touch controller outside its backend"

# Test10 companion-mode power/logging policy: local UI keeps its validated
# frequencies, while BT companion drops to 80 MHz only after radio/BLE/GPS
# initialization. The companion ISR path must choose Arduino ownership before
# probing the deinitialized EPDiy ISR service, and BLE scan response data must
# stay within the legacy 31-byte budget without duplicating the UART UUID.
assert "COMPANION_CPU_MHZ=80" in companion_source, "BT companion steady-state CPU target is 80 MHz"
assert 'companion_set_low_power_cpu();' in companion_source, "BT companion applies low-power CPU policy"
companion_setup_body = companion_source.split("void companion_setup() {",1)[1].split("void companion_loop()",1)[0]
assert companion_setup_body.index("meshink_board_boot_complete();") < companion_setup_body.index("companion_set_low_power_cpu();"), "companion lowers CPU only after hardware/BLE setup"
assert "board.begin();" not in companion_source and "board.beginLocal();" not in companion_source and "board.onBootComplete();" not in companion_source, "generic runtime must use board lifecycle abstraction"
assert "t5_companion_" not in companion_source, "generic runtime must not call T5-specific companion lifecycle hooks"
local_setup_body = companion_source.split("void local_mesh_setup() {",1)[1]
assert "companion_set_low_power_cpu();" not in local_setup_body, "local UI must not inherit companion CPU policy"
assert "companion_radio_uses_arduino_irq=true;" in board_target_source, "companion selects Arduino-owned radio IRQ service"
assert "companion_radio_uses_arduino_irq=false;" in board_target_source, "local UI selects EPDiy-owned radio IRQ service"
attach_body = board_target_source.split("void attachInterrupt(uint32_t interruptNum",1)[1].split("void detachInterrupt",1)[0]
assert attach_body.index("if(companion_radio_uses_arduino_irq)") < attach_body.index("gpio_isr_handler_add"), "companion must bypass missing-service probe before gpio_isr_handler_add"
assert "BLEAdvertisementData scan_response;" in companion_source, "companion supplies bounded custom BLE scan response"
assert "setScanResponseData(scan_response)" in companion_source, "companion overrides overflowing default BLE scan response"
assert "char scan_name[30]" in companion_source, "BLE advertised name is capped to the 29-byte legacy payload name budget"
assert "setShortName(scan_name)" in companion_source and "setName(scan_name)" in companion_source, "BLE scan response marks truncated names as short"
ui_version = re.search(r"-DT5_UI_VERSION='\"([^\"]+)\"'", platformio_source)
firmware_version = re.search(r"-DT5_FIRMWARE_VERSION='\"([^\"]+)\"'", platformio_source)
assert ui_version and firmware_version, "testing UI and firmware versions are explicit in PlatformIO configuration"
assert ui_version.group(1) == firmware_version.group(1), "testing UI and firmware version identifiers must match"
assert re.fullmatch(r"\d+\.\d+\.\d+(?:-test\.\d+)?", firmware_version.group(1)), "firmware version must be a stable semantic version or numbered test build"

# Status-bar refresh policy: clock and battery share one aligned five-minute
# cadence awake or in standby. Event-driven redraws may show newer values early;
# drawing the bar records the current slot so the visible schedule advances
# opportunistically instead of immediately redrawing again.
assert "status_bar_painted_minute" not in source, "minute-by-minute awake status cadence must not return"
contains("static int16_t status_bar_painted_slot = -1;", "status bar tracks the last painted five-minute wall-clock slot")
contains("status_slot>=0&&status_slot!=status_bar_painted_slot", "awake and standby status refreshes share the five-minute slot boundary")
contains("if(aligned_status_due)status_bar_dirty=true;", "scheduled five-minute wall-clock changes queue the status bar")
assert "status_bar_refreshed_at" not in source, "elapsed-time status cadence must not return"
contains("[T5-UI] status-bar clock=%02d:%02d", "status clock logging occurs when the bar is actually drawn")
contains("refresh_area(MeshInkRefreshMode::Direct,", "status bar uses area refresh")
contains("{0,0,portrait_layout().width,portrait_layout().status_height},wake);", "status area is limited to the bar")
contains("static int16_t status_gps_satellites_bar = 0;", "status bar owns a separately throttled satellite count")
contains("now-last_satellite_bar_refresh>=5000UL", "visible satellite count is throttled to five seconds")
contains("status_gps_satellites_bar=satellites;", "throttled satellite display eventually adopts latest live count")
contains('reason=gps-satellites-5s', "throttled satellite refreshes are observable in logs")
contains("!standby_active&&enabled&&has_fix&&", "satellite count does not trigger status refresh while hidden in standby")
contains("if(state_changed&&!standby_active)", "GPS state changes do not independently repaint the status bar in standby")
contains("if(!standby_active&&((screen==Screen::GpsSettings", "GPS detail/state changes do not redraw page content in standby")
assert "standby_quantized" not in source, "standby must keep exact clock and battery values"

# Test16 local wireless power policy. UI/runtime code must use the generic
# boundary, while the T5 backend owns ESP-IDF Wi-Fi/Bluetooth state control.
assert '#include "hardware/wireless.h"' in unified_source, "unified boot includes generic wireless boundary"
assert "esp_wifi" not in unified_source and "esp_bt_" not in unified_source and "esp_bluedroid_" not in unified_source, "unified boot must not own ESP wireless SDK details"
assert 'MESHINK_WIRELESS_BACKEND_HEADER' in wireless_selector_source, "wireless selector supports a replaceable board backend"
assert "struct MeshInkWirelessState" in wireless_types_source, "generic wireless state contract exists"
for backend_detail in (
    "esp_wifi_stop()", "esp_wifi_deinit()", "esp_wifi_get_mode",
    "esp_bluedroid_get_status()", "esp_bluedroid_disable()", "esp_bluedroid_deinit()",
    "esp_bt_controller_get_status()", "esp_bt_controller_disable()", "esp_bt_controller_deinit()"
):
    assert backend_detail in wireless_backend_source, f"T5 wireless backend missing {backend_detail}"
assert 'meshink_wireless_force_local_radios_off()' in unified_source, "local boot forces Wi-Fi and Bluetooth off"
assert unified_source.count('meshink_wireless_force_local_radios_off()') == 2, "local wireless policy is enforced before and after MeshCore startup"
assert 'check_local_wireless_state("local-pre"' in unified_source, "local boot verifies radios before UI startup"
assert 'check_local_wireless_state("local-post-mesh"' in unified_source, "local boot verifies radios after MeshCore startup"
assert 'meshink_wireless_force_wifi_off()' in unified_source, "companion boot explicitly keeps unused Wi-Fi off"
assert 'check_companion_wireless_state("companion-ready"' in unified_source, "companion boot verifies Wi-Fi off and Bluetooth active"
assert 'returning from companion mode always' not in unified_source.lower() or 'reboots through this same path' in unified_source, "companion return documents local re-verification"

# Companion exit must quiesce the active MeshCore runtime before ESP.restart().
# Test10 hardware proved that reinitializing EPDiy in the same companion boot
# aborts in the board/I2C init path, so test11 deliberately uses only the
# full-brightness frontlight as immediate exit feedback.
assert "void companion_prepare_exit()" in companion_source, "companion exposes orderly shutdown path"
shutdown_body = companion_source.split("void companion_prepare_exit() {",1)[1].split("void local_mesh_setup()",1)[0]
for shutdown_step in (
    "interface_manager.disable();",
    "BLEDevice::deinit(false);",
    "the_mesh.savePrefs();",
    "store.saveContacts(&the_mesh,companion_persist_contact);",
    "store.saveChannels(&the_mesh);",
    "meshink_gps_shutdown();",
    "meshink_radio_power_off();",
    "meshink_board_companion_release_resources();",
    "SPIFFS.end();",
):
    assert shutdown_step in shutdown_body, f"companion shutdown missing {shutdown_step}"
assert "const bool ble_connected=bluetooth_interface.isConnected();" in shutdown_body, "shutdown checks BLE connection before disabling transport"
assert shutdown_body.index("if(ble_connected)") < shutdown_body.index("interface_manager.disable();"), "connected BLE path disables interface normally"
assert shutdown_body.index("interface_manager.removeInterface(&bluetooth_interface);") < shutdown_body.index("BLEDevice::deinit(false);"), "disconnected BLE path detaches transport before stack deinit"
assert shutdown_body.index("BLEDevice::deinit(false);") < shutdown_body.index("meshink_radio_power_off();"), "Bluetooth stack stops before radio power-off"
assert shutdown_body.index("store.saveChannels(&the_mesh);") < shutdown_body.index("SPIFFS.end();"), "persist MeshCore state before filesystem shutdown"
assert "companion_prepare_exit();" in unified_source, "primary-button exit calls orderly companion shutdown"
exit_body = unified_source.split("static void companion_exit_button()",1)[1].split("void setup()",1)[0]
assert exit_body.index("companion_prepare_exit();") < exit_body.index("ESP.restart();"), "companion shutdown precedes reboot"
assert "meshink_board_companion_exit_feedback_begin();" in shutdown_body, "accepted primary-button hold gets immediate full-brightness acknowledgement"
exit_feedback_body = board_target_source.split("void meshink_board_companion_exit_feedback_begin() {",1)[1].split("void meshink_board_companion_release_resources()",1)[0]
assert "meshink_power_frontlight_begin();" in exit_feedback_body, "companion exit reasserts frontlight hardware before acknowledgement"
assert "meshink_power_frontlight_set(100);" in exit_feedback_body, "companion exit drives full-brightness acknowledgement"
assert "t5_companion_show_returning_notice" not in companion_source and "t5_companion_show_returning_notice" not in board_target_source, "companion exit must not reinitialize EPDiy before reboot"
assert 'RETURNING TO LOCAL UI' not in board_target_source, "unsafe retained reboot screen remains removed"
release_body = board_target_source.split("void meshink_board_companion_release_resources()",1)[1].split("void T5Board::begin()",1)[0]
for handoff_step in ("radio_hal.detachInterrupt(P_LORA_DIO_1);","radio_spi.end();"):
    assert handoff_step in release_body, f"companion radio cleanup missing {handoff_step}"
assert "gpio_uninstall_isr_service();" not in release_body, "frontlight-only exit must not tear down the global GPIO ISR service"
assert 'notice_meshink_logo(160, fb);' in companion_notice_source, "companion screen uses MeshInk splash artwork"
assert 'notice_centred("BLUETOOTH COMPANION MODE", 565, 3, fb, true);' in companion_notice_source, "companion screen labels Bluetooth mode below logo"
assert 'snprintf(hold_button,sizeof(hold_button),"HOLD %s BUTTON",meshink_primary_button_name());' in companion_notice_source, "companion screen uses board-provided primary-button label"
assert "meshink_show_companion_notice();" in companion_source, "companion runtime owns splash presentation"
assert companion_source.index("meshink_show_companion_notice();") < companion_source.index("meshink_board_begin_companion();"), "companion splash renders before board/MeshCore I2C setup"
assert "BLUETOOTH COMPANION MODE" not in board_target_source and "MESHINK_LOGO_" not in board_target_source, "board package must not contain MeshInk companion presentation"
assert "epd_set_lcd_pixel_clock_MHz(17);" in display_backend_source, "T5 display backend owns field-tested panel clock"
assert "meshink_display_set_pixel_clock_mhz" not in source and "meshink_display_set_pixel_clock_mhz" not in companion_notice_source, "application code must not select hardware display clock"
assert "MeshInkRotation::" not in source and "meshink_display_set_rotation(" not in source, "UI must use logical display orientation, not T5 native rotation"
assert "MeshInkRotation::" not in companion_notice_source and "meshink_display_set_rotation(" not in companion_notice_source, "companion splash must use logical display orientation"
assert "meshink_touch_set_orientation(orientation);" in source, "UI orientation changes update input and display together"
assert "const int16_t x=raw_y" not in source, "landscape keyboard handler must receive logical touch coordinates"
assert "TOUCH_PORTRAIT_WIDTH-1-raw_x" in touch_backend_source, "T5 touch backend owns landscape coordinate transform"
assert 'notice_centred("2 SECONDS TO EXIT", 705, 2, fb);' in companion_notice_source, "companion screen shows exit duration"
assert "{'.',{0,0,0,0,0,6,6}}, {'-',{0,0,0,31,0,0,0}}" in companion_notice_source, "companion tiny font includes firmware-version hyphen"
assert "if(key>='a'&&key<='z')key=(char)(key-'a'+'A');" in companion_notice_source, "companion tiny font renders lowercase firmware-version letters"
assert 'notice_centred("MESHCORE", 290, 7, fb, true);' not in companion_notice_source, "legacy MESHCORE companion splash removed"

contains("if(tap.map_sampled&&screen!=Screen::Maps)continue;", "discard stale Maps gestures after tab switch")
contains("if(touch_queue)xQueueReset(touch_queue);", "home clears previous-page touches")
contains("open_screen(setup_complete?Screen::Contacts:Screen::Welcome);", "home persists logical navigation")
contains("static constexpr size_t LIST_ITEMS_PER_PAGE = 5;", "contacts/channels use paged list rows")
contains("contacts_page*LIST_ITEMS_PER_PAGE", "Contacts taps and rendering address later pages")
contains("channels_page*LIST_ITEMS_PER_PAGE", "Channels taps and rendering address later pages")
contains("(screen==Screen::Contacts||screen==Screen::Channels||screen==Screen::Discovery)&&", "Contacts/Channels/Discovery vertical swipe changes pages")
contains("draw_list_page_footer(contacts_page,count);", "Contacts displays page count when multiple pages exist")
contains("rounded_box(layout.outer_margin,y,layout.outer_width,layout.list_row_height,", "list cards use shared logical interior width with rounded treatment")
contains("row*portrait_layout().list_row_stride", "list drawing/touch use shared row stride")
contains("draw_list_page_footer(channels_page,count);", "Channels displays page count when multiple pages exist")
contains("if(pages<=1)return;", "single-page Contacts/Channels hide the page footer")
contains("if(page>0)draw_page_arrow", "page indicator shows previous-page swipe-down arrow only when available")
contains("if(page+1<pages)draw_page_arrow", "page indicator shows next-page swipe-up arrow only when available")
contains("(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&!keyboard_visible&&abs(tap.dy)>60", "conversation history uses vertical swipe paging")
contains("chat_page_bounds_lazy(count,chat_history_available(),chat_page,first,end,has_older);", "conversation history discovers only the requested fixed-height page")
contains("draw_chat_page_indicator(chat_page,has_older,layout.bottom_nav_top-ui_h(60));", "conversation history page indicator follows scaled geometry without requiring total-page scan")
assert "chat_page_bounds(" not in source, "conversation drawing must not rescan all historical pages to calculate a total"
assert 'text("OLDER"' not in source and 'text("NEWER"' not in source, "conversation paging buttons must stay removed"
contains("static uint8_t node_info_page_count(uint8_t type){return node_has_status(type)?4:3;}", "Node Info page count is role-aware")
contains("node_has_status(uint8_t type){return type==(uint8_t)UiNodeRole::Repeater||type==(uint8_t)UiNodeRole::Room;}", "Status is exposed for repeaters and room servers")
contains("screen==Screen::ContactDetails&&!keyboard_visible&&abs(tap.dy)>60", "Node Info pages use vertical swipe paging")
contains('UiNodeInfoRequest::Status,"REQUEST STATUS"', "Node Info exposes an individual status request")
contains('UiNodeInfoRequest::Telemetry,"REQUEST TELEMETRY"', "Node Info exposes an individual telemetry request")
contains('UiNodeInfoRequest::Path,"DISCOVER PATH"', "Node Info exposes an individual path-discovery request")
contains('UiNodeInfoRequest::Trace,"TRACE ROUTE"', "Node Info exposes an individual trace request")
assert "REQUEST ALL INFO" not in source, "Node Info must not send every remote request at once"
contains("draw_node_role_icon(item.node_type", "Contacts and Discovery show node role icons")
contains("const MeshInkUiRect row=meshink_outer_row_rect(layout,reference_y,112);", "settings rows use shared scalable geometry")
contains("hit_outer_row(", "settings/list touch targets use shared interior geometry")
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
contains("const int status_scale=ui_text_max_line_width(node.status,3)<=layout.section_width?3:2;", "received status text keeps scale 3 when it fits and scale 2 for long counter lines")
contains("ui_wrapped_line_count(node.telemetry,layout.section_width,3)<=4?3:2", "received telemetry uses the largest scale that preserves all four visible lines")
contains("ui_draw_wrapped(node.path,layout.section_margin,ui_y(260),layout.section_width,3,0,true,3);", "received path text is larger, proportional and scaled")
contains("ui_draw_wrapped(node.trace,layout.section_margin,ui_y(400),layout.section_width,2,0,true,9);", "trace results use the available vertical space for up to nine readable lines")
contains('page==NodeInfoPage::Status&&hit(x,y,meshink_node_action_rect(portrait_layout()))', "status action touch follows shared control geometry")
contains('page==NodeInfoPage::Telemetry&&hit(x,y,meshink_node_action_rect(portrait_layout()))', "telemetry action touch follows shared control geometry")
contains('page==NodeInfoPage::Path&&hit(x,y,meshink_node_left_action_rect(portrait_layout()))', "path discovery touch follows shared left-action geometry")
contains('page==NodeInfoPage::Path&&hit(x,y,meshink_node_right_action_rect(portrait_layout()))', "trace touch follows shared right-action geometry")
assert source.count("active_node_saved_password(remote_password,sizeof(remote_password))")>=2, "saved credentials should prefill from both Status and Telemetry login"

contains('meshink_display_fill_rect({0,metrics.clear_top,layout.width,', "password keyboard clear area follows shared geometry")
contains('screen==Screen::ContactDetails&&!(keyboard_visible&&keyboard_password_mode)', "bottom navigation is hidden while password keyboard is open")
contains('ui_text_fit(remote_password[0]?remote_password:"Remote password"', "portrait password entry shows plain text")
contains('const char* value=keyboard_password_mode?remote_password:', "landscape password entry shows plain text")
assert "char masked[16]" not in source, "password entry must not mask typed text on-device"


assert "contact.type==ADV_TYPE_REPEATER||contact.type==ADV_TYPE_ROOM" in runtime_source, "status capability includes repeater and room server"
assert "if(request==UiNodeInfoRequest::Status&&(!protected_server||!detail_authenticated_))return false;" in runtime_source, "status requests require authenticated protected server"
assert "BATTERY %.2f V" in runtime_source and "PACKETS RX/TX" in runtime_source, "server status payload is decoded into readable metrics"
assert "POSTS %u" in runtime_source and "PUSHES %u" in runtime_source, "room server status exposes room-specific counters"

assert "request_active_node_info(UiNodeInfoRequest request)" in data_source, "Node Info provider must accept one typed request"
assert "advance_info" not in runtime_source, "Node Info transport must not auto-chain requests"
assert "pending_info.stage" not in runtime_source, "Node Info transport must not retain staged request-all state"
for request in ("Status", "Telemetry", "Path", "Trace"):
    assert f"pending_info.request==UiNodeInfoRequest::{request}" in runtime_source, f"{request} reply must match only its selected request"
assert "contact_count()&&i<5" not in source, "Contacts must not be hard-limited to the first five entries"
assert "channel_count()&&i<5" not in source, "Channels must not be hard-limited to the first five entries"
contains("text_refresh_pending=false;toast_visible=false;toast_opens_main=false;", "home cancels pending refreshes")
contains("for(int d=-3;d<=3;++d)line(x+2,y+2+d,x+27,y+27+d);", "bold GPS-off slash")
contains("meshink_display_fill_rect({x,y+5,30,3},0,fb);", "bold envelope frame")
contains("for(int d=-1;d<=1;++d) {\n        line(x+3,y+8+d", "bold envelope flap")
for index in range(3):
    contains(f"meshink_map_control_rect(layout,{index})", f"map control {index} draws from shared geometry")
    contains(f"meshink_map_control_rect(portrait_layout(),{index})", f"map control {index} touch uses shared geometry")
assert 'text("ME",' not in source, "locate icon must not display text"
contains("meshink_display_fill_rect({target_x,target_y+ui_h(21),ui_w(45),ui_h(5)},0xFF,fb);", "large white locate crosshair horizontal")
contains("meshink_display_fill_rect({target_x+ui_w(21),target_y,ui_w(5),ui_h(45)},0xFF,fb);", "large white locate crosshair vertical")
contains("draw_target_icon(sx-15,sy-15,false);", "device marker same icon as GPS fix")
contains("if(map_zoom<meshink_map_gestures::MAX_ZOOM)", "Maps plus button uses shared maximum zoom")
contains("if(map_zoom>meshink_map_gestures::MIN_ZOOM)", "Maps minus button reaches shared minimum zoom")
assert "if(map_zoom>8)" not in source, "stale Maps minimum zoom 8 must not return"
contains("if(next==Screen::Maps&&screen!=Screen::Maps&&!preserve_map_centre)", "automatic map recenter")
contains("open_screen(Screen::Maps,true);", "explicit node position preserved")
contains('prefs.getBool("map_fix_saved",false)', "reload last known position")
contains('location_store.putBool("map_fix_saved",true)', "persist verified last known position")
contains("if(enabled&&has_fix&&latitude>=-85051100L", "never replace last fix with disabled/no-fix coordinates")
contains("centre_map_on_device();", "current or stale position recenter")
contains('(current_fix?"CENTRED ON DEVICE":"CENTRED ON LAST FIX")', "stale position explicitly indicated")
contains("meshink_map_gestures::terrain_point(tap.x,tap.y,portrait_layout())", "map pan respects logical terrain viewport")
assert "bottom_nav_top==900" in ui_layout_source, "T5 bottom navigation remains at y=900"
assert "map_centre_y==474" in ui_layout_source, "T5 map centre remains y=474"
contains('draw_toast_message("Loading..");', "Maps keep the previous map visible beneath Loading")
contains('refresh_area(MeshInkRefreshMode::Direct,toast_message_rect("Loading.."));', "Maps pan/zoom Loading toast uses partial-area refresh")
contains('else\n        refresh(MeshInkRefreshMode::Direct);', "first Maps entry retains full Loading refresh")
contains('meshink_display_update_area(', "partial Loading path uses display backend area update API")
contains('[T5-MAP-LOAD] area-refresh=', "partial Loading refresh logs independent timing")
contains('refresh(MeshInkRefreshMode::Direct,false); // intentional transient black prep', "Maps retain dedicated contrast-preserving black-prep refresh")
contains('reveal_map_after_black_prep("MAP_BLACK_PREP_COMPLETE",false);', "Maps reveal final frame after black preparation")
contains("static inline int map_centre_y(){return portrait_layout().map_centre_y;}", "map projection centre derives from logical layout")
contains("result=map_tiles_render(fb,0,map_top(),layout.width,map_bottom()-map_top(),", "map fills logical viewport")
assert 'draw_app_header("MAPS")' not in source, "extra maps header must be removed"
tiles = (Path(__file__).resolve().parents[1] / "src" / "map_tiles.cpp").read_text(encoding="utf-8")
assert "render_clip={x,y,width,height};" in tiles, "map clipping derives from requested viewport"
assert "900" not in tiles and "540" not in tiles, "map renderer must not hard-code T5 display edges"
companion = (Path(__file__).resolve().parents[1] / "src" / "companion_runtime.cpp").read_text(encoding="utf-8")
assert 'initial_gps.getBool("complete",false)' in companion, "existing GPS settings must be preserved"
assert 'initial_gps.getBool("gps_default_v1",false)' in companion, "GPS defaults must only apply once"
assert "settings->gps_enabled=1;" in companion, "new setup GPS must default ON"
assert "settings->gps_interval=0;" in companion, "new setup GPS must default continuous"
assert 'initial_gps.putBool("gps_default_v1",true);' in companion, "GPS default marker missing"
# Portrait keyboard ergonomics: message entry uses a wide space bar with no
# adjacent HIDE key; Radio Settings name entry has a wide SAVE action and
# dismisses by tapping above the keyboard instead.
# Keyboard drawing, touch and message entry now share one scalable geometry
# source. The T5 reference remains exact while board profiles can tune offsets.
contains("static meshink_keyboard::Metrics keyboard_metrics(bool landscape)", "UI builds keyboard metrics from the active display")
contains("meshink_keyboard::make_metrics(", "UI uses shared scalable keyboard geometry")
contains("MESHINK_KEYBOARD_PORTRAIT_X_OFFSET", "board profile exposes portrait keyboard tuning")
contains("MESHINK_KEYBOARD_LANDSCAPE_X_OFFSET", "board profile exposes landscape keyboard tuning")
contains("draw_compose_entry(keyboard_layout);", "chat compose box follows keyboard entry geometry")
assert "static char compose_text[MESHINK_MESSAGE_TEXT_BYTES]" in source, "compose buffer accepts the full 160-byte message"
assert "if(n<MESHINK_MESSAGE_TEXT_MAX)" in source, "portrait and landscape typing share the full message limit"
assert "static void ui_draw_wrapped_tail(" in source, "compose rendering follows the newest wrapped lines"
assert source.count("ui_draw_wrapped_tail(")>=3, "portrait and landscape entry rendering both use bounded tail wrapping"
assert "landscape?198:618" in (root / "src" / "keyboard_geometry.h").read_text(encoding="utf-8"), "landscape keyboard is shifted to the bottom edge"
assert "Rect{16,14,928,165}" in (root / "src" / "keyboard_geometry.h").read_text(encoding="utf-8"), "landscape compose viewport uses the reclaimed white space"

assert "meshink_keyboard::in_row(y,828)" not in source, "portrait third-row touch must not use fixed T5 y coordinates"
assert "meshink_keyboard::in_row(y,425)" not in source, "landscape action-row touch must not use fixed T5 y coordinates"
contains('key("SPACE",metrics.space_key);', "space-capable keyboards use the shared wide space key")
contains('key(keyboard_password_mode?"LOGIN":"SEND",metrics.action_key);', "message/password action remains isolated at far right")
contains('if(y<metrics.dismiss_above){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}', "message keyboard dismiss boundary follows shared geometry")
assert source.count("x<meshink_keyboard::action_split(metrics)")>=3, "portrait/landscape SPACE boundaries come from shared geometry"
contains('key("SAVE",metrics.wide_action_key);', "name entry uses the shared wide SAVE action instead of a dead space bar")
contains('if(screen==Screen::RadioSettings&&y<metrics.dismiss_above){text_refresh_pending=false;keyboard_visible=false;draw_screen();refresh(MeshInkRefreshMode::Direct);return true;}', "Radio Settings dismiss boundary follows shared geometry")
assert 'key("HIDE",318,898,100);' not in source, "portrait HIDE key must be removed everywhere"

# 1.8.4 interaction-latency fixes and sentence-style message keyboard.
contains("static bool message_keyboard_case_dirty = false;", "message keyboard tracks one-time case redraw")
contains("meshink_keyboard::key_index_edge_extended(\n            meshink_keyboard::numbers(metrics),x,metrics.width)", "number row owns left/right screen-edge margins")
contains("const bool owns_screen_edges=r<2;", "top and home keyboard rows own left/right screen-edge margins")
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
contains("draw_screen();refresh(MeshInkRefreshMode::Direct);return true;", "same-page keyboard transitions use DU")

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
contains('set_cpu_target(previous_mhz,"ui-draw-complete");', "UI draw boost restores previous CPU clock")
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
assert "meshink_display_blit_gray4_dithered(target,blit);" in map_source, "Maps delegates packed framebuffer composition to display backend"
assert "physical_width" not in map_source and "physical_height" not in map_source, "Maps must not know physical panel geometry"
assert "EPD_ROT_" not in map_source and "Epd" not in map_source, "Maps must not depend on EPDiy types"
assert "const int physical_y=physical_height-px-1;" in display_backend_source, "T5 backend preserves inverted-portrait direct framebuffer fast path"
assert "out_row[(unsigned)py>>1]=(uint8_t)(low|high);" in display_backend_source, "T5 backend preserves packed 4-bit map composition"
assert "MeshInkRotation::InvertedPortrait" in display_backend_source, "T5 direct map path remains rotation guarded"
assert "struct MeshInkDisplayGeometry" in display_types_source, "display geometry is board-independent application vocabulary"
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
assert "delay(1);" in companion, "Bluetooth companion loop must yield so cache64 watchdog does not starve IDLE1"
assert "gpio_isr_handler_add(" in board_source, "radio attaches DIO handler to existing IDF ISR service"
assert "ArduinoHal::attachInterrupt" in board_source, "radio HAL retains companion-mode Arduino interrupt fallback"
assert "constexpr uint32_t LEARN_MS=5000;" in timing_source, "timing diagnostics use 5 second warm-up"

print("PASS: UI behaviour, full-height map, monochrome controls and first-setup continuous GPS defaults")
print("PASS: 10 UI issue checks (icon strokes, controls, Home/primary button, last GPS, brightness)")

compat_source = (root / "src" / "cache64_compat.cpp").read_text(encoding="utf-8")
assert ".global s3_rgb565" in compat_source and "ee.vld.128.ip" in compat_source, "cache64 uses PNGdec ESP32-S3 SIMD RGB565 assembly"
assert "alignas(16) static uint16_t pixels[TILE_SIZE];" in map_source, "SIMD RGB565 destination row is 16-byte aligned"
assert "alignas(16) PNG png;" in map_source, "PNG decoder object is 16-byte aligned for zero-copy SIMD source rows"
assert "alignas(16) static uint8_t simd_source[TILE_SIZE*4];" in map_source, "misaligned PNG RGBA rows have an aligned SIMD staging buffer"
assert "source_mod!=0U" in map_source and "[T5-PNG-SIMD] src-mod16=%u dst-mod16=%u staged=%u" in map_source, "cache64 logs and stages misaligned SIMD input rows"
assert "if(zoom<=12)draw_cached_epdiy(*tile,draw);" not in map_source, "failed low-zoom compositor A/B removed"
assert "decode_bits&&row->iPixelType==PNG_PIXEL_TRUECOLOR_ALPHA" in map_source, "cached RGBA tiles bypass intermediate RGB565 conversion"
assert "[T5-PNG-GRAY] direct-rgba=1 src-mod16=%u" in map_source, "direct grayscale path reports activation"

# Test19 Maps wake must use the same black-prep/full-DU reveal as normal Maps entry.
contains('static void reveal_map_after_black_prep(const char* reason,bool wake_light=false)', "shared map black-prep reveal helper")
contains('reveal_map_after_black_prep("MAP_BLACK_PREP_COMPLETE",false);', "normal Maps load uses shared black-prep reveal")
contains('reveal_map_after_black_prep("LEAVE_STANDBY_BLACK_PREP_COMPLETE",true);', "Maps wake uses black-prep reveal")
contains('if(screen==Screen::Maps&&!standby_active&&enabled&&has_fix&&', "Maps GPS marker redraw is suppressed while standby owns display")
contains('T5_DEBUGLN(T5_LOG_UI,"[T5-EPD] Maps wake uses black-prep reveal");', "Maps wake black-prep is observable in field logs")

# Boot splash map storage warmup: shallow inventory only, never recursively crawl XYZ tiles.
assert "map_tiles_warm_storage(); // hide SD/map inventory work behind splash" in unified_source, "map storage warms before interactive UI"
assert "for(unsigned zoom=0;zoom<25U;++zoom)zoom_folder_known[zoom]=true;" in map_source, "boot /maps scan records loose zoom folders"
assert "pmtiles_warm_archive(archive_paths[0])" in map_source, "first PMTiles archive root/FAT metadata warms during splash"
assert "bool pmtiles_warm_archive(const char* path)" in pmtiles_source, "PMTiles reader exposes retryable warmup"

# Standby charger changes must not wait for the slower standby status poll.
# They join the same centralized status-bar partial-refresh path and therefore
# refresh the exact clock/battery values at the same time.
contains("if(standby_active&&millis()-last_standby_charge_poll>=1000)", "standby charging icon refreshes promptly")
charger_poll = source.split("static uint32_t last_standby_charge_poll=0;", 1)[1].split("static uint32_t last_status_poll=0;", 1)[0]
assert "status_bar_dirty=true;" in charger_poll, "standby charging queues a status-bar partial refresh"
assert "refresh_area(" not in charger_poll, "standby charger polling must use the shared status refresh path"
contains("{0,0,portrait_layout().width,portrait_layout().status_height},wake);", "shared status refresh follows logical status bar")
standby_entry = source.split("static void enter_standby(const char* reason){", 1)[1].split("static void leave_standby(){", 1)[0]
assert "update_status_hardware();" in standby_entry, "standby entry samples exact clock, battery and charger state"
assert "wall-clock :00/:05/:10... boundaries" in standby_entry, "standby entry documents aligned five-minute status cadence"
contains('"HOLD %s FOR TWO SECONDS TO WAKE"', "standby wake wording includes FOR and explicit two-second hold")

# Test17 power abstraction: application/UI owns presentation only. Battery
# topology, chemistry, charger encoding and critical-battery policy are backend-owned.
assert '#include "hardware/power.h"' in source, "UI includes generic power boundary"
assert 'MESHINK_POWER_BACKEND_HEADER' in power_selector_source, "power selector supports a replaceable board backend"
assert "struct MeshInkPowerStatus" in power_types_source, "generic power status vocabulary exists"
assert "struct MeshInkPowerCriticalState" in power_types_source, "generic critical-battery result exists"
assert "enum class MeshInkChargeState" in power_types_source, "generic charging state replaces PMIC numeric encoding"
for leaked_power_detail in (
    "BQ27220", "BQ25896", "BATFET_DIS", "i2c_master_",
    "ledcWrite(", "ledcSetup(", "T5_PIN_FRONTLIGHT", "esp_deep_sleep_start",
    "CRITICAL_BATTERY_MV", "CRITICAL_BATTERY_SAMPLES", "CRITICAL_BATTERY_POLL_MS",
    "status_charge_state==1", "status_charge_state==2"
):
    assert leaked_power_detail not in source, f"UI leaked power hardware/policy detail: {leaked_power_detail}"
for backend_detail in (
    "BQ27220_ADDR", "BQ25896_PRIMARY_ADDR", "BATFET_DIS",
    "T5_CRITICAL_BATTERY_MV", "T5_CRITICAL_POLL_MS", "T5_CRITICAL_SAMPLES",
    "meshink_power_read_battery_mv", "meshink_power_read_battery_percent",
    "meshink_power_read_charge_state", "meshink_power_external_present",
    "meshink_power_boot_critical", "meshink_power_poll_critical",
    "meshink_power_frontlight_begin", "meshink_power_frontlight_set",
    "meshink_power_enter_ship_mode"
):
    assert backend_detail in power_backend_source, f"T5 power backend missing {backend_detail}"
assert "meshink_power_read_battery_mv(voltage)" in board_source, "MeshCore battery voltage uses shared power backend"
assert "meshink_power_read_battery_percent(soc)" not in board_source, "MeshCore battery cache avoids the removed diagnostic-only SOC read"
assert "meshink_power_recover_boot_path();" in source, "UI delegates boot battery-path recovery"
contains("MeshInkPowerCriticalState boot_power{};", "boot critical check uses generic power result")
contains("meshink_power_boot_critical(boot_power)", "boot critical decision belongs to backend")
contains("meshink_power_poll_critical(critical)", "runtime critical decision belongs to backend")
contains('centred("LOW BATTERY",ui_y(230),6,0,true);', "critical low battery persistent screen follows scaled geometry")
contains("meshink_power_enter_ship_mode(MeshInkPowerOffReason::LowBattery);", "critical battery delegates ship mode to backend")
contains("meshink_power_enter_ship_mode(MeshInkPowerOffReason::User);", "user power-off delegates ship mode to backend")
contains("service_critical_battery();", "runtime critical battery monitor remains active")
assert "meshink_power_external_present()" not in source, "UI must not derive cutoff from external-power/voltage state"
assert "meshink_power_read_battery_mv(" not in source, "UI must not derive local battery policy from voltage"
assert "0x2C" not in source and "0x08" not in source, "UI must not know fuel-gauge SOC/voltage registers"

# Map zoom/source label backing should hug the rendered text rather than
# leaving a wide opaque block over the terrain.
contains("const int zoom_label_width=ui_text_width(zoom,2)+ui_w(8);", "zoom label backing tracks proportional rendered text width")
contains("meshink_display_fill_rect({ui_x(18),ui_y(812),zoom_label_width,ui_h(30)},0xFF,fb);", "zoom label uses scaled dynamic white backing")
assert "meshink_display_fill_rect({18,812,260,30},0xFF,fb);" not in source, "fixed-width zoom backing must not return"

# Pre-hardware audit: external frames are bounded, and the field build
# carries narrowly scoped geometry/touch observability.
assert "DISCOVERED_CONTACT_BASE_LEN" in runtime_source, "discovered advert parser must define a complete base frame length"
assert 'len<DISCOVERED_CONTACT_BASE_LEN' in runtime_source, "truncated discovered adverts must be rejected"
assert '*slot=DiscoveredContact{};' in runtime_source, "discovered advert cache must clear stale optional bytes"
assert 'memset(&detail_contact_,0,sizeof(detail_contact_));' in runtime_source, "Node Info advert parse must start from zeroed contact state"
assert "[T5-MESH] rejected malformed new-advert frame" in runtime_source, "malformed advert rejection must remain observable"
contains("static void audit_ui_geometry()", "test8 boot-time geometry self-audit")
contains("[T5-GEOM] version=%s board=%s logical=%dx%d physical=%dx%d", "geometry audit emits versioned board/display summary")
contains("[T5-TOUCH] tap screen=%s x=%d y=%d", "touch diagnostics identify screen and coordinates")
assert "-DMESHINK_GEOMETRY_DIAGNOSTICS=1" not in cache64_build_flags, "release cache64 build must not force geometry serial diagnostics"
assert "-DMESHINK_TOUCH_DIAGNOSTICS=1" not in cache64_build_flags, "release cache64 build must not force touch serial diagnostics"
assert "-DT5_LOG_UI=1" not in cache64_build_flags, "release cache64 build must not force UI serial diagnostics"
assert 'if(!keyboard_visible&&!keyboard_landscape)' in source and '[T5-TOUCH] tap screen=%s x=%d y=%d quick=%u' in source, "test9 non-keyboard touch logging must remain consumer-side"
assert "-DT5_LOG_MAP=1" not in cache64_build_flags, "test9 must not enable high-volume map diagnostics in cache64 build"

# Test18 remaining non-storage hardware boundaries.
assert '#include "hardware/buttons.h"' in source, "UI includes generic button boundary"
assert '#include "hardware/buttons.h"' in unified_source, "unified companion path includes generic button boundary"
assert 'MESHINK_BUTTONS_BACKEND_HEADER' in buttons_selector_source, "button selector supports replaceable board backend"
for button_api in ("meshink_buttons_begin", "meshink_primary_button_pressed", "meshink_primary_button_name"):
    assert button_api in buttons_backend_header, f"button backend header missing {button_api}"
assert "T5_PIN_BOOT_BUTTON" in buttons_backend_source, "T5 button pin remains board-backend-owned"
for leaked_button_detail in ("T5_PIN_BOOT_BUTTON", "BOOT_BUTTON", "digitalRead(", "pinMode(BOOT"):
    assert leaked_button_detail not in source, f"UI leaked physical button detail: {leaked_button_detail}"
    assert leaked_button_detail not in unified_source, f"unified runtime leaked physical button detail: {leaked_button_detail}"
assert "meshink_primary_button_pressed()" in source, "local UI reads generic primary button"
assert "meshink_primary_button_pressed()" in unified_source, "companion exit reads generic primary button"
assert "meshink_buttons_begin();" in source and "meshink_buttons_begin();" in unified_source, "button backend initialized in local and unified paths"
for hardcoded_wake_text in ("PRESS PWR", "HOLD BOOT", "ON USB: HOLD BOOT"):
    assert hardcoded_wake_text not in source, f"UI hard-coded board wake guidance: {hardcoded_wake_text}"
assert "meshink_power_wake_info()" in source, "shutdown screens use board-owned wake guidance"
assert "T5_WAKE_INFO" in power_backend_source and "PRESS PWR BUTTON" in power_backend_source and "HOLD BOOT TO WAKE" in power_backend_source, "T5 backend owns physical wake instructions"
assert "T5_PIN_FRONTLIGHT" not in board_target_source, "board runtime no longer drives frontlight pin directly"
assert "meshink_power_frontlight_begin();" in board_target_source and "meshink_power_frontlight_set(100);" in board_target_source, "companion frontlight uses power backend"
for moved_power_impl in ("BQ27220_ADDR", "BQ25896", "T5_FACTORY_GAUGE_PROFILE", "gauge_apply_factory_profile_if_needed"):
    assert moved_power_impl not in board_target_source, f"target.cpp still owns power implementation: {moved_power_impl}"
    assert moved_power_impl in power_backend_source, f"T5 power backend missing consolidated implementation: {moved_power_impl}"
assert "meshink_power_prepare_board();" in board_target_source, "board startup delegates gauge/profile preparation"

# Test30 removable-storage boundary. Maps/PMTiles own archive semantics only;
# the selected board backend owns SD wiring, shared SPI and the tuned bus clock.
assert "MESHINK_STORAGE_BACKEND_HEADER" in storage_selector_source, "storage backend is compile-time selectable"
for storage_api in ("meshink_storage_begin", "meshink_storage_end", "meshink_storage_open", "meshink_storage_exists", "meshink_storage_bus_hz"):
    assert storage_api in storage_backend_header, f"storage backend header missing {storage_api}"
assert "T5_PIN_SD_CS" in storage_backend_source, "T5 SD chip select remains storage-backend-owned"
assert "t5_shared_spi()" in storage_backend_source, "T5 shared SPI selection remains storage-backend-owned"
assert "T5_STORAGE_SPI_HZ=25000000" in storage_backend_source, "field-tested 25 MHz SD access speed is preserved"
assert "SD.begin(T5_PIN_SD_CS,t5_shared_spi(),T5_STORAGE_SPI_HZ)" in storage_backend_source, "T5 storage backend binds CS, shared SPI and tuned clock"
assert '#include "hardware/storage.h"' in map_source, "Maps consumes generic storage boundary"
assert '#include "hardware/storage.h"' in pmtiles_header, "PMTiles consumes generic storage boundary"
for leaked_storage_detail in ('#include <SD.h>', '#include "board/target.h"', "T5_PIN_SD_CS", "t5_shared_spi()", "MAP_SD_SPI_HZ", "SD.begin(", "SD.open(", "SD.exists(", "SD.end("):
    assert leaked_storage_detail not in map_source, f"Maps leaked SD hardware detail: {leaked_storage_detail}"
    assert leaked_storage_detail not in pmtiles_source, f"PMTiles leaked SD hardware detail: {leaked_storage_detail}"
assert "meshink_storage_begin()" in map_source and "meshink_storage_bus_hz()" in map_source, "Maps mounts and reports storage through generic service"
assert "meshink_storage_open(" in map_source and "meshink_storage_open(" in pmtiles_source, "map readers open files through generic storage service"

# Test31 deterministic H752-01 radio startup. The SD card is deliberately
# deselected during radio power-up/recovery, but its normal 25 MHz access path
# above must remain unchanged.
assert "pinMode(T5_PIN_LORA_CS,OUTPUT);digitalWrite(T5_PIN_LORA_CS,HIGH);" in board_target_source, "radio startup deselects LoRa before shared-rail power"
assert "pinMode(T5_PIN_SD_CS,OUTPUT);digitalWrite(T5_PIN_SD_CS,HIGH);" in board_target_source, "radio startup deselects SD before shared-rail power"
assert "return t5_set_radio_gps_rail(true,1500);" in board_target_source, "H752-01 shared rail gets LilyGO-style 1500 ms startup settling"
assert "t5_set_radio_gps_rail(false,250)" in board_target_source and "t5_set_radio_gps_rail(true,1500)" in board_target_source, "final radio recovery power-cycles the shared rail before SD mount"
assert "recovery=spi-reset+sx1262-reset" in board_target_source, "second radio attempt performs clean SPI and SX1262 reset"
assert "for(uint8_t attempt=1;attempt<=3&&!ready;++attempt)" in board_target_source, "T5 radio backend owns escalating three-attempt recovery"
assert "for(uint8_t attempt=1;attempt<=3&&!radio_ready;++attempt)" not in companion_source, "generic runtime must not duplicate board-specific radio retries"
assert '-DSX126X_DIO3_TCXO_VOLTAGE=' not in platformio_source, "T5 build must not override RadioLib begin-stage TCXO voltage"
assert '-DSX126X_DIO2_AS_RF_SWITCH=' not in platformio_source, "T5 build must defer DIO2 RF-switch setup to LilyGO post-init order"
assert "T5_STORAGE_SPI_HZ=25000000" in storage_backend_source, "test31 must not regress field-tested SD speed"

# Test32 overlaps H752-01 rail settling with splash preparation instead of
# paying a fresh 1500 ms delay after the splash is already visible.
assert "meshink_board_start_local_radio_settle" in board_backend_source, "board backend exposes generic early-settle hook"
contains("meshink_display_init();\n    // EPDiy has now established the shared board/I2C environment.", "early radio power begins immediately after display board init")
contains("meshink_board_start_local_radio_settle();\n    set_ui_orientation", "UI starts rail before framebuffer/preferences/splash work")
assert "radio_gps_rail_started_at" in board_target_source, "T5 backend timestamps early rail assertion"
assert "REQUIRED_SETTLE_MS=1500" in board_target_source, "manufacturer-style total settle remains 1500 ms"
assert "remaining=elapsed<REQUIRED_SETTLE_MS?REQUIRED_SETTLE_MS-elapsed:0" in board_target_source, "local handoff waits only the unconsumed settle remainder"
assert "t5_wait_local_radio_settle();" in board_target_source, "local board handoff consumes early settle state"
assert "t5_radio_shared_bus_idle(true);\n    enableRadioGpsRail();" not in board_target_source.split("void T5Board::beginLocal()",1)[1].split("bool radio_init()",1)[0], "local handoff must not restart a full post-splash rail delay"
assert "T5_STORAGE_SPI_HZ=25000000" in storage_backend_source, "test32 overlap must not change SD access speed"
assert "void meshink_board_start_local_radio_settle() {}" in standalone_source, "UI-only target keeps a no-op early-radio hook"

# Test33 follows LILYGO H752-01 radio electrical initialization while retaining
# MeshCore's protocol-level LoRa settings.
assert "constexpr float LILYGO_TCXO_VOLTAGE=2.4f;" in board_target_source, "T5 backend uses LilyGO's 2.4 V post-init TCXO setting"
assert "radio.setTCXO(LILYGO_TCXO_VOLTAGE)" in board_target_source, "T5 backend explicitly applies LilyGO TCXO voltage"
assert "radio.setDio2AsRfSwitch(true)" in board_target_source, "T5 backend explicitly enables LilyGO DIO2 RF switch"
tcxo_at=board_target_source.index("radio.setTCXO(LILYGO_TCXO_VOLTAGE)")
rf_switch_at=board_target_source.index("radio.setDio2AsRfSwitch(true)")
std_init_at=board_target_source.index("ready=radio.std_init(&radio_spi)")
assert std_init_at < tcxo_at < rf_switch_at, "radio hardware order must be begin -> TCXO 2.4 V -> DIO2 RF switch"
assert "post-init TCXO=%.1fV result=%d" in board_target_source, "TCXO post-init result remains available to targeted diagnostics"
assert "post-init DIO2 RF-switch result=%d" in board_target_source, "RF-switch post-init result remains available to targeted diagnostics"
assert "[T5-ERROR] SX1262 TCXO 2.4V setup failed code=%d" in board_target_source, "release serial reports TCXO setup failures"
assert "[T5-ERROR] SX1262 DIO2 RF-switch setup failed code=%d" in board_target_source, "release serial reports DIO2 RF-switch setup failures"
assert '-DSX126X_DIO3_TCXO_VOLTAGE=' not in platformio_source, "RadioLib begin stage must use its default TCXO drive"
assert '-DSX126X_DIO2_AS_RF_SWITCH=' not in platformio_source, "DIO2 setup must not happen inside std_init before TCXO 2.4 V"
assert "T5_STORAGE_SPI_HZ=25000000" in storage_backend_source, "test33 LilyGO radio alignment must not change SD access speed"

# Test21 radio and board-capability boundaries.
assert "MESHINK_RADIO_BACKEND_HEADER" in radio_selector_source, "radio backend is compile-time selectable"
assert "enum class MeshInkRadioFailureClass" in radio_types_source, "radio failure vocabulary is board independent"
for api in (
    "meshink_radio_meshcore", "meshink_radio_initialize", "meshink_radio_rng_seed",
    "meshink_radio_apply_params", "meshink_radio_power_off", "meshink_radio_stats",
    "meshink_radio_classify_failure", "meshink_radio_name"
):
    assert api in radio_backend_header, f"radio backend header missing {api}"
assert "radio_driver" in radio_backend_source, "T5 concrete radio remains backend-owned"
assert "meshink_radio_meshcore()" in companion_source, "MeshCore composition consumes generic mesh::Radio"
assert "meshink_radio_initialize()" in companion_source, "companion/local startup uses radio backend"
assert "meshink_radio_rng_seed()" in companion_source, "runtime RNG seeding uses radio backend"
assert "meshink_radio_apply_params(" in runtime_source, "radio preset application uses backend"
assert "meshink_radio_stats()" not in runtime_source, "release runtime omits periodic radio health logging"
assert "meshink_radio_power_off()" in runtime_source and "meshink_radio_power_off()" in companion_source, "radio shutdown uses backend"
for leaked_radio in ("radio_driver", "CustomSX1262Wrapper", "t5_classify_radio_failure", "T5RadioFailureClass"):
    assert leaked_radio not in runtime_source, f"local runtime leaked T5 radio detail: {leaked_radio}"
    assert leaked_radio not in companion_source, f"companion runtime leaked T5 radio detail: {leaked_radio}"
assert "MESHINK_BOARD_BACKEND_HEADER" in board_selector_source, "board capability backend is compile-time selectable"
assert "meshink_board_name()" in source and "meshink_board_has_gps()" in source, "UI consumes generic board capabilities"
assert "T5_BOARD_LABEL" in board_backend_source and "T5_HAS_GPS" in board_backend_source, "T5 capability constants remain board-backend-owned"
for leaked_board in ('#include "board/board_profile.h"', "T5_UI_HAS_GPS", "T5_BOARD_LABEL"):
    assert leaked_board not in source, f"UI leaked T5 board capability detail: {leaked_board}"
assert "ED047TC1" not in source, "generic UI logging must not name the T5 panel"
assert "SX1262 NOT DETECTED" not in source and "T5 PRO LITE" not in source, "radio failure UI must not hard-code T5 radio/variant names"
assert '"LORA RADIO NOT DETECTED"' in source and 'ui_centred_fit("LORA RADIO NOT DETECTED"' in source, "radio failure UI uses generic LoRa wording with bounded width"

# Hardware-portability display boundary.
assert '#include "hardware/display.h"' in source, "UI must include generic display surface"
assert "EpdiyHighlevelState" not in source and "EpdDrawMode" not in source and "EpdRect" not in source, "UI must not depend on EPDiy display types"
assert "EPD_ROT_" not in source, "UI must use board-independent rotation vocabulary"
assert "meshink_display_invalidate_previous(&display);" in source, "backend owns previous-frame invalidation"
assert "meshink_display_fill_framebuffer(&display,0x00);" in source, "backend owns physical framebuffer fill"
assert "MESHINK_DISPLAY_BACKEND_HEADER" in (root / "src" / "hardware" / "display.h").read_text(encoding="utf-8"), "display backend is compile-time selectable"

# Hardware-portability GPS boundary.
gps_header_source = (root / "src" / "hardware" / "gps.h").read_text(encoding="utf-8")
gps_types_source = (root / "src" / "hardware" / "gps_types.h").read_text(encoding="utf-8")
t5_gps_backend_source = (root / "src" / "board" / "t5_gps_backend.h").read_text(encoding="utf-8")
assert "MESHINK_GPS_BACKEND_HEADER" in gps_header_source, "GPS backend is compile-time selectable"
assert "MeshInkGpsConstellationMode" in gps_types_source, "GPS tuning vocabulary is board independent"
assert '#include "hardware/gps.h"' in runtime_source, "local runtime must use generic GPS surface"
assert '#include "hardware/gps.h"' in companion_source, "companion runtime must use generic GPS surface"
assert "meshink_gps_background_tick();" in runtime_source, "GPS background servicing routes through generic backend"
assert "meshink_gps_shutdown();" in runtime_source and "meshink_gps_shutdown();" in companion_source, "GPS shutdown routes through generic backend"
assert "Serial1" not in runtime_source and "Serial1" not in companion_source and "Serial1" not in source, "application code must not own the GPS UART"
assert "t5_gps_" not in runtime_source and "t5_gps_" not in companion_source and "t5_gps_" not in source, "application code must not call T5-specific GPS APIs"
assert "L76K" not in runtime_source and "L76K" not in source, "receiver model details must stay in the board GPS implementation"
assert "PCAS03" not in runtime_source and "PCAS04" not in runtime_source and "PCAS03" not in source and "PCAS04" not in source, "receiver command syntax must stay in the board GPS implementation"
assert "meshink_gps_next_constellation_mode(mode)" in source, "UI uses board-independent GPS constellation cycle"
assert "local_mesh_gps_tuning_note()" in source, "GPS board-specific explanatory text comes from backend"
assert "meshink_gps_tuning_note" in t5_gps_backend_source, "T5 GPS backend exposes its board-specific tuning note"

# Logical UI geometry boundary preserves the field-tested T5 layout while
# scaling both axes for other display dimensions.
assert "meshink_make_ui_layout" in ui_layout_source, "logical layout factory missing"
assert "meshink_make_ui_layout(540,960)" in ui_layout_source, "T5 layout regression reference missing"
assert "meshink_make_ui_layout(480,800)" in (root / "tests" / "test_map_gestures.cpp").read_text(encoding="utf-8"), "compact logical layout host test missing"
assert "meshink_ui_ref_x" in ui_layout_source and "meshink_ui_ref_y" in ui_layout_source, "two-axis reference scaling helpers missing"
assert "meshink_ui_ref_rect" in ui_layout_source, "shared reference rectangle helper missing"
assert "box(i*135,900,135,60" not in source, "bottom navigation must not hard-code T5 screen edge"

# Except for the keyboard's deliberate extended hit ownership, visual controls
# and touch targets must consume the same named geometry.
for helper in (
    "meshink_welcome_name_rect", "meshink_welcome_preset_rect",
    "meshink_confirm_left_rect", "meshink_preset_row_rect",
    "meshink_node_action_rect", "meshink_node_map_rect",
    "meshink_map_control_rect", "meshink_quick_slider_track_rect",
    "meshink_quick_minus_rect", "meshink_quick_plus_rect",
    "meshink_quick_advert_rect", "meshink_quick_power_rect",
    "meshink_display_slider_track_rect", "meshink_shutdown_rect",
    "meshink_night_start_rect", "meshink_night_end_rect",
    "meshink_night_minus_rect", "meshink_night_plus_rect",
    "meshink_night_save_rect",
):
    assert helper in ui_layout_source, f"{helper} shared geometry helper missing"
assert "QUICK_SLIDER_LEFT" not in source and "QUICK_SLIDER_RIGHT" not in source, "Quick Settings must not keep duplicate fixed slider geometry"
assert "QUICK_PANEL_BOTTOM" not in source, "Quick Settings panel edge must derive from layout"
assert not re.search(r"\bhit\(x,y,\s*\d+\s*,\s*\d+\s*,\s*\d+\s*,\s*\d+", source), "non-keyboard touch target still hard-codes a screen rectangle"

# Screen placement calls must use layout/reference geometry. Local icon and
# map-marker primitives remain free to use offsets relative to their anchor.
assert not re.search(r"\bbox\(\s*\d+\s*,\s*\d+", source), "screen box still hard-codes an absolute position"
assert not re.search(r"\bcentred\([^,]+,\s*\d+", source), "centred text still hard-codes an absolute Y position"
assert not re.search(r"\btext\([^,]+,\s*\d+\s*,\s*\d+", source), "text still hard-codes an absolute screen position"
assert not re.search(r"\bdraw_wrapped\([^,]+,\s*\d+\s*,\s*\d+", source), "wrapped text still hard-codes an absolute screen position"

raw_absolute_rects = list(re.finditer(
    r"meshink_display_(?:fill_rect|draw_rect)\(\{\s*(-?\d+)\s*,\s*(-?\d+)", source))
assert all(m.group(1) == "0" and m.group(2) == "0" for m in raw_absolute_rects),     "raw display rectangle still hard-codes a non-origin screen position"
assert not re.search(r"\bline\(\s*-?\d+\s*,\s*-?\d+", source),     "line primitive still hard-codes an absolute screen position"
assert not re.search(r"\b(?:draw_target_icon|draw_search_icon|draw_envelope_icon|draw_battery_icon)\(\s*-?\d+\s*,\s*-?\d+", source),     "icon primitive still hard-codes an absolute screen position"

# Shared interior layout keeps T5 drawing and touch targets aligned while
# allowing future logical widths to generate different card/header dimensions.
assert "outer_width==516" in ui_layout_source, "T5 outer card width regression guard missing"
assert "section_width==492" in ui_layout_source, "T5 section width regression guard missing"
assert "header_action_x==470" in ui_layout_source, "T5 header action position regression guard missing"
assert "list_row_height==142" in ui_layout_source and "list_row_stride==150" in ui_layout_source, "T5 list geometry regression guards missing"
assert "box(12,y,516,142)" not in source, "list drawing must not hard-code T5 card width"
assert "box(12,y,516,112)" not in source, "settings drawing must not hard-code T5 card width"
assert "hit(x,y,12,130,516,112)" not in source, "settings touch must not hard-code T5 card width"
assert "box(470,58,58,48" not in source, "header action must derive from logical width"

# UI layout access must not copy the large geometry struct onto loopTask stack.
contains("static inline const MeshInkUiLayout& portrait_layout()", "logical layout is cached and returned by reference")
assert "const MeshInkUiLayout layout=portrait_layout();" not in source, "UI must not copy the full layout struct onto loopTask stack"
assert "const MeshInkUiLayout& layout=portrait_layout();" in source, "UI local layout aliases use const references"
assert "[T5-STACK]" not in source, "temporary Maps stack diagnostic should be removed after validation"

# Maps marker rendering must keep complete UiMapNode records off loopTask stack.
assert "Visible visible[50]" not in source, "Maps must not retain 50 complete node records on loopTask stack"
assert "map_marker_hits[count++]={(int16_t)sx,(int16_t)sy,i};" in source, "Maps reuses compact projected marker storage"
assert "struct Bounds {int16_t x,y,w,h;};" in source, "Maps collision bounds use compact 16-bit coordinates"
assert "map_marker_hit_count=count;" in source, "Maps publishes projected marker hit count after drawing"

# Shared X-axis interior geometry must derive from logical width.
assert "form_width==480" in ui_layout_source, "T5 setup form width guard missing"
assert "detail_value_x==230" in ui_layout_source, "T5 detail value column guard missing"
assert "meshink_form_pair_width" in ui_layout_source and "meshink_section_pair_width" in ui_layout_source, "responsive split-button helpers missing"
assert "meshink_pager_button_width" in ui_layout_source, "responsive pager helper missing"
assert "meshink_slider_width" in ui_layout_source, "responsive slider helper missing"
assert "box(30,180,480,64)" not in source, "setup name field must not hard-code T5 width"
assert "box(30,500,220,72)" not in source, "confirmation buttons must not hard-code T5 width"
assert "box(24,460,220,76)" not in source, "night schedule pair must not hard-code T5 width"
assert "hit(x,y,24,800,180,62)" not in source, "preset pager touch must not hard-code T5 width"
assert "hit(x,y,40,420,460,100)" not in source, "brightness slider touch must derive from logical width"

# Night Timer schedule editing lives inside the MODE row, not below Map Scale.
assert "meshink_settings_inline_action_rect" in ui_layout_source, "inline settings action rectangle missing"
contains('ui_action_button("EDIT TIMES",action,true);', "Night Timer MODE row shows Edit Times")
contains("hit(x,y,meshink_settings_inline_action_rect(portrait_layout(),118))", "Edit Times touch uses the same inline geometry")
contains("open_screen(Screen::NightSchedule);return true;", "Edit Times opens Night Schedule")
assert "meshink_night_schedule_top" not in ui_layout_source, "abandoned bottom Night Schedule geometry must be removed"
assert 'centred("NIGHT SCHEDULE",schedule_y' not in source, "Night Schedule must not be drawn over lower settings rows"

# Release cleanup after the temporary test34-test38 power experiments.
assert "meshink_power_light_sleep" not in runtime_source, "release runtime must not contain experimental light sleep"
assert "meshink_power_light_sleep" not in power_backend_source and "meshink_power_light_sleep" not in power_backend_header, "release power backend must not expose light-sleep experiment APIs"
assert "MeshInkPowerTelemetry" not in power_types_source and "meshink_power_read_telemetry" not in power_backend_header, "release build must not retain test-only current telemetry"
assert "MeshInkLightSleepStats" not in power_types_source, "release build must not retain light-sleep measurement counters"
assert "[T5-POWER-CACHE]" not in unified_source and "power_logging_tick" not in unified_source, "release unified runtime must not cache or print power-test reports"
assert "meshink_power_diagnostics_tick" not in board_target_source and "meshink_power_diagnostics_tick" not in power_backend_header, "release board loop must not run periodic power diagnostics"
assert "GaugeDiagnosticSnapshot" not in power_backend_source, "release backend must not retain power snapshot diagnostics"
for release_power_debug_source in (source, runtime_source, unified_source, board_target_source, power_backend_source):
    assert "T5_LOG_POWER" not in release_power_debug_source, "release source still contains power debug reporting"
assert "delay(12);" in source and "if(!standby_active)delay(12);" not in source, "UI restores the established unconditional 12 ms idle delay"

# Test40 release serial policy: normal operation is quiet; actionable faults remain.
assert '[T5-BOOT] MeshInk %s board=%s mode=%s' in unified_source, "release boot prints a concise version/board/mode header"
assert '[T5-BLE] scan response name=' not in companion_source, "release companion mode must not print BLE setup chatter"
assert 'companion shutdown complete elapsed=' not in companion_source, "release companion shutdown must not print routine timing"
assert 'Serial.printf("[T5-RADIO]' not in board_target_source and 'Serial.println("[T5-RADIO]' not in board_target_source, "release radio bring-up must not print routine diagnostics"
assert 'Serial.printf("[T5-BOOT] GPS module=' not in board_target_source, "release GPS startup must not print module diagnostics"
assert '[T5-ERROR]' in unified_source and '[T5-ERROR]' in power_backend_source, "release serial path retains actionable error reporting"

# Test41: five-minute clock/battery cadence applies both awake and in standby.
assert "status_bar_painted_minute" not in source, "awake UI no longer schedules minute-by-minute status refreshes"
assert "status_slot>=0&&status_slot!=status_bar_painted_slot" in source, "all modes use aligned five-minute status slots"

# Test42: concise startup transcript, then quiet steady state.
assert '[T5-INIT] psram=OK' in unified_source, "startup reports PSRAM readiness"
assert '[T5-INIT] display=initialized' in source and '[T5-INIT] touch=initialized' in source, "startup reports display/touch initialization without overclaiming verification"
assert '[T5-INIT] radio=SX1262 OK' in board_target_source, "startup reports radio readiness"
assert '[T5-INIT] gps=%s baud=%lu OK' in board_target_source and '[T5-WARN] gps=NMEA not confirmed; background retry active' in board_target_source, "startup reports confirmed GPS or explicit fallback warning"
assert '[T5-INIT] rtc=PCF8563 OK' in board_target_source and '[T5-WARN] rtc=' in board_target_source, "startup reports RTC success or fallback warning"
assert '[T5-INIT] battery-gauge=OK voltage=%umV' in board_target_source, "startup reports battery gauge readiness"
assert '[T5-INIT] storage=SPIFFS OK' in companion_source, "startup reports filesystem readiness"
assert '[T5-INIT] startup=READY' in unified_source, "local boot ends with a concise ready marker"

# Test43: final field candidate keeps startup terms unambiguous and radio
# electrical failures actionable without restoring verbose diagnostics.
assert '[T5-INIT] wifi-bt=OK wifi=off bt=off' in unified_source, "local startup labels ESP Wi-Fi/Bluetooth state explicitly"
assert '[T5-INIT] wifi-bt=OK wifi=off bt=ready' in unified_source, "companion startup labels ESP Wi-Fi/Bluetooth state explicitly"
assert '[T5-INIT] wireless=OK' not in unified_source, "ambiguous wireless startup label must not return"

# Test44: field standby redesign uses the existing full-size MeshInk bitmap and
# two side-by-side, thick-bordered unread summary cards.
assert "draw_meshink_logo(ui_y(70),false);" in source, "standby uses the existing full-size MeshInk logo"
assert 'centred("STANDBY"' not in source, "standby text heading is replaced by the logo"
assert "ui_rect(20,445,244,310)" in source and "ui_rect(276,445,244,310)" in source, "standby summary cards are side by side"
assert "draw_standby_envelope_icon" in source, "standby provides a dedicated large envelope icon"
assert "draw_standby_channel_icon" in source, "standby provides a dedicated large channel people icon"
assert "standby_centred(direct,direct_rect,direct_rect.y+ui_h(125),11)" in source, "private unread count is oversized"
assert "standby_centred(channel,channel_rect,channel_rect.y+ui_h(125),11)" in source, "channel unread count is oversized"
assert "rounded_box(rect,max(ui_w(22),ui_h(22)),false);" in source, "standby summary cards use the shared rounded visual language"
assert 'standby_centred("PRIVATE"' in source and 'standby_centred("CHANNEL"' in source, "standby cards retain clear private/channel labels"
assert "ui_y(805)" in source and "HOLD %s FOR TWO SECONDS TO WAKE" in source, "standby retains the lower wake instruction separator"

# Test45: MeshCore network feedback is surfaced and its useful RF metadata is
# persisted in the v2 device journal.
assert "const char* network" in data_source, "message model exposes network metadata"
assert "STORE_VERSION=3" in message_store_source, "message journal uses the 160-byte-text v3 format"
assert "int8_t snr_q4=0;" in message_store_header and "uint8_t path_len=MESHINK_MESSAGE_PATH_UNKNOWN;" in message_store_header, "received SNR/path metadata lives in the persistent record"
assert "uint8_t repeats=0;" in message_store_header and "MESHINK_MESSAGE_ROUTE_KNOWN" in message_store_header, "repeat and route metadata lives in the persistent record"
assert "local_protocol_query[2]={22,3}" in companion_source, "standalone runtime negotiates MeshCore v3 receive frames"
assert "frame[0]==16&&len>=16" in runtime_source and "frame[0]==17&&len>=11" in runtime_source, "v3 direct/channel frames are parsed explicitly"
assert "SNR %.1f DB  %u HOP%s" in runtime_source, "received messages expose SNR and hop count"
assert "(stored.flags&MESHINK_MESSAGE_ROUTE_FLOOD)" in runtime_source, "outgoing private messages expose persisted direct versus flood routing"
assert "frame[0]==0x88" in runtime_source and "handle_raw_repeat" in runtime_source, "raw RX frames drive channel repeat-hearing detection"
assert "HEARD %u REPEAT%s" in runtime_source, "channel sends expose persisted heard-repeat count"
assert "mesh::Utils::MACThenDecrypt" in runtime_source, "repeat matching validates/decrypts the echoed channel packet"
assert "Trace=3" in data_source, "trace is a first-class node-info request"
assert "frame[0]=36" in runtime_source and "frame[0]==0x89" in runtime_source, "trace command and response are wired through upstream MeshCore"
assert "TRACE %u HOP%s" in runtime_source and "DEST  %.1f DB" in runtime_source, "trace result reports repeater hashes/SNR and destination SNR"
assert 'settings_row("DIAGNOSTICS","LIVE MESHCORE RADIO STATS",650)' in source, "More exposes diagnostics"
assert "local_mesh_request_diagnostics()" in source and "draw_diagnostics()" in source, "diagnostics UI requests and renders live MeshCore stats"
assert "frame[2]={56,type}" in runtime_source, "diagnostics uses upstream CMD_GET_STATS"
assert "PACKETS RX/TX %lu / %lu" in runtime_source and "AIRTIME TX/RX %lu / %lu S" in runtime_source, "diagnostics decodes packet and radio counters"
assert 'settings_row("HELP","USING MESHINK",780)' in source, "Help moves below Diagnostics without overlapping bottom navigation"

# Test46: a message received while its conversation is visibly open is already
# seen and must not create contact/channel or bottom-tab unread dots.
assert "bool ui_chat_is_visible(bool channel)" in source, "UI exposes a single visible-chat predicate for unread handling"
assert "return !standby_active&&(channel?screen==Screen::ChannelChat:screen==Screen::ContactChat);" in source, "visible chat excludes standby and distinguishes private/channel chats"
assert "ui_chat_is_visible(false)&&!active_channel_&&!memcmp(active_key_,key,6)" in runtime_source, "matching visible private chat suppresses provider unread increment"
assert "ui_chat_is_visible(true)&&active_channel_&&active_key_[0]==channel" in runtime_source, "matching visible channel chat suppresses provider unread increment"
assert "if(!already_seen){auto& unread=direct_unread(key);if(unread<255)unread++;}" in runtime_source, "private unread increments only when unseen"
assert "if(!already_seen&&channel<MAX_UI_CHANNELS&&channel_unread_[channel]<255)channel_unread_[channel]++;" in runtime_source, "channel unread increments only when unseen"
assert "const bool visible=ui_chat_is_visible(channel);" in source, "bottom-tab unread uses the same visible-chat predicate"


# Test47: LAST HEARD uses MeshCore's per-contact lastmod (our T5 clock), while
# LAST ADVERT remains the remote advertisement timestamp.
assert "format_last_heard(contact.lastmod,heard)" in runtime_source, "contact list LAST HEARD must use MeshCore lastmod"
assert 'snprintf(item.subtitle,sizeof(item.subtitle),"%s  HEARD %s",role,heard)' in runtime_source, "contact list labels lastmod as heard activity"
assert "if(contact.lastmod)format_time(contact.lastmod,item.time)" in runtime_source, "contact list time column follows lastmod"
assert "format_last_heard(detail_contact_.lastmod,self->detail_seen_)" in runtime_source, "node Overview LAST HEARD must use lastmod"
assert "now>=detail_contact_.last_advert_timestamp" in runtime_source and "detail_advert_age_" in runtime_source, "LAST ADVERT remains based on last_advert_timestamp"
assert "INFO REPLY %s" not in runtime_source and "recent_info_.reply_millis" not in runtime_source, "one-node info cache must not override LAST HEARD"
assert "contact->lastmod=heard" in runtime_source and "meshink_rtc_current_time()" in runtime_source, "matched local receptions advance lastmod using the T5 clock"
assert "note_heard(key,6); // includes CLI/direct payloads" in runtime_source, "all attributable inbound direct payloads advance LAST HEARD"
assert "note_heard(detail_contact_.id.pub_key,PUB_KEY_SIZE);" in runtime_source, "matched status/telemetry/path/trace replies advance LAST HEARD"
assert "provider.heard(pending_direct.key,6)" in runtime_source, "valid end-to-end delivery ACK advances LAST HEARD"
assert "memcpy(&detail_contact_.lastmod,item.frame+p,4)" in runtime_source, "discovered-contact details retain MeshCore lastmod when supplied"


# Test48: Last Heard persistence. MeshInk-added lastmod updates must survive
# reboot without abusing LAST ADVERT or forcing a flash write per packet.
assert "local_mesh_schedule_contacts_save();" in runtime_source, "advancing Last Heard schedules contact persistence"
assert "local_mesh_flush_contacts_save_if_due();" in runtime_source, "local mesh loop services deferred contact persistence"
assert "local_contacts_save_due=millis()+5000UL;" in companion_source, "Last Heard persistence coalesces writes on MeshCore's five-second cadence"
assert "store.saveContacts(&the_mesh,local_persist_contact);" in companion_source, "deferred save persists MeshCore ContactInfo including lastmod"
assert "return contact.type!=ADV_TYPE_NONE;" in companion_source, "transient anonymous contacts are not persisted by Last Heard saves"
assert "if(local_mesh_is_running())local_mesh_flush_contacts_save_now();" in unified_source, "deliberate local reboot flushes pending Last Heard timestamps first"
assert "last_advert_timestamp=heard" not in runtime_source, "Last Heard persistence must never rewrite Last Advert"


# Test49: GPS telemetry updates the saved MeshCore contact position so Node Info
# and Maps keep the latest known coordinates after reboot.
assert "contact->gps_lat=recent_info_.lat;" in runtime_source, "GPS telemetry persists latitude into ContactInfo"
assert "contact->gps_lon=recent_info_.lon;" in runtime_source, "GPS telemetry persists longitude into ContactInfo"
assert "detail_contact_.gps_lat=recent_info_.lat;" in runtime_source and "detail_contact_.gps_lon=recent_info_.lon;" in runtime_source, "open Node Info immediately reflects the persisted contact coordinates"
assert "local_mesh_schedule_contacts_save();" in runtime_source, "GPS position changes use the deferred contact persistence path"
assert 'strcpy(self->detail_position_source_,"SAVED POSITION")' in runtime_source, "reloaded telemetry-derived coordinates use a provenance-neutral saved-position label"
assert '"SAVED ADVERT %s"' not in runtime_source, "persisted telemetry coordinates must not be mislabelled as advert-derived"


# Test50: one 250-message v3 journal spans standalone and Bluetooth Companion
# modes. Existing 96-message v1 and 250-message v2 testing journals migrate
# transactionally, with v2 RF/route metadata preserved.
assert "MESHINK_MESSAGE_CAPACITY=250" in message_store_header, "device journal capacity is 250 messages"
assert "MESHINK_MESSAGE_TEXT_MAX=160" in message_limits_source, "MeshInk exposes the full MeshCore direct-message text limit"
assert "LEGACY_STORE_CAPACITY_V1=96" in message_store_source and "LEGACY_STORE_VERSION_V1=1" in message_store_source, "v1 96-message store remains an explicit migration source"
assert "LEGACY_STORE_VERSION_V2=2" in message_store_source and "sizeof(LegacyStoredMessageV2)==172" in message_store_source, "current 250-message v2 store remains an explicit migration source"
assert "migrate_legacy(disk,disk.version)" in message_store_source, "v1/v2 stores are migrated instead of discarded"
assert "legacy_header.sequence" in message_store_source and "current.sequence=legacy.sequence" in message_store_source, "migration preserves journal and per-message sequence values"
assert 'STORE_TEMP_PATH[]="/ui_messages.v3.tmp"' in message_store_source and 'STORE_BACKUP_PATH[]="/ui_messages.legacy.bak"' in message_store_source, "migration uses v3 temporary and rollback files"
assert "SPIFFS.rename(STORE_PATH,STORE_BACKUP_PATH)" in message_store_source and "SPIFFS.rename(STORE_TEMP_PATH,STORE_PATH)" in message_store_source, "migration swaps v3 into place only after writing it"
assert "original retained" in message_store_source, "migration failure explicitly preserves the testing device's old history"
assert "sizeof(LegacyStoredMessageV1)==168" in message_store_source and "sizeof(MeshInkStoredMessage)==188" in message_store_source, "legacy and v3 fixed-record layouts are pinned"
assert "memcpy(current.text,legacy.text,sizeof(legacy.text));" in message_store_source, "legacy text copies into the larger v3 field without truncating existing messages"
assert "current.snr_q4=legacy.snr_q4;" in message_store_source and "current.flags=legacy.flags;" in message_store_source, "v2 RF/route metadata survives v2->v3 migration"
assert "for(uint16_t logical=0;ok&&logical<legacy_header.count;++logical)" in message_store_source, "migration reads only the old ring's logical messages"
assert "if(legacy.sequence==0){++skipped_blank;continue;}" in message_store_source, "unexpected blank legacy active slots are skipped"
assert "migrated.count=migrated_count;" in message_store_source, "v3 logical count is compacted to valid migrated records"
assert "for(uint16_t i=migrated_count;ok&&i<MESHINK_MESSAGE_CAPACITY;++i)" in message_store_source, "unused v3 capacity is explicitly blank but outside logical count"
assert "current.text[sizeof(current.text)-1]=0;" in message_store_source, "migrated text is forced NUL-terminated"
assert "verified.count==migrated_count" in message_store_source and "verify=SPIFFS.open(STORE_TEMP_PATH" in message_store_source, "completed temp journal is reopened and validated before swapping"
assert 'STORE_INVALID_PATH[]="/ui_messages.invalid.bak"' in message_store_source, "incompatible live journals get a non-destructive recovery backup"
assert "preserving before recreate" in message_store_source, "invalid journal handling is explicitly non-destructive"
assert "meshink_message_store().begin()" in companion_source, "companion mode opens/migrates the same journal"
assert "char text[MESHINK_MESSAGE_TEXT_BYTES]" in companion_source, "Bluetooth companion pending sends retain the full message"


# Test51: the 250-message journal is flash-authoritative. Neither the full
# journal nor 250 rendered message bodies may be duplicated in RAM/PSRAM.
assert "MeshInkStoredMessage* records_" not in message_store_header, "journal must not allocate a 250-record RAM/PSRAM backing array"
assert "MeshInkStoredMessage records_[MESHINK_MESSAGE_CAPACITY]" not in message_store_header, "journal records stay exclusively in SPIFFS"
assert "bool read(size_t logical,MeshInkStoredMessage& out) const;" in message_store_header, "journal exposes fixed-record on-demand reads"
assert "MESHINK_MESSAGE_CAPACITY*sizeof(MeshInkStoredMessage)" in message_store_source, "flash file retains all 250 fixed-size records"
assert "record-cache=0 header=%uB" in message_store_source, "boot transcript reports zero cached message records"
assert "mutable File file_{};" in message_store_header, "one lightweight journal file handle is reused for record seeks"
assert "uint16_t active_indices_[MESHINK_MESSAGE_CAPACITY]{};" in runtime_source, "active conversation keeps only tiny journal-position indices"
assert "mutable MessageView active_message_view_{};" in runtime_source, "UI keeps one scratch rendered message instead of 250"
assert "MessageView active_messages_[MAX_STORED_MESSAGES]" not in runtime_source and "MessageView* active_messages_" not in runtime_source, "full rendered history is never cached"
assert "store_.read(active_indices_[i],item)" in runtime_source, "individual visible/measured messages are loaded from flash on demand"
assert "heap_caps_calloc" not in message_store_source and "heap_caps_calloc" not in runtime_source, "message history no longer needs large PSRAM allocations"
assert "ui_setup();           // show boot logo while storage/radio initialize" in unified_source, "display still initializes before local message-store startup"
assert "local_mesh_setup();   // includes first-boot SPIFFS mount / format" in unified_source, "journal migration remains after display initialization"


# Test52: message storage stays responsive/battery-efficient and v1 migration
# preserves valid history without promoting zero-filled legacy capacity.
assert "uint32_t revision() const" in message_store_header, "journal exposes a cheap append revision for cache invalidation"
assert "conversation_store_revision_!=store_.revision()" in runtime_source, "conversation summaries rescan flash only after message history changes"
assert "conversation_contacts_signature_!=contact_signature" in runtime_source, "contact/name changes invalidate summaries without periodic journal scans"
assert "void rebuild_conversations(uint32_t contact_signature)" in runtime_source, "conversation previews are rebuilt with one linear journal pass"
assert "last_for(" not in runtime_source, "per-contact full-journal scans are removed"
assert "rebuild_active();" not in runtime_source[runtime_source.index("void refresh(bool force=false)"):runtime_source.index("void received_direct")], "periodic provider refresh must not rebuild the active message index"
assert "for(size_t n=header_.count;n>0;--n)" in message_store_source[message_store_source.index("bool MeshInkMessageStore::find_physical"):message_store_source.index("uint32_t MeshInkMessageStore::append")], "message state lookup searches newest-first"
assert "for(uint16_t logical=0;ok&&logical<legacy_header.count;++logical)" in message_store_source, "migration reads only the old ring's logical messages, never all 96 physical slots"
assert "if(legacy.sequence==0)" in message_store_source and "++skipped_blank;" in message_store_source, "unexpected blank legacy active slots are skipped"
assert "migrated.count=migrated_count;" in message_store_source, "v2 logical count is compacted to valid migrated records"
assert "for(uint16_t i=migrated_count;ok&&i<MESHINK_MESSAGE_CAPACITY;++i)" in message_store_source, "unused v2 capacity is explicitly blank but outside logical count"
assert "current.text[sizeof(current.text)-1]=0;" in message_store_source, "legacy text is forced NUL-terminated during migration"
assert "verified.count==migrated_count" in message_store_source and "verify=SPIFFS.open(STORE_TEMP_PATH" in message_store_source, "completed temp journal is reopened and validated before swapping"
assert "SPIFFS.rename(STORE_PATH,STORE_BACKUP_PATH)" in message_store_source and "SPIFFS.rename(STORE_TEMP_PATH,STORE_PATH)" in message_store_source, "validated migration keeps the old journal as rollback backup until swap succeeds"
assert 'STORE_INVALID_PATH[]="/ui_messages.invalid.bak"' in message_store_source, "incompatible live journals get a non-destructive recovery backup"
assert "preserving before recreate" in message_store_source, "invalid journal handling is explicitly non-destructive"
assert "CHAT_PAGE_ANCHORS=250" in source and "chat_page_starts[CHAT_PAGE_ANCHORS]" in source, "lazy chat navigation stores only tiny page anchors"
assert "chat_fill_backwards" in source and "chat_page_bounds_lazy" in source, "chat page composition remains height-aware and incremental"
assert "const UiMessage& message=ui_data->active_message(i);" in source, "visible message record is read once and reused for height plus drawing"
assert "item.sequence==0||!matches(item)" in runtime_source, "sequence-zero records can never enter an active chat index even if an older v2 conversion contained one"


# Test53: common message paths minimize flash traffic without weakening
# immediate persistence.
assert "bool has_rx=false,int8_t snr_q4=0" in message_store_header, "receive RF metadata can be included in the append write"
assert "if(has_rx){" in message_store_source and "item.flags|=MESHINK_MESSAGE_HAS_RX;" in message_store_source, "append persists SNR/path metadata atomically with the message"
assert "MeshInkMessageOrigin::LocalUi,has_rf,snr_q4,path_len" in runtime_source, "standalone receives avoid a second metadata rewrite"
assert "MeshInkMessageOrigin::CompanionApp," in companion_source and "pkt!=nullptr" in companion_source, "companion receives persist RF metadata in the same write"
assert "void update_outgoing(uint32_t sequence,UiMessageState state,uint32_t ack,bool route_flood);" in message_store_header, "direct send response has a coalesced metadata update"
assert "item.state=(uint8_t)state;" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes state"
assert "item.ack=ack;" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes ACK"
assert "MESHINK_MESSAGE_ROUTE_KNOWN" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes route"
assert "provider.confirm_direct_send(" in runtime_source, "local send response uses one persistent metadata update"
assert "provider.note_direct_ack(" not in runtime_source and "provider.note_direct_route(" not in runtime_source, "old multi-write direct-send path is removed"


# Test54: chat/contact polish stays readable and lightweight. Reuse the tiny
# built-in glyph bitmap instead of adding a font engine or full font asset.
assert "static void ui_glyph_bounds(" in source and "const uint8_t* rows=glyph(c)" in source, "polished text reuses the existing compact glyph table"
assert "static int ui_char_advance(char c,int scale)" in source and "static void ui_text(" in source, "chat/contact text uses lightweight proportional advances"
assert "static void rounded_fill(" in source and "xx*xx+yy*yy<=r*r" in source, "rounded panels use an integer framebuffer primitive"
assert "sqrt(" not in source[source.index("static void rounded_fill("):source.index("static void rounded_box(",source.index("static void rounded_fill("))], "rounded corners avoid floating-point geometry"
assert "rounded_box(layout.outer_margin,y,layout.outer_width,layout.list_row_height" in source, "contacts/channels/discovery use rounded cards"
assert "ui_draw_wrapped(item.subtitle" in source and "layout.outer_width-2*layout.text_inset,3" in source, "list body text is rendered at readable scale 3"
assert "ui_text_fit(item.title" in source and "3,0,true" in source[source.index("ui_text_fit(item.title"):source.index("ui_draw_wrapped(item.subtitle")], "list titles remain large and clipped safely"
assert "rounded_box(geometry.x,y,geometry.width,geometry.height,radius,message.outgoing)" in source, "chat bubbles use rounded incoming/outgoing surfaces"
assert "const int min_width=ui_w(240);" in source and "const int max_width=ui_w(456);" in source, "chat bubbles stay compact without becoming too narrow to read"
assert "ui_wrapped_line_count(message.text,text_width,3)" in source, "message paging measures the same proportional scale-3 body text that is drawn"
assert "geometry.text_width,3,color,false,16" in source, "long messages remain readable instead of being clipped at the former eight-line draw limit"
assert 'ui_text("Write a message..."' in source and 'const char* prompt=compose_text[0]?compose_text:"Write a message...";' in source, "composer uses a readable mixed-case prompt"
assert "rounded_box(back_rect" in source and "ui_action_button(action,action_rect,true)" in source, "chat header actions share the rounded visual language"
assert "malloc(" not in source[source.index("static void ui_glyph_bounds("):source.index("static meshink_keyboard::Metrics")], "polished typography/rounding adds no dynamic memory"
assert "const MessageBubbleGeometry geometry=message_bubble_geometry(message);" in source and "draw_message_bubble(message,y,geometry);" in source, "visible chat bubbles reuse one geometry measurement for drawing"


# Test55: the chat/contact visual language extends across the rest of the UI
# without changing touch geometry or adding heavyweight rendering state.
render_body = source[source.index("static void draw_welcome()"):]
assert "ui_section_card(row);" in source[source.index("static void settings_row"):], "settings use shared rounded cards"
assert "ui_action_button(" in source, "screens share one rounded action-button treatment"
assert "rounded_box(rect.x,rect.y,rect.width,rect.height" in source[source.index("static void key("):source.index("static void draw_keyboard")], "portrait keyboard keys are rounded without changing key rectangles"
assert "rounded_box(metrics.entry.x,metrics.entry.y,metrics.entry.width,metrics.entry.height" in source, "landscape keyboard entry uses the polished rounded field"
assert "rounded_box(zoom_in" in source and "rounded_box(zoom_out" in source and "rounded_box(locate" in source, "map controls use the same rounded style"
assert "ui_section_card(core);ui_section_card(radio);ui_section_card(packets);" in source, "diagnostics is grouped into readable cards"
assert "ui_section_card(quick);ui_section_card(button);ui_section_card(keyboard);" in source, "Help uses readable grouped cards"
assert "ui_action_button(\"ADVERT FLOOD\",advert_button,true);" in source and "ui_action_button(\"POWER OFF\",power_button,false);" in source, "Quick Settings actions use shared polished buttons"
assert "rounded_box(start_rect" in source and "ui_action_button(\"SAVE SCHEDULE\",save_rect,true);" in source, "Night Schedule uses rounded selected fields and action"
assert "ui_section_card(info);" in source[source.index("static void draw_about"):], "About metadata is grouped into a rounded card"
assert "static void box(int x" not in source and "box(" not in render_body.replace("rounded_box(", ""), "legacy square box primitive is fully removed from screen rendering"
assert "malloc(" not in source[source.index("static void ui_glyph_bounds("):source.index("static meshink_keyboard::Metrics")], "full visual polish still adds no dynamic memory"


# Test56: source-level 540x960 UI audit. Dynamic text stays inside its
# controls, chat pages do not waste or overpaint vertical space, and all
# list-style resources remain reachable.
assert "static void ui_centred_fit(" in source, "dynamic centred text has a bounded fit helper"
assert "ui_centred_fit(title,layout.header_title_y,layout.width-title_guard,4,0,true);" in source, "app headers constrain long contact/channel titles between controls"
assert "ui_centred_fit(node.name,ui_y(126),portrait_layout().section_width,4,0,true);" in source, "Node Info constrains long names"
assert "static int ui_text_max_line_width(" in source and "ui_text_max_line_width(message.text,3)" in source, "bubble width follows the longest explicit message line"
assert "min(16,ui_wrapped_line_count(message.text,text_width,3))" in source, "bubble measurement cannot exceed the renderer's sixteen-line limit"
assert "if(used+needed>available)break;" in source, "chat paging only admits complete bubbles into the visible viewport"
assert "static bool chat_needs_paging(size_t count)" in source and "chat_history_available_unpaged()" in source, "one-page conversations reclaim the unused pager band"
assert "const int compose_y=chat_compose_top();" in source, "message composer drawing uses shared vertical geometry"
assert source.count("chat_compose_top()")>=3, "composer draw and touch paths share the same top edge"
assert source.count("const int text_width=ui_text_width(page_text,2);")>=2, "list and chat page arrows use proportional label width"
assert "const int subtitle_scale=ui_text_width(subtitle,3)<=subtitle_width?3:2;" in source, "long settings subtitles shrink before clipping"
assert "const int detail_scale=ui_text_width(PRESETS[index].detail,3)<=detail_width?3:2;" in source, "long radio preset technical details shrink before clipping"
assert "if(!keyboard_visible)settings_row(\"PATH HASH MODE\",path_hash_label(),510);" in source, "Radio Settings does not draw a row beneath the portrait keyboard"
assert 'if(value>99)strcpy(out,"99+");' in source, "status unread counters are visually bounded"
assert "text(count,left,ui_y(17),2,0,true);" in source, "status secondary counters use compact scale-two text"
assert "ui_text_width(short_name,2),ui_text_width(age,2)" in source, "map label background accounts for both node name and age"
assert "meshink_map_control_rect(portrait_layout(),(int)control)" in source, "map labels avoid the visible map controls"
assert "const int label_bottom=ui_y(766);" in source, "map node labels stay clear of bottom map overlays"
assert "const int zoom_label_width=ui_text_width(zoom,2)+ui_w(8);" in source, "map zoom background follows proportional text width"
assert "const int scale_backing_width=max(pixels+ui_w(12),ui_text_width(scale,2)+ui_w(16));" in source, "map scale backing covers both the physical bar and proportional label"
assert "const int natural=ui_text_width(message,scale)+ui_w(48);" in source and "portrait_layout().width-ui_w(24)" in source, "toasts are proportional and screen-bounded"
assert "const int width=ui_text_width(value,scale);" in source[source.index("static void standby_centred"):], "standby labels use proportional centering"
assert "for (const auto& g : FONT) if (g.c == '?') return g.r;" in source, "unsupported text is visible rather than silently blank"
assert "ui_text_width(start,3)" in source and "ui_text_width(end,3)" in source, "Night Schedule time values are measured and right-aligned"
assert "const int status_scale=ui_text_max_line_width(node.status,3)<=layout.section_width?3:2;" in source, "dense Node Status counters shrink before wrapping can hide later metrics"
assert "ui_wrapped_line_count(node.telemetry,layout.section_width,3)<=4?3:2" in source, "dense telemetry shrinks only when needed to keep its allotted four lines"
assert "layout.section_width,2,0,true,9" in source, "Trace Route uses the safe extra vertical room for two more lines"
assert "ui_text_fit(node.position_source,layout.detail_value_x,ui_y(448)," in source, "two-line Overview position stays clear of its source label"
assert "static size_t discovery_page = 0;" in source, "Discovered adverts have independent paging state"
assert "const size_t first=discovery_page*LIST_ITEMS_PER_PAGE;" in source, "Discovered adverts render every page rather than only the first five"
assert "screen==Screen::Contacts||screen==Screen::Channels||screen==Screen::Discovery" in source, "Discovery shares vertical swipe paging with Contacts and Channels"
assert "ui_data->open_advert(index)" in source, "Discovery touch indexing follows the visible page"

assert "ui_text_fit(short_name,lx+4,ly+2,w-ui_w(8),2,0,true);" in source, "capped map label backings also clip long node names"
assert 'if(status_unread>99)strcpy(direct,"99+");' in source and 'if(status_channel_unread>99)strcpy(channel,"99+");' in source, "large standby unread counts stay inside their 244px cards"
assert "ui_centred_fit(wake.confirm_battery" in source and "ui_centred_fit(wake.confirm_external" in source, "board-specific shutdown guidance is screen-bounded"
assert "ui_centred_fit(wake.off_battery_line1" in source and "ui_centred_fit(wake.off_external_line2" in source, "powered-off guidance remains bounded for future board ports"
assert "ui_centred_fit(wake_button,ui_y(840)" in source, "standby wake guidance cannot overflow the display"
assert 'ui_centred_fit("LORA RADIO NOT DETECTED",ui_y(390),portrait_layout().width-ui_w(24),4,0,true);' in source, "hardware failure heading is screen-bounded"
assert "char line_text[160]{};" in source, "proportional wrapping preserves long unbroken lines without a 64-byte scratch truncation"
assert "const bool truncated=(row==max_lines-1)&&*next;" in source, "bounded multi-line text visibly marks intentional truncation"
assert "ui_text_fit(footer,geometry.x+ui_w(14)" in source, "oversized message metadata is clipped inside its bubble instead of drawing outside"


# Test57: full-length direct messages remain valid across the complete retry
# policy, channel sends are never silently truncated, and a terminal direct
# failure restores the draft only when it is safe to do so.
assert "DIRECT_RETRY_LIMIT=3" in runtime_source, "direct messages use at most three retries after the initial attempt"
assert "pending_direct.retry>=DIRECT_RETRY_LIMIT" in runtime_source, "retry exhaustion is governed by the shared three-retry limit"
assert "MESHINK_MESSAGE_TEXT_MAX==MAX_TEXT_LEN" in runtime_source, "MeshInk's 160-byte editor/store limit is compile-time tied to MeshCore"
assert "13+MESHINK_MESSAGE_TEXT_MAX<=MAX_FRAME_SIZE" in runtime_source, "a full direct-message command is compile-time checked against the companion frame"
assert "static size_t channel_message_limit(" in runtime_source, "channel payload capacity accounts for the sender-name prefix"
assert "if(text_len>limit)" in runtime_source and "channel send rejected" in runtime_source, "oversized channel messages are rejected instead of silently truncated"
assert "bool ui_restore_failed_compose(const char* text)" in source, "UI exposes bounded failed-draft recovery"
restore_body=source.split("bool ui_restore_failed_compose(const char* text)",1)[1].split("void ui_notify_advert_result",1)[0]
assert "!text||!text[0]||compose_text[0]" in restore_body, "failed draft never overwrites text the user already typed"
assert "pending_direct_is_visible_chat()" in runtime_source and "ui_chat_is_visible(false)" in runtime_source, "failed draft restoration is limited to the visible direct conversation"
assert "memcmp(active.id.pub_key,pending_direct.key,6)==0" in runtime_source, "failed draft cannot leak into a different direct conversation"
assert "ui_restore_failed_compose(pending_direct.text)" in runtime_source, "terminal direct failure offers the original message back to the composer"
assert 'fail_pending_direct("retry limit")' in runtime_source, "retry exhaustion marks failed and restores safely"
assert 'fail_pending_direct("retry queue busy")' in runtime_source and 'fail_pending_direct("initial queue busy")' in runtime_source, "local queue failures share the same safe terminal-failure path"
assert "if(!local_mesh_send_active(compose_text))return true;" in source, "landscape keeps rejected text editable instead of rotating away"

assert 'case UiMessageState::Retrying1:return "RETRYING 1/3"' in runtime_source, "runtime retry status matches three-retry policy"
assert 'case UiMessageState::Retrying3:state="RETRYING 3/3"' in source, "chat footer retry status matches three-retry policy"
