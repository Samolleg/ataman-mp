import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class LandscapeSculptPerformanceContractTests(unittest.TestCase):
    def test_sculpt_preview_uses_incremental_sector_updates(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        landscape = (ROOT / "source/render/landscape.cpp").read_text(encoding="utf-8")
        sector = (ROOT / "source/render/sector.cpp").read_text(encoding="utf-8")

        self.assertIn("previewCanonicalTerrainSectorDelta", view)
        self.assertIn("previewCanonicalOverlaySectorDelta", view)
        self.assertIn("previewCanonicalTerrainSectorDelta", landscape)
        self.assertIn("previewCanonicalTerrainDelta", sector)
        self.assertIn("updateLandTiles", sector)

    def test_incremental_uploads_orphan_live_vertex_buffers(self):
        sector = (ROOT / "source/render/sector.cpp").read_text(encoding="utf-8")
        terrain_delta = sector.split(
            "bool CSector::previewCanonicalTerrainDelta", 1
        )[1].split("bool CSector::previewCanonicalOverlayPayload", 1)[0]
        land_update = sector.split("void CSector::updateLandTiles", 1)[1].split(
            "void CSector::updateWaterTiles", 1
        )[0]
        water_update = sector.split("void CSector::updateWaterTiles", 1)[1].split(
            "void CSector::updateWaterDrawData", 1
        )[0]

        self.assertIn("updateLandTiles", terrain_delta)
        self.assertIn("m_vertexBuf.allocate", land_update)
        self.assertNotIn("m_vertexBuf.write", land_update)
        self.assertIn("m_waterVertexBuf.allocate", water_update)
        self.assertNotIn("m_waterVertexBuf.write", water_update)

    def test_stationary_hold_keeps_one_bounded_stamp_cadence(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        self.assertIn("m_sculptRepeatTimer->setInterval(33);", view)
        self.assertIn("sculptTerrainTowardLatestPoint();", view)
        self.assertIn("const int steps = distance > spacing", view)

    def test_dirty_title_refresh_is_deferred_until_stroke_release(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        sync = view.split("void CView::syncMprDirtyState()", 1)[1].split(
            "bool CView::applyMprParametersPreview", 1
        )[0]
        self.assertIn(
            "if(m_sculptStrokeActive || m_tileBrushStrokePolicy.isActive())",
            sync,
        )
        self.assertLess(
            sync.index("m_tileBrushStrokePolicy.isActive()"),
            sync.index("emit updateMainWindowTitle"),
        )

    def test_release_diagnostics_do_not_clone_and_rewind_the_whole_terrain(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        finish = view.split("void CView::finishSculptStroke()", 1)[1].split(
            "void CView::setDurty", 1
        )[0]

        self.assertIn("diagnoseApplied", finish)
        self.assertNotIn("CTerrainEditModel baseline = m_terrainEdit", finish)
        self.assertNotIn("baseline.apply", finish)

    def test_release_reprojects_only_the_changed_terrain_footprint(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        finish = view.split("void CView::finishSculptStroke()", 1)[1].split(
            "void CView::setDurty", 1
        )[0]

        self.assertIn("terrainHeightInfluenceBounds", finish)
        self.assertIn("reprojectForCurrentTerrain(influence)", finish)
        self.assertNotIn("mob->reprojectForCurrentTerrain();", finish)

    def test_overlay_sculpt_miss_still_owns_the_complete_left_drag(self):
        view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
        press = view.split("void CView::mousePressEvent", 1)[1].split(
            "void CView::sculptTerrainAt", 1
        )[0]
        move = view.split("void CView::mouseMoveEvent", 1)[1].split(
            "void CView::mouseReleaseEvent", 1
        )[0]
        release = view.split("void CView::mouseReleaseEvent", 1)[1].split(
            "QPoint CView::operationAnchor", 1
        )[0]

        self.assertIn("m_sculptGestureCaptured = true", press)
        self.assertIn("if(m_sculptGestureCaptured", move)
        self.assertIn("if(m_sculptGestureCaptured", release)
        self.assertIn("m_sculptGestureCaptured = false", release)


if __name__ == "__main__":
    unittest.main()
