#!/usr/bin/env python3
"""Apply the pinned R153 Tier 4 review decisions."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path

from apply_r106_catalog_decisions import prune_and_normalize_families


EXPECTED_INPUT_SHA256 = (
    "AF0F31FE756806C67A59164FDCDD4ABD32E89792A8C98A25EB3AB9E680529043")
FINAL_R153_SHA256 = (
    "4B9C7F467BFF86E76A5A5D2F6B61CB20978DE29C613411E752AAAE6727A49753")
WITHDRAWN_PRELIMINARY_R153_SHA256 = (
    "5EA2E6B5506ADE9049497964F806784718CBADFB390C1FC7975AAF26E3CD2EAB")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument("--decisions", type=Path, default=Path(__file__).with_name(
        "semantic_atlas_r153_decisions.json"))
    parser.add_argument("--revert-preliminary", action="store_true")
    args = parser.parse_args()
    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, FINAL_R153_SHA256,
               WITHDRAWN_PRELIMINARY_R153_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(f"expected R152 final or R153 final, got {actual}")

    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R153 decision/catalog input pin mismatch")
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
    if args.revert_preliminary:
        if actual != WITHDRAWN_PRELIMINARY_R153_SHA256:
            raise RuntimeError(
                "--revert-preliminary requires the pinned withdrawn catalog")
        restored = 0
        for card in decisions["reviewedCards"]:
            submitted = card["submittedMapping"]
            for location in card["locations"]:
                preset = presets[location["namespace"].casefold()]
                names = {row["name"]: int(row["id"])
                         for row in preset["families"]}
                payload = bytearray(base64.b64decode(
                    preset["signaturesBase64"], validate=True))
                for corner, current in location["currentMapping"].items():
                    if submitted[corner] in {current, "Still uncertain"}:
                        continue
                    offset = int(location["slot"]) * 4 + CORNER_INDEX[corner]
                    restored += payload[offset] != names[current]
                    payload[offset] = names[current]
                preset["signaturesBase64"] = base64.b64encode(
                    payload).decode("ascii")
        removed, paintability = prune_and_normalize_families(catalog)
        encoded = (json.dumps(catalog, ensure_ascii=False,
                              separators=(",", ":")) + "\n").encode("utf-8")
        args.catalog.write_bytes(encoded)
        print(json.dumps({
            "sha256": hashlib.sha256(encoded).hexdigest().upper(),
            "restoredCorners": restored,
            "removedFamilies": removed,
            "paintabilityChanges": paintability,
        }))
        return 0
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
