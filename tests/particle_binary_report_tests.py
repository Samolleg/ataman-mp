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
        self.assertEqual(fixture["constants"]["generic_light_z_offset"]["value"], 2.0)
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
            fixture["sites"]["tendri_texture_name"]["bytes"],
            "54656E647269536D6F6B6500",
        )
        self.assertEqual(fixture["sites"]["tornado_texture_name"]["bytes"],
                         "546F726E61646F00")
        self.assertEqual(fixture["sites"]["tendrilight_texture_name"]["bytes"],
                         "54656E6472694C6967687400")
        self.assertEqual(fixture["sites"]["firesmoke_texture_name"]["bytes"],
                         "46697265536D6F6B6500")
        self.assertEqual(fixture["sites"]["banshee_texture_name"]["bytes"],
                         "42616E7368656500")
        self.assertEqual(fixture["sites"]["acidray_texture_name"]["bytes"],
                         "4163696452617900")
        self.assertEqual(fixture["sites"]["changehero_texture_name"]["bytes"],
                         "4368616E67654865726F00")
        self.assertEqual(fixture["sites"]["rainsnow_texture_name"]["bytes"],
                         "5261696E536E6F7700")
        self.assertEqual(fixture["sites"]["firearrowsmoke_texture_name"]["bytes"],
                         "466972654172726F77536D6F6B6500")
        self.assertEqual(fixture["sites"]["stench_texture_name"]["bytes"],
                         "5374656E636800")
        self.assertEqual(fixture["sites"]["link_texture_name"]["bytes"],
                         "4C696E6B00")
        self.assertEqual(fixture["sites"]["protectiona_texture_name"]["bytes"],
                         "50726F74656374696F6E4100")
        self.assertEqual(fixture["sites"]["protectione_texture_name"]["bytes"],
                         "50726F74656374696F6E4500")
        self.assertEqual(fixture["sites"]["protectionf_texture_name"]["bytes"],
                         "50726F74656374696F6E4600")
        self.assertEqual(fixture["sites"]["clayring_texture_name"]["bytes"],
                         "436C617952696E6700")
        self.assertEqual(fixture["sites"]["teleport_texture_name"]["bytes"],
                         "54656C65706F727400")
        self.assertEqual(fixture["constants"]["tornado_height_float"]["value"], 12.0)
        self.assertAlmostEqual(
            fixture["constants"]["tornado_speed_factor_float"]["value"], 0.2)
        self.assertAlmostEqual(
            fixture["constants"]["lightningblast_gravity_float"]["value"], 0.075)
        self.assertEqual(fixture["ranges"]["tornado_create_callback"]["start_va"],
                         "0x00489F70")
        self.assertEqual(
            fixture["ranges"]["lightningblast_create_callback"]["start_va"],
            "0x004923B0")
        self.assertEqual(fixture["ranges"]["nuke_create_callback"]["start_va"],
                         "0x00488F90")
        self.assertEqual(fixture["ranges"]["casting_create_callback"]["start_va"],
                         "0x00489320")
        self.assertEqual(
            fixture["ranges"]["bansheecasting_create_callback"]["start_va"],
            "0x00489BE0")
        self.assertEqual(fixture["ranges"]["mushroom_create_callback"]["start_va"],
                         "0x004889D0")
        self.assertEqual(fixture["ranges"]["blood1_create_callback"]["start_va"],
                         "0x0048EEA0")
        self.assertEqual(fixture["ranges"]["firewall_create_callback"]["start_va"],
                         "0x0048ED60")
        self.assertEqual(fixture["ranges"]["firearrow_create_callback"]["start_va"],
                         "0x0048CBA0")
        self.assertEqual(fixture["ranges"]["acidray_create_callback"]["start_va"],
                         "0x0048D050")
        self.assertEqual(fixture["ranges"]["bluegas_create_callback"]["start_va"],
                         "0x0048D420")
        self.assertEqual(fixture["ranges"]["link_create_callback"]["start_va"],
                         "0x0048D8D0")
        self.assertEqual(fixture["ranges"]["sphere_create_callback"]["start_va"],
                         "0x0048E070")
        self.assertEqual(fixture["ranges"]["clayring_create_callback"]["start_va"],
                         "0x0048F7F0")
        self.assertEqual(fixture["ranges"]["teleport_create_callback"]["start_va"],
                         "0x0048FDB0")
        self.assertIn(
            "C783E0010000D0894800C783E4010000B08D4800",
            fixture["sites"]["mushroom_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E0010000A0EE4800C783E401000000F34800",
            fixture["sites"]["blood1_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E001000060ED4800C783E801000020EE4800",
            fixture["sites"]["firewall_callback_selection"]["bytes"],
        )
        self.assertEqual(
            fixture["sites"]["blood1_alpha_table"]["bytes"],
            "000000004000000080000000C0000000FF000000FF000000FF000000",
        )
        self.assertIn(
            "C783E0010000A0CB4800C783E4010000D0CE4800",
            fixture["sites"]["firearrow_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E001000050D04800C783E401000030D34800",
            fixture["sites"]["acidray_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E001000020D44800C783E4010000F0D74800",
            fixture["sites"]["bluegas_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E0010000D0D84800C783E401000030DE4800C783E801000040E04800",
            fixture["sites"]["link_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E001000070E04800894208C783E4010000C0E74800",
            fixture["sites"]["sphere_callback_selection"]["bytes"],
        )
        self.assertAlmostEqual(
            fixture["constants"]["sphere_carrier_radius_multiplier"]["value"], 1.2)
        self.assertAlmostEqual(
            fixture["constants"]["sphere_surface_radius_multiplier"]["value"], 0.65)
        self.assertAlmostEqual(
            fixture["constants"]["sphere_acid_size_multiplier"]["value"], 1.6)
        self.assertEqual(len(fixture["sites"]["sphere_size_table"]["bytes"]), 128)
        self.assertIn(
            "C783E0010000F0F74800C783E4010000B0FA4800C783E8010000E0FB4800",
            fixture["sites"]["clayring_callback_selection"]["bytes"],
        )
        self.assertIn(
            "C783E0010000B0FD4800C783E401000060004900C783E801000010014900",
            fixture["sites"]["teleport_callback_selection"]["bytes"],
        )
        self.assertEqual(len(fixture["sites"]["clayring_radius_table"]["bytes"]), 128)
        self.assertEqual(
            fixture["sites"]["clayring_alpha_table"]["bytes"],
            "0000000080000000FF000000FF000000FF000000FF000000FF000000FF000000"
            "FF000000A0000000500000002000000000000000000000000000000000000000",
        )
        self.assertAlmostEqual(
            fixture["constants"]["clayring_source_angle_step"]["value"], 0.62832)
        self.assertAlmostEqual(
            fixture["constants"]["clayring_direction_sine"]["value"], 0.31225)
        self.assertAlmostEqual(
            fixture["constants"]["teleport_positive_vertical_multiplier"]["value"],
            0.15,
        )
        self.assertEqual(
            fixture["constants"]["teleport_negative_vertical_multiplier"]["value"],
            2.25,
        )
        self.assertAlmostEqual(
            fixture["constants"]["firearrow_head_size_float"]["value"], 0.4)
        self.assertAlmostEqual(
            fixture["constants"]["firearrow_early_growth_float"]["value"], 1.4)
        self.assertAlmostEqual(
            fixture["constants"]["bluegas_vertical_acceleration_float"]["value"], 1.02)
        self.assertEqual(len(fixture["sites"]["acidray_width_table"]["bytes"]), 152)
        self.assertEqual(len(fixture["sites"]["acidray_size_table"]["bytes"]), 152)
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
        self.assertIn(
            "D88A04010000D84D1CD82D4CB87300",
            fixture["sites"]["native_point_light_squared_falloff"]["bytes"],
        )
        self.assertEqual(
            fixture["ranges"]["native_point_light_application_nonterrain"]["start_va"],
            "0x0068D450",
        )
        self.assertEqual(
            fixture["ranges"]["native_terrain_point_light_application"]["start_va"],
            "0x0068D5A0",
        )
        self.assertIn(
            "D9451C568BF1D88E04010000D84D1CD82D4CB87300",
            fixture["sites"]["native_terrain_point_light_normal_factor"]["bytes"],
        )
        self.assertIn(
            "E841A2FCFF",
            fixture["sites"]["terrain_local_light_helper_call"]["bytes"],
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
