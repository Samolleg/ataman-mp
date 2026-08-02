from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class TerrainShadowContractTests(unittest.TestCase):
    """Focused regression for terrain never entering the sun shadow atlas."""

    def test_terrain_shadow_is_a_distinct_persisted_toggle(self):
        settings = (ROOT / "source/app/settings.cpp").read_text(encoding="utf-8")
        settings_ui = (ROOT / "source/app/settings.ui").read_text(encoding="utf-8")

        self.assertIn('COptBool("renderTerrainShadows"', settings)
        self.assertIn('name="renderTerrainShadows"', settings_ui)

    def test_terrain_uses_heightfield_visibility_not_shadow_atlas_self_comparison(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")
        shader = (ROOT / "assets/shaders/landshader.fsh").read_text(encoding="utf-8")

        self.assertNotIn("m_pLand->drawShadow", view)
        self.assertIn("terrainShadowsEnabled", view)
        self.assertIn("updateTerrainShadowVisibility", view)
        self.assertIn("traceTerrainSunVisibility", landscape)
        self.assertIn("v_terrainSunVisibility", shader)

    def test_terrain_and_object_shadows_form_one_strongest_blocker_silhouette(self):
        shader = (ROOT / "assets/shaders/landshader.fsh").read_text(encoding="utf-8")

        self.assertIn("float combinedShadowVisibility = min(shadow,", shader)
        self.assertIn("clamp(v_terrainSunVisibility, 0.0, 1.0)", shader)
        self.assertNotIn("sunDiffuse\n                           * clamp(v_terrainSunVisibility", shader)
        self.assertEqual(shader.count("80.0 / 255.0"), 1)

    def test_terrain_field_rebuild_is_coalesced_off_the_render_thread(self):
        landscape_h = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")
        landscape_cpp = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        self.assertIn("QFuture<STerrainShadowFieldResult>", landscape_h)
        self.assertIn("QtConcurrent::run", landscape_cpp)
        self.assertIn("m_terrainShadowDesiredGeneration", landscape_h)
        self.assertIn("m_terrainShadowFutureGeneration", landscape_h)
        self.assertIn("m_terrainShadowFuture.isFinished()", landscape_cpp)
        self.assertIn("applyTerrainShadowVisibility", landscape_cpp)

    def test_terrain_generation_and_toggle_invalidate_visibility_field(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")

        self.assertIn("shadowGeneration", landscape)
        self.assertIn("terrainShadowsEnabled", view)
        self.assertIn("m_terrainShadowFieldGeneration", landscape)

    def test_disabling_toggle_immediately_uploads_fully_visible_terrain(self):
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        disabled = landscape.index("if(!enabled)")
        enabled = landscape.index("traceTerrainSunVisibility", disabled)
        transition = landscape[disabled:enabled]
        self.assertIn("fill(1.0f", transition)
        self.assertIn("applyTerrainShadowVisibility", transition)

    def test_angle_and_minimap_safeguards_remain_fail_closed(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("!m_openGlesCompatibility", view)
        self.assertIn("!m_minimapCaptureActive", view)
        self.assertIn("terrainShadowsEnabled", view)

    def test_terrain_shadow_path_has_no_depth_bias(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        vertex = (ROOT / "assets/shaders/vshader.vsh").read_text(encoding="utf-8")

        self.assertNotIn("u_shadowDepthBias", vertex)
        self.assertNotIn("GL_POLYGON_OFFSET_FILL", view)

    def test_map_weather_palette_uses_authored_allod_not_zone_type(self):
        main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")

        self.assertIn("setMapSkyType(m_mapTestEntry.skyType, m_mapTestEntry.allodId)",
                      main_window)

    def test_sun_rotation_and_terrain_length_match_game_proxy_contract(self):
        environment = (ROOT / "source/render/render_environment.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")

        azimuth_start = environment.index("const float azimuth")
        azimuth_end = environment.index(";", azimuth_start)
        self.assertNotIn("+ float(M_PI)", environment[azimuth_start:azimuth_end])
        self.assertIn("kTerrainShadowLengthScale", landscape)
        self.assertIn("/ kTerrainShadowLengthScale", landscape)


if __name__ == "__main__":
    unittest.main()
