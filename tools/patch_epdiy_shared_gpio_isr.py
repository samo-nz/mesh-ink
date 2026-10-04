"""Patch pinned EPDiy v7 GPIO-ISR ownership for MeshInk retained wake.

Normal MeshInk boot initializes EPDiy before the SX1262, so EPDiy creates the
process-wide ESP-IDF GPIO ISR service. Deep-sleep retained wake intentionally
does the reverse: RadioLib is restored first and owns the service before a
display-only message alert or in-place BOOT promotion.

The pinned EPDiy board-v7 implementation otherwise calls
gpio_install_isr_service() unconditionally and later uninstalls the whole
service. That is unsafe when the radio already owns it. This deterministic
build patch makes EPDiy ask MeshInk's weak ownership hook first, attach only its
own CFG_INTR handler when the service is shared, and uninstall the global
service only when EPDiy created it itself.
"""

from pathlib import Path

Import("env")

MARKER = "MESHINK_SHARED_GPIO_ISR_PATCH_V1"
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
if MARKER not in source:
    state_anchor = "static bool interrupt_done = false;\n"
    state_patch = """static bool interrupt_done = false;

// MESHINK_SHARED_GPIO_ISR_PATCH_V1
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

    // Remove only EPDiy's panel handler from a shared service. The retained
    // SX1262 DIO1 handler must survive the display-only alert teardown.
    gpio_isr_handler_remove(CFG_INTR);
    if (meshink_epdiy_owns_gpio_isr_service) {
        gpio_uninstall_isr_service();
    }
    meshink_epdiy_owns_gpio_isr_service = false;
"""
    if source.count(deinit_anchor) != 1:
        raise RuntimeError("Unexpected EPDiy v7 source: GPIO ISR deinit anchor changed")
    source = source.replace(deinit_anchor, deinit_patch, 1)

    source_path.write_text(source, encoding="utf-8")
    print(f"[MeshInk] patched EPDiy shared GPIO ISR ownership: {source_path}")
else:
    print(f"[MeshInk] EPDiy shared GPIO ISR ownership already patched: {source_path}")
