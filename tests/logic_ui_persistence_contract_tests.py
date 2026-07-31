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
    require(main_window, "const bool show = CScene::getInstance()->getMode() == eEditModeLogic;",
            "behavior setup remains visible throughout Behavior mode")
    require(main_window, 'tr("Logic %1 (alarm %1; selection: %2)")',
            "unit-selection enablement suffix")
    require(main_window, 'tr("Logic %1 (alarm %1)")',
            "selection-independent alarm-slot label")
    require(main_window, "updateLogicSlotBarVisibility();",
            "selection/mode visibility refresh")
    require(main_window, "Activates when world alarm %1 is raised",
            "behavior-setup option explanations")
    for slot in range(5):
        require(main_window, f'"Logic {slot}',
                f"zero-based logic slot {slot} label")
    require(main_window, "unit->getLogicParamAt(slot, enabled, eObjParam_LOGIC_USE);",
            "selected-unit slot enablement summary")
    require(main_window, 'tr("mixed")',
            "mixed slot enablement label")
    require(main_window, 'setItemText(0, tr("Logic 0 (default)"))',
            "concise default slot label")
    require(main_window, 'setObjectName("behaviorModeButton")',
            "bottom Behavior-mode toggle")
    require(main_window, 'tr("Switch to Behavior mode")',
            "contextual Object-to-Behavior action help")
    require(main_window, 'tr("Switch to Object mode")',
            "contextual Behavior-to-Object action help")
    require(main_window, "m_behaviorModeButton->setChecked(behaviorMode);",
            "bottom toggle synchronized to active mode")

    require(table_manager,
            "QString staticChoiceToolTip(EObjParam type, uint value)",
            "per-value dropdown explanations")
    require(table_manager, "setItemData(index, tooltip, Qt::ToolTipRole)",
            "dropdown item tooltip role")
    require(table_manager, "addItems(m_valueList.values());",
            "role-capable default static-choice model")
    require(table_manager, "if(!qobject_cast<QComboBox*>(editor))",
            "property tooltip excluded from combo popup children")
    for category in ("Human (50)", "Animal (51)", "Monster (52)"):
        require(source("source/resources/resourcemanager.cpp"), category,
                f"numeric unit category label: {category}")
    for expected in (
        "Attacks enemies on sight.",
        "Attacks only in response to aggression.",
        "Raises the configured alarm when the unit sees an enemy.",
        "Guards the position where the active alarm was raised.",
    ):
        require(table_manager, expected, f"known dropdown explanation: {expected}")
    require(table_manager, 'm_aRowName[eObjParam_LOGIC_ALARM_NUMBER] = "Alarm raised";',
            "outbound alarm label")
    require(table_manager, "Logic 0 is the default enabled slot and ignores this setting.",
            "default-slot Logic enabled explanation")

    print("logic UI persistence contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
