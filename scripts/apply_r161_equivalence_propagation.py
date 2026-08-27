#!/usr/bin/env python3
"""Apply the one missed exact-artwork propagation discovered before R161."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path


EXPECTED_INPUT_SHA256 = (
    "30BC8218F335792C7D5BB15D57EBD8E134CBF0927C48252AD146B0130B85BF28")
CORNER_INDEX = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r161_equivalence_propagation.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    if actual != EXPECTED_INPUT_SHA256:
        raise SystemExit(f"expected R160 catalog, got {actual}")
    catalog = json.loads(original)
    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row for row in catalog["presets"]}
    applied = changed = 0
    for namespace, slots in decisions["cornerCorrections"].items():
        preset = presets[namespace.casefold()]
        names = {row["name"]: int(row["id"]) for row in preset["families"]}
        payload = bytearray(base64.b64decode(
            preset["signaturesBase64"], validate=True))
        for slot_text, corners in slots.items():
            for corner, family in corners.items():
                offset = int(slot_text) * 4 + CORNER_INDEX[corner]
                replacement = names[family]
                changed += payload[offset] != replacement
                payload[offset] = replacement
                applied += 1
        preset["signaturesBase64"] = base64.b64encode(payload).decode("ascii")

    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "inputSha256": actual,
        "outputSha256": hashlib.sha256(encoded).hexdigest().upper(),
        "appliedCornerRows": applied,
        "changedCorners": changed,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
