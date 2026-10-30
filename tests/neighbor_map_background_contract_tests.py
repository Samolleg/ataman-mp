import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class NeighborMapBackgroundContractTests(unittest.TestCase):
    def test_background_landscape_is_fail_closed_for_editing_and_saving(self):
        landscape_h = source("source/render/landscape.h")
        landscape_cpp = source("source/render/landscape.cpp")
        self.assertIn("createReadOnlyBackground", landscape_h)
        self.assertIn("readMapInternal(path, false)", landscape_cpp)
        self.assertGreaterEqual(
            landscape_cpp.count("if(m_readOnlyBackground)\n        return false;"),
            2,
        )
        self.assertIn(
            "if(!m_readOnlyBackground) m_bDirty = bDirty", landscape_h
        )

    def test_neighbors_are_opt_in_and_excluded_from_minimap(self):
        view = source("source/render/view.cpp")
        self.assertIn(
            'QStringLiteral("ui/viewportVisuals/neighborLandscapes"), false',
            view,
        )
        terrain_guard = view.index(
            "if(!m_minimapCaptureActive && m_landscapeVisible\n"
            "               && m_neighborLandscapesVisible"
        )
        water_guard = view.index(
            "if(!m_minimapCaptureActive && !m_neighborLandscapes.isEmpty()"
        )
        self.assertLess(terrain_guard, water_guard)

    def test_only_the_active_landscape_reaches_editor_subsystems(self):
        view = source("source/render/view.cpp")
        neighbor_section = view[
            view.index("void CView::loadNeighborLandscapes()") :
            view.index("bool CView::isObjectVisualVisible")
        ]
        self.assertNotIn("m_landscapeTools", neighbor_section)
        self.assertNotIn("m_mprDocument", neighbor_section)
        self.assertNotIn("m_activeMob", neighbor_section)
        self.assertIn("CLandscape::createReadOnlyBackground()", neighbor_section)
        self.assertIn("loaded.landscape->readPreparedBackground(", neighbor_section)

    def test_hiding_neighbors_clears_active_list_and_bounds_retained_resources(self):
        view = source("source/render/view.cpp")
        resources = source("source/resources/resourcemanager.cpp")
        clear_section = view[
            view.index("void CView::clearNeighborLandscapes(bool reloading)") :
            view.index("void CView::loadNeighborLandscapes()")
        ]
        self.assertIn("m_neighborLandscapes.clear()", clear_section)
        self.assertIn("if(!reloading)trimNeighborRenderCache()", clear_section)
        self.assertNotIn("releaseTerrainTexture", clear_section)
        self.assertIn("releaseTerrainTexture(m_baseAtlasTexture)",
                      source("source/render/landscape.cpp"))
        self.assertIn("texture->destroy()", resources)
        self.assertIn("delete texture", resources)


if __name__ == "__main__":
    unittest.main()
