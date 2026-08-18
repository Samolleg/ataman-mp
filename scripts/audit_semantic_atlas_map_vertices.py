#!/usr/bin/env python3
"""Audit suspect atlas corners against vertex continuity in an authored MPR.

This is a read-only evidence pass. It never changes the map or the preset. For
each occurrence of a requested atlas slot, it rotates the current signature
into map space and asks whether the other tiles incident to the same terrain
vertex unanimously identify that corner. Suspect target slots are excluded
from the supporting vote so one bad uniform assignment cannot corroborate
another copy of itself.
"""

from __future__ import annotations

import argparse
import base64
from collections import Counter, defaultdict
import json
from pathlib import Path
import re
import struct
from typing import Iterable, Sequence

from render_semantic_visual_mpr import dimensions, land_tiles, members


CORNER_NAMES = ("NW", "NE", "SE", "SW")
WORLD_VERTEX_OFFSETS = ((0, 0), (1, 0), (1, 1), (0, 1))
INCIDENT_CELLS = (
    (0, 0, 0),    # tile southeast of the vertex contributes NW
    (-1, 0, 1),   # tile southwest contributes NE
    (-1, -1, 2),  # tile northwest contributes SE
    (0, -1, 3),   # tile northeast contributes SW
)
D4_BASE_MAPPINGS = {
    "identity": (0, 1, 2, 3),
    "rotate-90": (3, 0, 1, 2),
    "rotate-180": (2, 3, 0, 1),
    "rotate-270": (1, 2, 3, 0),
    "mirror-vertical": (3, 2, 1, 0),
    "mirror-horizontal": (1, 0, 3, 2),
    "mirror-main-diagonal": (0, 3, 2, 1),
    "mirror-anti-diagonal": (2, 1, 0, 3),
}


def unpack_tile(packed: int) -> tuple[int, int]:
    return (packed & 63) + (((packed >> 6) & 255) * 64), (packed >> 14) & 3


def rotate_signature_clockwise(signature: Sequence[int], turns: int) -> tuple[int, ...]:
    """Return NW/NE/SE/SW identities after clockwise quarter turns."""
    turns &= 3
    return tuple(signature[(index - turns) & 3] for index in range(4))


def mapped_world_signature(
        signature: Sequence[int], turns: int, base_mapping: Sequence[int],
        rotation_direction: int) -> tuple[int, ...]:
    """Map preview corners into stored-grid corners under one convention."""
    return tuple(signature[(base_mapping[index]
                            - rotation_direction * (turns & 3)) & 3]
                 for index in range(4))


def source_corner_for_world_corner(world_corner: int, turns: int) -> int:
    return (world_corner - (turns & 3)) & 3


def mapped_source_corner_for_world_corner(
        world_corner: int, turns: int, base_mapping: Sequence[int],
        rotation_direction: int) -> int:
    return (base_mapping[world_corner]
            - rotation_direction * (turns & 3)) & 3


def score_convention(
        grid: Sequence[Sequence[tuple[int, int]]],
        signatures: Sequence[Sequence[int]], base_mapping: Sequence[int],
        rotation_direction: int) -> dict[str, int]:
    """Score a corner/rotation convention using complete interior vertices."""
    height = len(grid)
    width = len(grid[0]) if height else 0
    equal_pairs = 0
    unanimous_vertices = 0
    checked_vertices = 0
    for vertex_y in range(1, height):
        for vertex_x in range(1, width):
            families: list[int] = []
            for cell_dx, cell_dy, world_corner in INCIDENT_CELLS:
                cell_x, cell_y = vertex_x + cell_dx, vertex_y + cell_dy
                slot, rotation = grid[cell_y][cell_x]
                if slot >= len(signatures):
                    families = []
                    break
                families.append(mapped_world_signature(
                    signatures[slot], rotation, base_mapping,
                    rotation_direction)[world_corner])
            if len(families) != 4:
                continue
            checked_vertices += 1
            equal_pairs += sum(families[left] == families[right]
                               for left in range(4) for right in range(left + 1, 4))
            unanimous_vertices += len(set(families)) == 1
    return {
        "checkedVertices": checked_vertices,
        "unanimousVertices": unanimous_vertices,
        "equalPairs": equal_pairs,
    }


def ranked_conventions(
        grid: Sequence[Sequence[tuple[int, int]]],
        signatures: Sequence[Sequence[int]]) -> list[dict[str, object]]:
    rankings = []
    for name, mapping in D4_BASE_MAPPINGS.items():
        for direction in (1, -1):
            rankings.append({
                "baseMapping": name,
                "rotationDirection": "clockwise" if direction == 1 else "counter-clockwise",
                **score_convention(grid, signatures, mapping, direction),
            })
    rankings.sort(key=lambda item: (
        item["unanimousVertices"], item["equalPairs"]), reverse=True)
    return rankings


def decoded_signatures(encoded: str, slot_count: int) -> list[tuple[int, int, int, int]]:
    payload = base64.b64decode(encoded)
    if len(payload) != slot_count * 4:
        raise ValueError(
            f"signature payload is {len(payload)} bytes; expected {slot_count * 4}")
    return [tuple(payload[index:index + 4]) for index in range(0, len(payload), 4)]


def mpr_grid(path: Path) -> list[list[tuple[int, int]]]:
    archive = members(path.read_bytes())
    mp_names = [name for name in archive if name.lower().endswith(".mp")]
    if len(mp_names) != 1:
        raise ValueError(f"expected one MP member, found {len(mp_names)}")
    sectors_x, sectors_y, _ = dimensions(archive[mp_names[0]])
    pattern = re.compile(r"^(.*?)(\d{3})(\d{3})\.sec$", re.I)
    sectors: dict[tuple[int, int], tuple[int, ...]] = {}
    for name, payload in archive.items():
        match = pattern.match(name)
        if match:
            sectors[(int(match.group(2)), int(match.group(3)))] = land_tiles(payload)
    if len(sectors) != sectors_x * sectors_y:
        raise ValueError(f"expected {sectors_x * sectors_y} sectors, found {len(sectors)}")

    grid = [[(0, 0) for _ in range(sectors_x * 16)]
            for _ in range(sectors_y * 16)]
    for (sector_x, sector_y), packed_tiles in sectors.items():
        for local_index, packed in enumerate(packed_tiles):
            x = sector_x * 16 + local_index % 16
            y = sector_y * 16 + local_index // 16
            grid[y][x] = unpack_tile(packed)
    return grid


def infer_target_corners(
        grid: Sequence[Sequence[tuple[int, int]]],
        signatures: Sequence[Sequence[int]],
        target_slots: Iterable[int],
        base_mapping: Sequence[int] = D4_BASE_MAPPINGS["identity"],
        rotation_direction: int = 1,
        exclude_all_target_slots: bool = True) -> dict[int, dict[str, object]]:
    targets = set(target_slots)
    height = len(grid)
    width = len(grid[0]) if height else 0
    votes: dict[int, list[Counter[int]]] = defaultdict(
        lambda: [Counter() for _ in range(4)])
    occurrences: Counter[int] = Counter()
    disagreements: dict[int, Counter[int]] = defaultdict(Counter)
    unsupported: dict[int, Counter[int]] = defaultdict(Counter)

    for y, row in enumerate(grid):
        if len(row) != width:
            raise ValueError("map grid rows differ in width")
        for x, (slot, rotation) in enumerate(row):
            if slot not in targets:
                continue
            occurrences[slot] += 1
            for world_corner, (offset_x, offset_y) in enumerate(WORLD_VERTEX_OFFSETS):
                vertex_x, vertex_y = x + offset_x, y + offset_y
                supporting: list[int] = []
                for cell_dx, cell_dy, neighbor_world_corner in INCIDENT_CELLS:
                    cell_x, cell_y = vertex_x + cell_dx, vertex_y + cell_dy
                    if not (0 <= cell_x < width and 0 <= cell_y < height):
                        continue
                    if cell_x == x and cell_y == y:
                        continue
                    neighbor_slot, neighbor_rotation = grid[cell_y][cell_x]
                    # A repeated copy of the focal slot must never validate
                    # itself.  Focused audits also exclude every explicitly
                    # requested suspect slot.  Whole-map discovery instead
                    # permits other slot identities to contribute evidence,
                    # otherwise selecting all used slots would suppress every
                    # possible vote.
                    if (neighbor_slot == slot
                            or (exclude_all_target_slots
                                and neighbor_slot in targets)
                            or neighbor_slot >= len(signatures)):
                        continue
                    world_signature = mapped_world_signature(
                        signatures[neighbor_slot], neighbor_rotation,
                        base_mapping, rotation_direction)
                    supporting.append(world_signature[neighbor_world_corner])

                source_corner = mapped_source_corner_for_world_corner(
                    world_corner, rotation, base_mapping, rotation_direction)
                if not supporting:
                    unsupported[slot][source_corner] += 1
                elif len(set(supporting)) == 1:
                    votes[slot][source_corner][supporting[0]] += 1
                else:
                    disagreements[slot][source_corner] += 1

    result: dict[int, dict[str, object]] = {}
    for slot in sorted(targets):
        result[slot] = {
            "occurrences": occurrences[slot],
            "corners": {
                CORNER_NAMES[index]: {
                    "consensusVotes": dict(sorted(votes[slot][index].items())),
                    "disagreeingVertices": disagreements[slot][index],
                    "unsupportedVertices": unsupported[slot][index],
                }
                for index in range(4)
            },
        }
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--mpr", type=Path, required=True)
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--target-slot", type=int, action="append", default=[],
                        help="suspect slot to audit; omit for convention calibration only")
    parser.add_argument(
        "--all-used-slots", action="store_true",
        help=("audit every slot used by the map; repeated copies of the focal "
              "slot remain excluded from its supporting votes"))
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    preset = next((item for item in catalog["presets"]
                   if item["namespace"].casefold() == args.namespace.casefold()), None)
    if preset is None:
        parser.error(f"namespace {args.namespace!r} is not in the preset catalog")
    signatures = decoded_signatures(preset["signaturesBase64"], preset["slotCount"])
    grid = mpr_grid(args.mpr)
    convention_ranking = ranked_conventions(grid, signatures)
    selected = convention_ranking[0]
    selected_mapping = D4_BASE_MAPPINGS[str(selected["baseMapping"])]
    selected_direction = 1 if selected["rotationDirection"] == "clockwise" else -1
    if args.all_used_slots and args.target_slot:
        parser.error("--all-used-slots cannot be combined with --target-slot")
    target_slots = (sorted({slot for row in grid for slot, _ in row
                            if slot < len(signatures)})
                    if args.all_used_slots else sorted(set(args.target_slot)))
    result = {
        "schema": "ei-semantic-atlas-map-vertex-audit-v1",
        "namespace": preset["namespace"],
        "mpr": str(args.mpr.resolve()),
        "targetSlots": target_slots,
        "cornerConventionRanking": convention_ranking,
        "selectedCornerConvention": selected,
        "results": infer_target_corners(
            grid, signatures, target_slots, selected_mapping,
            selected_direction,
            exclude_all_target_slots=not args.all_used_slots),
    }
    encoded = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(encoded, encoding="utf-8", newline="\n")
    else:
        print(encoded, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
