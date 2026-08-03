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
    view_h = source("source/render/view.h")
    view = source("source/render/view.cpp")
    trap = source("source/objects/magictrap.cpp")
    unit_h = source("source/objects/unit.h")
    unit = source("source/objects/unit.cpp")
    magic_trap = source("source/objects/magictrap.cpp")
    tree = source("source/layout_components/tree_view.cpp")
    undo_h = source("source/core/undo.h")
    undo = source("source/core/undo.cpp")
    collection_h = source("source/ui/behavior_collection_widget.h")
    collection = source("source/ui/behavior_collection_widget.cpp")
    trap_collection_h = source("source/ui/trap_collection_widget.h")
    trap_collection = source("source/ui/trap_collection_widget.cpp")
    trap_targets = source("source/core/trap_diplomacy.cpp")
    table = source("source/layout_components/tablemanager.cpp")

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
    require(view, 'tr("Logic %1%2 — %3 — %4")',
            "Logic-slot hierarchy labels with behavior and status")
    require(view, "logicIndex == 0 || logic->isUse()",
            "Logic 0 effective enabled-state display")
    require(unit_h, "EBehaviourType behaviorType() const",
            "read-only behavior label source")
    require(undo, "refreshLogicSlotOutliner",
            "immediate Logic-row refresh after edit and Undo/Redo")
    require(view, 'tr("Activation areas (%1)")', "trap activation hierarchy")
    require(view, 'tr("Cast targets (%1)")', "trap cast-target hierarchy")
    require(view, "deriveTrapTargetSummary(",
            "trap target summary derived from the containing MOB diplomacy")
    require(trap_targets, "if(row.at(target) != 2)",
            "War-only trap target derivation")
    require(trap_targets, "Q_UNUSED(storedMask);",
            "serialized trap mask excluded from effective target derivation")
    logic_trap_params = trap[trap.index("void CMagicTrap::collectlogicParams"):
                             trap.index("void CMagicTrap::applyLogicParam")]
    if "eObjParam_TRAP_DIPLOMACY" in logic_trap_params:
        raise AssertionError("legacy trap target mask remains editable in Logic mode")
    require(table, 'm_aRowName[eObjParam_TRAP_EFFECTIVE_TARGETS] = "Will attack (derived)";',
            "read-only effective trap-target property label")
    require(table, "case eObjParam_TRAP_EFFECTIVE_TARGETS:",
            "read-only effective trap-target property editor")
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
    paste_entry = view[view.index("void CView::clipboradObjectsToScene()"):
                       view.index("bool CView::pasteBehaviorPoints")]
    require(paste_entry, "cancelPendingOperation();",
            "repeat paste cancelling the preceding placement preview")
    if paste_entry.index("cancelPendingOperation();") > paste_entry.index(
            "m_clipboard_buffer_file.open(QIODevice::ReadOnly)"):
        raise AssertionError("paste cancellation occurs after the next action starts")
    duplicate_entry = view[view.index("void CView::duplicateSelectedObjects()"):
                           view.index("bool CView::canConvertSelectedWorldObjectToLever")]
    require(duplicate_entry, "cancelPendingOperation();",
            "repeat duplicate cancelling the preceding placement preview")
    if duplicate_entry.index("cancelPendingOperation();") > duplicate_entry.index(
            "selectedObjectToClipboardBuffer();"):
        raise AssertionError("duplicate cancellation occurs after copying the pending preview")
    selection_entry = view[view.index("bool CView::selectNodes"):
                           view.index("void CView::drawTilePreview")]
    require(selection_entry, "if(hasPendingOperation())\n        cancelPendingOperation();",
            "outliner selection cancelling placement before changing destination")
    if selection_entry.index("cancelPendingOperation();") > selection_entry.index(
            "const QList<CNode*> before = selectionSnapshot();"):
        raise AssertionError("selection changes before cancelling the pending preview")
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

    logic_draw = unit[unit.index("void CLogic::draw(bool isActive"):
                      unit.index("void CLogic::drawSelect")]
    require(logic_draw, "disabledAuthoringPreview",
            "disabled active-slot Behavior preview")
    require(logic_draw, "disabledLogicColor",
            "muted disabled-slot connection color")
    require(logic_draw, "pp->draw(isActive, program, disabledAuthoringPreview)",
            "muted disabled-slot patrol and look models")
    patrol_draw = unit[unit.index("void CPatrolPoint::draw(bool isActive"):
                       unit.index("bool CPatrolPoint::hasSelectedLookPoint")]
    require(patrol_draw, "RenderQueueContext::instance().flush();",
            "muted helper-model tint surviving deferred rendering")
    logic_select = unit[unit.index("void CLogic::drawSelect"):
                        unit.index("void CLogic::createLogicLines")]
    require(logic_select, "disabledAuthoringPreview",
            "picking disabled-slot Behavior points")
    require(logic_select, "CScene::getInstance()->getMode() != eEditModeLogic",
            "Behavior point picking independent of the ordinary Draw Logic option")

    get_logic = unit[unit.index("void CLogic::getLogicParam"):
                     unit.index("void CLogic::applyLogicParam")]
    apply_logic = unit[unit.index("void CLogic::applyLogicParam"):
                       unit.index("uint CLogic::deserialize")]
    for component in ("X", "Y", "Z"):
        require(get_logic, f"case eObjParam_GUARD_PLACE_{component}:",
                f"Guard-position {component} component readback")
        require(apply_logic, f"case eObjParam_GUARD_PLACE_{component}:",
                f"Guard-position {component} component editing")
    require(apply_logic, "createLogicLines();",
            "Guard-position visualization refresh after property edits")

    require(tree, "focusedLogicUnit()",
            "persistent virtual Logic-row focus")
    require(tree, "focusLogicSlot(owner, logicSlot",
            "direct Logic-row click focus before tree rebuild")
    require(tree, "Paste Logic slot at original world positions",
            "explicit world-position Logic-slot paste action")
    require(view, 'obj.insert("Content", "Behavior logic slot");',
            "typed complete Logic-slot clipboard payload")
    require(view, "pasteLogicSlot(clipboard, preserveWorldPositions)",
            "Logic-slot paste dispatch")
    logic_slot_paste = view[view.index("bool CView::pasteLogicSlotFromClipboard"):
                            view.index("void CView::selectedObjectToClipboardBuffer")]
    require(logic_slot_paste, "cancelPendingOperation();",
            "context-menu Logic-slot paste cancelling an unfinished placement")
    require(unit_h, "void replaceFromJson(const QJsonObject& data);",
            "complete slot replacement API")
    require(undo_h, "class CReplaceLogicSlotsCommand",
            "atomic multi-unit Logic-slot undo command")
    require(undo, "Paste Logic slot",
            "Logic-slot paste undo label")
    require(unit, "if(pp->isMarkDeleted())",
            "deleted patrol points excluded from slot clipboard data")

    require(collection_h, "class CBehaviorCollectionWidget",
            "embedded patrol/look collection editor")
    require(collection_h, "void selectPoint(CNode* point);",
            "explicit aim action instead of implicit object selection")
    require(collection_h, "void movePatrolPoint(int from, int to);",
            "patrol reorder request")
    require(collection_h, "void moveLookPoint(int patrol, int from, int to);",
            "look-point reorder request")
    require(collection, 'tr("Patrol route — Logic %1',
            "active-Logic collection heading")
    require(collection, 'tr("Select in outliner and viewport")',
            "aim-button tooltip")
    require(collection, "class CBehaviorCollectionTree",
            "constrained patrol/look drag-reorder tree")
    require(collection, "QAbstractItemView::InternalMove",
            "collection internal drag-reorder mode")
    require(collection, "requestDropMove",
            "drag reorder dispatch without mutating the widget hierarchy")
    require(collection, "void dragMoveEvent(QDragMoveEvent* event) override",
            "live patrol/look order preview while dragging")
    require(collection, "restoreOriginalOrder();",
            "cancelled patrol/look drag restoring its original order")
    require(collection, "updatePreviewLabels();",
            "patrol/look preview renumbering the entire visible route")
    require(collection, "if(finalIndex == m_previewIndex)",
            "patrol/look drag avoiding repeated work for one destination")
    require(collection, "worldSelectionBackground",
            "distinct collection highlight for viewport-selected points")
    require(unit_h, "bool movePatrolPoint(int from, int to);",
            "ordered patrol mutation API")
    require(unit_h, "bool moveLookPoint(int from, int to);",
            "ordered look-point mutation API")
    require(undo_h, "class CMoveBehaviorPointCommand",
            "atomic point reorder undo command")
    require(undo, 'setText(m_lookPoint ? "Reorder look point" : "Reorder patrol point")',
            "specific point reorder undo labels")
    require(view, "setBehaviorCollection(unit, m_logicSlotIndex)",
            "single-unit active-Logic collection integration")
    require(view, "selectNode(point, false)",
            "aim action selecting the actual Behavior point")
    require(unit, "m_parent->editorLogicIndex() == 0 || m_use",
            "Logic 0 effective enabled state for helper rendering")
    require(view, "prop->type() == eObjParam_LOGIC_USE && m_logicSlotIndex == 0",
            "Logic 0 enabled-field mutation guard")
    require(view, "setLogicZeroContext(m_logicSlotIndex == 0)",
            "read-only Logic 0 enabled field presentation")
    require(view, "lastVisiblePatrol",
            "Object Properties Add patrol append target")
    require(view, "behaviorCollectionOwner(selected)",
            "patrol/look selection resolving its owner collection")
    table = source("source/layout_components/tablemanager.cpp")
    require(table, "restoreScrollPosition();",
            "property scroll restoration after adding the collection row")
    require(table, "syncCollectionRowHeight();",
            "collection row height participating in the outer scroll range")
    require(table, "setSectionResizeMode(row, QHeaderView::Interactive)",
            "collection row using a concrete scrollable height")
    require(table, "setVerticalScrollMode(QAbstractItemView::ScrollPerPixel)",
            "outer table scrolling within a viewport-height collection row")
    require(table, "m_collectionRow = row;",
            "active collection row tracked across panel resizes")
    require(view, "m_pendingLogicPlacementOriginalSelection",
            "pre-creation selection retained for Esc cancellation")
    require(view, "restoreSelection(logicPlacementOriginalSelection)",
            "Esc restoring selection after cancelling point creation")
    require(undo_h, "const QList<CNode*>& originalSelection",
            "point creation commands retaining their source selection")
    require(undo, "m_pView->restoreSelection(m_originalSelection);",
            "Undo restoring the selection from before P/L creation")
    require(view, "CPatrolPoint* lastVisiblePatrol = nullptr;",
            "unit L shortcut resolving the active slot's last patrol")
    require(view, "if(createdForTarget)",
            "unsupported P/L shortcuts preserving the current selection")
    patrol_add = view[view.index("onBehaviorPatrolAdd"):
                      view.index("onBehaviorLookAdd")]
    look_add = view[view.index("onBehaviorLookAdd"):
                    view.index("m_pUndoStack = pStack")]
    require(patrol_add, "cancelPendingOperation();",
            "Add patrol cancelling an unfinished placement first")
    require(look_add, "cancelPendingOperation();",
            "Add look cancelling an unfinished placement first")
    require(main_window, "Dirty MOB validation deferred until explicit refresh",
            "no repeated dirty-candidate serialization during normal editing")
    require(main_window, "void MainWindow::keepFloatingHistoryRaised()",
            "floating History foreground restoration helper")
    require(main_window, "QTimer::singleShot(0, m_historyDock",
            "deferred History reactivation after Undo/Redo settles")
    undo_navigation = main_window[main_window.index("void MainWindow::runUndoToIndex"):
                                  main_window.index("bool MainWindow::eventFilter")]
    require(undo_navigation, "keepFloatingHistoryRaised();",
            "history navigation retaining the floating window foreground")

    require(trap_collection_h, "class CTrapCollectionWidget",
            "embedded magic-trap collection editor")
    require(trap_collection_h, "void moveActivationArea(int from, int to);",
            "activation-area reorder request")
    require(trap_collection_h, "void moveCastTarget(int from, int to);",
            "cast-target reorder request")
    require(trap_collection, 'tr("Activation areas")',
            "independent activation-area collection")
    require(trap_collection, 'tr("Cast targets")',
            "independent cast-target collection")
    require(trap_collection, "void dragMoveEvent(QDragMoveEvent* event) override",
            "live trap-point order preview while dragging")
    require(trap_collection, "restoreOriginalOrder();",
            "cancelled trap-point drag restoring its original order")
    require(trap_collection, "updatePreviewLabels();",
            "trap preview renumbering the entire visible section")
    require(trap_collection, "if(finalIndex == m_previewIndex)",
            "trap drag avoiding repeated work for one destination")
    require(trap_collection, "target->parent() != m_dragParent",
            "drag reorder constrained to one trap array")
    require(trap_collection, "worldSelectionBackground",
            "viewport-selected trap child highlight")
    require(magic_trap, "bool CMagicTrap::moveActivationArea(int from, int to)",
            "ordered activation-area mutation API")
    require(magic_trap, "bool CMagicTrap::moveCastTarget(int from, int to)",
            "ordered cast-target mutation API")
    require(undo_h, "class CMoveTrapPointCommand",
            "atomic trap-child reorder undo command")
    require(undo, 'm_castTarget ? "Reorder cast target" : "Reorder activation area"',
            "specific trap reorder undo labels")
    require(view_h, "void refreshBehaviorOrder(CNode* owner, int logicSlot);",
            "targeted outliner order refresh API")
    require(undo, "refreshBehaviorOrder(unit, m_logicSlot);",
            "behavior reorder avoiding a full outliner rebuild")
    require(undo, "refreshBehaviorOrder(trap, -1);",
            "trap reorder avoiding a full outliner rebuild")
    require(view, "trapCollectionOwner(selected)",
            "trap-child selection resolving its owner collection")
    require(view, "setTrapCollection(trap)",
            "magic-trap collection integration")
    require(table, "syncCollectionRowHeight();",
            "trap collection participating in the outer scroll range")
    require(view, "onTrapActivationAreaAdd",
            "activation-area add action integration")
    require(view, "onTrapCastTargetAdd",
            "cast-target add action integration")

    print("behavior authoring contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
