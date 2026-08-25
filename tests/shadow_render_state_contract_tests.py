from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class ShadowRenderStateContractTests(unittest.TestCase):
    """Regression contract for shadow-atlas state leaking into presentation."""

    def test_shadow_rebuild_restores_the_callers_clear_color(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        shadow_pass = view.split("bool CView::renderShadowMap", 1)[1].split(
            "QString CView::captureShadowDiagnostics", 1
        )[0]
        compact = " ".join(shadow_pass.split())

        save = "GetFloatv(GL_COLOR_CLEAR_VALUE, savedClearColor)"
        restore = (
            "ClearColor(savedClearColor[0], savedClearColor[1], "
            "savedClearColor[2], savedClearColor[3])"
        )
        atlas_clear = "ClearColor(1.0f, 1.0f, 1.0f, 1.0f)"

        self.assertIn(save, shadow_pass)
        self.assertIn(restore, compact)
        self.assertLess(shadow_pass.index(save), shadow_pass.index(atlas_clear))
        self.assertLess(compact.rindex(atlas_clear), compact.index(restore))
        self.assertLess(compact.index(restore), compact.rindex("return true;"))

    def test_fix_preserves_object_shadow_functionality(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

        self.assertIn("&& m_objectShadowsVisible", view)
        self.assertIn("renderShadowMap(cameraPosition, cameraPivot", view)
        self.assertIn("queue.flushShadow", view)

    def test_shadow_batch_does_not_expose_the_presentation_target_between_passes(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        shadow_pass = view.split("bool CView::renderShadowMap", 1)[1].split(
            "QString CView::captureShadowDiagnostics", 1
        )[0]
        end_target = shadow_pass.split("const auto endTarget", 1)[1].split(
            "if(rebuildStatic)", 1
        )[0]
        final_restore = shadow_pass[
            shadow_pass.rindex("endTarget(m_localShadowFbo") :
            shadow_pass.index("m_shadowInitialized = true")
        ]

        self.assertNotIn("target->release()", end_target)
        self.assertNotIn("bindDefault()", end_target)
        self.assertEqual(final_restore.count("bindDefault()"), 1)
        self.assertLess(
            shadow_pass.rindex("endTarget(m_localShadowFbo"),
            shadow_pass.rindex("bindDefault()"),
        )


if __name__ == "__main__":
    unittest.main()
