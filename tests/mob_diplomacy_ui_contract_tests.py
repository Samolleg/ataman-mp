#!/usr/bin/env python3
"""Regression contract for the editable MOB diplomacy-table window."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    source = (ROOT / "source/mob/mob_parameters.cpp").read_text(encoding="utf-8")
    expected = {
        "return QString::number(value);": "compact numeric cell text",
        'case 0: return QObject::tr(': "Ally tooltip",
        'case 1: return QObject::tr(': "Neutral tooltip",
        'case 2: return QObject::tr(': "War tooltip",
        '"0 (Ally): friendly. 1 (Neutral): neither friendly nor hostile.\\n"':
            "table legend",
        '"2 (War): hostile. Double-click any cell to cycle its value."':
            "interaction help",
        '"Apply changes in both directions"': "directional edit checkbox",
        '"ui/diplomacyWindowSizeV4"': "persisted isolated diplomacy window size",
        "Qt::ScrollBarAsNeeded": "scrollbars for a constrained window",
        "restoreDiplomacyWindowSize()": "first-open auto-fit/restore",
        "m_pSymmetricDiplomacyCheck->isChecked()": "explicit edit direction",
        "highlightDiplomacySelection": "row-and-column selection highlight",
        "QItemSelectionModel::Rows": "selected-row highlight",
        "QItemSelectionModel::Columns": "selected-column highlight",
        "diplomacyContentSize()": "content-derived first-open size",
        "QHeaderView::Fixed": "fixed-cost header sizing while resizing",
        "font.setBold(index == activeSection)": "active header bolding",
        "m_pTable->columnWidth(index)": "settled column-width summation",
        "m_pTable->rowHeight(index)": "settled row-height summation",
        '"skipDiplomacySizePersistence"': "test-isolated size persistence",
        "m_pDiplomacyResizeTimer->start()": "coalesced resize repaint",
        "m_pTable->setUpdatesEnabled(false)": "suppressed intermediate table paints",
        "setActiveSection(currentColumn)": "explicit rotated-column bold state",
    }
    for fragment, description in expected.items():
        if fragment not in source:
            raise AssertionError(f"missing {description}: {fragment}")
    if source.count("setDiplomacyCellPresentation(") < 4:
        raise AssertionError("not every diplomacy population/refresh path uses symbolic text")
    forbidden = {
        "if (r==c)": "diagonal/self-relation edit rejection",
        "setFixedSize(": "fixed diplomacy-table window",
        "Qt::ScrollBarAlwaysOff": "disabled diplomacy scrolling",
        "Qt::gray": "forced gray diagonal diplomacy cells",
        "QHeaderView::ResizeToContents": "continuous header remeasurement during resize",
    }
    for fragment, description in forbidden.items():
        if fragment in source:
            raise AssertionError(f"still contains {description}: {fragment}")

    undo = (ROOT / "source/core/undo.cpp").read_text(encoding="utf-8")
    if undo.count("if(m_bSymmetric && m_row != m_column)") < 2:
        raise AssertionError("redo and undo must both guard reciprocal edits")
    print("MOB diplomacy UI contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
