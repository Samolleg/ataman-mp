#!/usr/bin/env python3
"""Validate exhaustive reviewed-uniform artwork coverage in a tester map."""

from __future__ import annotations

import argparse
import base64
import json
import struct
import sys
from pathlib import Path


def fail(message: str) -> None:
    raise ValueError(message)


def reviewed_uniform_aliases(catalog_path: Path) -> set[tuple[str, int, str, int]]:
    catalog = json.loads(catalog_path.read_text(encoding="utf-8"))
    result: set[tuple[str, int, str, int]] = set()
    runtime_by_name: dict[str, int] = {}
    for preset in catalog["presets"]:
        signatures = base64.b64decode(preset["signaturesBase64"])
        slot_count = int(preset["slotCount"])
        if len(signatures) != slot_count * 4:
            fail(f"{preset['namespace']}: signature table length mismatch")
        for family in preset["families"]:
            if family.get("paintSelectable", True) is False:
                continue
            if "land" not in family.get("allowedLayers", []):
                continue
            family_id = int(family["id"])
            uniform_slots = []
            for slot in range(slot_count):
                corners = signatures[slot * 4 : slot * 4 + 4]
                if corners == bytes((family_id,)) * 4:
                    uniform_slots.append(slot)
            # Runtime identity is chosen by the first family that can
            # actually provide a uniform brush source, matching the C++
            # catalog. A transition-only record must not reserve the name.
            if not uniform_slots:
                continue
            name = family["name"].strip().casefold()
            runtime_type = int(family["tileType"])
            if name not in runtime_by_name:
                runtime_by_name[name] = runtime_type
            if runtime_by_name[name] != runtime_type:
                continue
            for slot in uniform_slots:
                result.add((name, runtime_type, preset["namespace"], slot))
    return result


def validate(manifest_path: Path, catalog_path: Path) -> dict[str, int]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "ei-modern-atlas-uniform-inventory-v1":
        fail("uniform inventory manifest is missing or has the wrong schema")
    if manifest.get("layout") != "uniform-brush-inventory-2x2-four-rotations":
        fail("uniform inventory layout is not the review-safe 2x2 rotation layout")

    width = int(manifest["packed_cell_width"])
    height = int(manifest["packed_cell_height"])
    packed_bytes = base64.b64decode(manifest["packed_cells_le16_base64"])
    if len(packed_bytes) != width * height * 2:
        fail("packed grid length does not match its declared dimensions")
    packed = struct.unpack(f"<{width * height}H", packed_bytes)

    identities = {
        int(item["physical_slot"]): item
        for item in manifest["physical_tile_identities"]
    }
    cards = manifest["artwork_cards"]
    covered_aliases: set[tuple[str, int, str, int]] = set()
    dedupe_keys: set[tuple[int, int, str]] = set()
    for card in cards:
        slot = int(card["physical_slot"])
        material_id = int(card["material_id"])
        runtime_type = int(card["runtime_terrain_type"])
        pixel_hash = card["pixel_sha256"].lower()
        key = (material_id, runtime_type, pixel_hash)
        if key in dedupe_keys:
            fail(f"duplicate exact-artwork card for {card['material']}: {pixel_hash}")
        dedupe_keys.add(key)

        identity = identities.get(slot)
        if identity is None:
            fail(f"card references missing physical slot {slot}")
        signature = identity["canonical_signature"]
        if set(signature.values()) != {material_id}:
            fail(f"card {slot} is not four-corner uniform in its material")
        if int(identity["runtime_terrain_type"]) != runtime_type:
            fail(f"card {slot} runtime terrain type does not match its identity")
        if identity["pixel_sha256"].lower() != pixel_hash:
            fail(f"card {slot} pixel hash does not match its identity")

        x0 = int(card["x0"])
        y0 = int(card["y0"])
        cells = ((x0, y0), (x0 + 1, y0), (x0, y0 + 1), (x0 + 1, y0 + 1))
        rotations: set[int] = set()
        for x, y in cells:
            if not (0 <= x < width and 0 <= y < height):
                fail(f"card {slot} lies outside the packed grid")
            value = packed[y * width + x]
            if value & 0x3FFF != slot:
                fail(f"card {slot} does not retain its physical artwork at ({x},{y})")
            rotations.add((value >> 14) & 3)
        if rotations != {0, 1, 2, 3}:
            fail(f"card {slot} does not display all four packed rotations")

        family_name = card["material"].strip().casefold()
        for alias in card["source_aliases"]:
            covered_aliases.add(
                (family_name, runtime_type, alias["namespace"], int(alias["slot"]))
            )

    expected_aliases = reviewed_uniform_aliases(catalog_path)
    excluded_aliases: set[tuple[str, int, str, int]] = set()
    for alias in manifest["excluded_nonmatching_tile_size_aliases"]:
        if int(alias["source_width"]) == int(alias["target_width"]):
            fail("a size-compatible source was incorrectly excluded")
        excluded_aliases.add(
            (
                alias["material"].strip().casefold(),
                int(alias["runtime_terrain_type"]),
                alias["namespace"],
                int(alias["slot"]),
            )
        )
    overlap = covered_aliases & excluded_aliases
    if overlap:
        fail(f"source alias is both displayed and excluded: {sorted(overlap)[0]}")
    accounted_aliases = covered_aliases | excluded_aliases
    missing = sorted(expected_aliases - accounted_aliases)
    unexpected = sorted(accounted_aliases - expected_aliases)
    if missing:
        fail(f"inventory omits {len(missing)} reviewed uniform source aliases; first={missing[0]}")
    if unexpected:
        fail(f"inventory contains {len(unexpected)} unauthorized aliases; first={unexpected[0]}")
    if int(manifest["catalog_uniform_source_aliases"]) != len(expected_aliases):
        fail("declared catalog source count does not match the catalog")
    if int(manifest["authorized_uniform_source_aliases"]) != len(covered_aliases):
        fail("declared usable source count does not match displayed provenance")
    if int(manifest["excluded_nonmatching_tile_size_count"]) != len(excluded_aliases):
        fail("declared resolution exclusion count does not match its aliases")
    if int(manifest["distinct_exact_artworks"]) != len(cards):
        fail("declared exact-artwork count does not match the card count")
    if int(manifest["displayed_exact_artworks"]) != len(cards):
        fail("not every distinct exact artwork is displayed")
    if manifest.get("coverage_status") != "pass":
        fail("inventory coverage status is not pass")

    return {
        "authorized_aliases": len(expected_aliases),
        "displayed_aliases": len(covered_aliases),
        "resolution_exclusions": len(excluded_aliases),
        "exact_artworks": len(cards),
        "displayed_rotations": len(cards) * 4,
        "packed_cells": width * height,
    }


def displayed_aliases(manifest_path: Path) -> set[tuple[str, int, str, int]]:
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    result: set[tuple[str, int, str, int]] = set()
    for card in manifest["artwork_cards"]:
        name = card["material"].strip().casefold()
        runtime_type = int(card["runtime_terrain_type"])
        for alias in card["source_aliases"]:
            result.add((name, runtime_type, alias["namespace"], int(alias["slot"])))
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("manifest", type=Path)
    parser.add_argument("catalog", type=Path)
    parser.add_argument(
        "--additional-manifest", action="append", default=[], type=Path
    )
    args = parser.parse_args()
    try:
        manifests = [args.manifest, *args.additional_manifest]
        validated = [validate(path, args.catalog) for path in manifests]
        counts = validated[0]
        if len(manifests) > 1:
            expected = reviewed_uniform_aliases(args.catalog)
            displayed: set[tuple[str, int, str, int]] = set()
            for path in manifests:
                current = displayed_aliases(path)
                overlap = displayed & current
                if overlap:
                    fail(
                        "source alias is displayed in multiple physical-resolution maps: "
                        f"{sorted(overlap)[0]}"
                    )
                displayed |= current
            missing = sorted(expected - displayed)
            if missing:
                fail(
                    f"combined physical-resolution maps omit {len(missing)} aliases; "
                    f"first={missing[0]}"
                )
            counts = {
                "authorized_aliases": len(expected),
                "displayed_aliases": len(displayed),
                "resolution_exclusions": 0,
                "exact_artworks": sum(item["exact_artworks"] for item in validated),
                "displayed_rotations": sum(
                    item["displayed_rotations"] for item in validated
                ),
                "packed_cells": sum(item["packed_cells"] for item in validated),
            }
    except (OSError, KeyError, ValueError, json.JSONDecodeError) as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 1
    print(
        "PASS: catalog_aliases={authorized_aliases} displayed_aliases={displayed_aliases} "
        "resolution_exclusions={resolution_exclusions} exact_artworks={exact_artworks} "
        "displayed_rotations={displayed_rotations} packed_cells={packed_cells}".format(**counts)
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
