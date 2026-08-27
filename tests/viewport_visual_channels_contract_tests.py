import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class ViewportVisualChannelsContractTests(unittest.TestCase):
    def test_visuals_widget_owns_all_requested_channels(self):
        view = source("source/render/view.cpp")
        for token in (
            'tr("Visuals")',
            'tr("Show all")',
            'tr("Hide all")',
            'tr("Entrances")',
            'tr("Object helpers")',
            'tr("Static objects")',
            'tr("Dynamic objects")',
            'tr("Terrain")',
            'tr("Overlays")',
            'tr("Fog")',
            'tr("Terrain shadows")',
            'tr("Object shadows")',
            'tr("Scene lights")',
            'tr("Skybox")',
            'tr("Particle visuals")',
            'tr("Freeze animations")',
        ):
            self.assertIn(token, view)
        self.assertIn('m_environmentPreviewBody->setVisible(false)', view)

    def test_entrance_eye_is_removed_from_test_toolbar(self):
        main_window = source("source/app/main_window.cpp")
        main_header = source("source/app/main_window.h")
        self.assertNotIn("showEntrancesButton", main_window)
        self.assertNotIn("m_showEntrancesButton", main_header)
        self.assertIn("entranceOverlaysVisible()", main_window)

    def test_channels_gate_their_render_passes_and_helpers(self):
        view = source("source/render/view.cpp")
        for token in (
            "m_objectHelpersVisible",
            "m_staticObjectsVisible",
            "m_dynamicObjectsVisible",
            "m_objectShadowsVisible",
            "m_particleVisualsVisible",
            "m_fogVisible",
            "m_terrainShadowsVisible",
            "m_skyboxVisible",
            "isEditorObjectHelper(node)",
            "m_animationFreezeTime",
        ):
            self.assertIn(token, view)
        self.assertIn("if(m_particleVisualsVisible)", view)

    def test_static_dynamic_and_object_shadow_channels_own_their_render_paths(self):
        view = source("source/render/view.cpp")
        self.assertIn("isDynamicViewportObject(node)", view)
        self.assertIn("m_dynamicObjectsVisible", view)
        self.assertIn("m_staticObjectsVisible", view)
        self.assertIn("m_objectShadowsVisible", view)
        self.assertIn("&& m_objectShadowsVisible", view)

    def test_freeze_animations_uses_a_normal_checkbox(self):
        view = source("source/render/view.cpp")
        header = source("source/render/view.h")
        self.assertIn("new QCheckBox(tr(\"Freeze animations\")", view)
        self.assertIn("QCheckBox* m_freezeAnimationsToggle", header)
        self.assertNotIn('makeToggle("visualsFreezeAnimations"', view)

    def test_landscape_tools_and_visuals_remain_bidirectionally_synchronized(self):
        view = source("source/render/view.cpp")
        self.assertIn("syncVisualChannelButtons()", view)
        self.assertIn("setDrawLand(drawLand)", view)
        self.assertIn("setDrawWater(drawWater)", view)
        self.assertIn("setTerrainVisibility(m_bDrawLand, m_bDrawWater)", view)

    def test_environment_support_and_animation_setting_gate_controls(self):
        view = source("source/render/view.cpp")
        self.assertIn("updateVisualChannelAvailability()", view)
        self.assertIn("requestedLightingMode == 2", view)
        self.assertIn("m_openGlesCompatibility", view)
        self.assertIn('m_pSettings->opt("animateUnits")', view)

    def test_scene_lights_channel_mirrors_the_render_option(self):
        view = source("source/render/view.cpp")
        self.assertIn("m_sceneLightsVisualToggle", view)
        self.assertIn('m_pSettings->opt("renderSceneLights")', view)
        self.assertIn('m_pSettings->persistOptions()', view)


if __name__ == "__main__":
    unittest.main()
