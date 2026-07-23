import importlib.util
import json
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "particle_backend_report.py"
SPEC = importlib.util.spec_from_file_location("particle_backend_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ParticleBackendReportTests(unittest.TestCase):
    def test_reference_fixture_pins_radius_to_four_corners(self):
        fixture_path = (Path(__file__).parent / "fixtures" /
                        "particle_runtime" / "3dfpfpu-billboard-evidence.json")
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
        self.assertEqual(fixture["source"]["sha256"], MODULE.REFERENCE_SHA256)
        self.assertEqual(fixture["interface"]["slot"], "0x60")
        self.assertEqual(fixture["interface"]["target_rva"], "0x00006F00")
        source = fixture["sites"]["source_position_and_scale_load"]["bytes"]
        self.assertTrue(source.startswith("D940F8D940FCD900D9C0"))
        self.assertIn("8B5004", source)
        projected = fixture["sites"]["project_submitted_scale"]["bytes"]
        self.assertIn("D84D18", projected)
        self.assertIn("D87130", projected)
        for name in ("corner_minus_minus", "corner_plus_minus",
                     "corner_plus_plus", "corner_minus_plus"):
            self.assertTrue(fixture["sites"][name]["bytes"])
        self.assertIn("D86518", fixture["sites"]["corner_minus_minus"]["bytes"])
        self.assertIn("D8450C", fixture["sites"]["corner_plus_minus"]["bytes"])
        self.assertIn("D84508", fixture["sites"]["corner_plus_plus"]["bytes"])
        self.assertIn("D84508", fixture["sites"]["corner_minus_plus"]["bytes"])

    def test_wrong_memory_image_size_is_rejected(self):
        with self.assertRaisesRegex(ValueError, "memory image size"):
            MODULE.report(Path(__file__), Path(__file__), False)


if __name__ == "__main__":
    unittest.main()
