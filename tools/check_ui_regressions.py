"""Static UI regression checks for MeshInk interaction and map geometry.

The firmware build catches C++ errors; these checks ensure the intended
screen/navigation behaviours and matching draw/touch targets remain wired.
They do not replace a physical touch controller, GPS, or e-paper test.
"""
from pathlib import Path
import re

root = Path(__file__).resolve().parents[1]
source = (root / "src" / "ui_onboarding.cpp").read_text(encoding="utf-8")
ui_header_source = (root / "src" / "ui_onboarding.h").read_text(encoding="utf-8")
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
board_target_header_source = (root / "src" / "board" / "target.h").read_text(encoding="utf-8")
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
epdiy_patch_source = (root / "tools" / "patch_epdiy_shared_gpio_isr.py").read_text(encoding="utf-8")
testing_workflow_source = (root / ".github" / "workflows" / "testing-firmware.yml").read_text(encoding="utf-8")
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
contains('last_user_activity=millis();\n            if(screen==Screen::Maps){', "primary-button short press counts as user activity")
contains('draw_screen();fast_full_redraw("SHORT_BUTTON_REFRESH",true);', "non-map primary-button refresh keeps the established fast redraw path")
finish_startup_source=source[source.index("void ui_finish_startup()"):source.index("void ui_loop()",source.index("void ui_finish_startup()"))]
assert "const bool retained_deep_wake=setup_complete&&retained_wake_tab_valid;" in finish_startup_source, "deep-sleep startup identifies retained first-frame ownership"
assert '?"RETAINED_TAB_AFTER_DEEP_WAKE":"CONTACTS_AFTER_BOOT"' in finish_startup_source, "retained Channels/More use the same forced complete-frame reveal as Contacts"
assert "load_map_with_feedback(false,retained_deep_wake);" in finish_startup_source, "retained Maps passes full first-frame treatment into its Loading screen"
assert "if(setup_complete)frontlight_event();" in finish_startup_source, "restored screens start a fresh frontlight timeout only after becoming visible"
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
assert "const bool pressed=sample.pressed&&!suppressed_home;" in non_map_sampler, "non-Maps preserves primary press semantics while fully suppressing keyboard Home"
assert "held=false;keyboard_delete_hold=false;keyboard_delete_repeated=false;" in non_map_sampler, "Home must not become ordinary non-Maps tap release"
assert "const bool keyboard_active=keyboard_visible||keyboard_landscape;" in non_map_sampler, "keyboard state gates capacitive Home"
assert "const bool suppressed_home=sample.home&&keyboard_active;" in non_map_sampler, "keyboard-active Home frame is explicitly consumed"
assert "const bool home=sample.home&&!keyboard_active;" in non_map_sampler, "capacitive Home navigation is disabled while either keyboard is active"
assert "const bool pressed=sample.pressed&&!suppressed_home;" in non_map_sampler, "suppressed Home cannot fall through as a normal pressed coordinate"
assert "if(suppressed_home){" in non_map_sampler, "keyboard-active Home frame has a dedicated discard path"
contains("if(keyboard_visible||keyboard_landscape)continue;", "queued Home events are fenced while keyboard is active")
assert "int16_t event_x=last_x,event_y=last_y;" in non_map_sampler, "ordinary non-keyboard UI release position remains the default"
assert "const bool keyboard_touch=!quick_panel_active&&" in non_map_sampler, "keyboard-only release anchoring gate"
assert "if(keyboard_touch){" in non_map_sampler, "all keyboard releases use touch-down ownership"
assert "event_x=start_x;" in non_map_sampler and "event_y=start_y;" in non_map_sampler, "keyboard releases anchor to touch-down"
assert "QueuedTap tap{event_x,event_y,dx,dy,false};" in non_map_sampler, "stabilized non-Maps release event path"
assert "const uint32_t sample_ms=(keyboard_visible||keyboard_landscape)?2:8;" in source, "keyboard sampler uses 2 ms test cadence"
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

# Test10 companion-mode power/logging policy: both steady-state runtimes use
# an 80 MHz cruise clock after initialization. Local UI separately bursts to
# 240 MHz for rendering/display work. SX1262 DIO1 uses one explicit IDF handler
# lifetime in every mode so EPDiy cannot invalidate Arduino IRQ bookkeeping.
# BLE scan response data stays within the legacy 31-byte budget.
assert "COMPANION_CPU_MHZ=80" in companion_source, "BT companion steady-state CPU target is 80 MHz"
assert 'companion_set_low_power_cpu();' in companion_source, "BT companion applies low-power CPU policy"
assert "UI_IDLE_CPU_MHZ=80" in source and "UI_RENDER_CPU_MHZ=240" in source, "local UI uses 80 MHz cruise and 240 MHz render clocks"
assert 'set_cpu_target(UI_IDLE_CPU_MHZ,"ui-ready")' in source, "local UI enters 80 MHz cruise after startup"
assert 'set_cpu_target(UI_IDLE_CPU_MHZ,"wake")' in source, "wake returns to the 80 MHz interactive cruise clock before burst rendering"
assert 'set_cpu_target(ui_post_render_cpu_target(),"display-complete")' in source and 'set_cpu_target(ui_post_render_cpu_target(),"display-area-complete")' in source, "display work returns to the boot-aware or steady-state cruise clock"
assert "set_cpu_target(160" not in source, "local UI no longer idles at 160 MHz"
companion_setup_body = companion_source.split("void companion_setup() {",1)[1].split("void companion_loop()",1)[0]
assert companion_setup_body.index("meshink_board_boot_complete();") < companion_setup_body.index("companion_set_low_power_cpu();"), "companion lowers CPU only after hardware/BLE setup"
assert "board.begin();" not in companion_source and "board.beginLocal();" not in companion_source and "board.onBootComplete();" not in companion_source, "generic runtime must use board lifecycle abstraction"
assert "t5_companion_" not in companion_source, "generic runtime must not call T5-specific companion lifecycle hooks"
local_setup_body = companion_source.split("void local_mesh_setup() {",1)[1]
assert "companion_set_low_power_cpu();" not in local_setup_body, "local UI must not inherit companion CPU policy"
attach_body = board_target_source.split("void attachInterrupt(uint32_t interruptNum",1)[1].split("void detachInterrupt",1)[0]
assert "gpio_isr_handler_add(" in attach_body, "all modes attach SX1262 DIO1 directly to the IDF ISR service"
assert "if(!ensureIsrService(nullptr))" in attach_body, "radio ensures the process-wide ISR service exists before adding its handler"
assert "ArduinoHal::attachInterrupt" not in attach_body, "radio IRQ lifetime must not depend on Arduino hidden bookkeeping"
assert "BLEAdvertisementData scan_response;" in companion_source, "companion supplies bounded custom BLE scan response"
assert "setScanResponseData(scan_response)" in companion_source, "companion overrides overflowing default BLE scan response"
assert "char scan_name[30]" in companion_source, "BLE advertised name is capped to the 29-byte legacy payload name budget"
assert "setShortName(scan_name)" in companion_source and "setName(scan_name)" in companion_source, "BLE scan response marks truncated names as short"
ui_version = re.search(r"-DT5_UI_VERSION='\"([^\"]+)\"'", platformio_source)
firmware_version = re.search(r"-DT5_FIRMWARE_VERSION='\"([^\"]+)\"'", platformio_source)
assert ui_version and firmware_version, "testing UI and firmware versions are explicit in PlatformIO configuration"
assert ui_version.group(1) == firmware_version.group(1), "testing UI and firmware version identifiers must match"
assert re.fullmatch(r"\d+\.\d+\.\d+(?:-test\.\d+|-rc\.\d+|deepsleep\d+)?", firmware_version.group(1)), "firmware version must be stable, a numbered test/RC build, or a numbered deepsleep experiment"

# Status-bar refresh policy: normal UI follows the wall-clock minute while
# standby retains the lower-power five-minute cadence. Event-driven redraws may
# show newer values early but must not postpone the next scheduled boundary.
contains("static int16_t status_bar_painted_minute = -1;", "normal UI tracks the last painted wall-clock minute")
contains("static int16_t status_bar_painted_slot = -1;", "standby tracks the last painted five-minute wall-clock slot")
contains("status_wall_minute!=status_bar_painted_minute", "normal UI queues a status refresh when the visible minute changes")
contains("status_slot!=status_bar_painted_slot", "standby queues only on a new five-minute wall-clock slot")
contains("const bool aligned_status_due=standby_active", "status cadence explicitly branches between standby and normal UI")
contains("if(aligned_status_due)status_bar_dirty=true;", "scheduled wall-clock changes queue the status bar")
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
assert "message_sync_required" in companion_source and "message_sync_inflight" in companion_source, "local receive queue has explicit drain state"
assert "frame[0]=10; // CMD_SYNC_NEXT_MESSAGE" in companion_source, "local receive queue polls CMD_SYNC_NEXT_MESSAGE"
assert "sync-empty: MeshCore receive queue fully drained" in companion_source, "local receive queue drains until explicit empty response"
assert "sleep deferred: received-message queue drain is still pending" in companion_source, "deep sleep waits for message persistence drain"
assert "sync_and_verify_for_deep_sleep" in companion_source, "headless deep sleep verifies message journal durability"
assert "RX direct journal seq=" in runtime_source, "received direct messages log real journal append result"
assert "local_mesh_receive_channel_from_core" in companion_source and "local_mesh_receive_channel_from_core" in runtime_source, "standalone channel messages persist directly from MeshCore's decrypted callback"
channel_callback = companion_source.split("void onChannelMessageRecv(const mesh::GroupChannel& channel",1)[1].split("};",1)[0]
assert "if(companion_mode_active)" in channel_callback and "MyMesh::onChannelMessageRecv(channel,pkt,timestamp,text);" in channel_callback, "BLE companion channel protocol remains upstream-compatible"
assert "local_mesh_receive_channel_from_core" in channel_callback and "return;" in channel_callback, "local channel receive bypasses the companion offline queue"
assert "deep-sleep verify expected seq=" in message_store_source, "journal verification logs expected and reopened durable state"
assert "local_rx_wake_indicator" not in companion_source, "headless RX standby stays dark outside the notification alert"
assert "deep_sleep_diag" not in companion_source and "deep_sleep_diag" not in unified_source and "deep_sleep_diag" not in board_target_source, "retired deep-sleep flash journal has no runtime hooks"
assert "+<deep_sleep_diag.cpp>" not in platformio_source, "retired deep-sleep flash journal is not built"
assert "retained diagnostics will replay" not in unified_source and "service_deep_sleep_probe_replay" not in unified_source, "next boot no longer repeats retained diagnostics"
retained_wake_body = companion_source.split("static bool local_mesh_setup_retained_wake",1)[1].split("bool local_mesh_setup_rx_wake",1)[0]
assert "frontlight" not in retained_wake_body, "retained MeshCore startup must not drive the frontlight"
assert 'set_cpu_target(UI_IDLE_CPU_MHZ,"headless-alert-idle")' in source, "display-only alert returns CPU to the 80 MHz headless cruise"
assert "local_mesh_service_startup();" in source, "final standby redraw services MeshCore while retaining the display session"
assert "headless display session retained through 40s cooldown" in source, "headless display remains initialized for later unread/channel redraws"
assert "return headless_alert_requested||message_alert_active;" in source, "idle initialized headless display does not block deep sleep"
assert "ui_quiesce_display_for_deep_sleep" in source and "panel HV/frontlight off" in source, "deep-sleep cleanup explicitly powers down the physical display while retaining reusable EPDiy state until reset"
sleep_entry = companion_source.split("bool local_mesh_enter_deep_sleep_standby()",1)[1].split("void local_mesh_rx_wake_loop()",1)[0]
assert "ui_quiesce_display_for_deep_sleep();" in sleep_entry, "deepsleep39 powers the display down at the final sleep handoff"
assert sleep_entry.index("ui_quiesce_display_for_deep_sleep();") < sleep_entry.index("meshink_board_enter_deep_sleep_standby()"), "display power-off precedes ESP deep sleep entry"
assert "display untouched" not in sleep_entry, "temporary display-isolation diagnostic is removed"
quiesce_body = source.split("void ui_quiesce_display_for_deep_sleep()",1)[1].split("bool ui_headless_message_alert_pending()",1)[0]
assert "meshink_display_poweroff();" in quiesce_body and "meshink_power_frontlight_set(0);" in quiesce_body, "deep-sleep quiesce turns off panel HV and frontlight"
assert "meshink_display_deinit();" not in quiesce_body, "display session remains initialized through the final race window instead of being torn down"
headless_loop = companion_source.split("void local_mesh_rx_wake_loop()",1)[1].split("bool local_mesh_is_running",1)[0]
assert "alert_was_busy" not in headless_loop and "alert_active" not in headless_loop, "display alert activity must not reset the genuine MeshCore quiet timer"
notify_body = source.split("void ui_notify_message_received(bool channel)",1)[1].split("bool ui_restore_failed_compose",1)[0]
assert "if(!headless_display_session&&!message_alert_active)" not in notify_body and "headless_alert_requested=true;" in notify_body, "messages received during a headless alert stay latched"
assert "if(headless_ui_state)headless_alert_requested=false;" in source, "final GC16 redraw coalesces messages already represented on the standby screen"
assert "message arrived during final standby redraw; restarting headless alert" in source, "messages processed after the final redraw trigger another notification cycle"
assert "meshink_epdiy_existing_gpio_isr_service" in board_target_source, "board runtime exposes live radio ISR ownership to EPDiy"
assert "ensureIsrService" in board_target_source and "gpio_install_isr_service(ESP_INTR_FLAG_EDGE)" in board_target_source, "headless radio path can explicitly create the shared GPIO ISR service"
assert board_target_source.index("if(!ensureIsrService(nullptr))") < board_target_source.index("gpio_isr_handler_add("), "radio HAL installs the global ISR service before adding DIO1 handler"
assert "T5_SPLIT_SLEEP_DIAG" not in platformio_source and "T5_SPLIT_SLEEP_DIAG" not in unified_source, "split deep-sleep diagnostic harness is removed from the cleanup build"
assert "meshink_board_diag_" not in board_target_source and "meshink_board_diag_" not in board_backend_source, "temporary board diagnostic probes are removed"
assert "ui_show_split_sleep_diag" not in source and "ui_show_split_sleep_diag" not in ui_header_source, "temporary diagnostic display screen is removed"
assert "t5_sx1262_raw_capture_wake_packet" in board_target_source, "radio wake copies the retained SX1262 FIFO before RadioLib initialization"
assert "raw wake packet captured BEFORE RadioLib init" in board_target_source, "raw wake capture ordering is explicit in the runtime log"
assert "t5_sx1262_raw_probe" not in board_target_source and "meshink_board_probe_deep_sleep_radio" not in board_target_source, "unused retained-radio probe scaffolding is removed while raw wake capture remains"
resume_body=board_target_source.split("static bool radio_resume_retained(bool packet_wake)",1)[1].split("bool radio_resume_rx_wake()",1)[0]
assert resume_body.index("t5_sx1262_raw_capture_wake_packet") < resume_body.index("radio.std_init(&radio_spi)"), "retained FIFO is copied before normal RadioLib/SX1262 initialization"
assert "resetOnStartup=false" not in resume_body and "radio_driver.begin()" not in resume_body, "wake capture no longer relies on Heltec-style retained RadioLib state"
assert "radio.resetOnStartup=true;" in resume_body and "radio.std_init(&radio_spi)" in resume_body, "each wake rebuilds SX1262 and RadioLib from the normal reset path"
assert "0x12" in board_target_source and "0x13" in board_target_source and "0x14" in board_target_source and "0x1E" in board_target_source, "raw wake capture reads IRQ, RX buffer metadata, packet status and FIFO directly"
retained_setup=companion_source.split("static bool local_mesh_setup_retained_wake(bool require_packet,const char* reason)",1)[1].split("bool local_mesh_setup_rx_wake()",1)[0]
assert 'if(require_packet)board.finishLocalRxWakeCapture();' in retained_setup, "RX_PACKET startup reason remains set until Dispatcher has called the radio wrapper begin"
assert retained_setup.index("the_mesh.begin(true);") < retained_setup.index("if(require_packet)board.finishLocalRxWakeCapture();"), "saved-packet ready flag is latched before startup reason is cleared"
assert "esp_sleep_enable_ext1_wakeup" in board_target_source and "ESP_EXT1_WAKEUP_ANY_HIGH" in board_target_source, "production SX1262 DIO1 wake remains EXT1 ANY_HIGH"
assert "return radio_gpio_irq_handler_active;" in board_target_source, "EPDiy sharing follows the actual SX1262 DIO1 handler lifetime"
assert "MESHINK_SHARED_GPIO_ISR_PATCH_V3" in epdiy_patch_source, "build carries deterministic EPDiy shared-ISR V2 patch"
assert "meshink_radio_gpio_isr" in epdiy_patch_source and "&& !meshink_radio_gpio_isr" in epdiy_patch_source, "EPDiy teardown preserves a live radio handler even when EPDiy created the service first"
assert "meshink_epdiy_owns_gpio_isr_service" in epdiy_patch_source, "EPDiy patch tracks global ISR ownership"
assert "meshink_epdiy_note_gpio_isr_service" in epdiy_patch_source and "gpio_isr_service_ready" in board_target_source, "EPDiy and radio share explicit global ISR-service installed state"
assert "if(gpio_isr_service_ready)return true;" in board_target_source, "radio skips duplicate gpio_install_isr_service calls when EPDiy already owns the service"
assert "gpio_isr_handler_remove(CFG_INTR)" in epdiy_patch_source, "EPDiy teardown removes only its own interrupt handler"
assert "if (meshink_epdiy_owns_gpio_isr_service && !meshink_radio_gpio_isr)" in epdiy_patch_source, "EPDiy uninstalls its service only when no live radio DIO1 handler depends on it"
assert platformio_source.count("pre:tools/patch_epdiy_shared_gpio_isr.py") == 2, "all EPDiy firmware targets apply the shared-ISR patch"
assert "if(!deep_sleep_standby)draw_status_bar();" in source, "deep-sleep standby omits the normal status bar"
assert '"DEEP SLEEP STANDBY"' in source, "deep-sleep standby visibly identifies its power state"
assert "esp_sleep_enable_timer_wakeup" in board_target_source and "60ULL*60ULL*1000000ULL" in board_target_source, "deep-sleep standby has an hourly battery timer wake"
assert "meshink_board_return_to_retained_deep_sleep" in unified_source, "timer and short-button wakes preserve retained SX1262 state when re-sleeping"
assert "struct MeshInkUiStartupPlan" in ui_header_source, "UI startup exposes configurable modular steps"
assert "plan.radio_settle" in source and "plan.splash" in source and "plan.touch" in source, "UI startup plan independently gates radio settle, splash and touch"
assert "ui_service_headless_message_alert" in companion_source and "headless message alert display session started" in source, "headless messages can temporarily initialize only the display alert path"
assert 'pre-alert battery check' in companion_source and 'pre-UI battery check' in companion_source, "deep-sleep display promotion paths perform an extra voltage guard"
assert "ui_display_session_active()" in companion_source, "battery guard detects any already-owned EPDiy I2C bus"
power_check_body = companion_source.split("static MeshInkPowerSleepCheck local_mesh_headless_power_check",1)[1].split("static void companion_set_low_power_cpu",1)[0]
assert power_check_body.index("ui_display_session_active()") < power_check_body.index("meshink_power_begin_minimal_bus()"), "normal or headless EPDiy I2C is reused before attempting another driver install"
assert "local_mesh_setup_button_wake" in unified_source and "local_mesh_promote_to_ui" in unified_source, "BOOT wake restores retained MeshCore before attaching full UI"
assert "bool i2c_ready_ = false;" in board_target_header_source, "RTC tracks whether its shared I2C lifecycle has actually started"
assert "uint32_t deferred_hardware_time_ = 0;" in board_target_header_source, "RTC retains one deferred bootstrap timestamp until interactive I2C exists"
assert "i2c_ready_=true;" in board_target_source, "RTC marks I2C ready only from interactive/cold RTC begin"
assert "deferred_hardware_time_=utc;" in board_target_source and "deferred hardware write" in board_target_source, "headless MeshCore time bootstrap records a deferred hardware update without touching absent I2C"
assert "rtc=PCF8563 restored from deferred startup time" in board_target_source, "invalid RTC is repaired from deferred startup time only after I2C returns"
assert board_target_source.index("settimeofday(&tv,nullptr);") < board_target_source.index("if(!i2c_ready_){"), "headless RTC fallback still keeps software time current"
runtime_promotion_body = companion_source.split("bool local_mesh_promote_to_ui(const char* source)",1)[1].split("bool local_mesh_enter_deep_sleep_standby()",1)[0]
assert "meshink_power_frontlight_begin();" in runtime_promotion_body and "meshink_power_frontlight_set(100);" in runtime_promotion_body, "awake/headless BOOT promotion immediately acknowledges with full frontlight"
assert runtime_promotion_body.index("meshink_power_frontlight_set(100);") < runtime_promotion_body.index("local_mesh_headless_power_check"), "awake BOOT acknowledgement precedes promotion battery/startup work"
deep_button_body = unified_source.split("if(button_wake){",1)[1].split("if(radio_wake){",1)[0]
assert "meshink_power_frontlight_begin();" in deep_button_body and "meshink_power_frontlight_set(100);" in deep_button_body, "deep-sleep BOOT hold immediately acknowledges with full frontlight"
assert deep_button_body.index("meshink_power_frontlight_set(100);") < deep_button_body.index("local_mesh_setup_button_wake()"), "deep-sleep BOOT acknowledgement precedes retained MeshCore restoration"
promotion_body = source.split("bool ui_promote_headless_to_interactive()",1)[1].split("void ui_prepare_headless_rx_wake()",1)[0]
assert "meshink_power_frontlight_set(100);" in promotion_body and "frontlight_lit=true;" in promotion_body, "interactive promotion keeps the BOOT acknowledgement lit through startup"
assert "meshink_power_frontlight_set(0);" not in promotion_body, "interactive promotion must not extinguish accepted BOOT feedback"
assert "plan.sample_status=false;" in promotion_body, "retained promotion avoids a premature --:-- status sample"
assert "plan.display=!reuse_display;" in promotion_body, "retained promotion reuses an existing EPDiy session instead of initializing it twice"
assert "ui_close_headless_display_session();" not in promotion_body, "BOOT promotion must not deinit/reinit EPDiy high-level singleton state"
assert "interactive promotion reusing initialized EPDiy session" in promotion_body, "display-session transfer is observable in retained BOOT logs"
assert "local_mesh_prepare_interactive_services();" in promotion_body, "retained promotion starts skipped RTC/GPS services without restarting radio"
assert promotion_body.index("local_mesh_prepare_interactive_services();") < promotion_body.index("ui_mesh_ready();"), "RTC/GPS are ready before first interactive status sample"
assert "map_tiles_warm_storage();" in promotion_body and promotion_body.index("map_tiles_warm_storage();") < promotion_body.index("ui_finish_startup();"), "retained promotion warms map storage before revealing UI"
assert "meshink_gps_prepare_runtime();" in companion_source and "retained UI promotion UART ready" in board_target_source, "retained promotion opens the GNSS UART skipped by radio-first wake"
assert "restarting into full UI boot" not in companion_source, "headless BOOT promotion must not restart the ESP32"
assert "meshink_radio_resume_retained_wake" in companion_source, "button wake uses retained-radio recovery before UI startup"
assert "meshink_board_restore_deep_sleep_wake_pads();" in unified_source, "deep-sleep wake restores EXT0/EXT1 RTC pads to normal digital GPIO"
wake_restore_body = board_target_source.split("void meshink_board_restore_deep_sleep_wake_pads()",1)[1].split("void meshink_board_prepare_retained_aux_wake()",1)[0]
assert "rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1)" in wake_restore_body, "DIO1 is detached from RTC IO before RadioLib GPIO ISR reuse"
assert "rtc_gpio_deinit((gpio_num_t)T5_PIN_BOOT_BUTTON)" in wake_restore_body, "BOOT is detached from RTC IO before digital button handling"
assert wake_restore_body.index("rtc_gpio_deinit((gpio_num_t)P_LORA_DIO_1)") < wake_restore_body.index("pinMode(P_LORA_DIO_1,INPUT)"), "DIO1 RTC mux is released before digital pinMode"
wake_source_body = board_target_source.split("static bool t5_enable_deep_sleep_wake_sources()",1)[1].split("bool meshink_board_enter_deep_sleep_standby()",1)[0]
assert "esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)" in wake_source_body, "each repeated deep-sleep cycle rebuilds wake sources from a clean RTC configuration"
assert wake_source_body.index("esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL)") < wake_source_body.index("esp_sleep_enable_ext1_wakeup"), "stale wake sources are cleared before DIO1 EXT1 is re-armed"
assert "meshink_board_begin_local_rx_wake(require_packet);" in companion_source, "retained startup passes packet-wake state into the board startup reason"
board_rx_wake_body = board_target_source.split("void T5Board::beginLocalRxWake(bool packet_wake)",1)[1].split("static bool t5_probe_deep_sleep_radio",1)[0]
assert "BD_STARTUP_RX_PACKET" in board_rx_wake_body and "BD_STARTUP_NORMAL" in board_rx_wake_body, "T5 retained startup mirrors upstream MeshCore board startup-reason handoff"
assert "packet_wake?BD_STARTUP_RX_PACKET:BD_STARTUP_NORMAL" in board_rx_wake_body, "only radio wakes advertise an already-received packet"
assert "stageWakePacket" in board_target_source and "wake_packet_" in board_target_header_source, "deepsleep39 stages the wake packet in ESP RAM before resetting the SX1262"
assert "MeshInkSX1262Wrapper::recvRaw" in board_target_source, "deepsleep39 replays the staged wake packet through the MeshCore radio wrapper"
retained_resume_body = board_target_source.split("static bool radio_resume_retained",1)[1].split("bool radio_resume_rx_wake",1)[0]
assert "t5_sx1262_raw_capture_wake_packet" in retained_resume_body, "packet wake captures the retained FIFO without invoking fresh RadioLib state"
assert retained_resume_body.index("t5_sx1262_raw_capture_wake_packet") < retained_resume_body.index("radio.std_init(&radio_spi)"), "saved packet is secured before the clean hardware-resetting reinit"
assert "radio.resetOnStartup=false;" not in retained_resume_body and "captureRetainedWakePacket" not in retained_resume_body, "Heltec-style retained RadioLib reconstruction is removed from production wake"
assert "radio.getIrqFlags()" not in retained_resume_body and "radio.getRSSI()" not in retained_resume_body and "radio.getSNR()" not in retained_resume_body, "no RadioLib read is attempted before normal std_init on deep-sleep wake"
assert "radio.resetOnStartup=true;" in retained_resume_body and "radio.std_init(&radio_spi)" in retained_resume_body, "wake returns SX1262 and RadioLib to a clean initialized baseline after raw capture"
sleep_entry_body = board_target_source.split("bool meshink_board_enter_deep_sleep_standby()",1)[1].split("bool meshink_board_return_to_retained_deep_sleep()",1)[0]
assert "digitalRead(P_LORA_BUSY)==HIGH" in sleep_entry_body, "sleep entry checks BUSY non-destructively instead of issuing a radio command"
assert "digitalRead(P_LORA_DIO_1)==HIGH" in sleep_entry_body, "sleep entry rejects a pending RX IRQ rather than disturbing it"
assert "radio.startReceive()" in sleep_entry_body, "deepsleep39 re-arms SX1262 RX/DIO1 mapping at every sleep boundary"
assert "1ULL<<P_LORA_DIO_1,ESP_EXT1_WAKEUP_ANY_HIGH" in board_target_source, "deepsleep39 keeps the known-good DIO1 EXT1 wake assignment"
assert "(gpio_num_t)T5_PIN_BOOT_BUTTON,0" in board_target_source, "deepsleep39 keeps BOOT EXT0 LOW"
assert "raw wake packet captured BEFORE RadioLib init" in board_target_source, "wake packet is copied from retained SX1262 FIFO before any RadioLib initialization"
assert "clean SX1262/RadioLib init complete saved-packet=" in board_target_source, "wake path deliberately returns radio hardware and RadioLib to a clean baseline after capture"
assert "injected saved wake packet after clean radio reset" in board_target_source, "captured wake packet is replayed into MeshCore after clean reinit"
assert "rtc_gpio_hold_dis((gpio_num_t)P_LORA_DIO_1)" in board_target_source, "wake restoration explicitly releases any RTC DIO1 hold"
assert "t5_prepare_retained_sx1262_transport" not in board_target_source, "unsafe partial RadioLib transport reconstruction stays removed"
assert 'minimal_battery_check("cold-boot"' in unified_source, "cold boot performs battery guard before full UI/MeshCore startup"
assert 'minimal_battery_check("deep-timer"' in unified_source, "timer wake performs minimal battery-only guard"
assert "critical check first=" in power_backend_source and "threshold=%umV" in power_backend_source, "battery guard logs both voltage samples and threshold"
assert "meshink_power_low_battery_latched" not in power_backend_source and "low_latch" not in power_backend_source, "low-battery policy must not persist a latch across recovery"
assert "ui_minimal_low_battery_shutdown" in source and "minimal low-battery shutdown" in source, "critical deep-sleep battery path uses minimal persistent EPD notice"
minimal_low_battery_body=source[source.index("[[noreturn]] void ui_minimal_low_battery_shutdown"):source.index("static void critical_battery_shutdown",source.index("[[noreturn]] void ui_minimal_low_battery_shutdown"))]
assert minimal_low_battery_body.index("meshink_display_deinit();") < minimal_low_battery_body.index("meshink_power_begin_minimal_bus()") < minimal_low_battery_body.index("meshink_power_enter_ship_mode(MeshInkPowerOffReason::LowBattery)"), "minimal low-battery path restores shared I2C after EPDiy teardown before BATFET ship command"
assert unified_source.count('meshink_wireless_force_local_radios_off()') == 4, "local wireless policy is enforced for normal pre/post MeshCore startup plus retained radio/button wake paths"
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
contains("if(tap.dy>0){", "message history swipes down to older pages")
contains("if(page>0)draw_page_arrow(text_left-ui_w(24),y+ui_h(7),true);", "message history newer-page hint points up")
contains("if(has_older)draw_page_arrow(text_left+text_width+ui_w(24),y+ui_h(7),false);", "message history older-page hint points down")
assert "int next=(int)page+(tap.dy<0?1:-1);" in source, "generic list paging keeps its original swipe direction"
assert "int next=(int)details_page+(tap.dy<0?1:-1);" in source, "Node Info paging keeps its original swipe direction"
contains("chat_page_bounds_lazy(count,chat_history_available_current(),", "conversation paging starts with the taskbar-aware current-page height")
contains("chat_history_available_paged(),", "conversation paging gives older pages their larger taskbar-free height")
contains("draw_chat_page_indicator(chat_page,has_older,layout.height-ui_h(38));", "older history page indicator uses the reclaimed lower screen area")
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
contains("Radio tower: tapered mast plus two signal arcs", "Contacts and Discovery repeater icon uses the radio-tower glyph")
contains('case (uint8_t)UiNodeRole::Room:return "ROOM SERVER";', "Room Server role label")
contains("Simple house silhouette: peaked roof", "Contacts and Discovery room-server icon uses the house glyph")
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
contains("const int path_lines=min(3,ui_wrapped_line_count(node.path,layout.section_width,3));", "received path text line count drives following layout")
contains("const int trace_heading_y=max(", "trace heading moves down when the discovered path uses all three lines")
contains("const int trace_lines=min(9,ui_wrapped_line_count(node.trace,layout.section_width,2));", "trace results reserve their actual wrapped height")
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
contains("if(disabled)draw_status_bold_line(x+3,y+3,x+27,y+27,3,0);", "bold GPS-off slash")
contains("rounded_fill(x,y+5,30,22,5,0);", "bold rounded envelope frame")
contains("draw_status_bold_line(x+4,y+9,x+15,y+18,2,0);", "bold envelope flap")
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
contains('fast_full_redraw("MAP_LOADING_AFTER_DEEP_WAKE",false);', "retained Maps forces the completed Loading frame over the physical standby image")
contains('else\n        refresh(MeshInkRefreshMode::Direct);', "ordinary first Maps entry retains the existing full Loading refresh")
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
assert "static void ui_draw_wrapped_tail(" in source, "bounded tail rendering remains available for non-message keyboard entry"
assert "static void ui_draw_compose_tail(" in source, "message composer has a dedicated clipped tail renderer"
assert source.count("ui_draw_compose_tail(")>=3, "portrait and landscape message entry both use the clipped compose tail renderer"
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

# Temporary performance/touch instrumentation is removed after field tuning.
contains("uint32_t queued_at_ms=0;", "queued touch timestamps remain for stale-navigation filtering")
assert "T5InputTimingScope" not in source and "t5_timing_" not in source, "UI timing instrumentation is removed"
assert "[T5-TOUCH] tap screen=" not in source, "temporary touch-coordinate logging is removed"
assert "T5_LOG_TOUCH" not in source and "T5_LOG_TOUCH" not in platformio_source, "keyboard/touch diagnostic logging is removed from the application build"

# Local UI framebuffer composition uses short 240 MHz bursts from the 80 MHz
# cruise clock, restoring the previous clock immediately afterwards.
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-draw");', "full UI drawing temporarily boosts CPU")
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-message-entry-draw");', "keyboard text redraw temporarily boosts CPU")
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-radio-name-draw");', "name-entry redraw temporarily boosts CPU")
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-status-draw");', "standalone status-bar composition temporarily boosts CPU")
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-quick-panel-draw");', "Quick Settings composition temporarily boosts CPU")
contains('MeshInkCpuBoostScope draw_cpu_boost(!standby_active,"ui-toast-draw");', "standalone toast composition temporarily boosts CPU")
contains('set_cpu_target(previous_mhz,"ui-draw-complete");', "UI draw boost restores previous CPU clock")
assert 'set_cpu_target(UI_RENDER_CPU_MHZ,"display-refresh")' in source and 'set_cpu_target(UI_RENDER_CPU_MHZ,"display-area-refresh")' in source, "e-paper updates still run at 240 MHz"
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
contains('const uint32_t sample_ms=(keyboard_visible||keyboard_landscape)?2:8;', "keyboard touch sampler uses 2 ms cadence for rapid repeated letters")
contains("ui_draw_compose_tail(compose_text,text_x,text_y,text_width,text_height,3);", "message composer uses bottom-tail renderer")
contains("const int multiline_lift=max(8,glyph_height/3);", "wrapped composer descenders are lifted clear of the lower field edge")
contains("constexpr int caret_width=2;", "message composer caret is two pixels wide")
contains("const int caret_height=min(max_height-4,max(14,(glyph_height*3)/2));", "message composer caret extends beyond character height")
contains("const int caret_y=max(y+2,min(y+max_height-2-caret_height,centred_caret_y));", "message composer caret stays inside the entry viewport")
contains("meshink_display_fill_rect({caret_x,caret_y,caret_width,caret_height},0,fb);", "message composer draws the enlarged subtle caret")
contains("keyboard_delete_repeat_at=pressed_at+350;", "message delete hold delay")
contains("keyboard_delete_repeat_at=millis()+45;", "message delete repeat cadence")
contains("keyboard_delete_hold&&keyboard_delete_repeated", "repeated delete suppresses release double-delete")
contains('"ZOOM %u (%s)"', "map displays compact source badge beside zoom")
assert 'has_pmtiles_magic' in map_source, "cache64 archive scan recognizes PMTiles v3 header"
assert 'archive-scan entry=%s dir=%u base=%s' in map_source, "archive scan logs cache64 directory enumeration"
assert 'archive-scan file=%s suffix=%u header=%u' in map_source, "archive scan reports suffix and PMTiles header detection"
board_source = (root / "src" / "board" / "target.cpp").read_text(encoding="utf-8")
assert "class T5RadioHal final : public ArduinoHal" in board_source, "radio uses custom HAL to share EPDiy GPIO ISR service"
assert "delay(1);" in companion, "Bluetooth companion loop must yield so cache64 watchdog does not starve IDLE1"
assert "gpio_isr_handler_add(" in board_source and "gpio_install_isr_service(ESP_INTR_FLAG_EDGE)" in board_source, "radio directly owns an IDF DIO1 handler and creates the global service when needed"
assert "ArduinoHal::attachInterrupt" not in board_source, "retained radio IRQ lifetime must not depend on Arduino hidden interrupt bookkeeping"
assert "meshink_board_service_asserted_radio_irq" in board_source and "DIO1 level recovery" in board_source, "asserted DIO1 has a polling recovery path when an edge is missed"
assert "meshink_board_service_asserted_radio_irq();" in headless_loop, "headless runtime polls asserted DIO1 before every MeshCore pass"

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
contains('"HOLD %s FOR TWO SECONDS"', "standby wake wording includes FOR and explicit two-second hold")
contains('ui_centred("TO WAKE",ui_y(892),3,0,true);', "standby wake wording is completed on the second smooth line")

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

# External-frame safety remains, while temporary geometry/touch field logging is gone.
assert "DISCOVERED_CONTACT_BASE_LEN" in runtime_source, "discovered advert parser must define a complete base frame length"
assert 'len<DISCOVERED_CONTACT_BASE_LEN' in runtime_source, "truncated discovered adverts must be rejected"
assert '*slot=DiscoveredContact{};' in runtime_source, "discovered advert cache must clear stale optional bytes"
assert 'detail_contact_=ContactInfo{};' in runtime_source, "Node Info advert parse must start from zeroed contact state"
assert "[T5-MESH] rejected malformed new-advert frame" in runtime_source, "malformed advert rejection must remain observable"
assert "audit_ui_geometry" not in source and "[T5-GEOM]" not in source, "temporary geometry self-audit is removed"
assert "[T5-TOUCH] tap screen=" not in source, "temporary touch coordinate logging is removed"
assert "-DMESHINK_GEOMETRY_DIAGNOSTICS=1" not in cache64_build_flags, "release cache64 build does not force geometry diagnostics"
assert "-DMESHINK_TOUCH_DIAGNOSTICS=1" not in cache64_build_flags, "release cache64 build does not force backend touch diagnostics"
assert "-DT5_LOG_UI=1" not in cache64_build_flags and "-DT5_LOG_MAP=1" not in cache64_build_flags, "release cache64 build keeps optional verbose logging disabled"

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
ui_startup_overlap=source[source.index("void ui_startup(const MeshInkUiStartupPlan& plan)"):source.index("void ui_setup()")]
assert ui_startup_overlap.index("meshink_display_init();") < ui_startup_overlap.index("meshink_board_start_local_radio_settle();"), "early radio power begins immediately after display board init when that startup step is enabled"
assert ui_startup_overlap.index("meshink_board_start_local_radio_settle();") < ui_startup_overlap.index("set_ui_orientation"), "UI startup plan starts rail before framebuffer/preferences/splash work"
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
radio_init_body=board_target_source.split("bool radio_init()",1)[1]
tcxo_at=radio_init_body.index("radio.setTCXO(LILYGO_TCXO_VOLTAGE)")
rf_switch_at=radio_init_body.index("radio.setDio2AsRfSwitch(true)")
std_init_at=radio_init_body.index("ready=radio.std_init(&radio_spi)")
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
assert "meshink_board_has_gps()" in source and "meshink_board_name()" not in source, "UI keeps functional GPS capability checks but removes diagnostic board-name queries"
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
assert "MeshInkGpsConstellationMode" in gps_types_source and "MeshInkGpsPowerExperiment" in gps_types_source, "GPS tuning and power-test vocabulary are board independent"
assert '#include "hardware/gps.h"' in runtime_source, "local runtime must use generic GPS surface"
assert '#include "hardware/gps.h"' in companion_source, "companion runtime must use generic GPS surface"
assert "meshink_gps_background_tick();" in runtime_source and "meshink_gps_power_test_tick();" in runtime_source, "GPS background and measured power-test servicing route through generic backend"
assert "meshink_gps_shutdown();" in runtime_source and "meshink_gps_shutdown();" in companion_source, "GPS shutdown routes through generic backend"
assert "Serial1" not in runtime_source and "Serial1" not in companion_source and "Serial1" not in source, "application code must not own the GPS UART"
assert "t5_gps_" not in runtime_source and "t5_gps_" not in companion_source and "t5_gps_" not in source, "application code must not call T5-specific GPS APIs"
assert "L76K" not in runtime_source and "L76K" not in source, "receiver model details must stay in the board GPS implementation"
for receiver_command in ("PCAS02","PCAS03","PCAS04","PCAS10","PCAS12"):
    assert receiver_command not in runtime_source and receiver_command not in source, "receiver command syntax must stay in the board GPS implementation"
assert "MeshInkGpsPowerExperiment::GpsOnly" in source and "MeshInkGpsPowerExperiment::RfOffUartHighImpedance" in source, "GPS power page uses explicit board-independent experiment selections"
assert '"REPLAY LAST LOG"' in source and "gps_power_page" in source, "GPS power experiments are paged explicit choices with retained-log replay"
assert "local_mesh_gps_enter_standby_power_mode();" in source and "local_mesh_gps_leave_standby_power_mode();" in source, "both standby entry and normal wake route through GPS single-system power policy"
assert "meshink_gps_power_test_start" in t5_gps_backend_source and "meshink_gps_enter_standby_power_mode" in t5_gps_backend_source, "T5 GPS backend exposes measured experiment and standby hooks"
assert "gps_power_touch_suspended" in source and "set_touch_power(false);" in source, "GPS power test disables touch during its measured window"
assert "gps_power_measurement_quiet" in source and "if(!gps_power_measurement_quiet)service_message_alert();" in source, "GPS power test suppresses display/message refresh activity while measuring"
assert "UI quiet mode ended: touch restored after measurement" in source, "GPS power measurement automatically restores interactive UI"
assert "GPS_POWER_BASELINE_MS = 30000" in board_target_source and "GPS_POWER_POST_MS = 60000" in board_target_source, "GPS experiments retain 30-second baseline and 60-second post windows"
assert "gps_power_prepare_baseline();" in board_target_source and '"PCAS04,3"' in board_target_source, "GPS power experiments normalize to a common dual-system baseline"

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
assert "map_marker_hits[count++]={(int16_t)sx,(int16_t)sy,i,node.node_type};" in source, "Maps reuses compact projected marker storage and carries node role"
assert "struct Bounds {int16_t x,y,w,h;};" in source, "Maps collision bounds use compact 16-bit coordinates"
assert "RankedLabel ranked[50]" in source, "Maps ranks labels without copying full node records"
assert "for(uint8_t candidate=0;candidate<12;++candidate)" in source, "Maps tries the bounded twelve-position label solver"
assert "Protect every true node position" in source, "Map labels protect all node markers from coverage"
assert "const size_t label_budget=" in source and "compact_labels=map_zoom<=10" in source, "Maps deliberately thins labels at wide zooms"
assert "draw_map_repeater_marker(n.x,n.y)" in source, "Repeater nodes use the dedicated tower marker"
assert "Two bold broadcast arcs per side" in source and "meshink_display_fill_rect({x-13,y-14,27,29}" in source, "Map repeater marker uses the larger bold separated-wave tower glyph"
assert "own_marker_reserved" in source and "own_marker_x+22" in source and "own_marker_y+22" in source, "Map labels reserve the own-location bullseye footprint"
assert "const int radius=m.node_type==(uint8_t)UiNodeRole::Repeater?14:9;" in source, "Label solver protects the enlarged repeater marker"
assert "thick_line(n.x,n.y,target_x,target_y);" in source, "Displaced map labels keep a three-pixel leader to their node"
assert "map_marker_hit_count=count;" in source, "Maps publishes projected marker hit count after drawing"
assert "uint8_t node_type=0;" in data_source, "Map node data carries the MeshCore role"
assert "item.node_type=positioned.type;" in runtime_source, "Saved map contacts expose their role"
assert "item.node_type=contact->type;" in runtime_source, "Telemetry-only map contacts expose their role"

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

# Test41: five-minute status cadence is standby-only; normal UI updates each minute.
assert "status_wall_minute!=status_bar_painted_minute" in source, "normal UI clock updates each wall-clock minute"
assert "status_slot!=status_bar_painted_slot" in source, "standby keeps five-minute clock cadence"

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

# Test44: field standby redesign uses the full-size MeshInk bitmap and only
# shows unread summary cards that contain unread messages.
assert "const int logo_top=any_unread?ui_y(70):ui_y(165);" in source and "draw_meshink_logo(logo_top,false);" in source, "standby lowers the full-size MeshInk logo only when there are no unread cards"
assert 'ui_centred("STANDBY",standby_state_y,5,0,true);' in source and '"DEEP SLEEP STANDBY"' in source, "standby keeps the large STANDBY identity and adds dedicated deep-sleep identity"
assert "has_direct=status_unread>0" in source and "has_channel=status_channel_unread>0" in source, "standby hides empty unread categories"
assert "const int centred_x=(portrait_layout().width-ui_w(244))/2;" in source, "single standby unread card is centred"
assert "ui_rect(20,445,244,310)" in source and "ui_rect(276,445,244,310)" in source, "dual unread cards retain their side-by-side geometry"
assert "draw_standby_envelope_icon" in source, "standby provides a dedicated large envelope icon"
assert "draw_standby_channel_icon" in source, "standby provides a dedicated large channel people icon"
assert "standby_centred(direct,direct_rect,direct_rect.y+ui_h(125),11)" in source, "private unread count is oversized"
assert "standby_centred(channel,channel_rect,channel_rect.y+ui_h(125),11)" in source, "channel unread count is oversized"
assert "rounded_box(rect,max(ui_w(22),ui_h(22)),false);" in source, "standby summary cards use the shared rounded visual language"
assert 'standby_centred("PRIVATE"' in source and 'standby_centred("CHANNEL"' in source, "standby cards retain clear private/channel labels"
assert "ui_y(830)" in source and "HOLD %s FOR TWO SECONDS" in source and 'ui_centred("TO WAKE",ui_y(892),3,0,true);' in source, "standby uses a lower divider and larger two-line smooth wake instruction"

# Test44b: unread state is journal authority without growing the v3 record.
assert "MESHINK_MESSAGE_UNREAD" in message_store_header and "MESHINK_MESSAGE_READ_THROUGH" in message_store_header, "v3 record flags persist unread arrival and read-through boundary"
assert "static_assert(sizeof(MeshInkStoredMessage)==188" in message_store_source, "persistent unread reuses existing flags without growing the record cache"
assert "if(unread)item.flags|=MESHINK_MESSAGE_UNREAD;" in message_store_source, "incoming unread state is committed in the same append transaction"
assert "MeshInkMessageStore::mark_read_through" in message_store_source and "MESHINK_MESSAGE_READ_THROUGH" in message_store_source, "opening a conversation persists a single read-through marker"
assert "rebuild_unread_from_journal()" in runtime_source and "unread restored direct=" in runtime_source, "RAM unread counters are derived from journal state at startup"
assert "MAX_UNREAD_PEERS=MAX_CONTACTS" in runtime_source and "direct_unread_[MAX_UNREAD_PEERS]" in runtime_source, "small unread cache covers configured contacts rather than only the 16 visible rows"
assert "direct_unread_count" in runtime_source and "direct_unread_peer(item.key,false)" in runtime_source and "direct_unread_peer(item.key,true)" in runtime_source, "journal replay uses read-only lookup and allocates peers only when unread state needs storage"
assert "if(auto* peer=direct_unread_peer(item.key,false))*peer=UnreadPeer{};" in runtime_source, "read-through markers release zero-count peer slots"
assert "ui_status_set_unread(direct_total);" in runtime_source and "ui_status_set_channel_unread(channel_total);" in runtime_source, "journal-derived peer/channel counters drive global status totals"
assert 'getUShort("unread_dm"' not in source and 'getUShort("unread_ch"' not in source, "legacy NVS aggregate unread values are no longer an authority"
unread_load_body=source[source.index("static void ui_load_persistent_state()"):source.index("void ui_startup(const MeshInkUiStartupPlan& plan)")]
assert "status_unread=0;" not in unread_load_body and "status_channel_unread=0;" not in unread_load_body, "ordinary UI preference loading must preserve journal-restored unread totals during the first headless alert"
headless_prepare_body=source[source.index("void ui_prepare_headless_rx_wake()"):source.index("static void ui_load_persistent_state()")]
assert "status_unread=0;" in headless_prepare_body and "status_channel_unread=0;" in headless_prepare_body, "retained RX wake still starts unread totals clean before journal replay"
headless_alert_body=source[source.index("bool ui_service_headless_message_alert()"):source.index("bool ui_promote_headless_to_interactive()")]
assert "MeshInkUiStartupPlan plan{};" in headless_alert_body and "ui_startup(plan);" in headless_alert_body, "first headless alert still initializes the display through normal UI startup"
assert "plan.load_state=false;" not in headless_alert_body, "first headless alert may load ordinary UI preferences without erasing journal unread totals"
assert "status_unread++" not in source and "status_channel_unread++" not in source, "UI notification code cannot maintain a second unread counter"
assert "mark_read(MessageKind::Direct" in runtime_source and "mark_read(MessageKind::Channel" in runtime_source, "opening direct/channel conversations persists read state instead of only zeroing RAM"
assert "path_len,unread)" in runtime_source, "RX append carries unread state directly into the durable record"
assert "if(sequence&&journal_full)rebuild_unread_from_journal();" in runtime_source, "ring overwrite reconciles derived unread counters with the 250-record journal"

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
assert 'settings_row("DIAGNOSTICS","Live MeshCore radio stats",650)' in source, "More exposes diagnostics with sentence-case subtitle"
assert "local_mesh_request_diagnostics()" in source and "draw_diagnostics()" in source, "diagnostics UI requests and renders live MeshCore stats"
assert "frame[2]={56,type}" in runtime_source, "diagnostics uses upstream CMD_GET_STATS"
assert "PACKETS RX/TX %lu / %lu" in runtime_source and "AIRTIME TX/RX %lu / %lu S" in runtime_source, "diagnostics decodes packet and radio counters"
assert 'settings_row("HELP","Using MeshInk",780)' in source, "Help moves below Diagnostics without overlapping bottom navigation"

# Test46: a message received while its conversation is visibly open is already
# seen and must not create contact/channel or bottom-tab unread dots.
assert "bool ui_chat_is_visible(bool channel)" in source, "UI exposes a single visible-chat predicate for unread handling"
assert "return !standby_active&&(channel?screen==Screen::ChannelChat:screen==Screen::ContactChat);" in source, "visible chat excludes standby and distinguishes private/channel chats"
assert "ui_chat_is_visible(false)&&!active_channel_&&!memcmp(active_key_,key,6)" in runtime_source, "matching visible private chat suppresses provider unread increment"
assert "ui_chat_is_visible(true)&&active_channel_&&active_key_[0]==channel" in runtime_source, "matching visible channel chat suppresses provider unread increment"
assert runtime_source.count("const bool unread=!already_seen;")>=2, "private/channel unread state is derived from the visible-chat predicate"
assert "if(auto* peer=direct_unread_peer(key,true))" in runtime_source and "if(peer->count<255)++peer->count;" in runtime_source, "private RAM unread cache increments only after a durable unseen RX append"
assert "if(unread&&channel<MAX_UI_CHANNELS&&channel_unread_[channel]<255)" in runtime_source and "++channel_unread_[channel];" in runtime_source, "channel RAM unread cache increments only after a durable unseen RX append"
assert "const bool visible=ui_chat_is_visible(channel);" in source, "message notification retains the visible-chat predicate without owning unread counts"


# Test47: LAST HEARD uses MeshCore's per-contact lastmod (our T5 clock), while
# LAST ADVERT remains the remote advertisement timestamp.
assert "format_last_heard(contact.lastmod,heard)" in runtime_source, "contact list LAST HEARD must use MeshCore lastmod"
assert 'snprintf(item.subtitle,sizeof(item.subtitle),"%s  HEARD %.43s",role,heard)' in runtime_source, "contact list labels lastmod as heard activity with bounded formatting"
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
# modes. The one-off pre-release v1/v2 migration path has been retired.
assert "MESHINK_MESSAGE_CAPACITY=250" in message_store_header, "device journal capacity is 250 messages"
assert "MESHINK_MESSAGE_TEXT_MAX=160" in message_limits_source, "MeshInk exposes the full MeshCore direct-message text limit"
assert "STORE_VERSION=3" in message_store_source and "sizeof(MeshInkStoredMessage)==188" in message_store_source, "current v3 fixed-record layout is pinned"
assert "migrate_legacy" not in message_store_source and "migrate_legacy" not in message_store_header, "legacy message migration code is removed"
assert "LEGACY_STORE_VERSION" not in message_store_source and "LegacyStoredMessage" not in message_store_source, "legacy v1/v2 record formats are removed"
assert "STORE_TEMP_PATH" not in message_store_source and "STORE_BACKUP_PATH" not in message_store_source, "migration temporary and rollback paths are removed"
assert 'STORE_INVALID_PATH[]="/ui_messages.invalid.bak"' in message_store_source, "unsupported live journals get a non-destructive recovery backup"
assert "journal unsupported" in message_store_source and "preserving before recreate" in message_store_source, "unsupported journal handling is explicit and non-destructive"
assert "static bool incomplete_direct_state(uint8_t state)" in message_store_source, "boot recovery identifies stale in-flight direct states"
assert "item.kind!=(uint8_t)MeshInkMessageKind::Direct" in message_store_source, "boot recovery never rewrites channel sends"
assert "write_record(physical,item)" in message_store_source, "stale in-flight direct sends are durably failed before history loads"
assert "recovered-failed=%u errors=%u" in message_store_source, "boot reports stale-send recovery results"
assert "meshink_message_store().begin()" in companion_source, "companion mode opens the same current-format journal"
assert "char text[MESHINK_MESSAGE_TEXT_BYTES]" in companion_source, "Bluetooth companion pending sends retain the full message"


# Test51: SPIFFS remains the persistent authority, but the running session is
# RAM-first. Only raw records are mirrored; rendered history/pages are not.
assert "MeshInkStoredMessage* records_=nullptr;" in message_store_header, "journal owns a runtime raw-record cache"
assert "MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT" in message_store_source, "raw journal cache prefers PSRAM"
assert "source.read((uint8_t*)records_,bytes)" in message_store_source, "journal is loaded sequentially once at startup"
assert "out=records_[physical];" in message_store_source, "normal logical reads use the RAM mirror"
assert "if(records_)records_[physical]=record;" in message_store_source, "successful metadata writes update the RAM mirror"
assert "if(records_)records_[physical]=item;" in message_store_source, "successful appends update the RAM mirror"
assert "record-cache=%s header=%uB" in message_store_source, "boot transcript reports cache placement"
assert "uint16_t active_indices_[MESHINK_MESSAGE_CAPACITY]{};" in runtime_source, "active conversation keeps compact journal indices"
assert "mutable MessageView active_message_view_{};" in runtime_source, "UI still formats only one scratch message at a time"
assert "MessageView active_messages_[MAX_STORED_MESSAGES]" not in runtime_source and "MessageView* active_messages_" not in runtime_source, "formatted history is never prebuilt"
assert "store_.read(active_indices_[i],item)" in runtime_source, "on-demand message formatting reads the RAM-backed journal"
assert "ui_setup();           // show boot logo while storage/radio initialize" in unified_source, "display still initializes before local message-store startup"
assert "local_mesh_setup();   // includes first-boot SPIFFS mount / format" in unified_source, "journal startup remains after display initialization"


# Test52: page history stays on-demand. Only tiny anchors and lazy per-message
# geometry metadata are retained; pages themselves are never materialized.
assert "uint32_t revision() const" in message_store_header, "journal exposes a cheap append revision for cache invalidation"
assert "conversation_store_revision_!=store_.revision()" in runtime_source, "conversation summaries rebuild only after message history changes"
assert "conversation_contacts_signature_!=contact_signature" in runtime_source, "contact/name changes invalidate summaries without periodic journal scans"
assert "void rebuild_conversations(uint32_t contact_signature)" in runtime_source, "conversation previews use one linear RAM-backed journal pass"
assert "last_for(" not in runtime_source, "per-contact full-journal scans are removed"
assert "rebuild_active();" not in runtime_source[runtime_source.index("void refresh(bool force=false)"):runtime_source.index("void received_direct")], "periodic provider refresh must not rebuild the active message index"
assert "CHAT_PAGE_ANCHORS=250" in source and "chat_page_starts[CHAT_PAGE_ANCHORS]" in source, "lazy chat navigation stores only compact page anchors"
assert "chat_fill_backwards" in source and "chat_page_bounds_lazy" in source, "chat pages remain height-aware and built only when requested"
assert "struct ChatGeometryCache" in source and "uint8_t valid[MESHINK_MESSAGE_CAPACITY]" in source, "only compact lazy geometry metadata is cached"
assert "if(!chat_geometry_cache.valid[index])" in source, "message geometry is computed only on first demand"
assert "rebuild_chat_geometry_cache" not in source, "history geometry is never prebuilt in bulk"
assert "chat_needs_paging" not in source, "draw and swipe paths share the same lazy page-boundary calculation"
assert "const UiMessage& message=ui_data->active_message(i);" in source, "visible message content remains on-demand"
assert "item.sequence==0||!matches(item)" in runtime_source, "sequence-zero records can never enter an active chat index"


# Test53: common message paths minimize flash traffic without weakening
# immediate persistence.
assert "bool has_rx=false,int8_t snr_q4=0" in message_store_header, "receive RF metadata can be included in the append write"
assert "if(has_rx){" in message_store_source and "item.flags|=MESHINK_MESSAGE_HAS_RX;" in message_store_source, "append persists SNR/path metadata atomically with the message"
assert "MeshInkMessageOrigin::LocalUi,has_rf,snr_q4,path_len" in runtime_source, "standalone receives avoid a second metadata rewrite"
assert "MeshInkMessageOrigin::CompanionApp," in companion_source and "pkt!=nullptr" in companion_source, "companion receives persist RF metadata in the same write"
assert "void update_outgoing(uint32_t sequence,UiMessageState state,uint32_t ack,bool route_flood);" in message_store_header, "direct send response has a coalesced metadata update"
assert "bool update_state(uint32_t sequence,UiMessageState state);" in message_store_header, "message journal reports whether a durable final state update succeeded"
assert "item.state=(uint8_t)state;" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes state"
assert "item.ack=ack;" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes ACK"
assert "MESHINK_MESSAGE_ROUTE_KNOWN" in message_store_source[message_store_source.index("void MeshInkMessageStore::update_outgoing"):], "coalesced direct update writes route"
direct_attempt_response = runtime_source[
    runtime_source.index("else if(frame[0]==6&&len>=10&&pending_direct.active)"):
    runtime_source.index("else if(frame[0]==0x82&&len>=5&&pending_direct.active)")
]
assert "provider.confirm_direct_send(" not in direct_attempt_response and "provider.update_message(" not in direct_attempt_response, "direct attempt ACK/route metadata never reaches persistent provider methods"
assert "provider.transient_direct_status(" in direct_attempt_response, "direct attempt ACK/route metadata updates the RAM-only UI overlay"
direct_retry_loop = runtime_source[
    runtime_source.index("else if(pending_direct.active&&!pending_direct.waiting_response&&pending_direct.deadline"):
    runtime_source.index("#if ENV_INCLUDE_GPS == 1", runtime_source.index("else if(pending_direct.active&&!pending_direct.waiting_response&&pending_direct.deadline"))
]
assert "provider.update_message(" not in direct_retry_loop and "provider.confirm_direct_send(" not in direct_retry_loop, "direct retries never rewrite the journal"
assert "provider.transient_direct_status(" in direct_retry_loop and "retry_state" in direct_retry_loop, "direct retry stages remain visible through the RAM-only UI overlay"
assert "pending_direct_route(route_flood)" in direct_retry_loop, "each retry derives the route MeshCore will actually use from the current contact path"
direct_delivery = runtime_source[
    runtime_source.index("else if(frame[0]==0x82&&len>=5&&pending_direct.active)"):
    runtime_source.index("else if(frame[0]==1&&pending_direct.active&&pending_direct.waiting_response)")
]
assert "provider.confirm_direct_send(" in direct_delivery and "UiMessageState::Delivered" in direct_delivery, "direct delivery persists one final ACK/route/state update"
fail_direct = runtime_source[
    runtime_source.index("static void fail_pending_direct("):
    runtime_source.index("static bool enqueue_info_request(")
]
assert "provider.update_message(pending_direct.sequence,UiMessageState::Failed)" in fail_direct, "direct failure persists one final state update"
assert "pending_direct.finalizing_failure=true;" in fail_direct, "terminal failure enters durable finalization before runtime state is cleared"
assert "pending_direct.deadline=millis()+250;" in fail_direct and "retrying finalization" in fail_direct, "failed-state persistence is retried instead of being forgotten"
assert "journal=FAILED" in fail_direct, "terminal failure is only completed after durable journal confirmation"
assert "provider.note_direct_ack(" not in runtime_source and "provider.note_direct_route(" not in runtime_source, "old multi-write direct-send path is removed"

# Test64: test.8 keeps the 80 MHz steady state but races actual message-store
# flash I/O at 240 MHz. Cached PSRAM/RAM reads must never pay a clock switch.
assert "STORE_FLASH_CPU_MHZ=240" in message_store_source, "message-store flash work has an explicit 240 MHz race-to-idle target"
assert "struct StoreCpuBoostScope" in message_store_source and "meshink_performance_set_cpu_mhz(STORE_FLASH_CPU_MHZ)" in message_store_source, "message-store owns a scoped flash CPU boost"
assert "if(restore)meshink_performance_set_cpu_mhz(previous_mhz);" in message_store_source, "message-store flash boost restores the previous CPU clock"
for method in (
    "bool MeshInkMessageStore::load_cache(File& source)",
    "bool MeshInkMessageStore::create_empty()",
    "void MeshInkMessageStore::write_header()",
    "bool MeshInkMessageStore::write_record(",
    "bool MeshInkMessageStore::begin()",
    "uint32_t MeshInkMessageStore::append(",
):
    body=message_store_source[message_store_source.index(method):]
    assert "StoreCpuBoostScope" in body[:5000], f"{method} must race flash work at 240 MHz"
read_body=message_store_source[
    message_store_source.index("bool MeshInkMessageStore::read("):
    message_store_source.index("bool MeshInkMessageStore::find_physical(")
]
assert read_body.index("if(records_){") < read_body.index("StoreCpuBoostScope cpu_boost;"), "cached message reads return before any CPU boost"
find_body=message_store_source[
    message_store_source.index("bool MeshInkMessageStore::find_physical("):
    message_store_source.index("uint32_t MeshInkMessageStore::append(")
]
assert "if(records_){" in find_body and find_body.index("if(records_){") < find_body.index("StoreCpuBoostScope cpu_boost;"), "sequence lookup stays RAM-only when the journal mirror exists"
assert "new StoreCpuBoostScope" not in message_store_source and "delete flash_boost" not in message_store_source, "storage race-to-idle adds no dynamic allocation"
assert "[T5-STOREPERF]" not in message_store_source and "meshink_message_store_perf_snapshot" not in message_store_source, "message-store profiling instrumentation is removed"

# Test65 cleanup: temporary boot-stage timing probes are removed after tuning.
ui_setup_boot=source[source.index("void ui_startup(const MeshInkUiStartupPlan& plan)"):source.index("void ui_setup()")]
for tuned_source in (unified_source, companion_source, board_target_source, source):
    assert "[T5-BOOTPERF]" not in tuned_source, "boot performance probes are removed from the field build"
assert "bootperf_" not in unified_source and "bootperf_" not in companion_source and "bootperf_" not in board_target_source and "bootperf_" not in source, "boot timer scaffolding is removed"

# Test66: test.10 keeps the complete boot path at 240 MHz, including Maps
# warm-up and panel refresh restore, then drops once to the validated 80 MHz
# interactive cruise. The T5 GPS manager reuses the board-level NMEA probe
# instead of paying upstream EnvironmentSensorManager's fixed 1000 ms detect.
setup_body=unified_source[unified_source.index("void setup()"):unified_source.index("void loop()")]
assert "meshink_performance_set_cpu_mhz(240)" in setup_body, "boot explicitly requests the ESP32-S3 maximum CPU clock"
assert setup_body.index("meshink_performance_set_cpu_mhz(240)") < setup_body.index("meshink_buttons_begin()"), "240 MHz is selected before startup work begins"
assert "ui_boot_cpu_active=true;" in source[source.index("void ui_startup(const MeshInkUiStartupPlan& plan)"):source.index("void ui_setup()")], "UI startup phase explicitly stays at render clock"
assert "return ui_boot_cpu_active?UI_RENDER_CPU_MHZ:UI_IDLE_CPU_MHZ;" in source, "post-refresh clock target is boot-aware"
assert 'set_cpu_target(ui_post_render_cpu_target(),"display-complete");' in source, "full panel refresh cannot drop boot to 80 MHz"
assert 'set_cpu_target(ui_post_render_cpu_target(),"display-area-complete");' in source, "area refresh cannot drop boot to 80 MHz"
finish_body=source[source.index("void ui_finish_startup()"):source.index("void ui_loop()")]
assert finish_body.index("ui_boot_cpu_active=false;") < finish_body.index('set_cpu_target(UI_IDLE_CPU_MHZ,"ui-ready")'), "interactive-ready is the single boot-to-80 transition"
assert setup_body.index("map_tiles_warm_storage();") < setup_body.index("ui_finish_startup();"), "Maps remains warmed before the interactive screen"
assert "-DT5_TIMING_DIAGNOSTICS" not in platformio_source, "correlated timing diagnostics are no longer built"

assert "class T5EnvironmentSensorManager final : public EnvironmentSensorManager" in board_target_header_source, "T5 target exposes its fast GPS manager to MeshCore"
assert "bool T5EnvironmentSensorManager::begin()" in board_target_source, "T5 target overrides MeshCore environment startup"
fast_gps_begin=board_target_source[
    board_target_source.index("bool T5EnvironmentSensorManager::begin()"):
    board_target_source.index("static T5GPS gps;")
]
assert "gps_detected=true;" in fast_gps_begin and "gps_active=false;" in fast_gps_begin, "fast manager preserves ENV_SKIP_GPS_DETECT visibility and preference-driven activation"
assert "delay(1000)" not in fast_gps_begin and "scanI2CBus" not in fast_gps_begin, "T5 GPS manager does not repeat upstream fixed detect wait or unused environment scan"
assert "T5EnvironmentSensorManager sensors(gps);" in board_target_source, "MeshCore global sensors object uses the T5 fast manager"
assert platformio_source.count("-DENV_INCLUDE_")==1 and "-DENV_INCLUDE_GPS=1" in platformio_source, "fast T5 environment startup is valid only while GPS is the sole enabled environment provider"
assert 'gps_send_pcas("PCAS02' not in board_target_source, "test.10 leaves GNSS positioning rate unchanged at the normal 1 Hz"
assert "PCAS02" not in platformio_source, "build flags do not introduce a GPS update-rate override"

# Test69: test.11 keeps only low-risk boot scheduling wins. Internal SPIFFS
# mounting is independent of the LoRa/GPS rail, so do it while the rail is
# still settling; MeshCore datastore/core lifecycle remains behind radio init.
local_setup_body=companion_source[companion_source.index("void local_mesh_setup()"):companion_source.index("static bool local_mesh_setup_retained_wake(")]
assert local_setup_body.index("SPIFFS.begin(false)") < local_setup_body.index("meshink_board_begin_local();"), "internal SPIFFS mount overlaps the remaining radio-rail settle interval"
radio_ready_pos=local_setup_body.index("const bool radio_ready=meshink_radio_initialize();")
assert radio_ready_pos < local_setup_body.index("store.begin();"), "MeshCore datastore initialization stays after radio initialization"
assert radio_ready_pos < local_setup_body.index("the_mesh.begin(true);"), "MeshCore core initialization stays after radio initialization"
assert local_setup_body.count("SPIFFS.begin(false)") == 1, "local startup mounts existing SPIFFS exactly once"
assert "delay(200)" not in ui_setup_boot, "local UI no longer burns a fixed 200 ms serial delay before useful startup work"
assert "Serial.begin(115200);" in ui_setup_boot, "standalone UI target still initializes Serial without the fixed wait"

# Satellite count is primary status information and matches clock/battery size.
status_bar_body=source[source.index("static void draw_status_bar()"):source.index("static MeshInkRect toast_message_rect")]
assert "text(satellites,ui_x(43),ui_y(13),3,0,true);" in status_bar_body, "satellite count uses the same scale and baseline as clock/battery status text"
assert "ui_text_width(satellites,3)" in status_bar_body, "satellite status spacing follows the primary proportional font metrics"


# Testing and release artifacts use the same versioned naming convention.
assert 'name: meshink-${{ steps.version.outputs.version }}' in testing_workflow_source, "testing artifact is named with the firmware version"
assert 'meshink-$VERSION-update.bin' in testing_workflow_source, "testing update binary uses the same versioned filename as release builds"
assert "SHA256SUMS.txt" in testing_workflow_source, "testing and release packages share the checksum filename"
assert "meshink-testing-update.bin" not in testing_workflow_source and "meshink-testing-firmware" not in testing_workflow_source, "legacy generic testing artifact names are removed"


# Test54: primary text uses built-in 1-bit Inter while compact technical/status
# copy keeps the original bitmap path.
assert all(name in source for name in ("fonts/inter_15_regular.h","fonts/inter_20_regular.h","fonts/inter_25_regular.h","fonts/inter_30_regular.h","fonts/inter_50_digits.h")), "native 1-bit Inter raster tiers are compiled into firmware"
assert "return {&inter_15_regular,23,23,34};" in source and "return {&inter_30_regular,46,46,65};" in source, "normal and largest heading tiers use native raster faces"
assert "return {&inter_50_digits,77,79,81};" in source, "oversized standby unread count uses a native digit raster"
assert "ui_smooth_metric" not in source and "numerator" not in source[source.index("struct UiSmoothFont"):source.index("static void ui_text_fit")], "primary fonts are never scaled at runtime"
assert "const int baseline=y+face.baseline_from_top;" in source, "each native face carries its own baseline anchor"
assert "meshink_display_draw_pixel(gx+sx,gy+sy,color,fb);" in source, "native glyph pixels are drawn one-for-one without resampling"
ui_text_body=source[source.index("static void ui_text("):source.index("static void ui_text_fit(")]
assert "if(scale>=3)" in ui_text_body and "ui_smooth_text(s,x,y,scale,color,bold);" in ui_text_body, "scale-three and larger primary text uses smooth raster glyphs"
assert "return scale>=3?ui_smooth_char_advance(c,scale):ui_legacy_char_advance(c,scale);" in source, "small technical text keeps the legacy bitmap renderer"
assert "0x80U>>(bit&7)" in source, "smooth glyph renderer consumes one-bit black/white coverage only"
rounded_fill_body=source[source.index("static void rounded_fill("):source.index("static void rounded_box(",source.index("static void rounded_fill("))]
assert "meshink_display_fill_rounded_rect({x,y,w,h},radius,color,fb);" in rounded_fill_body, "rounded panels delegate one shape to the display backend"
assert "sqrt(" not in rounded_fill_body, "rounded UI path avoids floating-point geometry"
assert "rounded_box(layout.outer_margin,y,layout.outer_width,layout.list_row_height" in source, "contacts/channels/discovery use rounded cards"
assert '#include "board/t5_packed_framebuffer.h"' in display_backend_source, "T5 display backend owns packed framebuffer accelerator"
assert "color==0x00U||color==0xFFU" in display_backend_source, "only exact monochrome fills bypass EPDiy"
assert "meshink_t5_packed::fill_logical_gray4(" in display_backend_source, "monochrome rectangles use direct packed framebuffer fill"
assert "meshink_t5_packed::fill_logical_rounded_gray4(" in display_backend_source, "rounded monochrome surfaces use orientation-aware packed fills"
assert "meshink_t5_packed::rounded_row_inset" in display_backend_source, "grayscale rounded fallback preserves integer corner geometry"
assert "epd_fill_rect(meshink_display_native_rect(rect),color,framebuffer);" in display_backend_source, "grayscale rectangle fallback remains EPDiy"
assert "static void draw_list_entry(const UiListEntry& item,int y,int subtitle_scale=3)" in source, "list rows support compact secondary metadata without shrinking titles"
assert "ui_data->contact(first+row),portrait_layout().list_top+row*portrait_layout().list_row_stride,2" in source, "Contacts secondary Last Heard text uses delivery-notice scale"
assert "ui_data->channel(first+row),portrait_layout().list_top+row*portrait_layout().list_row_stride,2" in source, "Channels MeshCore channel subtitle uses delivery-notice scale"
assert "ui_text_fit(item.title" in source and "3,0,true" in source[source.index("ui_text_fit(item.title"):source.index("ui_draw_wrapped(item.subtitle")], "list titles remain large and clipped safely"
assert "rounded_box(geometry.x,y,geometry.width,geometry.height,radius,message.outgoing)" in source, "chat bubbles use rounded incoming/outgoing surfaces"
assert "const int min_width=ui_w(240);" in source and "const int max_width=ui_w(456);" in source, "chat bubbles stay compact without becoming too narrow to read"
assert "ui_wrapped_line_count(message.text,text_width,3)" in source, "message paging measures the same proportional scale-3 body text that is drawn"
assert "geometry.text_width,3,color,false,16" in source, "long messages remain readable instead of being clipped at the former eight-line draw limit"
assert 'ui_text("Write a message..."' in source and 'const char* prompt=compose_text[0]?compose_text:"Write a message...";' in source, "composer uses a readable mixed-case prompt"
assert "rounded_box(back_rect" in source and "ui_action_button(action,action_rect,true)" in source, "chat header actions share the rounded visual language"
assert "malloc(" not in source[source.index("static void ui_glyph_bounds("):source.index("static meshink_keyboard::Metrics")], "built-in typography/rounding adds no dynamic memory"
assert "const MessageBubbleGeometry geometry=chat_message_geometry(i);" in source and "draw_message_bubble(message,y,geometry);" in source, "visible chat bubbles reuse lazily cached geometry for drawing"


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
assert "chat_page_bounds_lazy(count,chat_history_available_current()," in source, "current conversation lazy paging reserves the taskbar and composer"
assert "const int compose_y=chat_compose_top();" in source, "message composer drawing uses shared vertical geometry"
assert source.count("chat_compose_top()")>=3, "current-page composer draw and touch paths share the same top edge"
assert source.count("const int text_width=ui_text_width(page_text,2);")>=2, "list and chat page arrows use proportional label width"
assert "const int subtitle_scale=ui_text_width(subtitle,3)<=subtitle_width?3:2;" in source, "long settings subtitles shrink before clipping"
assert "const int detail_scale=ui_text_width(PRESETS[index].detail,3)<=detail_width?3:2;" in source, "long radio preset technical details shrink before clipping"
assert "if(!keyboard_visible)settings_row(\"PATH HASH MODE\",path_hash_label(),510);" in source, "Radio Settings does not draw a row beneath the portrait keyboard"
assert 'if(value>99)strcpy(out,"99+");' in source, "status unread counters are visually bounded"
assert source.count("text(count,left,ui_y(13),3,0,true);")>=2, "direct and channel status counters use the same primary numeric face/size as clock and battery"
assert "text(satellites,ui_x(43),ui_y(13),3,0,true);" in source, "GPS satellite count uses the same primary numeric face/size"
assert "ui_text_width(count,3)" in source and "ui_text_width(satellites,3)" in source, "primary status-number spacing follows proportional font metrics"
assert "!(status_unread&&status_channel_unread)" in source, "GPS satellite number yields space when both enlarged unread counters are present"
assert "ui_text_width(short_name,2),ui_text_width(age,2)" in source, "map label background accounts for both node name and age"
assert "meshink_map_control_rect(layout,(int)control)" in source, "map labels avoid the visible map controls through the cached layout reference"
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
assert "overview_position_lines*ui_text_line_step(3)+ui_h(8)" in source, "two-line Overview position reserves explicit space before its source label"
assert "overview_source_y+ui_text_height(2)+ui_h(18)" in source, "Overview source label keeps breathing room before Last Heard"
assert "telemetry_position_lines*ui_text_line_step(3)+ui_h(8)" in source, "Telemetry position reserves explicit space before provenance"
assert "static size_t discovery_page = 0;" in source, "Discovered adverts have independent paging state"
assert "const size_t first=discovery_page*LIST_ITEMS_PER_PAGE;" in source, "Discovered adverts render every page rather than only the first five"
assert "screen==Screen::Contacts||screen==Screen::Channels||screen==Screen::Discovery" in source, "Discovery shares vertical swipe paging with Contacts and Channels"
assert "ui_data->open_advert(index)" in source, "Discovery touch indexing follows the visible page"

assert "ui_text_fit(short_name,best_x+4,best_y+2,w-ui_w(8),2,0,true);" in source, "capped map label backings also clip long node names"
assert 'if(status_unread>99)strcpy(direct,"99+");' in source and 'if(status_channel_unread>99)strcpy(channel,"99+");' in source, "large standby unread counts stay inside their 244px cards"
assert "ui_centred_fit(wake.confirm_battery" in source and "ui_centred_fit(wake.confirm_external" in source, "board-specific shutdown guidance is screen-bounded"
assert "ui_centred_fit(wake.off_battery_line1" in source and "ui_centred_fit(wake.off_external_line2" in source, "powered-off guidance remains bounded for future board ports"
assert "ui_centred_fit(wake_line,ui_y(852),portrait_layout().width-ui_w(32),3,0,true);" in source, "standby wake guidance cannot overflow the display"
assert 'ui_centred_fit("LORA RADIO NOT DETECTED",ui_y(390),portrait_layout().width-ui_w(24),4,0,true);' in source, "hardware failure heading is screen-bounded"
assert "char line_text[160]{};" in source, "proportional wrapping preserves long unbroken lines without a 64-byte scratch truncation"
assert "const bool truncated=(row==max_lines-1)&&*next;" in source, "bounded multi-line text visibly marks intentional truncation"
assert "ui_text_fit(footer,geometry.x+ui_w(14)" in source, "oversized message metadata is clipped inside its bubble instead of drawing outside"


# Test57: full-length direct messages remain valid across the complete retry
# policy, channel sends are never silently truncated, and a terminal direct
# failure restores the draft only when it is safe to do so.
assert "DIRECT_RETRY_LIMIT=3" in runtime_source, "direct messages use two route retries plus one flood fallback after the initial attempt"
assert "pending_direct.retry>=DIRECT_RETRY_LIMIT" in runtime_source, "retry exhaustion occurs after the flood fallback attempt"
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

assert 'case UiMessageState::Retrying1:return "RETRYING 1/2"' in runtime_source, "legacy retry states remain readable after upgrading"
assert 'case UiMessageState::Retrying3:return "SENDING"' in runtime_source, "final retry state remains route-neutral until actual route metadata is applied"
assert 'case UiMessageState::Retrying3:state="FINAL FLOOD"' in source, "chat footer fallback uses compact final-flood wording"
assert "force_pending_direct_flood()" in runtime_source and "contact->out_path_len=OUT_PATH_UNKNOWN;" in runtime_source, "final runtime retry resets the stale saved path so MeshCore uses flood"
assert "attempt==0?UiMessageState::Sending" in runtime_source and "provider.transient_direct_status(" in runtime_source and "journal=unchanged" in runtime_source, "radio attempt status is visible in RAM while the journal remains unchanged"
assert "pending_direct.route_flood[attempt]=frame[1]!=0;" in runtime_source, "MeshCore RESP_CODE_SENT route flag is retained as the authoritative actual route"
assert "pending_direct.route_flood[attempt]);" in direct_attempt_response, "actual MeshCore route is pushed into the RAM-only UI overlay"
send_active=runtime_source[runtime_source.index("bool local_mesh_send_active("):runtime_source.index("bool local_mesh_send_direct(",runtime_source.index("bool local_mesh_send_active("))]
assert "contact.out_path_len==OUT_PATH_UNKNOWN" in send_active and "provider.transient_direct_status(" in send_active, "initial UI route reflects the same saved-path decision MeshCore will use"
assert "transient_direct_sequence_" in runtime_source and "item.sequence==transient_direct_sequence_" in runtime_source, "active message rendering overlays transient direct state by sequence"
assert "clear_transient_direct(sequence);" in runtime_source, "final persistent delivery/failure clears the RAM-only overlay"
assert "pending_direct.active&&pending_direct.finalizing_failure" in runtime_source, "failed-state journal persistence is retried before normal send retries"
assert "if(!sequence||!store_.update_state(sequence,state))return false;" in runtime_source, "RAM overlay is not cleared unless the durable final state write succeeds"
assert "return write_record(p,item);" in message_store_source, "message-store state updates return the actual record-write result"
formatter=runtime_source[runtime_source.index("void format_message_network"):runtime_source.index("bool matches(",runtime_source.index("void format_message_network"))]
assert '"RETRYING %s %u/2"' in formatter, "retry footer reports both actual route and retry number"
assert '"FINAL %s"' in formatter, "final attempt uses compact route-aware wording"
assert 'snprintf(out,len,"%s %s",base,route);' in formatter, "send footer reports the actual direct/flood route"
assert "state!=UiMessageState::Sending" not in formatter, "sending records and transient route metadata remain displayable"
assert '"SENT DIRECT"' not in source and '"SENT DIRECT"' not in runtime_source, "direct transmit acknowledgement is never presented as delivery"


# Test58: direct-message ACK tracking survives retry overlap. A delayed ACK
# from any earlier in-flight attempt can still complete the one logical message.
assert "uint32_t acks[DIRECT_RETRY_LIMIT+1]" in runtime_source, "pending direct send retains ACK hashes for initial send plus every retry"
assert "bool route_flood[DIRECT_RETRY_LIMIT+1]" in runtime_source, "each ACK keeps the route used by its own attempt"
assert "for(uint8_t attempt=0;attempt<=DIRECT_RETRY_LIMIT;++attempt)" in runtime_source, "delivery checks every in-flight attempt ACK"
assert "pending_direct.acks[attempt]==ack" in runtime_source, "late ACKs are matched against per-attempt history"
assert "provider.confirm_direct_send(" in runtime_source and "UiMessageState::Delivered" in runtime_source, "matched late ACK persists delivered state and the route that actually delivered"
assert "memcpy(&pending_direct.ack," not in runtime_source, "single newest-ACK tracking cannot regress"


# Test59: companion-mode journal persistence distinguishes a new message from
# a protocol retry and keeps all in-flight ACK hashes until delivery resolves.
assert "uint8_t attempt=0;" in companion_source and "pending_.attempt=frame[2];" in companion_source, "companion direct send captures the MeshCore attempt number"
assert "pending_.kind==MeshInkMessageKind::Direct&&pending_.attempt>0" in companion_source, "only explicit direct retries may deduplicate a journal entry"
assert "CompanionAckRef ack_refs_[8]" in companion_source, "companion tracks MeshCore's eight possible in-flight ACK hashes"
assert "remember_ack(ack,sequence,route_flood)" in companion_source, "every successful companion direct attempt retains its ACK and route"
assert "deliver_ack(ack);" in companion_source, "companion delivery resolves against per-attempt ACK history"
assert "for(auto& item:ack_refs_)if(item.sequence==delivered_sequence)item={};" in companion_source, "all stale ACK references for a delivered logical message are cleared"

assert "CompanionAckRef& slot=ack_refs_[next_ack_ref_];" in companion_source, "companion ACK ring uses C++11-safe explicit field assignment"
assert "ack_refs_[next_ack_ref_]={ack,sequence,route_flood}" not in companion_source, "C++11-incompatible aggregate assignment must not return"


# Test60/Test61: test20/21 refinements keep refresh/layout/text behaviour explicit.
button_body=source[source.index("static void service_primary_button()"):source.index("void ui_setup()",source.index("static void service_primary_button()"))]
map_short=button_body[button_body.index('if(screen==Screen::Maps){'):button_body.index('            }else{',button_body.index('if(screen==Screen::Maps){'))]
assert "local_mesh_refresh_ui_data();" in map_short, "physical Maps refresh obtains current node marker data"
assert "meshink_display_fill_framebuffer(&display,0x00);" in map_short and '"SHORT_BUTTON_MAP_BLACK"' in map_short, "physical Maps refresh flashes the ready screen black"
assert 'draw_screen();' in map_short and 'fast_full_redraw("SHORT_BUTTON_MAP_REFRESH",true);' in map_short, "physical Maps refresh restores the cached viewport with fresh overlays"
assert "map_base_valid=false" not in map_short and "open_screen(Screen::Maps)" not in map_short and "load_map_with_feedback" not in map_short, "physical Maps refresh never invalidates or reloads decoded terrain"
assert "ui_draw_compose_tail(compose_text,text_x,text_y,text_width,text_height,3);" in source, "portrait composer uses the clipped bottom-tail entry renderer"
assert "metrics.entry.y+(metrics.entry.height-ui_text_height(3))/2" in source, "single-line portrait composer text is vertically centred"
assert 'settings_row("SETTINGS","Device and radio",390)' in source and 'settings_row("DISPLAY & POWER","Frontlight, refresh, standby",478)' in source, "More/Settings subtitles use calmer sentence case"

# Test62: native smooth tiers are deliberately a little larger than the old
# 5x7 primary sizes (21/28/35/42px) without any bitmap enlargement.
assert "return {&inter_15_regular,23,23,34};" in source, "scale-three primary text increases from about 21px to a native 23px cap"
assert "return {&inter_20_regular,31,31,44};" in source, "scale-four text uses a native 31px cap"
assert "return {&inter_25_regular,38,38,55};" in source, "scale-five text uses a native 38px cap"
assert "return {&inter_30_regular,46,46,65};" in source, "scale-six text uses a native 46px cap"
assert "ui_text_line_step(scale)" in source and "lines*ui_text_line_step(3)" in source, "wrapping and message bubble height follow the larger native text metrics"

# Test61 status-bar icon polish: battery remains unchanged; all other symbols
# use bold rounded geometry that survives low-resolution DU refreshes.
assert "static void draw_status_bold_line(" in source and "radius=2" in source, "status icons share a bold rounded stroke helper"
assert "rounded_fill(x+2,y+2,22,22,11,0);" in source and "rounded_fill(x+7,y+7,12,12,6,0xFF);" in source, "GPS searching icon is a circular magnifying glass"
assert "draw_status_bold_line(x+20,y+20,x+29,y+29,3,0);" in source, "GPS search handle is deliberately bold"
assert "rounded_fill(x+3,y+3,25,25,12,0);" in source and "draw_status_disc(x+15,y+15,4,0);" in source, "GPS fix/off target is rounded and bold"
assert "rounded_fill(x,y+5,30,22,5,0);" in source, "private unread envelope has rounded heavy corners"
assert "static void draw_channel_status_icon(" in source and "draw_channel_status_icon(left,ui_y(9));" in source, "channel unread replaces the hash glyph with a rounded group icon"
assert 'meshink_display_draw_rect({x,y+6,31,18},0,fb);meshink_display_fill_rect({x+31,y+11,4,8},0,fb);' in source, "battery icon geometry is intentionally unchanged"

# Test61 standby composition: a prominent smooth STANDBY label balances both
# the empty and unread-card layouts without colliding with the wake footer.
standby_body=source[source.index("static void draw_standby(){"):source.index("static void format_minutes",source.index("static void draw_standby(){"))]
assert "const bool any_unread=has_direct||has_channel;" in standby_body, "standby layout branches only on whether any unread cards are present"
assert "const int standby_state_y=any_unread?ui_y(775):ui_y(620);" in standby_body and 'ui_centred("STANDBY",standby_state_y,5,0,true);' in standby_body, "standby state label moves below unread cards when needed"
assert 'ui_centred_fit(wake_line,ui_y(852),portrait_layout().width-ui_w(32),3,0,true);' in standby_body, "wake instruction uses readable smooth scale-three text"
assert "ui_y(830)" in standby_body and "ui_y(892)" in standby_body, "standby footer stays below the unread-card region"

# Test63: the bottom taskbar remains on the live/current conversation and is
# hidden only after swiping back to an older history page.
draw_screen_body=source[source.index("static void draw_screen() {"):source.index("static void refresh(",source.index("static void draw_screen() {"))]
assert '(screen==Screen::ContactChat||screen==Screen::ChannelChat)&&' in draw_screen_body and '!keyboard_visible&&chat_page==0' in draw_screen_body, "current chat page keeps the taskbar"
assert 'draw_bottom_nav(screen==Screen::ContactChat?0:1);' in draw_screen_body, "chat taskbar selects Contacts or Channels appropriately"
assert "static int chat_compose_top()" in source, "composer geometry exists only for the current conversation page"
assert "portrait_layout().bottom_nav_top-metrics.key_height-ui_h(12)" in source, "current chat compose box stays above the visible taskbar"
assert "chat_history_available_current()" in source and "chat_history_available_paged()" in source, "pagination has separate current/history capacities"
assert "chat_fill_backwards(previous_start,history_available)" in source, "older pages use the extra space released by hiding the taskbar"
assert "return portrait_layout().height-ui_h(62);" in source, "older pages reclaim both composer and taskbar vertical space"
assert "if(history_page){" in source and "History pages are read-only views: no composer and no taskbar." in source, "older history pages do not draw the composer"
assert "if(chat_page==0&&hit(x,y,layout.outer_margin,compose_y" in source, "hidden history composer cannot be tapped"
assert "const bool chat_main_page=" in source and "chat_page==0;" in source[source.index("const bool chat_main_page="):source.index("switch(screen)",source.index("const bool chat_main_page="))], "visible chat taskbar remains tappable"

# Test64: history pages show only messages plus the page footer; current page
# restores both composer and taskbar when swiping back down.
chat_body=source[source.index("static void draw_chat(bool channel)"):source.index("static void draw_message_entry_fast",source.index("static void draw_chat(bool channel)"))]
history_branch=chat_body[chat_body.index("if(history_page){"):chat_body.index("}else{",chat_body.index("if(history_page){"))]
assert "rounded_box(layout.outer_margin,compose_y" not in history_branch and "Write a message..." not in history_branch, "older history pages contain no compose box"
assert "layout.height-ui_h(38)" in history_branch, "older history pages retain only the bottom page indicator"

# Test65: native-size typography layout audit.
assert "rect.y+(rect.height-ui_text_height(scale))/2" in source, "keyboard key labels use native font height for vertical centring"
assert "rect.height-7*scale" not in source, "no interactive label still centres using the old 5x7 primary-font height"
assert "meshink_outer_row_rect(layout,490,180)" in source, "Advert explanatory card has safe padding for four native scale-three lines"
assert "saved_route_y+ui_h(34)" in source, "Node Path saved-route block follows the dynamic trace extent"


# Test66: release cleanup retires the one-off pre-release message migrator.
assert "migrate_legacy" not in message_store_source and "LegacyStoredMessage" not in message_store_source, "one-off message migration implementation is gone"
assert "/ui_messages.v3.tmp" not in message_store_source and "/ui_messages.legacy.bak" not in message_store_source, "migration scratch files are no longer referenced"

# Test67: USB terminal screenshots export the live framebuffer to SD without
# redrawing or reprocessing the current screen.
assert "bool ui_save_screenshot(char* path_out,size_t path_len)" in source, "UI exposes a framebuffer screenshot export"
assert '"/SHOT%04u.BMP"' in source, "screenshots use simple sequential FAT-friendly filenames"
assert "map_tiles_media_ready()" in source[source.index("bool ui_save_screenshot"):source.index("static Preferences prefs")], "screenshot reuses the already-mounted removable storage path"
shot_body=source[source.index("bool ui_save_screenshot"):source.index("static Preferences prefs")]
assert "draw_screen(" not in shot_body and "map_tiles_render(" not in shot_body, "screenshot capture never redraws or processes map tiles"
assert "screenshot_gray8_at(x,y)" in shot_body and "top-down BMP" in shot_body, "BMP captures the current framebuffer in logical screen orientation"
assert "meshink_display_read_logical_gray8(fb,logical_x,logical_y)" in source, "UI delegates framebuffer orientation/packing to the display backend"
assert "meshink_display_read_logical_gray8" in display_backend_source and "MeshInkRotation::InvertedPortrait" in display_backend_source, "T5 backend owns logical-to-physical screenshot readback"
assert "meshink_storage_open_write" in storage_backend_header and "SD.open(path,FILE_WRITE)" in storage_backend_source, "storage backend provides explicit screenshot write access"
assert 'strcmp(terminal_line,"screenshot")' in unified_source and 'strcmp(terminal_line,"shot")' in unified_source, "local USB terminal accepts screenshot and shot commands"
assert "service_local_terminal();" in unified_source, "terminal command service runs in local UI mode"


# Test68: screenshot collection mode is session-scoped and one Enter equals one capture.
assert "static bool screenshot_capture_mode=false;" in unified_source, "screenshot terminal has an explicit capture-mode latch"
assert "if(screenshot_capture_mode&&!Serial)" in unified_source, "capture mode exits automatically when native USB CDC disconnects"
assert 'Serial.println("[T5-CMD] Screenshot mode armed");' in unified_source, "screenshot command clearly arms continuous capture mode"
assert 'Serial.println("[T5-CMD] Press Enter to save a screenshot");' in unified_source, "capture mode prompts for each blank Enter"
assert 'Serial.printf("[T5-CMD] Saved %s\\n",path);' in unified_source, "each successful capture reports the saved filename"
assert "if(!terminal_length)" in unified_source and "if(screenshot_capture_mode)terminal_save_screenshot();" in unified_source, "blank Enter captures while armed"
assert "if(ch=='\\n'&&terminal_last_was_cr)" in unified_source, "CRLF terminals cannot double-capture one Enter"
assert "Disconnect serial to exit screenshot mode" in unified_source, "session lifetime is explained to the user"


# 2.1.0: companion unread/read handoff, server login convenience, telemetry
# provenance wording and deep-sleep top-tab restoration remain intentionally
# narrow changes with no journal-layout or flash-partition migration.
assert "mark_matching_received_read" in message_store_header and "MeshInkMessageStore::mark_matching_received_read" in message_store_source, "companion sync can clear one exact unread journal record"
assert "item.flags&=(uint8_t)~MESHINK_MESSAGE_UNREAD;" in message_store_source, "companion sync clears only the unread bit"
assert "MESHINK_MESSAGE_READ_THROUGH" not in message_store_source[message_store_source.index("MeshInkMessageStore::mark_matching_received_read"):message_store_source.index("void MeshInkMessageStore::update_ack")], "companion sync never marks newer conversation messages read"
assert "if(queued==len)mark_synced_message_read(src,len);" in companion_source, "BLE sync responses clear unread only after the frame is accepted for transmit"
assert companion_source.count("MESHINK_MESSAGE_PATH_UNKNOWN,true);")>=2 and "pkt->path_len:MESHINK_MESSAGE_PATH_UNKNOWN,true);" in companion_source, "companion receives start unread"
assert '"TELEMETRY POSITION %s"' in runtime_source, "requested location is labelled as telemetry position rather than a verified live fix"
assert 'ui_text("TELEMETRY POSITION",layout.section_margin,ui_y(420),2,0,true);' in source, "telemetry page names the coordinate source explicitly"
assert "const bool status_requested=provider.request_active_node_info(UiNodeInfoRequest::Status);" in runtime_source, "successful repeater/room login immediately requests status"
assert "auto-status=%u" in runtime_source, "automatic post-login status request is diagnosable"
assert "meshink_power_retain_ui_tab" in power_backend_header and "meshink_power_get_retained_ui_tab" in power_backend_header and "meshink_power_clear_retained_ui_tab" in power_backend_header, "power boundary exposes RTC-retained top-tab handoff"
assert "esp_sleep_get_wakeup_cause()!=ESP_SLEEP_WAKEUP_UNDEFINED" in power_backend_source, "retained tab is accepted only on a real deep-sleep wake"
assert "retained_tab_for_screen" in source and "screen_for_retained_tab" in source, "UI maps nested screens to stable top-level tabs"
assert "meshink_power_retain_ui_tab(retained_tab);" in source, "deep-sleep entry stores the current top-level tab"
assert "if(meshink_power_get_retained_ui_tab(retained_tab)){" in source, "wake reads the retained top-level tab"
assert "if(!setup_complete)screen=Screen::Welcome;" in source, "headless promotion preserves a restored existing-user tab"
assert "retained_wake_tab_valid=true;" in source and "retained_wake_tab_valid=false;" in source, "retained tab survives headless display reinitialization only until interactive wake completes"
assert "meshink_power_clear_retained_ui_tab();" in source, "interactive wake consumes the RTC-retained tab only after the screen is visible"
assert "meshink_power_clear_retained_ui_tab();" in power_backend_source and "A normal reset/cold boot must never replay stale RTC UI state." in power_backend_source, "cold boot clears stale retained UI state"
assert "-DT5_FIRMWARE_VERSION='\"2.1.1-test.1\"'" in platformio_source and "-DT5_UI_VERSION='\"2.1.1-test.1\"'" in platformio_source, "gps-powersave firmware/UI identity stays aligned"
