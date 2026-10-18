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
assert "LeafSettings" not in code and "LEAF_NVS" not in code and "LEAF_NODES" not in code
assert 'Preferences p;\n    if(!p.begin("meshtastic",true))' not in code
assert "MESHTASTIC_VERSION=2" in format
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
