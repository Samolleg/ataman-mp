#!/usr/bin/env python3
"""Apply the pinned R154 focused follow-up decisions."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "4B9C7F467BFF86E76A5A5D2F6B61CB20978DE29C613411E752AAAE6727A49753")
FINAL_R154_SHA256 = (
    "5EA2E6B5506ADE9049497964F806784718CBADFB390C1FC7975AAF26E3CD2EAB")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r154_decisions.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, FINAL_R154_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(f"expected R153 or R154 catalog, got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R154 decision/catalog input pin mismatch")

    presets = {row["namespace"].casefold(): row
               for row in catalog["presets"]}
    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        row = presets[namespace.casefold()]
        names = {family["name"]: int(family["id"])
                 for family in row["families"]}
        payload = bytearray(base64.b64decode(
            row["signaturesBase64"], validate=True))
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
        row["signaturesBase64"] = base64.b64encode(payload).decode("ascii")

    removed, paintability = prune_and_normalize_families(catalog)
    if removed:
        raise RuntimeError(f"R154 unexpectedly removed families: {removed}")
    expected_paintability = decisions["expectedPaintabilityChanges"]
    if actual == EXPECTED_INPUT_SHA256 and paintability != expected_paintability:
        raise RuntimeError(
            "R154 paintability consequence mismatch: "
            f"expected {expected_paintability}, got {paintability}")
    if actual == FINAL_R154_SHA256 and paintability:
        raise RuntimeError(
            f"idempotent R154 replay changed paintability: {paintability}")

    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    output_sha = hashlib.sha256(encoded).hexdigest().upper()
    if FINAL_R154_SHA256 and output_sha != FINAL_R154_SHA256:
        raise RuntimeError(
            f"R154 output hash mismatch: expected {FINAL_R154_SHA256}, "
            f"got {output_sha}")
    print(json.dumps({
        "sha256": output_sha,
        "appliedCornerRows": applied,
        "changedCorners": changed,
        "removedFamilies": removed,
        "paintabilityChanges": paintability,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
