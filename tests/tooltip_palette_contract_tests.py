#!/usr/bin/env python3
"""Regression contract for readable application tooltips."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    main_cpp = (ROOT / "source/app/main.cpp").read_text(encoding="utf-8")
    theme_cpp = (ROOT / "source/ui/ui_theme.cpp").read_text(encoding="utf-8")

    expected = (
        "darkPalette.setColor(QPalette::ToolTipBase, QColor(250, 248, 240));",
        "darkPalette.setColor(QPalette::ToolTipText, QColor(32, 32, 32));",
        "QToolTip { color: #202020; background-color: #faf8f0; "
        "border: 1px solid #8a8a8a; }",
    )
    for fragment in expected:
        if fragment not in theme_cpp:
            raise AssertionError(f"missing readable tooltip contract: {fragment}")

    if "#07be07" in main_cpp or "#07be07" in theme_cpp:
        raise AssertionError("bright-green tooltip background remains")

    print("tooltip palette contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
