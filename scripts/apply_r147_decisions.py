#!/usr/bin/env python3
"""Apply the pinned concrete R147 exact-HD-owner corner corrections."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "D765371DF8CA2355C424906A63290FD87AB04484512BC656381FAEBA8E7ADDC8")
FINAL_R147_SHA256 = (
    "2E3CAE554D10E67B241F75B1ED13C481F32F6D93EC6A881AD7C127488201A886")
SUPERSEDED_R147_SHA256 = (
    "44F30DF6425C5B7BDC24FA705C086C7F74AB0426F5E835FAFDCFF6E1A52C5C69")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r147_decisions.json"))
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, SUPERSEDED_R147_SHA256,
               FINAL_R147_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(
            f"expected R146 {EXPECTED_INPUT_SHA256} or final R147, got {actual}")
    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
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
        names = {row["name"]: int(row["id"]) for row in preset["families"]}
        payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
        for slot_text, corner_mapping in slots.items():
            for corner, family in corner_mapping.items():
                if family not in names:
                    raise RuntimeError(
                        f"{namespace}:{slot_text}:{corner}: unavailable family {family!r}")
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
        "removedFamilies": removed,
        "paintabilityChanges": paintability,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
