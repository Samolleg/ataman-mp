#!/usr/bin/env python3
"""Regression contract for dock migration and unit-only logic controls."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> int:
    main_window = source("source/app/main_window.cpp")
    main_window_h = source("source/app/main_window.h")
    table_manager = source("source/layout_components/tablemanager.cpp")

    require(main_window, "const QByteArray savedMainWindowState =",
            "saved dock-state snapshot")
    require(main_window,
            "const bool initializeDefaultDockLayout = firstRunSetup "
            "&& savedMainWindowState.isEmpty();",
            "resource setup and first UI layout kept separate")
    require(main_window, "if(initializeDefaultDockLayout)",
            "default docking limited to a genuinely new UI profile")

    require(main_window_h, "void updateLogicSlotBarVisibility();",
            "central logic-selector visibility helper")
    require(main_window, "void MainWindow::updateLogicSlotBarVisibility()",
            "logic-selector visibility implementation")
    require(main_window, "dynamic_cast<CUnit*>(selected.first())",
            "single-unit selection requirement")
    require(main_window, "updateLogicSlotBarVisibility();",
            "selection/mode visibility refresh")
    require(main_window, "Activates when world alarm %1 is raised",
            "behavior-setup option explanations")

    require(table_manager,
            "QString staticChoiceToolTip(EObjParam type, uint value)",
            "per-value dropdown explanations")
    require(table_manager, "setItemData(index, tooltip, Qt::ToolTipRole)",
            "dropdown item tooltip role")
    for expected in (
        "Attacks enemies on sight.",
        "Attacks only in response to aggression.",
        "Raises the configured alarm when the unit sees an enemy.",
        "Guards the position where the active alarm was raised.",
    ):
        require(table_manager, expected, f"known dropdown explanation: {expected}")
    require(table_manager, 'm_aRowName[eObjParam_LOGIC_ALARM_NUMBER] = "Alarm raised";',
            "outbound alarm label")

    print("logic UI persistence contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
