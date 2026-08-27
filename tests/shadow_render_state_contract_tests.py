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

    def test_visible_frame_is_not_cleared_or_drawn_before_shadow_rebuilds_finish(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        paint = view.split("void CView::paintGL()", 1)[1].split(
            "bool CView::prepareForApplicationShutdown", 1
        )[0]
        draw = view.split("void CView::draw(", 1)[1].split(
            "SMinimapSceneCaptureResult CView::captureMinimapScene", 1
        )[0]

        # A shadow refresh may switch through several private FBOs.  Until it
        # finishes, the presentation target must retain the previous complete
        # frame; otherwise a compositor can expose a clear/sky-only frame.
        self.assertNotIn(
            "Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)", paint
        )
        self.assertIn("const auto beginVisibleFrame", draw)
        self.assertEqual(draw.count("beginVisibleFrame();"), 2)
        self.assertLess(
            draw.index("renderShadowMap(cameraPosition, cameraPivot"),
            draw.index("beginVisibleFrame();"),
        )
        visible_frame = draw.split("const auto beginVisibleFrame", 1)[1].split(
            "const auto visibleNodes", 1
        )[0]
        self.assertIn(
            "Clear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT)", visible_frame
        )
        self.assertIn("drawSkybox(camMatrix, environment)", visible_frame)


if __name__ == "__main__":
    unittest.main()
