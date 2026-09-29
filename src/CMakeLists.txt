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
        "map_tiles.cpp"
        "pmtiles_reader.cpp"
        "t5_timing.cpp"
        "ui_onboarding.cpp"
        "unified_main.cpp"
        "cache64_compat.cpp"
    INCLUDE_DIRS
        "."
        "board"
        "../include"
        "../lib/MeshCore/examples/companion_radio"
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
