#!/usr/bin/env python3
"""Fingerprint stable byte evidence from the read-only EI executable."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


REFERENCE_SHA256 = "BE0FC330035E03983CCE590038CC7F881F1F5D1BCF082717F31776DD4DEE94E2"
RANGES = {
    "global_render_particle_lookup": (0x40EB60, 0x40ECA5),
    "particle_scale_mutator": (0x485480, 0x485523),
    "atlas_rectangle_builder": (0x4885B4, 0x488790),
    "tendri_factory_branch": (0x485B72, 0x485D58),
    "tornado_factory_branch": (0x486574, 0x4867A0),
    "tornado_create_callback": (0x489F70, 0x48A310),
    "tornado_update_callback": (0x48A310, 0x48A560),
    "tornado_local_source_callback": (0x48A560, 0x48A8F0),
    "nuke_factory_branch": (0x486A72, 0x486BD5),
    "nuke_create_callback": (0x488F90, 0x489190),
    "nuke_update_callback": (0x489190, 0x4892E0),
    "nuke_local_source_callback": (0x4892E0, 0x489320),
    "mushroom_factory_branch": (0x486BD5, 0x486C43),
    "mushroom_create_callback": (0x4889D0, 0x488DB0),
    "mushroom_update_callback": (0x488DB0, 0x488F00),
    "mushroom_local_source_callback": (0x488F00, 0x488F90),
    "blood1_factory_branch": (0x486C43, 0x486E7F),
    "blood1_create_callback": (0x48EEA0, 0x48F300),
    "blood1_update_callback": (0x48F300, 0x48F380),
    "blood1_local_source_callback": (0x48F380, 0x48F3B0),
    "a4blood_factory_branch": (0x486E7F, 0x4870D2),
    "a4blood_create_callback": (0x48F3B0, 0x48F670),
    "a4blood_update_callback": (0x48F670, 0x48F6F0),
    "a4blood_local_source_callback": (0x48F6F0, 0x48F7F0),
    "firewall_factory_specialization": (0x485BBF, 0x485D47),
    "firewall_create_callback": (0x48ED60, 0x48EE20),
    "firewall_local_source_callback": (0x48EE20, 0x48EEA0),
    "firearrow_factory_branch": (0x485803, 0x4858E3),
    "firearrow_create_callback": (0x48CBA0, 0x48CED0),
    "firearrow_update_callback": (0x48CED0, 0x48D050),
    "acidray_factory_branch": (0x4858E3, 0x48599D),
    "acidray_create_callback": (0x48D050, 0x48D330),
    "acidray_update_callback": (0x48D330, 0x48D420),
    "bluegas_factory_branch": (0x485A9E, 0x485B72),
    "bluegas_create_callback": (0x48D420, 0x48D7F0),
    "bluegas_update_callback": (0x48D7F0, 0x48D8D0),
    "link_factory_branch": (0x48599D, 0x485A9E),
    "link_create_callback": (0x48D8D0, 0x48DE30),
    "link_update_callback": (0x48DE30, 0x48E040),
    "link_local_source_callback": (0x48E040, 0x48E060),
    "sphere_factory_branch": (0x48560E, 0x485735),
    "sphere_create_callback": (0x48E070, 0x48E7C0),
    "sphere_update_callback": (0x48E7C0, 0x48E9E0),
    "clayring_factory_branch": (0x4870D2, 0x4872E5),
    "clayring_create_callback": (0x48F7F0, 0x48FAB0),
    "clayring_update_callback": (0x48FAB0, 0x48FBE0),
    "clayring_local_source_callback": (0x48FBE0, 0x48FDB0),
    "teleport_factory_branch": (0x4872E5, 0x48745F),
    "teleport_create_callback": (0x48FDB0, 0x490060),
    "teleport_update_callback": (0x490060, 0x490110),
    "teleport_local_source_callback": (0x490110, 0x490130),
    "modifier_factory_branch": (0x48745F, 0x4874B1),
    "modifier_create_callback": (0x490130, 0x490990),
    "modifier_update_callback": (0x490990, 0x490B00),
    "modifier_local_source_callback": (0x490B00, 0x490C10),
    "zoneexit_factory_branch": (0x48783B, 0x487888),
    "zoneexit_create_callback": (0x492D30, 0x492F40),
    "zoneexit_update_callback": (0x492F40, 0x492FB0),
    "zoneexit_local_source_callback": (0x492FB0, 0x492FC0),
    "path_factory_branch": (0x487888, 0x4878CD),
    "path_create_callback": (0x492FC0, 0x493280),
    "path_update_callback": (0x493280, 0x4932F0),
    "path_local_source_callback": (0x4932F0, 0x493300),
    "pathdestination_factory_branch": (0x4878CD, 0x487A40),
    "pathdestination_create_callback": (0x493300, 0x4933F0),
    "pathdestination_update_callback": (0x4933F0, 0x4934F0),
    "pathdestination_local_source_callback": (0x4934F0, 0x493560),
    "moshka_factory_branch": (0x487A40, 0x487B94),
    "moshka_create_callback": (0x493560, 0x493770),
    "moshka_update_callback": (0x493770, 0x4938C0),
    "moshka_local_source_callback": (0x4938C0, 0x493D10),
    "portalstar_factory_branch": (0x487D03, 0x487D3E),
    "portalstar_create_callback": (0x493D10, 0x493D50),
    "portalstar_update_callback": (0x493D50, 0x493DD0),
    "portalstar_local_source_callback": (0x493DD0, 0x493DE0),
    "cylinder_factory_branch": (0x487D3E, 0x487EEA),
    "cylinder_create_callback": (0x493DE0, 0x494170),
    "cylinder_update_callback": (0x494170, 0x494340),
    "cylinder_local_source_callback": (0x494340, 0x494470),
    "portal_factory_branch": (0x487B94, 0x487D03),
    "portal_create_callback": (0x494470, 0x494770),
    "portal_update_callback": (0x494770, 0x494930),
    "portal_local_source_callback": (0x494930, 0x494950),
    "casting_factory_branch": (0x486993, 0x4869DD),
    "casting_create_callback": (0x489320, 0x489A50),
    "casting_update_callback": (0x489A50, 0x489B40),
    "casting_local_source_callback": (0x489B40, 0x489BE0),
    "bansheecasting_factory_branch": (0x4869DD, 0x486A72),
    "bansheecasting_create_callback": (0x489BE0, 0x489EF0),
    "bansheecasting_update_callback": (0x489EF0, 0x489F30),
    "bansheecasting_local_source_callback": (0x489F30, 0x489F70),
    "lightningblast_factory_branch": (0x487746, 0x487840),
    "lightningblast_create_callback": (0x4923B0, 0x492B80),
    "lightningblast_update_callback": (0x492B80, 0x492CC0),
    "lightningblast_local_source_callback": (0x492CC0, 0x492D25),
    "spawn_callback": (0x48E9E0, 0x48EC20),
    "update_callback": (0x48EC20, 0x48ED60),
    "type_0x2010_spawn_wrapper": (0x48ED60, 0x48EE20),
    "particle_attach_snapshot": (0x496A20, 0x496A60),
    "scheduler": (0x496EB0, 0x4975B0),
    "render_interpolation": (0x4976B0, 0x497CD5),
    "particle_submission": (0x502CA0, 0x50367C),
    "particle_batch_scheduler": (0x498480, 0x498620),
    "blend_switch": (0x498A8A, 0x498AC3),
    "logical_quant_loop": (0x4572FC, 0x45759A),
    "native_world_set_loader": (0x4821DC, 0x48230D),
    "weather_table_loader": (0x563930, 0x563F60),
    "weather_runtime_interpolation": (0x566360, 0x566BF7),
    "torch_native_mob_tag_dispatch": (0x46B3C0, 0x46B49B),
    "torch_client_particle_setup": (0x52D330, 0x52D40E),
    "torch_client_light_setup": (0x52D410, 0x52D517),
    "native_d3d_light_constructor": (0x68CEF0, 0x68CF9F),
    "native_point_light_application_nonterrain": (0x68D450, 0x68D590),
    "native_terrain_point_light_application": (0x68D5A0, 0x68D728),
    "generic_light_setup": (0x511830, 0x51194C),
    "spatial_light_registration": (0x6CA5D0, 0x6CA715),
    "spatial_light_cell_insertion": (0x6CAC10, 0x6CAF69),
    "terrain_vertex_local_light_application": (0x6C31C0, 0x6C36AB),
    "terrain_vertex_global_light_application": (0x6C36B0, 0x6C39F8),
    "terrain_cell_relighting": (0x6C3A00, 0x6C3C7C),
    "torch_authored_runtime_integration": (0x592968, 0x5929D4),
    "world_quant_particle_dispatch": (0x5641F0, 0x56448A),
    "world_wind_common_setter": (0x55FCB0, 0x55FCD3),
    "world_wind_wrapper": (0x564190, 0x5641E6),
    "world_wind_script_handler": (0x669800, 0x669842),
    "direct3d_particle_blend_setup": (0x56A1CD, 0x56A1F1),
    "isaac_refill": (0x49EE70, 0x49F149),
    "isaac_entropy_initialization": (0x49F150, 0x49F4B0),
}
SITES = {
    "tendri_texture_name": (0x78AEA4, 12),
    "tornado_texture_name": (0x78AE68, 8),
    "tendrilight_texture_name": (0x78ADC8, 12),
    "firesmoke_texture_name": (0x78AEE8, 10),
    "changehero_texture_name": (0x78AE54, 11),
    "rainsnow_texture_name": (0x78AE48, 9),
    "banshee_texture_name": (0x78AE60, 8),
    "acidray_texture_name": (0x78AEC0, 8),
    "firearrowsmoke_texture_name": (0x78AEC8, 15),
    "stench_texture_name": (0x78AEB0, 7),
    "link_texture_name": (0x78AEB8, 5),
    "protectiona_texture_name": (0x78AEF4, 12),
    "protectione_texture_name": (0x78AF0C, 12),
    "protectionf_texture_name": (0x78AF18, 12),
    "antimagic_texture_name": (0x78AF00, 10),
    "stun_texture_name": (0x78AE28, 5),
    "clayring_texture_name": (0x78AE3C, 9),
    "teleport_texture_name": (0x78AE30, 9),
    "zoneexit_texture_name": (0x78ADA0, 9),
    "shapechange_texture_name": (0x78ADBC, 12),
    "bag_texture_name": (0x78ADB8, 4),
    "casting6_texture_name": (0x78AE10, 9),
    "casting2_texture_name": (0x78AE1C, 9),
    "casting1_texture_name": (0x78ADF8, 9),
    "tornado_factory_parameters": (0x486574, 0x227),
    "tornado_callback_selection": (0x48677D, 30),
    "lightningblast_factory_parameters": (0x487746, 0xFA),
    "lightningblast_callback_selection": (0x487804, 42),
    "casting_callback_selection": (0x486993, 74),
    "bansheecasting_callback_selection": (0x4869DD, 72),
    "nuke_factory_parameters": (0x486A72, 0x152),
    "nuke_callback_selection": (0x486AA6, 30),
    "mushroom_callback_selection": (0x486C08, 30),
    "blood1_callback_selection": (0x486CE4, 30),
    "firewall_callback_selection": (0x485C28, 0x11F),
    "mushroom_lifetime_phase_alpha": (0x488D1D, 0x87),
    "mushroom_update_motion": (0x488DB0, 0x140),
    "mushroom_source_growth_and_stop": (0x488F42, 0x48),
    "blood1_factory_parameters": (0x486CB4, 0x4E),
    "blood1_create_count_from_scale": (0x48EEBC, 0x43),
    "blood1_update_motion_alpha": (0x48F300, 0x78),
    "blood1_source_stop": (0x48F380, 0x23),
    "blood1_alpha_table": (0x78A6B4, 28),
    "blood_phase_type_selection": (0x486E17, 0x63),
    "a4blood_factory_parameters": (0x486EE8, 0x1DF),
    "a4blood_radial_birth": (0x48F3B0, 0x276),
    "a4blood_velocity_and_ground_update": (0x48F670, 0x7E),
    "a4blood_source_initialization_and_stop": (0x48F6F0, 0xF2),
    "firewall_ground_snap_and_wall_offset": (0x48ED82, 0x8E),
    "firewall_attempts_from_wall_length": (0x48EE20, 0x75),
    "firearrow_callback_selection": (0x4858A8, 0x36),
    "acidray_callback_selection": (0x485978, 0x20),
    "bluegas_callback_selection": (0x485B39, 0x34),
    "link_callback_selection": (0x485A65, 0x2E),
    "sphere_callback_selection": (0x4856BB, 0x28),
    "sphere_type_texture_selection": (0x4856E6, 0x4A),
    "sphere_antimagic_factory_parameters": (0x48560E, 0x3A),
    "sphere_birth_speed_selection": (0x48E3A6, 0x130),
    "sphere_surface_and_phase_initialization": (0x48E4D6, 0x13B),
    "sphere_type_size_selection_create": (0x48E718, 0xA0),
    "sphere_lifetime_rotation_and_projection": (0x48E7C0, 0x17D),
    "sphere_type_size_selection_update": (0x48E93D, 0xA0),
    "sphere_size_table": (0x73CB38, 64),
    "sphere_antimagic_size_table": (0x73CB78, 24),
    "link_basis_and_source_initialization": (0x48D8D8, 0x378),
    "link_birth_motion_and_phase": (0x48DC50, 0x1D6),
    "link_arc_motion_alpha_and_finish": (0x48DE30, 0x230),
    "clayring_callback_selection": (0x4870DC, 0x42),
    "clayring_source_initialization": (0x487147, 0x19E),
    "clayring_anchor_and_trail_birth": (0x48F7F0, 0x2B1),
    "clayring_anchor_and_trail_update": (0x48FAB0, 0x124),
    "clayring_source_radius_rotation_and_ground": (0x48FBE0, 0x1C2),
    "clayring_radius_table": (0x78A6F4, 64),
    "clayring_alpha_table": (0x78A734, 64),
    "teleport_callback_selection": (0x4872EF, 0x42),
    "teleport_radial_birth_and_phase_partition": (0x48FDB0, 0x21F),
    "teleport_signed_vertical_and_alpha_birth": (0x48FFCF, 0x8E),
    "teleport_phase_vertical_scale_and_alpha_update": (0x490060, 0xA1),
    "teleport_source_stop": (0x490110, 0x19),
    "modifier_callback_selection": (0x487461, 0x44),
    "modifier_source_allocation_and_axes": (0x490187, 0x25D),
    "modifier_source_initialization": (0x4903E4, 0x1DD),
    "modifier_anchor_and_trail_birth": (0x4905C1, 0x36F),
    "modifier_anchor_and_trail_update": (0x490990, 0x15F),
    "modifier_source_motion_and_stop": (0x490B00, 0x10A),
    "modifier_birth_alpha_table": (0x78A774, 44),
    "modifier_negative_alpha_table": (0x78A800, 52),
    "modifier_positive_alpha_table": (0x78A88C, 52),
    "zoneexit_callback_selection": (0x48783B, 0x4D),
    "zoneexit_birth_basis_size_and_ground": (0x492D30, 0x210),
    "zoneexit_phase_rise_and_alpha_update": (0x492F40, 0x70),
    "zoneexit_alpha_table": (0x78AA38, 64),
    "path_callback_selection": (0x487888, 0x45),
    "path_carrier_prediction_and_birth": (0x492FC0, 0x2C0),
    "path_lifetime_and_brightness_update": (0x493280, 0x70),
    "path_brightness_table": (0x78AA78, 64),
    "pathdestination_callback_selection": (0x4879C8, 0x78),
    "pathdestination_ring_cross_birth": (0x493300, 0xF0),
    "pathdestination_motion_scale_alpha_update": (0x4933F0, 0x100),
    "pathdestination_source_projection_stop": (0x4934F0, 0x70),
    "pathdestination_dx_table": (0x78AAB8, 16),
    "pathdestination_dy_table": (0x78AAC8, 16),
    "pathdestination_alpha_table": (0x78AAD8, 64),
    "moshka_callback_selection": (0x487A40, 0x34),
    "moshka_source_allocation": (0x487A77, 0x11D),
    "moshka_anchor_and_trail_birth": (0x493560, 0x210),
    "moshka_anchor_and_trail_update": (0x493770, 0x150),
    "moshka_source_motion": (0x4938C0, 0x450),
    "moshka_color_table": (0x73CBA4, 144),
    "portalstar_callback_selection": (0x487D03, 0x3B),
    "portalstar_random_scale_update": (0x493D50, 0x80),
    "cylinder_callback_and_texture_selection": (0x487D3E, 0x1AC),
    "cylinder_anchor_and_trail_birth": (0x493DE0, 0x390),
    "cylinder_anchor_and_trail_update": (0x494170, 0x1D0),
    "cylinder_source_orbit_and_stop": (0x494340, 0x130),
    "cylinder_alpha_table": (0x78AB18, 128),
    "portal_callback_selection": (0x487B94, 0x16F),
    "portal_head_and_trail_birth": (0x494470, 0x300),
    "portal_phase_motion_scale_alpha_update": (0x494770, 0x1C0),
    "portal_source_update": (0x494930, 0x20),
    "moshka_alpha_table": (0x73CC34, 64),
    "acidray_width_table": (0x78A61C, 76),
    "acidray_size_table": (0x78A668, 76),
    "global_render_lookup_type_0x2001": (0x40EBD8, 18),
    "scale_mutator_all_fields": (0x485480, 0xA3),
    "global_render_lookup_call": (0x4575AC, 26),
    "spawn_initial_color_white": (0x48E9EC, 21),
    "tendri_callback_selection": (0x485C28, 37),
    "ordinary_update_scale_and_motion": (0x48ECFD, 96),
    "special_spawn_random_offset": (0x48EDC4, 64),
    "attach_copies_color_endpoint": (0x496A4A, 12),
    "scheduler_refreshes_motion_fields": (0x496ECC, 82),
    "scheduler_particle_snapshot": (0x4972DE, 48),
    "render_position_scale_interpolation": (0x497B4C, 76),
    "render_argb_interpolation": (0x497B98, 236),
    "particle_submission_inputs": (0x502F8E, 54),
    "particle_pipeline_dispatch": (0x503183, 32),
    "scheduler_update_call": (0x4972F5, 24),
    "scheduler_spawn_call": (0x497514, 24),
    "scheduler_fallback_spawn_call": (0x497575, 24),
    "main_world_quant_call": (0x457338, 59),
    "world_set_tag_dispatch": (0x481F5B, 34),
    "world_set_wind_reads": (0x48223D, 207),
    "world_set_time_ambient_sun_dispatch": (0x4821DC, 0x131),
    "weather_rgb_table_parse": (0x563B80, 0x2E0),
    "weather_hour_interpolation": (0x566691, 0x103),
    "weather_sun_direction": (0x566794, 0x2BC),
    "world_particle_batch_call": (0x564471, 25),
    "world_wind_setter_fields": (0x55FCB0, 35),
    "world_wind_wrapper_call": (0x564190, 86),
    "world_wind_script_stream_and_call": (0x669800, 66),
    "world_wind_handler_registration": (0x6673E7, 36),
    "setwind_metadata_record": (0x78C460, 16),
    "batch_emitter_scheduler_call": (0x4984D4, 31),
    "torch_client_class_name": (0x52D190, 6),
    "torch_mob_strength_and_link_fields": (0x46B3DB, 178),
    "torch_runtime_hardcoded_campfire": (0x592968, 32),
    "torch_runtime_light_and_sound_dispatch": (0x592986, 78),
    "torch_particle_factory_call": (0x52D38E, 18),
    "torch_particle_prewarm_loop": (0x52D3F0, 30),
    "torch_light_setup_arguments": (0x592986, 63),
    "torch_light_position_and_radius": (0x52D42C, 235),
    "native_d3d_light_range_and_attenuation": (0x68CF46, 75),
    "native_point_light_squared_falloff": (0x68D450, 0x141),
    "native_terrain_point_light_normal_factor": (0x68D5A0, 0x188),
    "terrain_local_light_helper_call": (0x6C3339, 0x26),
    "terrain_local_light_channel_maximum": (0x6C3431, 0x1AF),
    "terrain_global_light_channel_maximum": (0x6C3965, 0x8B),
    "terrain_global_then_local_calls": (0x6C3ADD, 0x1D),
    "generic_light_z_offset_and_registration": (0x511830, 0x11C),
    "spatial_light_radius_test": (0x6CAC6D, 0xF2),
    "blend_additive_value_2": (0x498A8A, 29),
    "blend_alpha_value_6": (0x498AA7, 28),
    "direct3d_source_blend_srcalpha": (0x56A1CD, 18),
    "direct3d_destination_blend_invsrcalpha": (0x56A1DF, 18),
    "particle_spawn_random_calls": (0x48EA2B, 0x1E5),
    "particle_late_survival_random": (0x48EC8A, 0x30),
    "isaac_golden_ratio_and_mix": (0x49F16F, 165),
    "isaac_global_state_access": (0x48EA2B, 40),
}
CONSTANTS = {
    "half_float": (0x73B86C, "f"),
    "three_tenths_float": (0x73C54C, "f"),
    "u32_normalization_double": (0x73C468, "d"),
    "half_double": (0x73CC88, "d"),
    "late_motion_float": (0x73CD18, "f"),
    "late_scale_growth_float": (0x73CE38, "f"),
    "logical_quant_normalization_float": (0x73C348, "f"),
    "logical_quant_milliseconds": (0x78FF34, "I"),
    "torch_light_radius_multiplier": (0x73BAAC, "f"),
    "generic_light_z_offset": (0x73DBE8, "f"),
    "rgb_normalization_float": (0x73C36C, "f"),
    "weather_day_hours_float": (0x73CE28, "f"),
    "weather_sunrise_float": (0x73F3F4, "f"),
    "weather_sunset_float": (0x73F3F8, "f"),
    "weather_night_elevation_span_float": (0x73F74C, "f"),
    "weather_day_fraction_float": (0x73F748, "f"),
    "weather_minimum_elevation_float": (0x73BAF0, "f"),
    "pi_float": (0x73F3D0, "f"),
    "degrees_to_radians_float": (0x73BA30, "f"),
    "tornado_height_float": (0x73CB18, "f"),
    "tornado_speed_factor_float": (0x73CCA0, "f"),
    "sphere_carrier_radius_multiplier": (0x73CDF4, "f"),
    "sphere_surface_radius_multiplier": (0x73CE08, "f"),
    "sphere_acid_size_multiplier": (0x73CC80, "f"),
    "sphere_antimagic_attempt_multiplier": (0x73CE28, "f"),
    "sphere_antimagic_rare_size_base": (0x73CE18, "f"),
    "modifier_positive_direction_factor": (0x73CEAC, "f"),
    "modifier_negative_direction_factor": (0x73CDF8, "f"),
    "modifier_positive_scale_factor": (0x73CEA4, "f"),
    "modifier_negative_scale_factor": (0x73CEA8, "f"),
    "a4blood_particle_scale_random_multiplier": (0x73CE68, "d"),
    "a4blood_source_radius_random_multiplier": (0x73CE70, "d"),
    "a4blood_vertical_acceleration_random_multiplier": (0x73CE60, "d"),
    "a4blood_terminal_velocity_random_multiplier": (0x73CE58, "d"),
    "link_angular_step_multiplier": (0x73BAD4, "f"),
    "link_arc_height_multiplier": (0x73CD70, "f"),
    "link_alpha_multiplier": (0x73C34C, "f"),
    "clayring_source_angle_step": (0x73CC98, "d"),
    "clayring_direction_cosine": (0x73CD50, "f"),
    "clayring_direction_sine": (0x73CE78, "f"),
    "teleport_angle_normalization": (0x73CE88, "d"),
    "teleport_positive_vertical_multiplier": (0x73CE80, "f"),
    "teleport_negative_vertical_multiplier": (0x73CE7C, "f"),
    "zoneexit_size_base": (0x73CF2C, "f"),
    "zoneexit_size_random_low": (0x73C538, "f"),
    "zoneexit_size_random_multiplier": (0x73CE10, "d"),
    "pathdestination_ring_angle_step": (0x73CF38, "f"),
    "pathdestination_arm_length_step": (0x73CF3C, "f"),
    "pathdestination_radius": (0x73CF30, "d"),
    "pathdestination_offset_growth": (0x73CEE0, "f"),
    "moshka_phase_size_decay": (0x73CF40, "f"),
    "moshka_source_negative_extent": (0x73CF44, "f"),
    "moshka_source_extent": (0x73BAD4, "f"),
    "moshka_source_jitter": (0x73C52C, "f"),
    "moshka_anchor_size": (0x73CD18, "f"),
    "portalstar_scale_low": (0x73CCA8, "f"),
    "portalstar_scale_random_multiplier": (0x73CCB0, "d"),
    "portal_radius": (0x73CC78, "f"),
    "portal_height": (0x73CC7C, "f"),
    "portal_trail_scale_factor": (0x73C54C, "f"),
    "portal_trail_rotation_step": (0x73B850, "f"),
    "portal_head_scale": (0x73C51C, "f"),
    "portal_vertical_growth": (0x73CDE8, "f"),
    "cylinder_circle_radius_factor": (0x73C538, "f"),
    "cylinder_source_height_factor": (0x73BAD4, "f"),
    "cylinder_vertical_growth": (0x73CF50, "f"),
    "cylinder_trail_tangent_factor": (0x73CDE0, "f"),
    "cylinder_trail_scale_decay": (0x73CD70, "f"),
    "lightningblast_gravity_float": (0x73CF28, "f"),
    "firearrow_head_size_float": (0x73B868, "f"),
    "firearrow_trail_size_float": (0x73BAD4, "f"),
    "firearrow_early_growth_float": (0x73CDF8, "f"),
    "firearrow_regular_velocity_growth_float": (0x73BAC4, "f"),
    "firearrow_slow_velocity_decay_float": (0x73CD50, "f"),
    "bluegas_radius_factor_float": (0x73C54C, "f"),
    "bluegas_attempt_factor_float": (0x73BF34, "f"),
    "bluegas_size_factor_float": (0x73C538, "f"),
    "bluegas_vertical_speed_min_float": (0x73CCD8, "f"),
    "bluegas_vertical_speed_max_float": (0x73CD74, "f"),
    "bluegas_vertical_acceleration_float": (0x73CDE4, "f"),
    "bluegas_size_growth_float": (0x73CD40, "f"),
}


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


class PeImage:
    def __init__(self, data: bytes):
        self.data = data
        if len(data) < 0x40 or data[:2] != b"MZ":
            raise ValueError("not a DOS/PE image")
        pe_offset = struct.unpack_from("<I", data, 0x3C)[0]
        if pe_offset + 24 > len(data) or data[pe_offset:pe_offset + 4] != b"PE\0\0":
            raise ValueError("PE header is missing or truncated")
        coff = pe_offset + 4
        section_count = struct.unpack_from("<H", data, coff + 2)[0]
        optional_size = struct.unpack_from("<H", data, coff + 16)[0]
        optional = coff + 20
        if optional + optional_size > len(data):
            raise ValueError("PE optional header is truncated")
        magic = struct.unpack_from("<H", data, optional)[0]
        if magic != 0x10B:
            raise ValueError(f"expected PE32 optional header, found 0x{magic:04X}")
        self.image_base = struct.unpack_from("<I", data, optional + 28)[0]
        sections_offset = optional + optional_size
        self.sections = []
        for index in range(section_count):
            offset = sections_offset + index * 40
            if offset + 40 > len(data):
                raise ValueError("PE section table is truncated")
            raw_name, virtual_size, rva, raw_size, raw_offset = struct.unpack_from("<8s4I", data, offset)
            self.sections.append({
                "name": raw_name.rstrip(b"\0").decode("ascii", errors="replace"),
                "virtual_size": virtual_size, "rva": rva,
                "raw_size": raw_size, "raw_offset": raw_offset,
            })

    def read_va(self, va: int, size: int) -> bytes:
        rva = va - self.image_base
        for section in self.sections:
            extent = max(section["virtual_size"], section["raw_size"])
            if section["rva"] <= rva and rva + size <= section["rva"] + extent:
                offset = section["raw_offset"] + rva - section["rva"]
                if offset + size <= len(self.data):
                    return self.data[offset:offset + size]
        raise ValueError(f"VA range 0x{va:08X}+0x{size:X} is not file-backed")


def report(path: Path, required_sha256: str | None = None) -> dict[str, Any]:
    data = path.read_bytes()
    fingerprint = sha256(data)
    if required_sha256 and fingerprint.upper() != required_sha256.upper():
        raise ValueError(f"executable SHA-256 {fingerprint} does not match required {required_sha256.upper()}")
    image = PeImage(data)
    ranges = {}
    for name, (start, end) in RANGES.items():
        payload = image.read_va(start, end - start)
        ranges[name] = {"start_va": f"0x{start:08X}", "end_va": f"0x{end:08X}",
                        "size": len(payload), "sha256": sha256(payload)}
    sites = {}
    for name, (address, size) in SITES.items():
        sites[name] = {"va": f"0x{address:08X}",
                       "bytes": image.read_va(address, size).hex().upper()}
    constants = {}
    for name, (address, kind) in CONSTANTS.items():
        payload = image.read_va(address, struct.calcsize("<" + kind))
        constants[name] = {"va": f"0x{address:08X}", "bytes": payload.hex().upper(),
                           "value": struct.unpack("<" + kind, payload)[0]}
    vtable_entry = struct.unpack("<I", image.read_va(0x73CFD0 + 0x18, 4))[0]
    return {
        "format": "ei-particle-binary-report-v1",
        "source": {"path": str(path.resolve()), "size": len(data), "sha256": fingerprint},
        "image_base": f"0x{image.image_base:08X}", "sections": image.sections,
        "particle_vtable_scheduler_slot": {
            "slot_va": "0x0073CFE8", "target_va": f"0x{vtable_entry:08X}"},
        "ranges": ranges, "sites": sites, "constants": constants,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("exe", type=Path)
    parser.add_argument("--output", type=Path, help="JSON destination; stdout when omitted")
    parser.add_argument("--require-reference", action="store_true")
    args = parser.parse_args(argv)
    try:
        result = report(args.exe, REFERENCE_SHA256 if args.require_reference else None)
        encoded = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
