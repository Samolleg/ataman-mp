#!/usr/bin/env python3
"""Regression checks for audited MOB property and five-slot logic UI coverage."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def source(relative: str) -> str:
    return (ROOT / relative).read_text(encoding="utf-8")


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> int:
    main_window = source("source/app/main_window.cpp")
    view_h = source("source/render/view.h")
    view_cpp = source("source/render/view.cpp")
    unit_cpp = source("source/objects/unit.cpp")
    object_base_cpp = source("source/objects/object_base.cpp")
    undo_cpp = source("source/core/undo.cpp")
    world_cpp = source("source/objects/worldobj.cpp")
    sound_cpp = source("source/objects/sound.cpp")
    particle_cpp = source("source/objects/particle.cpp")
    table_cpp = source("source/layout_components/tablemanager.cpp")

    for slot in range(5):
        require(main_window, f'"Logic {slot}', f"logic-slot {slot} selector entry")
    require(view_h, "int logicSlotIndex() const", "selected logic-slot API")
    require(view_cpp, "void CView::setLogicSlotIndex(int index)", "slot switching")
    require(unit_cpp, "editorLogic()->collectlogicParams", "slot-aware logic properties")
    require(unit_cpp, "editorLogic()->collectPatrolNodes", "slot-aware patrol projection")
    require(object_base_cpp, "editorHelperFigureIdentity",
            "distinct shared-cache identity for editor helper figures")
    require(undo_cpp, "m_logicSlot(pView ? pView->logicSlotIndex() : 0)",
            "slot provenance in undo commands")
    require(undo_cpp, "applyLogicParamAt(m_logicSlot", "slot-aware unit undo")

    for param in (
        "eObjParam_LOGIC_USE", "eObjParam_LOGIC_CYCLIC",
        "eObjParam_LOGIC_WAIT", "eObjParam_LOGIC_ALARM_CONDITION",
        "eObjParam_LOGIC_ALARM_NUMBER", "eObjParam_VIEW_FLAGS",
    ):
        require(unit_cpp, param, f"exposed {param}")

    require(world_cpp, "eObjParam_OBJECT_TYPE", "read-only ordinary object type")
    require(world_cpp, "eObjParam_SEC_TXTR", "secondary texture")
    require(sound_cpp, "propStr name(eObjParam_NAME", "sound name")
    require(particle_cpp, "propStr name(eObjParam_NAME", "particle name")
    require(table_cpp, "Object type is read-only", "unsafe raw type-paste refusal")
    require(table_cpp, "m_aRowToolTip[eObjParam_PARENT_TEMPLATE]", "audited tooltips")
    require(
        table_cpp,
        "WARNING: An empty list enables every model part; incompatible combinations can crash the game, especially on units.",
        "explicit empty-model-parts crash warning",
    )
    require(
        table_cpp,
        'm_aRowToolTip[eObjParam_GUARD_PLACE] = tr("Center of random roaming for Guard area. Alarm guard ignores this value and uses the active alarm coordinates.");',
        "guard-position tooltip distinguishes Guard area from Alarm guard",
    )
    require(
        table_cpp,
        'm_aRowToolTip[eObjParam_LOGIC_WAIT] = tr("Stored and preserved in the MOB. The audited unit AI does not use this value.");',
        "unused guard-idle value remains editable but is explained",
    )
    require(
        table_cpp,
        'm_aRowToolTip[eObjParam_SOUND_IS_MUSIC] = tr("Stored and preserved in the MOB. No confirmed runtime audio behavior uses this flag.");',
        "unconfirmed Music flag remains editable but is explained",
    )
    require(table_cpp, "m_magicTrapPropertyContext",
            "magic-trap-specific tooltip context")
    require(table_cpp, "m_lookPointPropertyContext",
            "look-point-specific tooltip context")
    require(
        table_cpp,
        "Z is stored but is not used when the look angle is calculated.",
        "look-point Z storage-only explanation",
    )
    require(
        table_cpp,
        "Preserved in the MOB, but no confirmed native magic-trap behavior or visualization uses this value.",
        "unused inherited magic-trap field explanation",
    )

    forbidden = (
        "m_aLogic.front()->collectlogicParams",
        "m_aLogic.front()->collectPatrolNodes",
        "m_aLogic.front()->getLogicParam",
        "m_aLogic.front()->applyLogicParam",
    )
    for fragment in forbidden:
        if fragment in unit_cpp:
            raise AssertionError(f"first-slot-only editor path remains: {fragment}")

    print("object property UI contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
