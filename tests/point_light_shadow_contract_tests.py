from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PointLightShadowContractTests(unittest.TestCase):
    """Focused regression for LIGHT_SHADOW being dropped by the renderer."""

    def test_authored_shadow_flag_reaches_scene_light(self):
        environment = (ROOT / "source/render/render_environment.h").read_text(
            encoding="utf-8"
        )
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("bool castsShadow", environment)
        self.assertIn("sceneLight.castsShadow = light->shadowEnabled();", view)

    def test_shadow_enabled_point_lights_drive_the_shadow_projection(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        environment = (ROOT / "source/render/render_environment.h").read_text(
            encoding="utf-8"
        )

        self.assertIn("shadowProjectionDirection", environment)
        self.assertIn("shadowProjectionDirection", view)
        self.assertIn("sceneLights", view)

    def test_existing_receivers_keep_sampling_the_combined_projection(self):
        for shader_name in ("fshader.fsh", "landshader.fsh"):
            shader = (ROOT / "assets/shaders" / shader_name).read_text(
                encoding="utf-8"
            )
            with self.subTest(shader=shader_name):
                self.assertIn("u_shadowsEnabled", shader)
                self.assertIn("cascadeVisibility", shader)

    def test_angle_compatibility_keeps_the_atlas_path_disabled(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("!m_openGlesCompatibility", view)
        self.assertIn("!shadowContext->isOpenGLES()", view)


if __name__ == "__main__":
    unittest.main()
