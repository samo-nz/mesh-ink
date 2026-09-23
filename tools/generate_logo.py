"""Build an e-paper-ready grayscale logo from MeshInk's original PNG.

Runs automatically as a PlatformIO pre-script for both on-device UI builds.
Install Pillow in the Python environment that runs PlatformIO.
"""
from pathlib import Path

# PlatformIO executes extra_scripts through SCons, where __file__ is unset.
Import("env")

try:
    from PIL import Image
except ImportError as exc:
    raise RuntimeError(
        "Building MeshInk requires Pillow: python -m pip install Pillow"
    ) from exc

ROOT = Path(env.subst("$PROJECT_DIR")).resolve()
SOURCE = ROOT / "docs" / "file_00000000248c820a81311144dc2df47d.png"
OUTPUT = ROOT / "src" / "meshink_logo_bitmap.h"
WIDTH, HEIGHT = 520, 347


def generate():
    if not SOURCE.is_file():
        raise FileNotFoundError(f"MeshInk original PNG artwork missing: {SOURCE}")

    with Image.open(SOURCE) as original:
        # Flatten optional alpha against the display's white background.
        rgba = original.convert("RGBA")
        white = Image.new("RGBA", rgba.size, "white")
        white.alpha_composite(rgba)
        grayscale = white.convert("L").resize(
            (WIDTH, HEIGHT), Image.Resampling.LANCZOS
        )
        pixels = grayscale.tobytes()

    packed = bytearray((len(pixels) + 3) // 4)
    for index, luminance in enumerate(pixels):
        # 0=black, 1=dark gray, 2=light gray, 3=white.
        shade = min(3, (luminance + 42) // 85)
        packed[index // 4] |= shade << (6 - (index % 4) * 2)

    rows = [
        "    " + ", ".join(f"0x{byte:02X}" for byte in packed[i : i + 20]) + ","
        for i in range(0, len(packed), 20)
    ]
    header = (
        "// Generated from docs/file_00000000248c820a81311144dc2df47d.png.\n"
        "// Do not edit; PlatformIO regenerates this from the original PNG.\n"
        "#pragma once\n"
        "#include <stdint.h>\n"
        f"static constexpr int MESHINK_LOGO_WIDTH = {WIDTH};\n"
        f"static constexpr int MESHINK_LOGO_HEIGHT = {HEIGHT};\n"
        "// Row-major, four two-bit grayscale pixels per byte, high bits first.\n"
        f"static const uint8_t MESHINK_LOGO_PIXELS[{len(packed)}] = {{\n"
        + "\n".join(rows)
        + "\n};\n"
    )
    OUTPUT.parent.mkdir(parents=True, exist_ok=True)
    if OUTPUT.is_file() and OUTPUT.read_text(encoding="ascii") == header:
        return
    OUTPUT.write_text(header, encoding="ascii")
    print(
        f"[MeshInk] original PNG -> {WIDTH}x{HEIGHT} e-paper logo "
        f"({len(packed)} bytes)"
    )


generate()
