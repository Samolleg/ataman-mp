import importlib.util
import json
from pathlib import Path
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "particle_binary_report.py"
SPEC = importlib.util.spec_from_file_location("particle_binary_report", SCRIPT)
MODULE = importlib.util.module_from_spec(SPEC)
assert SPEC.loader
SPEC.loader.exec_module(MODULE)


class ParticleBinaryReportTests(unittest.TestCase):
    def test_reference_fixture_pins_scheduler_and_constants(self):
        fixture_path = Path(__file__).parent / "fixtures" / "particle_runtime" / "game-exe-evidence.json"
        fixture = json.loads(fixture_path.read_text(encoding="utf-8"))
        self.assertEqual(fixture["source"]["sha256"], MODULE.REFERENCE_SHA256)
        self.assertEqual(fixture["particle_vtable_scheduler_slot"]["target_va"], "0x00496EB0")
        self.assertEqual(fixture["constants"]["half_float"]["value"], 0.5)
        self.assertEqual(fixture["constants"]["late_scale_growth_float"]["bytes"], "14AE873F")
        self.assertEqual(fixture["constants"]["late_scale_growth_float"]["value"], 1.059999942779541)
        self.assertEqual(fixture["constants"]["logical_quant_milliseconds"]["value"], 55)
        self.assertEqual(
            fixture["constants"]["logical_quant_normalization_float"]["value"],
            0.0181818176060915,
        )
        self.assertEqual(
            fixture["sites"]["global_render_lookup_type_0x2001"]["bytes"][18:26],
            "01200000",
        )
        self.assertIn(
            "C74658FFFFFFFF",
            fixture["sites"]["spawn_initial_color_white"]["bytes"],
        )
        self.assertEqual(
            fixture["sites"]["attach_copies_color_endpoint"]["bytes"][:12],
            "8B57588B490C",
        )
        self.assertEqual(
            fixture["sites"]["direct3d_source_blend_srcalpha"]["bytes"][:18],
            "A180F07A006A056A13",
        )
        self.assertEqual(
            fixture["sites"]["direct3d_destination_blend_invsrcalpha"]["bytes"][:18],
            "A180F07A006A066A14",
        )
        self.assertIn("E883CE1000", fixture["sites"]["main_world_quant_call"]["bytes"])
        self.assertIn("E8F63FF3FF", fixture["sites"]["world_particle_batch_call"]["bytes"])
        self.assertIn("FF5218", fixture["sites"]["batch_emitter_scheduler_call"]["bytes"])
        self.assertEqual(fixture["sites"]["torch_client_class_name"]["bytes"], "B8BCF77800C3")
        self.assertIn("E866B0F6FF", fixture["sites"]["torch_particle_factory_call"]["bytes"])
        self.assertIn("BF3C000000", fixture["sites"]["torch_particle_prewarm_loop"]["bytes"])
        self.assertIn("FF5018", fixture["sites"]["torch_particle_prewarm_loop"]["bytes"])

    def test_reference_hash_mismatch_is_rejected_before_parsing(self):
        with self.assertRaisesRegex(ValueError, "does not match required"):
            MODULE.report(Path(__file__), MODULE.REFERENCE_SHA256)


if __name__ == "__main__":
    unittest.main()
