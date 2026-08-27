#!/usr/bin/env python3
"""Replay the explicit R106 consolidated semantic-atlas decisions."""

from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
from pathlib import Path


EXPECTED_INPUT_SHA256 = {
    "BD8F7C8D4FBD279C9BE8ADF56244219F0C456388B274333814AA0A86A23981DD",
}
UNPRUNED_R106_SHA256 = (
    "D5D874FE396FEF51FB1B5C562933346903C1370F01A1ADDE2AA45785A5C3ED28"
)
PRUNED_R106_PRE_BINDING_SHA256 = (
    "894DC87FB76D327B09CC36C085C8AC8022551BAF36E5229BE8995F6D3E75681D"
)
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def family_is_brush_expressible(signatures: list[tuple[int, ...]],
                                family_id: int) -> bool:
    """Match the editor's authored-brush expressibility contract exactly."""
    for corners in signatures:
        count = corners.count(family_id)
        others = set(corners) - {family_id}
        if count == 4 or (count == 1 and len(others) == 1):
            return True
    return False


def prune_and_normalize_families(catalog: dict) -> tuple[list[dict], list[dict]]:
    """Remove dead records and recompute paintability after corner rewrites.

    R106 can remove a family's final corner.  Keeping that family in the
    palette creates a brush that has no authored semantic representation.
    Transition-only records are retained only while at least one corner still
    references them.
    """
    removed: list[dict] = []
    paintability_changes: list[dict] = []
    for preset in catalog["presets"]:
        payload = base64.b64decode(
            preset["signaturesBase64"], validate=True)
        signatures = [tuple(payload[offset:offset + 4])
                      for offset in range(0, len(payload), 4)]
        referenced = set(payload)
        kept = []
        for family in preset["families"]:
            family_id = int(family["id"])
            if family_id not in referenced:
                removed.append({
                    "namespace": preset["namespace"],
                    "id": family_id,
                    "name": family["name"],
                    "paintSelectable": bool(family["paintSelectable"]),
                })
                continue
            expected = family_is_brush_expressible(signatures, family_id)
            if bool(family["paintSelectable"]) != expected:
                paintability_changes.append({
                    "namespace": preset["namespace"],
                    "id": family_id,
                    "name": family["name"],
                    "from": bool(family["paintSelectable"]),
                    "to": expected,
                })
                family["paintSelectable"] = expected
            kept.append(family)
        preset["families"] = kept
    return removed, paintability_changes


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r106_consolidated_decisions.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    accepted = EXPECTED_INPUT_SHA256 | {
        UNPRUNED_R106_SHA256, PRUNED_R106_PRE_BINDING_SHA256}
    if actual not in accepted:
        raise SystemExit(
            "expected R105 or unpruned R106 catalog, "
            f"got {actual}")
    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}

    touched: set[str] = set()
    if actual in EXPECTED_INPUT_SHA256:
        for namespace, family_specs in decisions["newFamilies"].items():
            preset = presets[namespace.casefold()]
            for spec in family_specs:
                family_id = int(spec["id"])
                if any(int(row["id"]) == family_id for row in preset["families"]):
                    raise SystemExit(
                        f"{preset['namespace']}: family ID {family_id} already exists")
                template = copy.deepcopy(next(
                    row for row in preset["families"]
                    if row["name"] == spec["templateFamily"]))
                template["id"] = family_id
                template["name"] = spec["name"]
                template["thumbnailSlot"] = int(spec["thumbnailSlot"])
                template["paintSelectable"] = bool(spec["paintSelectable"])
                preset["families"].append(template)
                preset["families"].sort(key=lambda row: int(row["id"]))
                touched.add(preset["namespace"])

    # Corner application is idempotent. Reapply it when repairing the first
    # unpruned/pruned R106 artifacts so newly recorded exact-binding
    # propagation cannot be skipped merely because the explicit R106 rows
    # were already written.
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        payload = bytearray(base64.b64decode(
            preset["signaturesBase64"], validate=True))
        by_name = {row["name"]: int(row["id"])
                   for row in preset["families"]}
        for slot_text, corners in slots.items():
            slot = int(slot_text)
            for corner_name, family_name in corners.items():
                if family_name not in by_name:
                    raise SystemExit(
                        f"{namespace} slot {slot}: missing family {family_name!r}")
                payload[slot * 4 + CORNER_INDEX[corner_name]] = by_name[family_name]
        preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")
        touched.add(preset["namespace"])

    removed, paintability_changes = prune_and_normalize_families(catalog)
    touched.update(row["namespace"] for row in removed)
    touched.update(row["namespace"] for row in paintability_changes)

    encoded = (json.dumps(catalog, separators=(",", ":"),
                          ensure_ascii=False) + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "sha256": hashlib.sha256(encoded).hexdigest().upper(),
        "touchedPresets": sorted(touched, key=str.casefold),
        "cornerCount": decisions["counts"]["explicitAndPropagatedCorners"],
        "removedFamilies": removed,
        "paintabilityChanges": paintability_changes,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
