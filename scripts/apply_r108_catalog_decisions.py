#!/usr/bin/env python3
"""Replay the explicit R108 semantic-atlas decisions on the R107 catalog."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "91A99F0337AC9B271665F01734E77673BDD6FB877FAB5C3C2164E01CD118B639"
)
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r108_consolidated_decisions.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    if actual != EXPECTED_INPUT_SHA256:
        raise SystemExit(
            f"expected R107 catalog {EXPECTED_INPUT_SHA256}, got {actual}")
    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row
               for row in catalog["presets"]}
    touched = set()
    corner_count = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        payload = bytearray(base64.b64decode(
            preset["signaturesBase64"], validate=True))
        by_name = {row["name"]: int(row["id"])
                   for row in preset["families"]}
        for slot_text, corners in slots.items():
            slot = int(slot_text)
            for corner_name, family_name in corners.items():
                payload[slot * 4 + CORNER_INDEX[corner_name]] = by_name[family_name]
                corner_count += 1
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
        "cornerCount": corner_count,
        "touchedPresets": sorted(touched, key=str.casefold),
        "removedFamilies": removed,
        "paintabilityChanges": paintability_changes,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
