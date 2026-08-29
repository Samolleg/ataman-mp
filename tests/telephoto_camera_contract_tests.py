import pathlib
import re
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class TelephotoCameraSourceContractTests(unittest.TestCase):
    def read(self, relative_path):
        return (ROOT / relative_path).read_text(encoding="utf-8")

    def test_multiplier_and_direction_are_persistent(self):
        settings = self.read("source/app/settings.cpp")
        option_order = self.read("assets/resources/optSet.txt")
        self.assertIn('new COptDouble("telephotoCameraMultiplier", 2.0)', settings)
        self.assertIn('new COptInt("telephotoCameraDirection", 0)', settings)
        self.assertIn("telephotoCameraMultiplier", option_order)
        self.assertIn("telephotoCameraDirection", option_order)
        self.assertNotIn('COptBool("telephotoCameraEnabled"', settings)
        self.assertNotIn("telephotoCameraEnabled", option_order)

    def test_camera_bar_exposes_bounded_session_only_toggle(self):
        controls = self.read("source/ui/camera_telephoto_controls.cpp")
        self.assertIn('setObjectName("telephotoCameraEnabled")', controls)
        self.assertIn('setObjectName("telephotoCameraMultiplier")', controls)
        self.assertIn('setObjectName("telephotoCameraDirection")', controls)
        self.assertIn("SouthwestToNortheast", controls)
        self.assertIn("SoutheastToNorthwest", controls)
        self.assertIn("NortheastToSouthwest", controls)
        self.assertIn("NorthwestToSoutheast", controls)
        self.assertRegex(controls, r"setRange\(\s*ei::kTelephotoMultiplierMinimum\s*,\s*ei::kTelephotoMultiplierMaximum\s*\)")
        self.assertIn("setChecked(false)", controls)
        main_window = self.read("source/app/main_window.cpp")
        self.assertIn("telephotoCameraMultiplier", main_window)
        self.assertIn("telephotoCameraDirection", main_window)
        self.assertIn("setTelephotoCameraEnabled", main_window)
        camera = self.read("source/render/camera.cpp")
        self.assertGreaterEqual(camera.count("m_telephotoDirection"), 8)

    def test_world_and_query_paths_share_active_matrices(self):
        view = self.read("source/render/view.cpp")
        self.assertIn("updateCameraProjection", view)
        self.assertIn("logicalCullingPosition", view)
        self.assertIn(
            "scheduleUnitAppearancePreparation(logicalCameraPosition, cameraMoving)",
            view,
        )
        self.assertIn("renderQueue.begin(&m_program, cameraPosition, logicalCameraPosition)", view)
        self.assertIn(
            "renderShadowMap(logicalCameraPosition, cameraPivot", view
        )
        self.assertGreaterEqual(view.count('setUniformValue("u_projMmatrix", m_projection)'), 5)
        self.assertGreaterEqual(view.count("unproject(view, m_projection"), 3)
        self.assertIn("CViewFrustum frustum(m_projection * camMatrix)", view)

    def test_rotation_is_blocked_at_camera_boundary(self):
        camera = self.read("source/render/camera.cpp")
        self.assertGreaterEqual(camera.count("cameraRotationInputAllowed"), 2)
        view = self.read("source/render/view.cpp")
        operation = self.read("source/app/operationmanager.cpp")
        self.assertIn("telephotoCameraEnabled() || width()", view)
        self.assertIn("current->m_pView->telephotoCameraEnabled()", operation)

    def test_user_visible_strings_have_russian_catalog_entries(self):
        catalog = self.read("translations/ei_mapper_ru.ts")
        self.assertIn("<source>Telephoto</source>", catalog)
        self.assertIn("Телефотографическая камера JamaisVu", catalog)
        self.assertIn("<source>Multiplier:</source>", catalog)
        self.assertIn("<source>Direction:</source>", catalog)
        self.assertIn("<source>SW → NE</source>", catalog)
        self.assertIn("<translation>ЮЗ → СВ</translation>", catalog)
        self.assertIn("<source>SE → NW</source>", catalog)
        self.assertIn("<translation>ЮВ → СЗ</translation>", catalog)
        self.assertIn("<source>NE → SW</source>", catalog)
        self.assertIn("<translation>СВ → ЮЗ</translation>", catalog)
        self.assertIn("<source>NW → SE</source>", catalog)
        self.assertIn("<translation>СЗ → ЮВ</translation>", catalog)


if __name__ == "__main__":
    unittest.main()
