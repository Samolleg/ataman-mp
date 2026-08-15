#!/usr/bin/env python3
"""Regression contract for dock migration and unit-only logic controls."""

from pathlib import Path
import xml.etree.ElementTree as ET


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

    ui_root = ET.fromstring(source("source/app/main_window.ui"))
    status_help = ui_root.find(".//widget[@name='statusBar']")
    if status_help is None:
        raise AssertionError("missing viewport context-help widget: statusBar")
    if status_help.get("class") != "QLabel":
        raise AssertionError("viewport context help must be a passive QLabel")

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
    require(main_window, "m_behaviorModeButton->setText(target);",
            "bottom mode help names the mode Ctrl+Tab will switch to")

    view = source("source/render/view.cpp")
    view_h = source("source/render/view.h")
    operation_manager = source("source/app/operationmanager.cpp")
    scene = source("source/render/scene.cpp")
    require(view_h, "void refreshSelectContextHelp();",
            "selection-aware viewport context help entry point")
    require(view, 'tr("Ctrl+Tab: Switch to Object mode")',
            "Behavior-mode footer naming its Ctrl+Tab target")
    require(view, 'tr("Ctrl+Tab: Switch to Behavior mode")',
            "Object-mode footer naming its Ctrl+Tab target")
    require(view, 'tr("P: Add activation area")',
            "trap activation shortcut help")
    require(view, 'tr("L: Add cast target")',
            "trap cast shortcut help")
    require(view, 'tr("L: Add look to last patrol")',
            "unit look shortcut help when a patrol exists")
    if "CTLR+Tab" in operation_manager or "CTLR+Tab" in scene:
        raise AssertionError("stale misspelled generic Ctrl+Tab footer remains")
    if "Add Patrol(trap zone)" in operation_manager or "Add Patrol(trap zone)" in scene:
        raise AssertionError("stale non-contextual P/L footer remains")

    require(table_manager,
            "QString staticChoiceToolTip(EObjParam type, uint value)",
            "per-value dropdown explanations")
    require(table_manager, "view()->viewport()->installEventFilter(this);",
            "explicit popup tooltip delivery on retained Qt")
    require(table_manager, "event->type() == QEvent::ToolTip",
            "popup tooltip event handling")
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
        "Roams to random points within Guard radius around Guard position.",
        "Roams to random points within Guard radius around the active alarm coordinates.",
    ):
        require(table_manager, expected, f"known dropdown explanation: {expected}")
    require(table_manager, "EBehaviourType::eGuardAlaram",
            "Guard radius is visible for Alarm guard as well as Guard area")
    require(table_manager,
            'm_aRowName[eObjParam_LOGIC_BEHAVIOUR] = "Behavior";',
            "American-English Behavior property label")
    for relative in (
        "source/layout_components/tablemanager.cpp",
        "source/app/main_window.cpp",
        "source/app/main_window.ui",
        "source/app/settings.ui",
        "source/core/undo.cpp",
    ):
        text = source(relative)
        if '"Behaviour' in text or ' behaviour"' in text or ' behaviour setup' in text:
            raise AssertionError(f"user-visible British-English spelling remains: {relative}")
    require(table_manager, 'm_aRowName[eObjParam_LOGIC_ALARM_NUMBER] = "Alarm raised";',
            "outbound alarm label")
    require(table_manager, "Logic 0 is the default enabled slot and ignores this setting.",
            "default-slot Logic enabled explanation")

    print("logic UI persistence contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
