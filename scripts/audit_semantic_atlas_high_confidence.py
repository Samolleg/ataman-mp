#!/usr/bin/env python3
"""Audit reviewed semantic atlases using exact, corroborated quadrants.

This is deliberately stricter than the older vertex-patch evidence pass.  A
corner is an automatic candidate only when its complete quadrant is byte-for-
byte equal to quadrants in at least two distinct uniform tiles of exactly one
compatible family.  Weaker and conflicting evidence is emitted for human
review and never changes the authored catalog.
"""

from __future__ import annotations

import argparse
import json
from pathlib import Path

from generate_terrain_atlas_presets import (
    CANONICAL_IDENTITY_NAMESPACE,
    apply_canonical_identity,
    corroborated_quadrant_identity_evidence,
    editor_tiles,
    res_index,
    reviewed_fragment_corrections,
    reviewed_zone2_stone_families,
)


# Explicit R76 visual decisions.  Entries use family names as well as physical
# location so a later family-id renumber cannot silently turn a reviewed keep
# into a different exception.  Byte-identical atlas siblings are enumerated:
# they inherit the reviewed pixels, but do not count as independent anchors.
REVIEWED_KEEP_CURRENT = {
    (namespace.casefold(), slot, corner, current, candidate)
    for namespaces, slot, corner, current, candidate in (
        (("bz13h", "Zone15"), 222, "northWest",
         "Sand / Common", "Sand / Gravel"),
        (("bz14h", "bz18h", "Zone16"), 146, "northWest",
         "Ground / Tan", "Stone / Red"),
        (("bz14h", "bz18h", "Zone16"), 232, "southWest",
         "Stone / Red", "Rubble / Red"),
        (("bz6g", "Zone8"), 123, "northWest",
         "Sand / Common", "Sand / Light"),
    )
    for namespace in namespaces
}


def reviewed_keep_current(record: dict) -> bool:
    candidates = record.get("candidateFamilies", [])
    return len(candidates) == 1 and (
        str(record["namespace"]).casefold(), int(record["slot"]),
        str(record["corner"]), str(record["currentFamilyName"]),
        str(candidates[0]["familyName"]),
    ) in REVIEWED_KEEP_CURRENT


def family_name(recipe: dict, family_id: int) -> str:
    for family in recipe["families"]:
        if family["id"] == family_id:
            return family.get("suggestedName", f"Family {family_id}")
    return f"Family {family_id}"


def decorate(records: list[dict], recipe: dict) -> list[dict]:
    result = []
    for record in records:
        item = dict(record)
        item["currentFamilyName"] = family_name(
            recipe, item["currentFamilyId"])
        if "replacementFamilyId" in item:
            item["replacementFamilyName"] = family_name(
                recipe, item["replacementFamilyId"])
        item["candidateFamilies"] = [
            {**candidate, "familyName": family_name(
                recipe, candidate["familyId"])}
            for candidate in item.get("candidateFamilies", [])
        ]
        result.append(item)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiled", required=True, type=Path)
    parser.add_argument("--hd", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    compiled = json.loads(args.compiled.read_text(encoding="utf-8"))
    index = res_index(args.hd)
    corrected = []
    tiles_by_namespace = {}
    for source in compiled["recipes"]:
        namespace = source["namespace"]
        tiles = editor_tiles(
            args.hd, index, namespace, source["slotCount"] // 64)
        recipe = reviewed_zone2_stone_families(source, tiles)
        recipe = reviewed_fragment_corrections(recipe)
        corrected.append(recipe)
        tiles_by_namespace[namespace.lower()] = tiles

    by_namespace = {item["namespace"].lower(): item for item in corrected}
    reviewed = [
        apply_canonical_identity(
            recipe,
            by_namespace[CANONICAL_IDENTITY_NAMESPACE.get(
                recipe["namespace"].lower(), recipe["namespace"].lower())])
        for recipe in corrected
    ]

    accepted = []
    suspicious = []
    reviewed_keeps = []
    anchorless = []
    for recipe in reviewed:
        namespace = recipe["namespace"]
        _unchanged, strong, weak = corroborated_quadrant_identity_evidence(
            recipe, tiles_by_namespace[namespace.lower()], apply=False)
        decorated_strong = decorate(strong, recipe)
        decorated_weak = decorate(weak, recipe)
        for record in decorated_strong + decorated_weak:
            if reviewed_keep_current(record):
                reviewed_keeps.append(record)
            elif record in decorated_strong:
                accepted.append(record)
            else:
                suspicious.append(record)
        for family in recipe["families"]:
            family_id = family["id"]
            if not any(
                    len(set(slot["signature"].values())) == 1
                    and next(iter(slot["signature"].values())) == family_id
                    for slot in recipe["slots"]):
                anchorless.append({
                    "namespace": namespace,
                    "familyId": family_id,
                    "familyName": family.get(
                        "suggestedName", f"Family {family_id}"),
                    "cornerCount": sum(
                        family_id in slot["signature"].values()
                        for slot in recipe["slots"]),
                })

    report = {
        "schema": "ei-mapper-semantic-atlas-high-confidence-audit-v1",
        "gate": {
            "pixelScope": "complete half-by-half RGB quadrant",
            "minimumDistinctUniformAnchorSlots": 2,
            "requiresUniqueCompatibleFamily": True,
            "applied": False,
        },
        "counts": {
            "recipes": len(reviewed),
            "accepted": len(accepted),
            "suspicious": len(suspicious),
            "reviewedKeeps": len(reviewed_keeps),
            "anchorlessFamilies": len(anchorless),
        },
        "accepted": accepted,
        "suspicious": suspicious,
        "reviewedKeeps": reviewed_keeps,
        "anchorlessFamilies": anchorless,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(report, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8")
    print(json.dumps(report["counts"], sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
