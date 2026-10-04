"""Patch pinned EPDiy v7 GPIO-ISR ownership for MeshInk.

The GPIO ISR service is process-wide. On retained wake the SX1262 creates it
before EPDiy; on a normal cold UI boot EPDiy creates it before the SX1262.
Either way, once MeshInk has a live DIO1 handler EPDiy must only remove its own
CFG_INTR handler and must not uninstall the global service.

This deterministic build patch also upgrades an already-patched local V1
PlatformIO dependency, so incremental developer builds get the corrected
teardown semantics without deleting .pio manually.
"""

from pathlib import Path

Import("env")

MARKER = "MESHINK_SHARED_GPIO_ISR_PATCH_V2"
OLD_MARKER = "MESHINK_SHARED_GPIO_ISR_PATCH_V1"
env_name = env.subst("$PIOENV")
source_path = (
    Path(env.subst("$PROJECT_LIBDEPS_DIR"))
    / env_name
    / "epdiy"
    / "src"
    / "board"
    / "epd_board_v7.c"
)

if not source_path.exists():
    raise RuntimeError(f"MeshInk EPDiy patch target not found: {source_path}")

source = source_path.read_text(encoding="utf-8")

old_v1_deinit = """    // Remove only EPDiy's panel handler from a shared service. The retained
    // SX1262 DIO1 handler must survive the display-only alert teardown.
    gpio_isr_handler_remove(CFG_INTR);
    if (meshink_epdiy_owns_gpio_isr_service) {
        gpio_uninstall_isr_service();
    }
    meshink_epdiy_owns_gpio_isr_service = false;
"""

new_v2_deinit = """    // Remove only EPDiy's panel handler. If MeshInk reports a live radio DIO1
    // handler, preserve the process-wide ISR service even when EPDiy created
    // that service first during a cold UI boot.
    gpio_isr_handler_remove(CFG_INTR);
    const bool meshink_radio_gpio_isr =
        meshink_epdiy_existing_gpio_isr_service &&
        meshink_epdiy_existing_gpio_isr_service();
    if (meshink_epdiy_owns_gpio_isr_service && !meshink_radio_gpio_isr) {
        gpio_uninstall_isr_service();
    }
    meshink_epdiy_owns_gpio_isr_service = false;
"""

if MARKER in source:
    print(f"[MeshInk] EPDiy shared GPIO ISR ownership V2 already patched: {source_path}")
elif OLD_MARKER in source:
    if source.count(old_v1_deinit) != 1:
        raise RuntimeError("Unexpected EPDiy V1 patch: GPIO ISR deinit block changed")
    source = source.replace(OLD_MARKER, MARKER, 1)
    source = source.replace(old_v1_deinit, new_v2_deinit, 1)
    source_path.write_text(source, encoding="utf-8")
    print(f"[MeshInk] upgraded EPDiy shared GPIO ISR ownership V1 -> V2: {source_path}")
else:
    state_anchor = "static bool interrupt_done = false;\n"
    state_patch = """static bool interrupt_done = false;

// MESHINK_SHARED_GPIO_ISR_PATCH_V2
extern bool meshink_epdiy_existing_gpio_isr_service(void) __attribute__((weak));
static bool meshink_epdiy_owns_gpio_isr_service = false;
"""
    if source.count(state_anchor) != 1:
        raise RuntimeError("Unexpected EPDiy v7 source: ISR state anchor changed")
    source = source.replace(state_anchor, state_patch, 1)

    init_anchor = """    ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));

    ESP_ERROR_CHECK(gpio_isr_handler_add(CFG_INTR, interrupt_handler, (void*)CFG_INTR));
"""
    init_patch = """    const bool meshink_shared_gpio_isr =
        meshink_epdiy_existing_gpio_isr_service &&
        meshink_epdiy_existing_gpio_isr_service();
    if (meshink_shared_gpio_isr) {
        meshink_epdiy_owns_gpio_isr_service = false;
    } else {
        ESP_ERROR_CHECK(gpio_install_isr_service(ESP_INTR_FLAG_EDGE));
        meshink_epdiy_owns_gpio_isr_service = true;
    }

    ESP_ERROR_CHECK(gpio_isr_handler_add(CFG_INTR, interrupt_handler, (void*)CFG_INTR));
"""
    if source.count(init_anchor) != 1:
        raise RuntimeError("Unexpected EPDiy v7 source: GPIO ISR init anchor changed")
    source = source.replace(init_anchor, init_patch, 1)

    deinit_anchor = """    i2c_driver_delete(EPDIY_I2C_PORT);

    gpio_uninstall_isr_service();
"""
    deinit_patch = """    i2c_driver_delete(EPDIY_I2C_PORT);

""" + new_v2_deinit
    if source.count(deinit_anchor) != 1:
        raise RuntimeError("Unexpected EPDiy v7 source: GPIO ISR deinit anchor changed")
    source = source.replace(deinit_anchor, deinit_patch, 1)

    source_path.write_text(source, encoding="utf-8")
    print(f"[MeshInk] patched EPDiy shared GPIO ISR ownership V2: {source_path}")
