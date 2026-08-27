#!/usr/bin/env python3
"""Replace two mixed bz4g brush thumbnails with proven local uniforms."""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path


EXPECTED_INPUT_SHA256 = (
    "69534DAC5FE7CBBA8F312D334545073949A7470CE9EE16F9AEB761D5F60E193C"
)
PARTIAL_BZ4G_ONLY_SHA256 = (
    "BCF448DCFCD4B3A81BE25C6EEFD2DF6BE91A7AF562D0FD703C553DF2779FCA68"
)
REPLACEMENTS = {
    "Ground / Gray": (154, 353),
    "Ground / Gravel greenish": (172, 204),
}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    if actual not in {EXPECTED_INPUT_SHA256, PARTIAL_BZ4G_ONLY_SHA256}:
        raise SystemExit(
            f"expected R140 catalog {EXPECTED_INPUT_SHA256} or partial "
            f"{PARTIAL_BZ4G_ONLY_SHA256}, got {actual}")

    catalog = json.loads(original)
    changed = []
    verified = 0
    for namespace in ("bz4g", "Zone4"):
        preset = next(row for row in catalog["presets"]
                      if row["namespace"] == namespace)
        payload = base64.b64decode(preset["signaturesBase64"])
        for family in preset["families"]:
            replacement = REPLACEMENTS.get(family["name"])
            if replacement is None:
                continue
            old_slot, new_slot = replacement
            if family["thumbnailSlot"] not in {old_slot, new_slot}:
                raise SystemExit(
                    f"unexpected {namespace} {family['name']} thumbnail: "
                    f"{family['thumbnailSlot']}")
            corners = list(payload[new_slot * 4:(new_slot + 1) * 4])
            if corners != [family["id"]] * 4:
                raise SystemExit(
                    f"replacement {namespace}:{new_slot} is not uniform "
                    f"{family['name']}: {corners}")
            verified += 1
            if family["thumbnailSlot"] == old_slot:
                family["thumbnailSlot"] = new_slot
                changed.append({
                    "namespace": namespace,
                    "family": family["name"],
                    "from": old_slot,
                    "to": new_slot,
                })

    if verified != len(REPLACEMENTS) * 2:
        raise SystemExit(f"expected 4 verified fields, got {verified}")
    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "sha256": hashlib.sha256(encoded).hexdigest().upper(),
        "changes": changed,
    }))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
