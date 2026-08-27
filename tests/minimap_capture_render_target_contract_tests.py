from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class MinimapCaptureRenderTargetContractTests(unittest.TestCase):
    """The offscreen minimap renderer must never draw into the visible widget."""

    @classmethod
    def setUpClass(cls):
        cls.view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

    def test_capture_draw_keeps_the_current_offscreen_framebuffer(self):
        draw = self.view.split("void CView::draw(", 1)[1].split(
            "SMinimapSceneCaptureResult CView::captureMinimapScene", 1
        )[0]
        begin_scene = draw.split("const auto beginVisibleFrame", 1)[1].split(
            "const auto visibleNodes", 1
        )[0]
        compact = " ".join(begin_scene.split())

        # captureMinimapScene binds its private FBO before calling draw().  The
        # normal widget path may bind the default framebuffer, but capture must
        # retain the caller-owned FBO and its review-size viewport.
        guarded_default_bind = (
            "if(!m_minimapCaptureActive) { "
            "QOpenGLFramebufferObject::bindDefault(); "
            "ei::gl::Viewport(0, 0, m_width, m_height); }"
        )
        self.assertIn(guarded_default_bind, compact)
        self.assertEqual(begin_scene.count("bindDefault()"), 1)

    def test_both_capture_passes_explicitly_rebind_the_owned_target(self):
        capture = self.view.split(
            "SMinimapSceneCaptureResult CView::captureMinimapScene", 1
        )[1].split("void CView::loadLandscape", 1)[0]

        self.assertIn("const auto bindCaptureTarget", capture)
        self.assertEqual(capture.count("if(!bindCaptureTarget())"), 2)
        first_draw = capture.index("draw();")
        second_draw = capture.index("draw();", first_draw + 1)
        first_bind = capture.index("if(!bindCaptureTarget())")
        second_bind = capture.index("if(!bindCaptureTarget())", first_bind + 1)
        self.assertLess(first_bind, first_draw)
        self.assertLess(first_draw, second_bind)
        self.assertLess(second_bind, second_draw)


if __name__ == "__main__":
    unittest.main()
