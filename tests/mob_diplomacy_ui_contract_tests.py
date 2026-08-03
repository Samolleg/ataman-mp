#!/usr/bin/env python3
"""Regression contract for symbolic MOB diplomacy-table values."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def main() -> int:
    source = (ROOT / "source/mob/mob_parameters.cpp").read_text(encoding="utf-8")
    expected = {
        'return QObject::tr("0 (Ally)");': "Ally label",
        'return QObject::tr("1 (Neutral)");': "Neutral label",
        'return QObject::tr("2 (War)");': "War label",
        '"0 (Ally): friendly. 1 (Neutral): neither friendly nor hostile. "':
            "table legend",
        '"2 (War): hostile. Double-click a non-diagonal cell to cycle its value."':
            "interaction help",
    }
    for fragment, description in expected.items():
        if fragment not in source:
            raise AssertionError(f"missing {description}: {fragment}")
    if source.count("setDiplomacyCellPresentation(") < 4:
        raise AssertionError("not every diplomacy population/refresh path uses symbolic text")
    print("MOB diplomacy UI contract: PASS")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
