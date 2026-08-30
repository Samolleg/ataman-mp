#!/usr/bin/env python3
"""Contracts for long dialogs remaining usable on constrained displays."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def function_slice(source: str, start: str, end: str) -> str:
    begin = source.index(start)
    finish = source.index(end, begin)
    return source[begin:finish]


def require_responsive_scroll(block: str, object_name: str, buttons: str) -> None:
    expected = {
        f'"{object_name}"': "stable scroll-area identity",
        "setWidgetResizable(true)": "content resizing inside the viewport",
        "setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded)":
            "horizontal scrollbar only when constrained",
        "setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded)":
            "vertical scrollbar only when constrained",
        "QFrame::NoFrame": "invisible scroll container when everything fits",
        "availableGeometry()": "screen-aware initial size",
    }
    for fragment, description in expected.items():
        if fragment not in block:
            raise AssertionError(f"{object_name}: missing {description}: {fragment}")
    scroll_add = block.index("addWidget(scroll", block.index(object_name))
    button_add = block.index(buttons)
    if scroll_add >= button_add:
        raise AssertionError(f"{object_name}: action buttons must remain outside/below scrolling content")


def main() -> int:
    main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")
    new_mpr = function_slice(
        main_window,
        "void MainWindow::on_actionNew_MPR_triggered()",
        "void MainWindow::on_actionNew_MOB_triggered()",
    )
    require_responsive_scroll(
        new_mpr, "newMprScrollArea", "root->addWidget(buttons)")

    new_mob = function_slice(
        main_window,
        "void MainWindow::on_actionNew_MOB_triggered()",
        "void MainWindow::on_actionOpen_MOB_triggered()",
    )
    require_responsive_scroll(
        new_mob, "newMobScrollArea", "root->addWidget(buttons)")

    minimap = (ROOT / "source/ui/minimap_export_dialog.cpp").read_text(
        encoding="utf-8")
    minimap_ctor = function_slice(
        minimap,
        "CMinimapExportDialog::CMinimapExportDialog(",
        "void CMinimapExportDialog::syncComparisonViews(",
    )
    require_responsive_scroll(
        minimap_ctor, "minimapExportScrollArea", "root->addWidget(buttons)")

    atlas = (ROOT / "source/ui/terrain_atlas_review_dialog.cpp").read_text(
        encoding="utf-8")
    atlas_ctor = function_slice(
        atlas,
        "CTerrainAtlasReviewDialog::CTerrainAtlasReviewDialog(",
        "STerrainAtlasProfileFamily* CTerrainAtlasReviewDialog::family(",
    )
    require_responsive_scroll(
        atlas_ctor, "terrainAtlasReviewScrollArea", "layout->addWidget(buttons)")

    print("Responsive dialog scroll contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
