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
    undo_cpp = source("source/core/undo.cpp")
    world_cpp = source("source/objects/worldobj.cpp")
    sound_cpp = source("source/objects/sound.cpp")
    particle_cpp = source("source/objects/particle.cpp")
    table_cpp = source("source/layout_components/tablemanager.cpp")

    for slot in range(1, 6):
        require(main_window, f'"Logic {slot}', f"logic-slot {slot} selector entry")
    require(view_h, "int logicSlotIndex() const", "selected logic-slot API")
    require(view_cpp, "void CView::setLogicSlotIndex(int index)", "slot switching")
    require(unit_cpp, "editorLogic()->collectlogicParams", "slot-aware logic properties")
    require(unit_cpp, "editorLogic()->collectPatrolNodes", "slot-aware patrol projection")
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
