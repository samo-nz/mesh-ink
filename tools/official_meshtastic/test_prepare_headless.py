"""Offline regression tests for the isolated headless upstream overlay.

These are preparation safety tests, not a successful Meshtastic compile.
"""
import tempfile
import unittest
from pathlib import Path

from prepare_headless import ENV, UPSTREAM_GUARD_PATCHES, prepare


class PrepareHeadlessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self._make("src/mesh/PhoneAPI.cpp", "// upstream marker\n")
        self._make("variants/esp32s3/t5s3_epaper/variant.h", "// board marker\n")
        for relative, old, _ in UPSTREAM_GUARD_PATCHES:
            self._make(relative, old + "\n// upstream code\n#endif\n")

    def _make(self, relative, content):
        p = self.root / relative
        p.parent.mkdir(parents=True, exist_ok=True)
        p.write_text(content)

    def test_retains_power_telemetry_and_is_idempotent(self):
        prepare(self.root)
        for relative, old, new in UPSTREAM_GUARD_PATCHES:
            contents = (self.root / relative).read_text()
            self.assertEqual(contents.count(new), 1, relative)
            self.assertNotIn(old + "\n", contents, relative)
            self.assertIn("// upstream code", contents, relative)
        board = self.root / "variants/esp32s3/meshink_h752_headless"
        self.assertTrue((board / "meshink_phoneapi.cpp").is_file())
        self.assertIn("-DMESHTASTIC_EXCLUDE_ENVIRONMENTAL_SENSOR=1",
                      (board / "platformio.ini").read_text())
        self.assertNotIn("-DMESHTASTIC_EXCLUDE_POWER_TELEMETRY=1",
                         (board / "platformio.ini").read_text())
        before = {str(p.relative_to(self.root)): p.read_bytes()
                  for p in self.root.rglob("*") if p.is_file()}
        prepare(self.root)
        after = {str(p.relative_to(self.root)): p.read_bytes()
                 for p in self.root.rglob("*") if p.is_file()}
        self.assertEqual(before, after)

    def test_refuses_changed_upstream_guard(self):
        relative, _, _ = UPSTREAM_GUARD_PATCHES[1]
        self._make(relative, "#if UNEXPECTED_GUARD\n#endif\n")
        with self.assertRaisesRegex(ValueError, "Unexpected upstream guard"):
            prepare(self.root)

    def test_does_not_overwrite_modified_board_overlay(self):
        prepare(self.root)
        overlay = self.root / "variants/esp32s3/meshink_h752_headless/variant.h"
        overlay.write_text("// independent manual change\n")
        with self.assertRaisesRegex(ValueError, "Refusing to overwrite"):
            prepare(self.root)


if __name__ == "__main__":
    unittest.main()
