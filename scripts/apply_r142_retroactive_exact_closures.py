#!/usr/bin/env python3
"""Apply the complete-history unambiguous exact-corner closures."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "DA0B01AC61ED49FE17187D4D28DCF33864D4723993049912C2A8ADFC72C92719"
)
FINAL_R142_SHA256 = (
    "137AC7279363E9BF82B13724F0BB4C2377EF9FF386C7514AD9973A4378F617E6"
)
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r142_retroactive_exact_closures.json"))
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256}
    if FINAL_R142_SHA256:
        allowed.add(FINAL_R142_SHA256)
    if actual not in allowed:
        raise SystemExit(
            f"expected R141 base {EXPECTED_INPUT_SHA256} or final R142 "
            f"{FINAL_R142_SHA256}, got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        names = {row["name"]: int(row["id"]) for row in preset["families"]}
        payload = bytearray(base64.b64decode(preset["signaturesBase64"]))
        for slot_text, mapping in slots.items():
            for corner, family in mapping.items():
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
