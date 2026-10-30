from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


def function_body(source: str, signature: str, next_signature: str) -> str:
    return source.split(signature, 1)[1].split(next_signature, 1)[0]


class TerrainAtlasPresentationContractTests(unittest.TestCase):
    """Paint publication must not disturb the widget presentation target."""

    @classmethod
    def setUpClass(cls):
        cls.landscape = (ROOT / "source/render/landscape.cpp").read_text(
            encoding="utf-8"
        )
        cls.view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")

    def test_changed_slots_update_the_stable_preview_texture_in_place(self):
        upload = function_body(
            self.landscape,
            "bool CLandscape::previewTerrainAtlasIncremental",
            "bool CLandscape::previewTerrainAtlasRegridCopy",
        )

        self.assertIn("m_previewAtlasTexture->textureId()", upload)
        self.assertIn("glTexSubImage2D", upload)
        self.assertNotIn("new QOpenGLTexture", upload)
        self.assertNotIn("glGenFramebuffers", upload)
        self.assertNotIn("glBindFramebuffer", upload)
        self.assertNotIn("glCopyTexSubImage2D", upload)
        self.assertNotIn("previous->destroy()", upload)
        self.assertIn("framebufferTouched:false", upload)

    def test_partial_upload_rolls_back_pixels_before_reporting_failure(self):
        upload = function_body(
            self.landscape,
            "bool CLandscape::previewTerrainAtlasIncremental",
            "bool CLandscape::previewTerrainAtlasRegridCopy",
        )

        self.assertIn("rollbackTileImages", upload)
        self.assertIn("rollbackChangedSlots", upload)
        self.assertLess(
            upload.index("rollbackChangedSlots"),
            upload.index("return false;", upload.index("rollbackChangedSlots")),
        )

    def test_live_working_atlas_is_primed_before_paint_can_publish(self):
        catalog = function_body(
            self.view,
            "void CView::rebuildTerrainAutoCatalog",
            "void CView::startDeferredGlobalTerrainPaletteHydration",
        )

        ready = catalog.index("Modern terrain working atlas ready")
        prime = catalog.index("primeWorkingTerrainAtlasPreview", ready)
        deferred = catalog.index("startDeferredGlobalTerrainPaletteHydration", prime)
        self.assertLess(ready, prime)
        self.assertLess(prime, deferred)

    def test_completed_publication_uses_one_queued_widget_frame(self):
        present = function_body(
            self.view,
            "void CView::presentCompletedTerrainPublication",
            "void CView::finalizeVirtualTerrainAtlas",
        )

        self.assertNotIn("updateGL()", present)
        self.assertEqual(present.count("update();"), 1)

    def test_stroke_completion_does_not_reenter_qglwidget_swap(self):
        stroke_end = function_body(
            self.view,
            "void CView::endTileBrushGroup",
            "void CView::cancelTileBrushGroup",
        )

        # This handler runs for every completed stamp, including strokes that
        # neither generate nor upload atlas slots. An immediate updateGL()
        # paints and swaps recursively from the release event while earlier
        # terrain updates are still queued; completion must request only the
        # normal coalesced Qt paint event.
        self.assertNotIn("updateGL();", stroke_end)
        self.assertIn("update();", stroke_end)

    def test_fix_does_not_disable_object_shadows(self):
        self.assertIn("&& m_objectShadowsVisible", self.view)
        self.assertIn("renderShadowMap(logicalCameraPosition, cameraPivot", self.view)


if __name__ == "__main__":
    unittest.main()
