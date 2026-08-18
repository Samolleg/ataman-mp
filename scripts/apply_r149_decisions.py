#!/usr/bin/env python3
"""Apply the pinned R149 focused follow-up corner corrections."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "43B56C10B6425F606FB68B88FC5FFD86A8334D4B4FB7D874A3C174BB8937A169")
FINAL_R149_SHA256 = (
    "2BDD2BD74CFB39F88F1649D94AC54D1AC3938BAF6206F76019A3408255261EB0")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r149_decisions.json"))
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, FINAL_R149_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(f"expected R148 final or R149 final, got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R149 decision/catalog input pin mismatch")
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
