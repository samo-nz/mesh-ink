idf_component_register(
    SRCS
        "board/target.cpp"
        "board/t5_power_backend.cpp"
        "board/t5_buttons_backend.cpp"
        "board/t5_radio_backend.cpp"
        "board/t5_storage_backend.cpp"
        "companion_notice.cpp"
        "companion_runtime.cpp"
        "local_mesh_runtime.cpp"
        "protocol/mesh_protocol.cpp"
        "protocol/meshcore_protocol.cpp"
        "protocol/meshtastic_protocol.cpp"
        "protocol/meshtastic_official_phoneapi.cpp"
        "message_store.cpp"
        "backup_restore.cpp"
        "device_settings_snapshot.cpp"
        "map_tiles.cpp"
        "pmtiles_reader.cpp"
        "ui_onboarding.cpp"
        "unified_main.cpp"
        "cache64_compat.cpp"
    INCLUDE_DIRS
        "."
        "board"
        "../include"
        "../lib/MeshCore/examples/companion_radio"
        "../lib/Meshtastic/src"
        "../lib/Meshtastic/src/mesh"
        "../lib/Meshtastic/src/mesh/generated"
    REQUIRES
        bt
)

# The normal Arduino build does not promote these legacy-code warnings to
# errors. Match that policy for this experimental Arduino-as-IDF build.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=class-memaccess
    -Wno-error=misleading-indentation
    -Wno-error=format-truncation
    -Wno-error=reorder
)
