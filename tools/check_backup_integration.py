"""Static integration tests: a backup must never be run against a live core."""
from pathlib import Path
root=Path(__file__).resolve().parent.parent
get=lambda x:(root/x).read_text(encoding="utf-8")
code=get("src/backup_restore.cpp")
boot=get("src/unified_main.cpp")
ui=get("src/ui_onboarding.cpp")
format=get("src/backup_format.h")
assert "LeafSettings" not in code and "LEAF_NVS" not in code and "LEAF_NODES" not in code
assert 'Preferences p;\n    if(!p.begin("meshtastic",true))' not in code
assert "MESHTASTIC_VERSION=2" in format
assert 'MT_CORE_REVISION[]="@mt-core-version"' in code
assert 'sizeof(MESHINK_BUILD_MESHTASTIC_VERSION)' in code
assert 'meshink_backup_revision(' in code
assert 'MeshInkBackupRevision::Different' in code
assert 'MeshInkBackupRevision::Unknown' in code
assert 'backup_revision_status=meshink_backup_revision(' in ui
assert 'revision_warning?"OK, RESTORE":"RESTORE"' in ui
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
assert "meshink_backup_schedule(protocol,backup_flags,false)" in ui
assert "meshink_backup_schedule(protocol,backup_flags,true,backup_filename)" in ui
assert "meshink_backup_take_boot_result(" in ui
assert 'meshink_backup_cancel_request();' in ui
print("PASS: offline backup bootstrap, native Meshtastic manifest, full-only policy, no Leaf")
