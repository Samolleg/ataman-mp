#!/usr/bin/env python3
"""Apply the pinned concrete R148 batch-2 exact-occurrence corrections."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "1D980F2DD24ADE17DD2AD3A0F37E80C0885840FB550CDC937B45FF6B6223EA89")
FINAL_R148_BATCH2_SHA256 = (
    "43B56C10B6425F606FB68B88FC5FFD86A8334D4B4FB7D874A3C174BB8937A169")
WITHDRAWN_R148_BATCH2_SHA256 = (
    "DAFD16F6C1880260358F90319DDC3194A749B2E0B1ACDBC25DCA340DF0AD8F53")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r148_batch2_decisions.json"))
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, WITHDRAWN_R148_BATCH2_SHA256,
               FINAL_R148_BATCH2_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(
            f"expected R148 batch 1 {EXPECTED_INPUT_SHA256} or final R148 batch 2, "
            f"got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R148 batch 2 decision/catalog input pin mismatch")
    presets = {row["namespace"].casefold(): row
               for row in catalog["presets"]}
    reverted_non_brush_corners = removed_non_brush_families = 0
    for namespace in ("bz4g", "Zone4"):
        preset = presets[namespace.casefold()]
        withdrawn = [row for row in preset["families"]
                     if int(row["id"]) == 27
                     and row["name"] == "Ground / Gravel"]
        if not withdrawn:
            continue
        names = {row["name"]: int(row["id"])
                 for row in preset["families"]}
        payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
        offset = 436 * 4 + CORNER_INDEX["SW"]
        if payload[offset] == 27:
            payload[offset] = names["Ground / Common"]
            reverted_non_brush_corners += 1
        if 27 in payload:
            raise RuntimeError(
                f"{namespace}: withdrawn non-brush family 27 remains in use")
        preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")
        preset["families"] = [row for row in preset["families"]
                              if row not in withdrawn]
        removed_non_brush_families += 1
    added_families = 0
    for namespace, specifications in decisions.get("newFamilies", {}).items():
        preset = presets[namespace.casefold()]
        for spec in specifications:
            existing = [family for family in preset["families"]
                        if family["name"] == spec["name"]]
            if existing:
                if int(existing[0]["id"]) != int(spec["id"]):
                    raise RuntimeError(
                        f"{namespace}: {spec['name']} has unexpected ID")
                continue
            if any(int(family["id"]) == int(spec["id"])
                   for family in preset["families"]):
                raise RuntimeError(
                    f"{namespace}: reviewed family ID {spec['id']} is occupied")
            template = next(
                family for family in preset["families"]
                if family["name"] == spec["templateFamily"])
            family = dict(template)
            family.update({
                "id": int(spec["id"]),
                "name": spec["name"],
                "thumbnailSlot": int(spec["thumbnailSlot"]),
                "paintSelectable": bool(spec["paintSelectable"]),
            })
            preset["families"].append(family)
            preset["families"].sort(key=lambda row: int(row["id"]))
            added_families += 1
    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        names = {row["name"]: int(row["id"])
                 for row in preset["families"]}
        payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
        for slot_text, mapping in slots.items():
            for corner, family in mapping.items():
                if family not in names:
                    raise RuntimeError(
                        f"{namespace}:{slot_text}:{corner}: unavailable "
                        f"reviewed family {family!r}")
                offset = int(slot_text) * 4 + CORNER_INDEX[corner]
                replacement = names[family]
                changed += payload[offset] != replacement
                payload[offset] = replacement
                applied += 1
        preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")

    removed, paintability = prune_and_normalize_families(catalog)
    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "sha256": hashlib.sha256(encoded).hexdigest().upper(),
        "appliedCornerRows": applied,
        "changedCorners": changed,
        "addedFamilies": added_families,
        "revertedNonBrushCorners": reverted_non_brush_corners,
        "removedNonBrushFamilies": removed_non_brush_families,
        "removedFamilies": removed,
        "paintabilityChanges": paintability,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
