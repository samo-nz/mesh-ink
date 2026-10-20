"""Static integration tests: a backup must never be run against a live core."""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
get=lambda x:(root/x).read_text(encoding="utf-8")
code=get("src/backup_restore.cpp")
boot=get("src/unified_main.cpp")
ui=get("src/ui_onboarding.cpp")
format=get("src/backup_format.h")
storage=get("src/board/t5_storage_backend.cpp")
assert "if(root&&root.isDirectory())" in storage
assert "if(sd_started){SD.end();sd_started=false;}" in storage
assert storage.index("if(root&&root.isDirectory())") < storage.index("if(sd_started){SD.end();sd_started=false;}")
assert 'sd_started=SD.begin(T5_PIN_SD_CS,shared_spi,T5_STORAGE_SPI_HZ)' in storage
# Factory-new SPIFFS is the ONLY time the first-format notice may appear.
# Keep erase verification authoritative: no mount failure may imply a wipe.
assert 'if(SPIFFS.begin(false))return true;' in storage
assert 'if(bytes[i]!=0xFF)' in storage
assert 'autoformat refused' in storage
assert 'if(first_format_notice)first_format_notice();' in storage
assert storage.index('if(bytes[i]!=0xFF)') < storage.index('if(first_format_notice)first_format_notice();') < storage.index('return SPIFFS.begin(true);')
assert 'ui_show_cold_boot_splash();' in boot, "normal cold-boot splash precedes storage mount"
assert 'early_local_splash=!backup_requested&&!companion_pending' in boot, "never replace maintenance notice or companion screen"
assert 'meshink_storage_set_first_format_notice(' in boot and 'early_local_splash?ui_show_first_format_preparation:nullptr' in boot
assert boot.index('ui_show_cold_boot_splash();') < boot.index('if(!meshink_backup_run_pending_on_boot())') < boot.index('ui_setup();')
assert 'void ui_show_cold_boot_splash()' in ui
assert 'void ui_show_first_format_preparation()' in ui and 'paint_cold_boot_frame(true);' in ui
assert 'selected_protocol_has_completed_setup()' in ui, "unconfigured setup uses neutral branding"
assert 'draw_meshink_logo(ui_y(245),false);' in ui, "pre-selection splash has MeshInk logo only"
assert 'if(!first_format&&selected_protocol_has_completed_setup())' in ui
assert 'const bool reuse_prepared_display=plan.display&&display_session_active;' in ui
assert 'if(plan.display&&!reuse_prepared_display)' in ui
assert 'display=reusing-cold-boot-session' in ui
assert 'plan.splash&&plan.display&&!cold_boot_splash_drawn' in ui, "no second startup splash after early boot"
early_paint=ui.split('static void paint_cold_boot_frame(bool first_format){',1)[1].split('void ui_show_cold_boot_splash()',1)[0]
assert early_paint.index('meshink_display_clear();') < early_paint.index('meshink_display_set_all_white(&display);')
assert early_paint.index('meshink_display_invalidate_previous(&display);') < early_paint.index('meshink_display_update_screen(')
assert 'MeshInkRefreshMode::Gray16' in early_paint, "cold boot uses full grayscale refresh, not differential fast-only"
assert storage.index('if(bytes[i]!=0xFF)') < storage.index('if(first_format_notice)first_format_notice();') < storage.index('return SPIFFS.begin(true);')

assert "LeafSettings" not in code and "LEAF_NVS" not in code and "LEAF_NODES" not in code
assert 'Preferences p;\n    if(!p.begin("meshtastic",true))' not in code
assert "MESHTASTIC_VERSION=2" in format
assert 'return meshink_backup_filename::valid(protocol,file);' in code
assert 'meshink_backup_filename::make_timestamped(protocol,' in code
assert 'meshink_backup_filename::with_suffix(' in code
assert 'meshink_backup_filename::compare(' in code
assert 'candidate.filename' in code and 'size_t slot=count<capacity?count++:count-1;' in code
assert 'static bool draw_backup_filename_lines(' in ui
assert 'ui_text("SELECTED SD ARCHIVE"' in ui
assert 'draw_backup_filename_lines(entry.filename' in ui
assert 'draw_backup_filename_lines(backup_filename' in ui
assert 'mesh_protocol_time_valid()' in code
assert 'char output[64]{};' in code
assert 'MIBT%03u.TMP' in code
assert 'char filename[64]{}' in get("src/backup_restore.h")
assert 'static char backup_filename[64]{}' in ui
assert "meshink_backup_integrity::valid_journal" in code
assert "bi::valid_node_header" in code
assert "valid_message_journal(stage.staged,protocol)" in code
assert "valid_mt_node_snapshot(stage.staged)" in code
assert "Earlier restore safety snapshot present; aborting" in code
assert "SPIFFS.remove(stages[i].previous)" in code
assert 'MT_CORE_REVISION[]="@mt-core-version"' in code
assert 'sizeof(MESHINK_BUILD_MESHTASTIC_VERSION)' in code
assert 'meshink_backup_revision(' in code
assert 'MeshInkBackupRevision::Different' in code
assert 'MeshInkBackupRevision::Unknown' in code
assert 'backup_revision_status=meshink_backup_revision(' in ui
assert 'ui_action_button("OK",ui_rect(284,735,232,85),true);' in ui
assert 'Press OK to reboot the device and back up selected data' in ui
assert 'Press OK to reboot the device and restore selected data' in ui
assert 'if(screen==Screen::BackupProgress)return true;' in ui
assert 'if(protocol==2&&!strcmp(part.path,MT_CORE_REVISION))' in code
assert 'header.protocol==protocol' in format
assert "header.categories==15" in format
assert "protocol==2&&categories!=15" in code
assert "meshink_backup_run_pending_on_boot()" in boot
assert boot.index("meshink_backup_run_pending_on_boot()") < boot.index("mesh_protocol_setup();")
assert boot.index("meshink_backup_run_pending_on_boot()") < boot.index("mesh_protocol_companion_setup();")
assert "if(!offline_operation)return fail" in code
assert "meshink_backup_recover_pending()" in code
assert "authoritative_path(" in code
assert "MT_RECOVERY" in code and "MC_RECOVERY" in code
assert "stage_delete(MT_NODES_A" in code
assert "stage_delete(MT_NODES_B" in code
for name in ("MT_CONFIG","MT_CHANNELS","MT_DEVICE","MT_MODULE","MT_NATIVE_NODES","MT_LOCATION_NVS"):
    assert name in code, name
assert 'meshink_backup_schedule(protocol,backup_flags,restoring,' in ui
assert 'open_screen(Screen::BackupProgress);' in ui
assert 'open_screen(Screen::BackupConfirm);return true;' in ui
assert 'mesh_protocol_restart_into(protocol)' in ui
assert "meshink_backup_take_boot_result(" in ui
assert 'meshink_backup_cancel_request();' in ui
print("PASS: offline backup bootstrap, native Meshtastic manifest, full-only policy, no Leaf")

# Test.63: a full-wipe restore failure must identify its failed stage, and
# successful rollback must not strand virtual NVS /rstNN.old snapshots.
# Orphaned scratch from test.61 must be cleaned for automatic retry.
assert 'failed_phase="install-file"' in code
assert 'failed_phase="preserve-original"' in code
assert 'phase="apply-credentials"' in code
assert 'phase="apply-shared-name"' in code
assert 'phase="apply-device-settings"' in code
assert 'meshink_device_settings::valid(snapshot)' in code
assert 'if(stages[i].path[0]==\'@\'&&SPIFFS.exists(stages[i].previous))' in code
assert 'Existing restore salvage files; refusing overwrite' not in code
assert 'SPIFFS.remove(RESTORE_TXN)' in code
assert 'Restore %s failed; rolled back' in code
assert 'if(setup_protocol_choice){' in ui
assert 'if(setup_protocol_choice&&hit(x,y,ui_rect(60,760,420,62)))' in ui
assert 'if(setup_protocol_choice&&hit(x,y,ui_rect(284,850,232,74)))' in ui


# Test.64: check complete restore-retry order, journal protection, finite
# allowlist, and no permanent orphan-induced block after failed restore.
assert 'cleanup_abandoned_restore_files()' in code
assert 'meshink_restore_scratch::make_path(' in code
assert 'if(SPIFFS.exists(RESTORE_TXN))' in code
assert 'if(!SPIFFS.remove(path))' in code
assert 'if(SPIFFS.exists(RESTORE_TXN_TEMP))' in code
body=code.split('bool meshink_backup_restore(uint8_t protocol',1)[1]
assert body.index('if(!inspect(filename,protocol,hdr,parts.entries,true))') < body.index('if(!cleanup_abandoned_restore_files())')
assert body.index('if(!cleanup_abandoned_restore_files())') < body.index('const size_t total=SPIFFS.totalBytes()')
assert 'Existing restore salvage files; refusing overwrite' not in code
assert 'preserving %u legacy salvage' not in code
assert '"/r2s%02u.tmp"' in body and '"/r2s%02u.old"' in body
assert '(strncmp(stage.staged,"/rst",4)&&' in code
assert 'strncmp(stage.staged,"/r2s",4)' in code
assert '(strncmp(stage.previous,"/rst",4)&&' in code
assert 'strncmp(stage.previous,"/r2s",4)' in code
assert '"/rst%02u.tmp"' not in body
assert '"/rst%02u.old"' not in body

# Test.75: old @node-name archive entries cannot override native protocol owners.
assert 'SHARED_NAME[]="@node-name"' in code
assert 'clear("mt_name_pending")' in code
assert 'clear("mc_name")' in code
assert 'struct NameCacheUndo' in code
assert 'snapshot_previous_nvs(stage,protocol)' in code
assert 'p.putString("name",new_name)' not in code
