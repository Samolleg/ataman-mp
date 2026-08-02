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

    def test_terrain_enters_only_directional_static_shadow_passes(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("m_pLand->drawShadow", view)
        self.assertIn("terrainShadowsEnabled", view)
        static_start = view.index("if(rebuildStatic)")
        local_start = view.index("if(rebuildLocal)", static_start)
        directional = view[static_start:local_start]
        local = view[local_start:view.index("m_shadowInitialized = true", local_start)]
        self.assertGreaterEqual(directional.count("drawTerrainShadow"), 2)
        self.assertNotIn("drawTerrainShadow", local)

    def test_terrain_generation_and_toggle_invalidate_static_atlases(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.h").read_text(encoding="utf-8")

        self.assertIn("shadowGeneration", landscape)
        self.assertIn("terrainShadowGeneration", view)
        self.assertIn("terrainShadowsEnabled", view)
        self.assertIn("staticHash.addData", view)
        self.assertIn("farStaticHash.addData", view)

    def test_angle_and_minimap_safeguards_remain_fail_closed(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("!m_openGlesCompatibility", view)
        self.assertIn("!m_minimapCaptureActive", view)
        self.assertIn("terrainShadowsEnabled", view)

    def test_terrain_uses_explicit_depth_bias_and_objects_do_not_inherit_it(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        vertex = (ROOT / "assets/shaders/vshader.vsh").read_text(encoding="utf-8")

        self.assertIn("u_shadowDepthBias", vertex)
        self.assertIn("gl_Position.z += u_shadowDepthBias * gl_Position.w", vertex)
        terrain_draw = view.index("m_pLand->drawShadow")
        terrain_bias = view.rfind('setUniformValue("u_shadowDepthBias"', 0, terrain_draw)
        object_reset = view.find('setUniformValue("u_shadowDepthBias", 0.0f)', terrain_draw)
        object_draw = view.find("queue.flushShadow", terrain_draw)
        self.assertGreaterEqual(terrain_bias, 0)
        self.assertGreater(object_reset, terrain_draw)
        self.assertGreater(object_draw, object_reset)
        self.assertNotIn("GL_POLYGON_OFFSET_FILL", view)


if __name__ == "__main__":
    unittest.main()
