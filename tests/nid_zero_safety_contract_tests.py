#!/usr/bin/env python3
"""Source-level regression contract for authored object NID-zero boundaries."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require_order(text: str, first: str, second: str, context: str) -> None:
    first_at = text.find(first)
    second_at = text.find(second)
    if first_at < 0 or second_at < 0 or first_at >= second_at:
        raise AssertionError(f"{context}: expected {first!r} before {second!r}")


def main() -> None:
    view = (ROOT / "source/render/view.cpp").read_text(encoding="utf-8")
    main_window = (ROOT / "source/app/main_window.cpp").read_text(encoding="utf-8")
    mob = (ROOT / "source/mob/mob.cpp").read_text(encoding="utf-8")

    if 'QMessageBox::critical(nullptr, "Critical runtime compatibility notice"' in mob:
        raise AssertionError("critical MOB findings must use the normal warning presentation")
    if 'QMessageBox::warning(nullptr, "MOB validation warning"' not in mob:
        raise AssertionError("critical MOB findings need the standard high-tier warning path")

    on_param = view[view.index("void CView::onParamChange("):]
    on_param = on_param[:on_param.index("bool CView::applyFamilyNidRepairs")]
    require_order(on_param, "isAuthorableObjectNid", "m_pUndoStack->beginMacro",
                  "manual NID edit must reject zero before creating undo state")

    repair = view[view.index("bool CView::applyFamilyNidRepairs"):]
    repair = repair[:repair.index("quint32 CView::suggestFamilyNid")]
    require_order(repair, "isAuthorableObjectNid", "beginMacro",
                  "explicit repair must reject zero before opening an undo macro")

    reset = view[view.index("void CView::resetSelectedId"):]
    reset = reset[:reset.index("void CView::openMapParameters")]
    require_order(reset, "startId == 0", "new CResetIdCommand",
                  "bulk reset must abort when allocation retains its zero sentinel")

    launch = main_window[main_window.index("void MainWindow::launchCurrentMapTest"):]
    launch = launch[:launch.index("QScopedPointer<QProgressDialog>")]
    if "hasBlockingRuntimeFindings" not in launch or "return;" not in launch:
        raise AssertionError("Test Current Map must stop on runtime-blocking family findings")

    if "if(id == 0)" not in mob or "return 0;" not in mob:
        raise AssertionError("object creation allocator failure must remain explicit")

    print("authored NID zero safety contract: PASS")


if __name__ == "__main__":
    main()
