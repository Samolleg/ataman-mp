#!/usr/bin/env python3
"""Apply the pinned R161 current-queue semantic-atlas decisions."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "B10DCED75A509AFB9AE2EF527A133C89717F80D0D19C769D600AEB1B552A292C")
FINAL_R161_SHA256 = (
    "AA39AE534C82377DDDB1CFD800213B8159BFBADB6F655B8438DB7C719B645518")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name("semantic_atlas_r161_decisions.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, FINAL_R161_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(f"expected pre-apply or final R161 catalog, got {actual}")

    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R161 decision/catalog input pin mismatch")
    catalog = json.loads(original)
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}

    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        names = {row["name"]: int(row["id"]) for row in preset["families"]}
        payload = bytearray(base64.b64decode(
            preset["signaturesBase64"], validate=True))
        for slot_text, corners in slots.items():
            for corner, family in corners.items():
                if family not in names:
                    raise RuntimeError(
                        f"{namespace}:{slot_text}:{corner}: unavailable {family}")
                offset = int(slot_text) * 4 + CORNER_INDEX[corner]
                replacement = names[family]
                changed += payload[offset] != replacement
                payload[offset] = replacement
                applied += 1
        preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")

    removed, paintability = prune_and_normalize_families(catalog)
    expected_removed = {
        (namespace.casefold(), family)
        for namespace, families in decisions["expectedRemovedFamilies"].items()
        for family in families
    }
    actual_removed = {
        (row["namespace"].casefold(), row["name"]) for row in removed
    }
    if actual == EXPECTED_INPUT_SHA256 and actual_removed != expected_removed:
        raise RuntimeError({
            "expectedRemovedFamilies": sorted(expected_removed),
            "actualRemovedFamilies": sorted(actual_removed),
        })

    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "inputSha256": actual,
        "outputSha256": hashlib.sha256(encoded).hexdigest().upper(),
        "appliedCornerRows": applied,
        "changedCorners": changed,
        "removedFamilies": removed,
        "paintabilityChanges": paintability,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
