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
        self.assertEqual(fixture["constants"]["torch_light_radius_multiplier"]["value"], 10.0)
        self.assertAlmostEqual(fixture["constants"]["rgb_normalization_float"]["value"], 1.0 / 255.0)
        self.assertEqual(
            fixture["constants"]["logical_quant_normalization_float"]["value"],
            0.0181818176060915,
        )
        self.assertEqual(
            fixture["sites"]["global_render_lookup_type_0x2001"]["bytes"][18:26],
            "01200000",
        )
        self.assertEqual(
            fixture["ranges"]["particle_scale_mutator"]["sha256"],
            "D0328F6AB7F419CB2A8F63BF9632A1BCADFAFF9398EB70F8E59A96A3E0953E24",
        )
        scale_mutator = fixture["sites"]["scale_mutator_all_fields"]["bytes"]
        for field in ("F0000000", "F4000000", "F8000000", "FC000000",
                      "00010000", "04010000", "0C010000", "28010000",
                      "10010000", "D4000000"):
            self.assertIn("D889" + field, scale_mutator)
            self.assertIn("D999" + field, scale_mutator)
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
        self.assertIn(
            "3D0100BFBB",
            fixture["sites"]["torch_mob_strength_and_link_fields"]["bytes"],
        )
        self.assertIn(
            "3D0200BFBB",
            fixture["sites"]["torch_mob_strength_and_link_fields"]["bytes"],
        )
        self.assertIn(
            "D99E8C",
            fixture["sites"]["torch_mob_strength_and_link_fields"]["bytes"],
        )
        self.assertIn(
            "8D8390000000",
            fixture["sites"]["torch_runtime_hardcoded_campfire"]["bytes"],
        )
        self.assertIn(
            "8B8B8C000000",
            fixture["sites"]["torch_runtime_hardcoded_campfire"]["bytes"],
        )
        self.assertIn(
            "6801200000FF5240",
            fixture["sites"]["torch_runtime_hardcoded_campfire"]["bytes"],
        )
        self.assertIn(
            "8B839C000000508B11FF5248",
            fixture["sites"]["torch_runtime_light_and_sound_dispatch"]["bytes"],
        )
        self.assertIn(
            "C786E80000000000803F899EEC000000899EF0000000",
            fixture["sites"]["native_d3d_light_range_and_attenuation"]["bytes"],
        )
        self.assertEqual(
            fixture["ranges"]["weather_runtime_interpolation"]["start_va"],
            "0x00566360",
        )
        self.assertIn(
            "C783E0010000E0E94800C783E401000020EC48000F85",
            fixture["sites"]["tendri_callback_selection"]["bytes"],
        )
        self.assertIn(
            "8B0DDCF57A00",
            fixture["sites"]["scheduler_refreshes_motion_fields"]["bytes"],
        )
        self.assertEqual(
            fixture["sites"]["setwind_metadata_record"]["bytes"],
            "B8CF78001E0000000000000088D67800",
        )
        self.assertIn(
            "8D41248B3289308B7204897004",
            fixture["sites"]["world_wind_setter_fields"]["bytes"],
        )
        self.assertIn(
            "B9B8F57A00E858A9EFFF",
            fixture["sites"]["world_wind_script_stream_and_call"]["bytes"],
        )
        self.assertIn(
            "C781C801000000986600",
            fixture["sites"]["world_wind_handler_registration"]["bytes"],
        )
        self.assertIn(
            "8B15E8F57A00",
            fixture["sites"]["scheduler_refreshes_motion_fields"]["bytes"],
        )
        self.assertIn(
            "D98714010000D84E0C",
            fixture["sites"]["ordinary_update_scale_and_motion"]["bytes"],
        )
        self.assertIn(
            "D80D18CD7300",
            fixture["sites"]["ordinary_update_scale_and_motion"]["bytes"],
        )
        self.assertIn(
            "8B57588B490C89575C",
            fixture["sites"]["scheduler_particle_snapshot"]["bytes"],
        )
        self.assertIn(
            "D945F8D84E10D945FCD80E",
            fixture["sites"]["render_position_scale_interpolation"]["bytes"],
        )
        self.assertIn(
            "8B088B70048A8424A4000000",
            fixture["sites"]["particle_submission_inputs"]["bytes"],
        )
        self.assertIn(
            "8B357CEF7A00",
            fixture["sites"]["particle_pipeline_dispatch"]["bytes"],
        )
        self.assertIn(
            "FF5260",
            fixture["sites"]["particle_pipeline_dispatch"]["bytes"],
        )
        self.assertIn(
            "8B5E588B7E5C",
            fixture["sites"]["render_argb_interpolation"]["bytes"],
        )
        self.assertEqual(
            fixture["ranges"]["isaac_refill"]["start_va"],
            "0x0049EE70",
        )
        self.assertIn(
            "B979379E",
            fixture["sites"]["isaac_golden_ratio_and_mix"]["bytes"],
        )
        self.assertIn(
            "8B0D449D7A00",
            fixture["sites"]["isaac_global_state_access"]["bytes"],
        )

    def test_reference_hash_mismatch_is_rejected_before_parsing(self):
        with self.assertRaisesRegex(ValueError, "does not match required"):
            MODULE.report(Path(__file__), MODULE.REFERENCE_SHA256)


if __name__ == "__main__":
    unittest.main()
