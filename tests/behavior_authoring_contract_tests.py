#!/usr/bin/env python3
"""Regression contract for Behavior-mode authoring and helper rendering."""

from pathlib import Path
import re


ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> int:
    settings = source("source/app/settings.cpp")
    settings_ui = source("source/app/settings.ui")
    main_window = source("source/app/main_window.cpp")
    operation_manager = source("source/app/operationmanager.cpp")
    view = source("source/render/view.cpp")
    trap = source("source/objects/magictrap.cpp")
    unit_h = source("source/objects/unit.h")
    unit = source("source/objects/unit.cpp")
    magic_trap = source("source/objects/magictrap.cpp")
    tree = source("source/layout_components/tree_view.cpp")
    undo_h = source("source/core/undo.h")
    undo = source("source/core/undo.cpp")

    require(settings, '{"shortcutHistory", "Ctrl+H"}',
            "Undo History default shortcut")
    require(settings, '{"shortcutTileBrushTool", "Ctrl+L"}',
            "Landscape Tools default shortcut")
    require(settings_ui, '<string>Ctrl+H</string>',
            "Undo History settings shortcut")
    require(settings_ui, '<string>Ctrl+L</string>',
            "Landscape Tools settings shortcut")
    require(main_window, 'shortcut("shortcutHistory", "Ctrl+H")',
            "Undo History runtime fallback")
    require(main_window, '"shortcutTileBrushTool", "Ctrl+L", "Landscape Tools"',
            "Landscape Tools runtime fallback")
    require(operation_manager, "case eKey_L:", "Look Point key handler")
    require(operation_manager, "m_pView->addLogicPoint(true);",
            "unmodified L shortcut for Look Point")
    require(operation_manager, "case eKey_L:\n    {\n        if(!pOp->keyManager()->isPressed(eKey_Ctrl)",
            "Ctrl+L excluded from Look Point handling")
    require(operation_manager, "else if(!pOp->keyManager()->isPressed(eKey_Ctrl))\n            m_pView->hideSelectedNodes();",
            "Ctrl+H excluded from Hide handling")
    require(operation_manager, "case eKey_P:\n    {\n        if(!pOp->keyManager()->isPressed(eKey_Ctrl)",
            "Ctrl+P excluded from Patrol Point handling")
    require(operation_manager, "&& !pOp->keyManager()->isPressed(eKey_Alt)",
            "Alt+L excluded from Look Point handling")

    defaults = re.findall(r'\{"(shortcut[^"]+)",\s*"([^"]+)"\}', settings)
    by_sequence = {}
    for name, sequence in defaults:
        normalized = sequence.casefold().replace(" ", "")
        by_sequence.setdefault(normalized, []).append(name)
    duplicates = {key: names for key, names in by_sequence.items() if len(names) > 1}
    if duplicates:
        raise AssertionError(f"duplicate default shortcuts remain: {duplicates}")

    terrain = view.index("drawTerrainColor(shadowReady);")
    helpers = view.index("drawBehaviorHelpers();", terrain)
    if helpers < terrain:
        raise AssertionError("Behavior helpers are still rendered before terrain")
    require(trap, "if(m_aActZone.empty() && m_aCastPoint.empty())",
            "cast-target-only trap relationship geometry")
    require(main_window, "propertiesLayout->addWidget(m_objectPropertiesHeader);",
            "selection title above Object Properties")
    require(main_window, "propertiesLayout->addWidget(m_behaviorNavigationBar);",
            "contextual Behavior navigation row in the dynamic properties area")
    require(main_window, "Qt::ElideLeft",
            "selection hierarchy that preserves the selected leaf when narrow")
    require(view, "appendBehaviorTreeChildren",
            "Behavior-mode outliner hierarchy builder")
    require(view, 'tr("Logic %1%2")', "five logic-slot hierarchy labels")
    require(view, 'tr("Activation areas (%1)")', "trap activation hierarchy")
    require(view, 'tr("Cast targets (%1)")', "trap cast-target hierarchy")
    require(unit_h, "const QVector<CPatrolPoint*>& patrolPoints() const",
            "read-only patrol hierarchy access")
    require(tree, "setLogicSlotIndex(logicSlot);",
            "outliner child selection switches to its owning logic slot")
    require(tree, "BehaviorItemRole",
            "cached Behavior-row filtering without node dereferences")
    require(tree, "m_filterTimer->stop();",
            "pending filter cancellation before an outliner rebuild")
    require(view, 'obj.insert("Content", "Behavior points");',
            "typed Behavior-point clipboard payload")
    require(view, "pasteBehaviorPoints(obj)",
            "Behavior-point paste dispatch")
    require(undo_h, "class CAppendBehaviorPointCommand",
            "atomic append undo command")
    require(undo, "Paste patrol point", "patrol append undo label")
    require(undo, "Paste look point", "look append undo label")
    require(main_window, 'setToolTip(tr("Select owning object"))',
            "parent navigation action")
    require(main_window, 'setToolTip(tr("Select previous sibling point"))',
            "previous-point navigation action")
    require(main_window, 'setToolTip(tr("Select next sibling point"))',
            "next-point navigation action")
    require(view, "void CView::selectBehaviorParent()",
            "parent selection implementation")
    require(view, "void CView::selectAdjacentBehaviorPoint(int offset)",
            "sibling navigation implementation")
    behavior_functions = view[view.index("void CView::selectedObjectToClipboardBuffer()"):
                              view.index("void CView::hideSelectedNodes()")]
    if "for(CNode* node : selectedNodes())" in behavior_functions:
        raise AssertionError("Behavior authoring still drops logic-point selections")
    require(undo, "m_pView->collectObjectTreeData();",
            "immediate outliner refresh from Behavior point undo commands")
    require(undo_h, "class CBehaviorPasteCommand",
            "one-command Behavior paste/duplicate history transaction")
    require(view, "m_pendingBehaviorPasteNodes",
            "temporary Behavior paste preview state")
    require(view, 'nodeKind = QStringLiteral("Activation area")',
            "activation-area clipboard support")
    require(view, 'nodeKind = QStringLiteral("Cast target")',
            "cast-target clipboard support")
    patrol_move = unit[unit.index("bool CPatrolPoint::updatePos"):
                       unit.index("void CPatrolPoint::markAsDeleted")]
    require(patrol_move, "look->updatePos",
            "patrol translation carrying its look-point children")
    trap_move = magic_trap[magic_trap.index("bool CMagicTrap::updatePos"):
                           magic_trap.index("void CMagicTrap::setDrawPosition")]
    if "eEditModeLogic" in trap_move:
        raise AssertionError("Behavior-mode trap movement still leaves trap children behind")
    require(tree, "expandBehaviorHierarchy",
            "unit/trap owner selection expanding its Behavior hierarchy")
    add_logic = view[view.index("void CView::addLogicPoint"):
                     view.index("void CView::copySelectedIDsToClipboard")]
    require(add_logic, "collectObjectTreeData();",
            "post-creation outliner selection handoff")

    print("behavior authoring contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
