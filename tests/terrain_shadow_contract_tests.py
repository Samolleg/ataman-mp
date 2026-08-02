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


if __name__ == "__main__":
    unittest.main()
