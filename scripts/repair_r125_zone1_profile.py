#!/usr/bin/env python3
"""Rebuild the one R125 Zone1 profile damaged by incomplete finalization.

This migration is intentionally hash-gated.  It reads the affected MPR and
profile, restores exact imported-authored signatures from the current reviewed
catalog, and solves generated signatures from shared terrain vertices plus the
four brush identities recorded in the R125 stroke log.  It always writes a
separate profile; installation is a distinct, backed-up operation.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import re
import struct


EXPECTED_MPR_SHA256 = "81e911073443a0ec36de058e90b6607fa5aa4b0b694372966cabe0240aa8917b"
EXPECTED_PROFILE_SHA256 = "fbfd88aee8aa5b295a62dc9aca7faddb87de9ee582c87baf7a2af8dfaafd47f3"
EXPECTED_PRESET_SHA256 = "1b8493a9cc562603d57b4bde97f069534b94afe9a85d1f901c0af7941b1e6d7a"
LOGICAL_SLOT_COUNT = 473
CORNER_KEYS = ("northWest", "northEast", "southEast", "southWest")


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


def global_id(name: str, layer: str = "land") -> int:
    value = 2166136261
    for byte in f"{layer}:{name.strip().casefold()}".encode("utf-8"):
        value = ((value ^ byte) * 16777619) & 0xFFFFFFFF
    return 0x40000000 | (value & 0x3FFFFFFF)


def archive_members(data: bytes) -> dict[str, bytes]:
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != 0x019CE23C:
        raise ValueError("input is not an EI MPR archive")
    entry_size = struct.calcsize("<iIIIHI")
    names_offset = table_offset + count * entry_size
    names = data[names_offset:names_offset + names_size]
    result = {}
    for index in range(count):
        _, size, offset, _, name_length, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size)
        name = names[name_offset:name_offset + name_length].decode(
            "cp1251", errors="replace")
        result[name] = data[offset:offset + size]
    return result


def land_tiles(payload: bytes) -> tuple[int, ...]:
    signature, sector_type = struct.unpack_from("<IB", payload)
    if signature != 0xCF4BF774:
        raise ValueError("invalid SEC member")
    offset = 5 + 33 * 33 * 8
    if sector_type == 3:
        offset += 33 * 33 * 8
    return struct.unpack_from("<256H", payload, offset)


def globalize_profile(profile: dict) -> dict[int, dict]:
    families = {item["id"]: item for item in profile["families"]}
    slots = {item["slot"]: item for item in profile["slots"]}
    for slot in slots.values():
        if not slot["assigned"]:
            continue
        for key in CORNER_KEYS:
            material = slot["signature"][key]
            if material in families:
                slot["signature"][key] = global_id(families[material]["name"])
    return slots


def restore_authored_aliases(slots: dict[int, dict], presets: dict[str, dict]) -> int:
    restored = 0
    for slot_id in range(LOGICAL_SLOT_COUNT):
        slot = slots[slot_id]
        source_namespace = slot.get("sourceNamespace")
        source_slots = slot.get("sourceSlots", [])
        if slot["assigned"] or not source_namespace or not source_slots:
            continue
        preset = presets[source_namespace.casefold()]
        signatures = base64.b64decode(preset["signaturesBase64"])
        names = {family["id"]: family["name"] for family in preset["families"]}
        source_slot = source_slots[0]
        local = signatures[source_slot * 4:source_slot * 4 + 4]
        resolved = [global_id(names[value]) for value in local]
        slot["signature"] = dict(zip(CORNER_KEYS, resolved))
        slot["assigned"] = True
        restored += 1
    return restored


class UnionFind:
    def __init__(self) -> None:
        self.parent: dict[tuple[int, int], tuple[int, int]] = {}
        self.anchors: dict[tuple[int, int], set[int]] = {}

    def find(self, item: tuple[int, int]) -> tuple[int, int]:
        self.parent.setdefault(item, item)
        if self.parent[item] != item:
            self.parent[item] = self.find(self.parent[item])
        return self.parent[item]

    def union(self, left: tuple[int, int], right: tuple[int, int]) -> None:
        left_root, right_root = self.find(left), self.find(right)
        if left_root == right_root:
            return
        self.parent[right_root] = left_root
        self.anchors.setdefault(left_root, set()).update(
            self.anchors.pop(right_root, set()))


def source_corner(world_corner: int, rotation: int) -> int:
    # Stored profile corners are preview-space; the grid is mirrored in Y and
    # packed clockwise rotation has opposite paint-space chirality.
    return (3 - world_corner - rotation) & 3


def recover_generated(slots: dict[int, dict], mpr: Path) -> tuple[int, dict[int, str]]:
    missing = {slot for slot in range(LOGICAL_SLOT_COUNT)
               if not slots[slot]["assigned"]}
    members = archive_members(mpr.read_bytes())
    mp = next(payload for name, payload in members.items()
              if name.casefold().endswith(".mp"))
    sector_count_x, sector_count_y = struct.unpack_from("<2I", mp, 8)
    grid = [[(0, 0) for _ in range(sector_count_x * 16)]
            for _ in range(sector_count_y * 16)]
    pattern = re.compile(r"^(.*?)(\d{3})(\d{3})\.sec$", re.I)
    for name, payload in members.items():
        match = pattern.match(name)
        if not match:
            continue
        sector_x, sector_y = int(match.group(2)), int(match.group(3))
        for local_index, packed in enumerate(land_tiles(payload)):
            x = sector_x * 16 + local_index % 16
            y = sector_y * 16 + local_index // 16
            grid[y][x] = ((packed & 63) + (((packed >> 6) & 255) * 64),
                          (packed >> 14) & 3)

    def world_material(slot_id: int, rotation: int, world_corner: int) -> int:
        signature = slots[slot_id]["signature"]
        return signature[CORNER_KEYS[source_corner(world_corner, rotation)]]

    union = UnionFind()
    incident = ((0, 0, 0), (-1, 0, 1), (-1, -1, 2), (0, -1, 3))
    height, width = len(grid), len(grid[0])
    for vertex_y in range(height + 1):
        for vertex_x in range(width + 1):
            unknowns, anchors = [], []
            for delta_x, delta_y, world_corner in incident:
                x, y = vertex_x + delta_x, vertex_y + delta_y
                if not (0 <= x < width and 0 <= y < height):
                    continue
                slot_id, rotation = grid[y][x]
                if slot_id in missing:
                    unknowns.append((slot_id,
                                     source_corner(world_corner, rotation)))
                else:
                    anchors.append(world_material(slot_id, rotation, world_corner))
            for unknown in unknowns[1:]:
                union.union(unknowns[0], unknown)
            if unknowns:
                union.anchors.setdefault(union.find(unknowns[0]), set()).update(anchors)

    normalized_anchors: dict[tuple[int, int], set[int]] = {}
    components: dict[tuple[int, int], list[tuple[int, int]]] = {}
    for root, values in union.anchors.items():
        normalized_anchors.setdefault(union.find(root), set()).update(values)
    for slot_id in sorted(missing):
        for corner in range(4):
            components.setdefault(union.find((slot_id, corner)), []).append(
                (slot_id, corner))

    ground_common = global_id("Ground / Common")
    stone_lime = global_id("Stone / Lime")
    paving_sand = global_id("Paving / Sand ruins")
    rock_underwater = global_id("Rock / Underwater emerald")
    expected_unknown_components = {
        ((214, 3), (215, 3), (217, 1), (217, 3), (218, 1), (218, 3),
         (222, 1), (223, 0), (224, 0), (224, 2), (233, 0), (234, 1)),
        ((217, 2), (218, 2)),
        ((222, 3), (223, 2), (231, 0), (232, 0)),
        ((223, 3), (224, 3), (232, 3), (233, 1)),
    }
    assignments: dict[tuple[int, int], int] = {}
    decisions: dict[int, str] = {}
    for index, (root, variables) in enumerate(sorted(
            components.items(), key=lambda item: min(item[1]))):
        anchors = normalized_anchors.get(root, set())
        key = tuple(variables)
        if len(anchors) == 1:
            material = next(iter(anchors))
            reason = "shared-vertex-anchor"
        elif key in expected_unknown_components:
            material = rock_underwater
            reason = "R125-stroke-log-and-exact-corner-pixels"
        else:
            raise ValueError(
                f"unrecognized unanchored component {index}: {variables}, anchors={anchors}")
        if material not in {ground_common, stone_lime, paving_sand,
                            rock_underwater}:
            raise ValueError(f"component {index} resolved to unexpected material {material}")
        for variable in variables:
            assignments[variable] = material
        decisions[index] = reason

    for slot_id in sorted(missing):
        resolved = [assignments[(slot_id, corner)] for corner in range(4)]
        slots[slot_id]["signature"] = dict(zip(CORNER_KEYS, resolved))
        slots[slot_id]["assigned"] = True
    return len(missing), decisions


def normalize_families(profile: dict, slots: dict[int, dict]) -> None:
    old_by_name = {family["name"]: family for family in profile["families"]}
    names = {
        global_id(family["name"]): family["name"]
        for family in profile["families"]
    }
    runtime_types = {
        global_id("Ground / Common"): 0,
        global_id("Stone / Lime"): 0,
        global_id("Rock / Underwater emerald"): 3,
        global_id("Grass / Burnt"): 0,
        global_id("Paving / Sand ruins"): 7,
        global_id("Paving / Cobblestone"): 7,
    }
    names.update({
        global_id(name): name for name in (
            "Ground / Common", "Stone / Lime", "Rock / Underwater emerald",
            "Grass / Burnt", "Paving / Sand ruins", "Paving / Cobblestone")
    })
    used = {value for slot_id in range(LOGICAL_SLOT_COUNT)
            for value in slots[slot_id]["signature"].values()}
    padding_material = min(used)
    for slot_id in range(LOGICAL_SLOT_COUNT, len(slots)):
        slots[slot_id]["signature"] = {
            key: padding_material for key in CORNER_KEYS
        }
    families = []
    for material_id in sorted(used):
        name = names[material_id]
        source = old_by_name.get(name, {})
        members = [slot_id for slot_id in range(LOGICAL_SLOT_COUNT)
                   if material_id in slots[slot_id]["signature"].values()]
        uniform = [slot_id for slot_id in members
                   if set(slots[slot_id]["signature"].values()) == {material_id}]
        family = {
            "allowedLayers": ["land"],
            "ambiguous": False,
            "id": material_id,
            "layer": "land",
            "name": name,
            "paintSelectable": bool(uniform),
            "slots": members,
            "thumbnailSlot": uniform[0] if uniform else -1,
            "tileType": runtime_types.get(material_id,
                source.get("tileType", -1)),
            "underwater": source.get("underwater", False),
            "underwaterRole": source.get("underwaterRole", "surface"),
        }
        families.append(family)
    profile["families"] = families
    profile["slots"] = [slots[index] for index in sorted(slots)]


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mpr", type=Path, required=True)
    parser.add_argument("--profile", type=Path, required=True)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    actual = {
        "mpr": sha256(args.mpr),
        "profile": sha256(args.profile),
        "presets": sha256(args.presets),
    }
    expected = {
        "mpr": EXPECTED_MPR_SHA256,
        "profile": EXPECTED_PROFILE_SHA256,
        "presets": EXPECTED_PRESET_SHA256,
    }
    if actual != expected:
        raise SystemExit(f"hash gate refused recovery: actual={actual}")
    if args.output.resolve() == args.profile.resolve():
        raise SystemExit("refusing to replace the input profile; write a separate recovery artifact")

    profile = json.loads(args.profile.read_text(encoding="utf-8"))
    presets_document = json.loads(args.presets.read_text(encoding="utf-8"))
    presets = {preset["namespace"].casefold(): preset
               for preset in presets_document["presets"]}
    slots = globalize_profile(profile)
    authored_count = restore_authored_aliases(slots, presets)
    generated_count, decisions = recover_generated(slots, args.mpr)
    if authored_count != 9 or generated_count != 23:
        raise SystemExit(
            f"unexpected recovery counts authored={authored_count} generated={generated_count}")
    if sum(slots[index]["assigned"] for index in range(LOGICAL_SLOT_COUNT)) \
            != LOGICAL_SLOT_COUNT:
        raise SystemExit("not every logical atlas slot was recovered")
    normalize_families(profile, slots)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(profile, ensure_ascii=False, indent=2) + "\n",
                           encoding="utf-8")
    print(json.dumps({
        "mprSha256": actual["mpr"],
        "damagedProfileSha256": actual["profile"],
        "repairedProfileSha256": sha256(args.output),
        "authoredSlotsRecovered": authored_count,
        "generatedSlotsRecovered": generated_count,
        "logicalSlotsAssigned": LOGICAL_SLOT_COUNT,
        "physicalSlots": len(slots),
        "componentDecisions": decisions,
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
