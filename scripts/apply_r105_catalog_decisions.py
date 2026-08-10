#!/usr/bin/env python3
"""Replay the six explicit R105 authored-blend corner decisions."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path


EXPECTED_INPUT_SHA256 = {
    "0411D38DE59C255D36448203F4522939B4D43191AD07E45EDA882291AA09519B",
}
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r105_authored_blend_decisions.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    if actual not in EXPECTED_INPUT_SHA256:
        raise SystemExit(
            f"expected R102 catalog {sorted(EXPECTED_INPUT_SHA256)}, got {actual}")
    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}

    touched: set[str] = set()
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

    encoded = (json.dumps(catalog, separators=(",", ":"),
                          ensure_ascii=False) + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "sha256": hashlib.sha256(encoded).hexdigest().upper(),
        "touchedPresets": sorted(touched, key=str.casefold),
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
