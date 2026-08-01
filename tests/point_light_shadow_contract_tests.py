from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PointLightShadowContractTests(unittest.TestCase):
    """Focused regression for LIGHT_SHADOW being dropped by the renderer."""

    def test_authored_shadow_flags_reach_shadow_light_collection(self):
        environment = (ROOT / "source/render/render_environment.h").read_text(
            encoding="utf-8"
        )
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("bool castsShadow", environment)
        self.assertIn("sceneLight.castsShadow = light->shadowEnabled();", view)
        self.assertIn("sceneLight.castsShadow = true;", view)
        self.assertIn("shadowLights.append(sceneLight);", view)

    def test_shadow_light_membership_is_not_camera_frustum_dependent(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        collection_start = view.index("QVector<ei::SScenePointLight> sceneLights;")
        collection_end = view.index("RenderPerformanceContext& performance", collection_start)
        collection = view[collection_start:collection_end]
        self.assertIn("QVector<ei::SScenePointLight> shadowLights;", collection)
        self.assertIn("shadowLights.append(sceneLight);", collection)
        self.assertIn("frustum.intersectsSphere", collection)
        self.assertRegex(collection, re.compile(
            r"sceneLight\.castsShadow = light->shadowEnabled\(\);\s*"
            r"if\(sceneLight\.castsShadow\)\s*"
            r"shadowLights\.append\(sceneLight\);\s*"
            r"if\(!cullingEnabled\s*\|\|\s*frustum\.intersectsSphere",
            re.DOTALL,
        ))
        self.assertRegex(collection, re.compile(
            r"sceneLight\.castsShadow = true;\s*"
            r"shadowLights\.append\(sceneLight\);\s*"
            r"if\(!cullingEnabled\s*\|\|\s*frustum\.intersectsSphere",
            re.DOTALL,
        ))
        self.assertNotIn("shadowLights.resize", collection)

        self.assertIn(
            "environment.sunStrength, shadowLights, cameraPivot)", view
        )
        self.assertIn(
            "renderShadowMap(cameraPosition, cameraPivot, environment, shadowLights,",
            view,
        )

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
