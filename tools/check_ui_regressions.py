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
data_source = (root / "src" / "ui_data.h").read_text(encoding="utf-8")
map_source = (root / "src" / "map_tiles.cpp").read_text(encoding="utf-8")
pmtiles_source = (root / "src" / "pmtiles_reader.cpp").read_text(encoding="utf-8")
pmtiles_header = (root / "src" / "pmtiles_reader.h").read_text(encoding="utf-8")
unified_source = (root / "src" / "unified_main.cpp").read_text(encoding="utf-8")
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
contains('draw_screen();fast_full_redraw("SHORT_BUTTON_REFRESH",false);', "primary-button refresh without home navigation")
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
assert re.fullmatch(r"1\.9\.1-test\.\d+", firmware_version.group(1)), "testing firmware version keeps the 1.9.1-test.N format"

# Test29 status-bar refresh policy: clock/battery periodic updates are aligned
# to wall-clock five-minute boundaries. Event-driven GPS/message redraws may
# show newer values early but must not postpone the next :00/:05/:10... slot.
contains("static int8_t status_bar_painted_hour = -1;", "status bar tracks the last painted wall-clock time")
contains("status_hour>=0&&status_minute>=0&&(status_minute%5)==0&&", "periodic status cadence is aligned to five-minute wall-clock boundaries")
contains("(status_bar_painted_hour!=status_hour||status_bar_painted_minute!=status_minute);", "an aligned minute is painted only once unless another event redraws it")
contains("if(aligned_status_due)status_bar_dirty=true;", "aligned five-minute boundary queues the status bar")
assert "status_bar_refreshed_at" not in source, "elapsed-time status cadence must not return"
assert "if(changed&&!standby_active)status_bar_dirty=true;" not in source, "minute-by-minute hardware sampling must not repaint the status bar"
contains("[T5-UI] status-bar clock=%02d:%02d", "status clock logging occurs when the bar is actually drawn")
contains("refresh_area(MeshInkRefreshMode::Direct,", "status bar uses area refresh")
contains("{0,0,portrait_layout().width,portrait_layout().status_height},wake);", "status area is limited to the bar")
contains("static int16_t status_gps_satellites_bar = 0;", "status bar owns a separately throttled satellite count")
contains("now-last_satellite_bar_refresh>=3000UL", "visible satellite count is throttled to three seconds")
contains("status_gps_satellites_bar=satellites;", "throttled satellite display eventually adopts latest live count")
contains('reason=gps-satellites-3s', "throttled satellite refreshes are observable in logs")
contains("!standby_active&&enabled&&has_fix&&", "satellite count does not trigger status refresh while hidden in standby")
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
assert 'report_local_wireless_state("local-pre"' in unified_source, "local boot verifies radios before UI startup"
assert 'report_local_wireless_state("local-post-mesh"' in unified_source, "local boot verifies radios after MeshCore startup"
assert 'meshink_wireless_force_wifi_off()' in unified_source, "companion boot explicitly keeps unused Wi-Fi off"
assert 'report_companion_wireless_state("companion-ready"' in unified_source, "companion boot verifies Wi-Fi off and Bluetooth active"
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
    "Serial.flush();",
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
assert "[T5-LIGHT] companion exit acknowledgement brightness=100%" in exit_feedback_body, "companion exit frontlight acknowledgement is observable in field logs"
assert "t5_companion_show_returning_notice" not in companion_source and "t5_companion_show_returning_notice" not in board_target_source, "companion exit must not reinitialize EPDiy before reboot"
assert 'RETURNING TO LOCAL UI' not in board_target_source, "unsafe retained reboot screen remains removed"
release_body = board_target_source.split("void meshink_board_companion_release_resources()",1)[1].split("void T5Board::begin()",1)[0]
for handoff_step in ("radio_hal.detachInterrupt(P_LORA_DIO_1);","radio_spi.end();"):
    assert handoff_step in release_body, f"companion radio cleanup missing {handoff_step}"
assert "gpio_uninstall_isr_service();" not in release_body, "frontlight-only exit must not tear down the global GPIO ISR service"
assert 'companion shutdown complete elapsed=%lums' in companion_source, "hardware log reports measured companion shutdown duration"
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
contains("(screen==Screen::Contacts||screen==Screen::Channels)&&abs(tap.dy)>60", "Contacts/Channels vertical swipe changes pages")
contains("draw_list_page_footer(contacts_page,count);", "Contacts displays page count when multiple pages exist")
contains("box(layout.outer_margin,y,layout.outer_width,layout.list_row_height);", "list cards use shared logical interior width")
contains("row*portrait_layout().list_row_stride", "list drawing/touch use shared row stride")
contains("draw_list_page_footer(channels_page,count);", "Channels displays page count when multiple pages exist")
contains("if(pages<=1)return;", "single-page Contacts/Channels hide the page footer")
contains("if(page>0)draw_page_arrow", "page indicator shows previous-page swipe-down arrow only when available")
contains("if(page+1<pages)draw_page_arrow", "page indicator shows next-page swipe-up arrow only when available")
contains("(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&!keyboard_visible&&abs(tap.dy)>60", "conversation history uses vertical swipe paging")
contains("draw_page_indicator(chat_page,pages,layout.bottom_nav_top-ui_h(60));", "conversation history page indicator follows scaled geometry")
assert 'text("OLDER"' not in source and 'text("NEWER"' not in source, "conversation paging buttons must stay removed"
contains("static uint8_t node_info_page_count(uint8_t type){return node_has_status(type)?4:3;}", "Node Info page count is role-aware")
contains("node_has_status(uint8_t type){return type==(uint8_t)UiNodeRole::Repeater||type==(uint8_t)UiNodeRole::Room;}", "Status is exposed for repeaters and room servers")
contains("screen==Screen::ContactDetails&&!keyboard_visible&&abs(tap.dy)>60", "Node Info pages use vertical swipe paging")
contains('UiNodeInfoRequest::Status,"REQUEST STATUS"', "Node Info exposes an individual status request")
contains('UiNodeInfoRequest::Telemetry,"REQUEST TELEMETRY"', "Node Info exposes an individual telemetry request")
contains('UiNodeInfoRequest::Path,"REQUEST PATH"', "Node Info exposes an individual path request")
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
contains("draw_wrapped(node.status,layout.section_margin,ui_y(294),27,3,0,true,14);", "received status text is larger and scaled")
contains("draw_wrapped(node.telemetry,layout.section_margin,ui_y(270),27,3,0,true,4);", "received telemetry text is larger and scaled")
contains("draw_wrapped(node.path,layout.section_margin,ui_y(270),27,3,0,true,5);", "received path text is larger and scaled")
contains('page==NodeInfoPage::Status&&hit(x,y,meshink_node_action_rect(portrait_layout()))', "status action touch follows shared control geometry")
contains('page==NodeInfoPage::Telemetry&&hit(x,y,meshink_node_action_rect(portrait_layout()))', "telemetry action touch follows shared control geometry")
contains('page==NodeInfoPage::Path&&hit(x,y,meshink_node_action_rect(portrait_layout()))', "path action touch follows shared control geometry")
assert source.count("active_node_saved_password(remote_password,sizeof(remote_password))")>=2, "saved credentials should prefill from both Status and Telemetry login"

contains('meshink_display_fill_rect({0,metrics.clear_top,layout.width,', "password keyboard clear area follows shared geometry")
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
assert "meshink_power_read_battery_percent(soc)" in board_source, "MeshCore diagnostics use backend-provided battery percentage"
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
contains("const int zoom_label_width=(int)strlen(zoom)*12+ui_w(8);", "zoom label backing tracks rendered text width")
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
assert "-DMESHINK_GEOMETRY_DIAGNOSTICS=1" in cache64_build_flags, "test9 cache64 build must run boot geometry audit"
assert "-DMESHINK_TOUCH_DIAGNOSTICS=1" in cache64_build_flags, "test9 field build must enable backend identity diagnostics"
assert "-DT5_LOG_UI=1" in cache64_build_flags, "test9 cache64 build must include targeted UI logs"
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
for moved_power_impl in ("BQ27220_ADDR", "BQ25896", "T5_FACTORY_GAUGE_PROFILE", "GaugeDiagnosticSnapshot", "gauge_apply_factory_profile_if_needed"):
    assert moved_power_impl not in board_target_source, f"target.cpp still owns power implementation: {moved_power_impl}"
    assert moved_power_impl in power_backend_source, f"T5 power backend missing consolidated implementation: {moved_power_impl}"
assert "meshink_power_prepare_board();" in board_target_source, "board startup delegates gauge/profile preparation"
assert "meshink_power_diagnostics_tick();" in board_target_source, "GPS loop delegates power diagnostics"

# Test21 radio and board-capability boundaries. SD/map storage stays separate.
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
assert "meshink_radio_stats()" in runtime_source, "radio health logging uses generic stats"
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
assert 'centred("LORA RADIO NOT DETECTED"' in source, "radio failure UI uses generic LoRa wording"

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
contains('text("EDIT TIMES",action.x+ui_w(17),action.y+ui_h(20),2,0xFF,true);', "Night Timer MODE row shows Edit Times")
contains("hit(x,y,meshink_settings_inline_action_rect(portrait_layout(),118))", "Edit Times touch uses the same inline geometry")
contains("open_screen(Screen::NightSchedule);return true;", "Edit Times opens Night Schedule")
assert "meshink_night_schedule_top" not in ui_layout_source, "abandoned bottom Night Schedule geometry must be removed"
assert 'centred("NIGHT SCHEDULE",schedule_y' not in source, "Night Schedule must not be drawn over lower settings rows"
