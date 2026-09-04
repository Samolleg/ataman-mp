import pathlib
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


class NeighborMapNavigationContractTests(unittest.TestCase):
    def test_neighbor_double_click_reaches_the_normal_map_pair_open_flow(self):
        view_h = source("source/render/view.h")
        view_cpp = source("source/render/view.cpp")
        main_window = source("source/app/main_window.cpp")
        layout_h = source("source/resources/neighbor_map_layout.h")

        expected = (
            "void mouseDoubleClickEvent(QMouseEvent* event) override;",
            "void CView::mouseDoubleClickEvent(QMouseEvent* event)",
            "void neighborMapOpenRequested(",
            "QString mobPath;",
            "&CView::neighborMapOpenRequested",
            "&MainWindow::openMapPair",
        )
        combined = "\n".join((view_h, view_cpp, main_window, layout_h))
        for needle in expected:
            self.assertTrue(needle in combined, f"missing navigation boundary: {needle}")

    def test_neighbor_pointer_gestures_do_not_reach_editing_or_selection(self):
        view_cpp = source("source/render/view.cpp")
        press_start = view_cpp.index("void CView::mousePressEvent(QMouseEvent* event)")
        press_end = view_cpp.index("void CView::sculptTerrainAt", press_start)
        press = view_cpp[press_start:press_end]
        neighbor_guard = press.index("neighborMapAtViewport(event->pos(), nullptr)")
        operation_dispatch = press.rindex("m_pOp->mousePressEvent(event)")

        self.assertLess(neighbor_guard, operation_dispatch)
        self.assertIn("m_neighborPointerGestureCaptured = true;", press)
        self.assertIn("event->accept();", press[neighbor_guard:operation_dispatch])

    def test_navigation_is_reentrant_safe_and_does_not_create_edit_history(self):
        view_cpp = source("source/render/view.cpp")
        start = view_cpp.index("void CView::mouseDoubleClickEvent(QMouseEvent* event)")
        end = view_cpp.index("void CView::sculptTerrainAt", start)
        handler = view_cpp[start:end]

        self.assertIn("m_neighborNavigationInProgress", handler)
        self.assertIn("m_landscapeInteractionSuspended", handler)
        self.assertIn("emit neighborMapOpenRequested", handler)
        for forbidden in ("m_pUndoStack", "setDirty", "save", "minimap", "Test Current Map"):
            self.assertNotIn(forbidden, handler)

    def test_invalid_target_is_rejected_before_dirty_prompt_or_workspace_mutation(self):
        main_window = source("source/app/main_window.cpp")
        start = main_window.index("void MainWindow::openMapPair(")
        end = main_window.index("void MainWindow::connectUiButtons()", start)
        handler = main_window[start:end]

        mpr_preflight = handler.index("CMprDocument targetMprDocument")
        mob_preflight = handler.index("CMobDocument targetMobDocument")
        mob_projection = handler.index("CMob::validateEditableProjection")
        dirty_prompt = handler.index("if(!isExitAllowed())")
        post_prompt_recheck = handler.index("targetMpr.refresh();")
        first_mutation = handler.index("m_undoStack->clear()")
        self.assertLess(mpr_preflight, dirty_prompt)
        self.assertLess(mob_preflight, dirty_prompt)
        self.assertLess(mob_projection, dirty_prompt)
        self.assertLess(dirty_prompt, post_prompt_recheck)
        self.assertLess(post_prompt_recheck, first_mutation)
        self.assertLess(dirty_prompt, first_mutation)
        self.assertIn("The current workspace was not changed.", handler)

    def test_user_hint_explains_single_and_double_clicks(self):
        view_cpp = source("source/render/view.cpp")
        self.assertIn("Single-clicking a neighbor does not select or edit it.", view_cpp)
        self.assertIn(
            "Double-click a neighboring map to open its MPR/MOB pair.", view_cpp
        )


if __name__ == "__main__":
    unittest.main()
