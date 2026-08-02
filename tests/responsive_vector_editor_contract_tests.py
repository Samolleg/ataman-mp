#!/usr/bin/env python3
"""Regression contract for responsive Vector3 property editors."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> int:
    header = (ROOT / "source/layout_components/tablemanager.h").read_text(
        encoding="utf-8")
    source = (ROOT / "source/layout_components/tablemanager.cpp").read_text(
        encoding="utf-8")
    widget_check = (ROOT / "source/app/main.cpp").read_text(encoding="utf-8")

    require(header, "void resizeEvent(QResizeEvent* event) override;",
            "Vector3 resize handling")
    require(header, "void updateResponsiveLayout(int availableWidth);",
            "responsive layout decision")
    require(source, "QBoxLayout::LeftToRight",
            "wide horizontal component layout")
    require(source, "QBoxLayout::TopToBottom",
            "narrow vertical component layout")
    require(source, "horizontalWidthHint",
            "content-derived fit threshold")
    require(source, "availableWidth >= m_horizontalWidthHint",
            "fit-based orientation switch")
    require(header, "void setCompactDisplay(bool compact);",
            "compact Vector3 component previews")
    require(source, "Qt::ElideRight",
            "ellipsis for an overflowing component value")
    require(source, "void CValueItem::focusInEvent(QFocusEvent* event)",
            "full component value restored on focus")
    require(source, "setText(m_pValue->toString());",
            "stored value used instead of the shortened preview")
    require(widget_check, "componentLayout->contentsMargins()",
            "widget check accepting the responsive component layout")

    print("responsive Vector3 editor contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
