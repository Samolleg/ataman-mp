#!/usr/bin/env python3
"""Apply the provenance-qualified R163 semantic metadata correction."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


EXPECTED_INPUT_SHA256 = (
    "1E0609597348F6405BC2AF04870773C036DB779A1D74FD3D1AE957D2ED4E9DF3")
FINAL_R163_SHA256 = (
    "50CA765659E721D03AB896A78962AB675FDE7BEFF537675580DC338F63488104")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--decisions", type=Path,
        default=Path(__file__).with_name(
            "semantic_atlas_r163_semantic_review.json"))
    args = parser.parse_args()

    original = args.catalog.read_bytes()
    actual = hashlib.sha256(original).hexdigest().upper()
    allowed = {EXPECTED_INPUT_SHA256, FINAL_R163_SHA256} - {""}
    if actual not in allowed:
        raise SystemExit(
            f"expected pre-apply or final R163 catalog, got {actual}")

    decisions = json.loads(args.decisions.read_text(encoding="utf-8"))
    if decisions["catalogInputSha256"] != EXPECTED_INPUT_SHA256:
        raise RuntimeError("R163 decision/catalog input pin mismatch")
    if decisions["catalogOutputSha256"] != FINAL_R163_SHA256:
        raise RuntimeError("R163 decision/catalog output pin mismatch")
    catalog = json.loads(original)
    presets = {row["namespace"].casefold(): row
               for row in catalog["presets"]}

    changed = 0
    applied = 0
    for namespace, families in decisions[
            "familyMetadataCorrections"].items():
        preset = presets[namespace.casefold()]
        by_name = {row["name"]: row for row in preset["families"]}
        for family_name, fields in families.items():
            family = by_name[family_name]
            for field, replacement in fields.items():
                changed += family.get(field) != replacement
                family[field] = replacement
                applied += 1

    encoded = (json.dumps(catalog, ensure_ascii=False, separators=(",", ":"))
               + "\n").encode("utf-8")
    output_sha256 = hashlib.sha256(encoded).hexdigest().upper()
    if output_sha256 != FINAL_R163_SHA256:
        raise RuntimeError(
            f"R163 output hash mismatch: expected {FINAL_R163_SHA256}, "
            f"got {output_sha256}")
    args.catalog.write_bytes(encoded)
    print(json.dumps({
        "inputSha256": actual,
        "outputSha256": output_sha256,
        "appliedMetadataFields": applied,
        "changedMetadataFields": changed,
    }, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
