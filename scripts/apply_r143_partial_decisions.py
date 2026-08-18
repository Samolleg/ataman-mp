#!/usr/bin/env python3
"""Apply only the explicit partial R143 decisions and exact-HD closure."""

from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "137AC7279363E9BF82B13724F0BB4C2377EF9FF386C7514AD9973A4378F617E6"
)
FINAL_R143_SHA256 = (
    "7742016B208D2D49F7787BFBB0370B6098DBF33F6B17480B7F92FF947BE3AB28"
)
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r143_partial_decisions.json"))
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256}
    if FINAL_R143_SHA256:
        allowed.add(FINAL_R143_SHA256)
    if actual not in allowed:
        raise SystemExit(
            f"expected R142 base {EXPECTED_INPUT_SHA256} or final R143 "
            f"{FINAL_R143_SHA256}, got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
    for namespace, specs in decisions.get("newFamilies", {}).items():
        preset = presets[namespace.casefold()]
        for spec in specs:
            existing = next((row for row in preset["families"]
                             if row["name"] == spec["name"]), None)
            if existing is not None:
                if int(existing["id"]) != int(spec["id"]):
                    raise RuntimeError(
                        f"{namespace}: {spec['name']!r} has ID "
                        f"{existing['id']}, expected {spec['id']}")
                continue
            if any(int(row["id"]) == int(spec["id"])
                   for row in preset["families"]):
                raise RuntimeError(
                    f"{namespace}: family ID {spec['id']} already exists")
            family = copy.deepcopy(next(
                row for row in preset["families"]
                if row["name"] == spec["templateFamily"]))
            for field in ("id", "name", "tileType", "allowedLayers",
                          "underwaterRole", "thumbnailSlot",
                          "paintSelectable"):
                family[field] = copy.deepcopy(spec[field])
            preset["families"].append(family)
            preset["families"].sort(key=lambda row: int(row["id"]))
    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        names = {row["name"]: int(row["id"]) for row in preset["families"]}
        payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
        for slot_text, mapping in slots.items():
            for corner, family in mapping.items():
                if family not in names:
                    raise RuntimeError(
                        f"{namespace}:{slot_text}:{corner}: family {family!r} "
                        "is not available in this atlas")
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
        "removedFamilies": removed,
        "paintabilityChanges": paintability,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
