#!/usr/bin/env python3
"""Build a deterministic flat-MPR collection for the particle catalog."""

from __future__ import annotations

import argparse
from collections.abc import Iterable
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
from typing import Any


RES_SIGNATURE = 0x019CE23C
MP_SIGNATURE = 0xCE4AF672
SEC_SIGNATURE = 0xCF4BF774
REFERENCE_MOB_SIZE = 12_094
REFERENCE_MOB_SHA256 = "6EBE2B6892EC700CF7FA4B6F776A7142D960CB389EEDF112693F280599C8AB1F"
SECTOR_COUNT_X = 1
SECTOR_COUNT_Y = 1
SECTOR_WORLD_SIZE = 32
ZONE1_GROUND_PACKED_TILE = 1
ZONE1_GROUND_ATLAS_SLOT = 1
FLAT_NORMAL = (1000 << 22) | (1000 << 11) | 1000
TEXTURE_COUNT = 8
TEXTURE_SIZE = 512
TILE_COUNT = 512
TILE_SIZE = 64


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def read_members(data: bytes) -> list[dict[str, Any]]:
    if len(data) < 16:
        raise ValueError("MPR/RES header is truncated")
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != RES_SIGNATURE:
        raise ValueError("input is not an EI MPR/RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or table_end > len(data) or names_end > len(data):
        raise ValueError("MPR member table is out of bounds")
    names = data[table_end:names_end]
    members: list[dict[str, Any]] = []
    for index in range(count):
        next_index, size, offset, timestamp, name_length, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size
        )
        if name_offset + name_length > len(names) or offset + size > table_offset:
            raise ValueError(f"MPR member {index} is out of bounds")
        encoded_name = names[name_offset:name_offset + name_length]
        members.append({
            "name": encoded_name.decode("cp1251"),
            "payload": data[offset:offset + size],
            "timestamp": timestamp,
            "next_index": next_index,
        })
    return members


def synthetic_zone1_mp() -> bytes:
    result = bytearray(struct.pack(
        "<If6IHI", MP_SIGNATURE, 1.0, SECTOR_COUNT_X, SECTOR_COUNT_Y,
        TEXTURE_COUNT, TEXTURE_SIZE, TILE_COUNT, TILE_SIZE, 1, 0
    ))
    result.extend(struct.pack(
        "<i10f", 1, 0.5, 0.5, 0.5, 1.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0
    ))
    tile_types = [0] * TILE_COUNT
    tile_types[ZONE1_GROUND_ATLAS_SLOT] = 1
    result.extend(struct.pack(f"<{TILE_COUNT}i", *tile_types))
    return bytes(result)


def flat_sector(packed_tile: int = ZONE1_GROUND_PACKED_TILE) -> bytes:
    if not 0 <= packed_tile <= 0xFFFF:
        raise ValueError("packed tile must fit uint16")
    result = bytearray(struct.pack("<IB", SEC_SIGNATURE, 0))
    vertex = struct.pack("<bbHI", 0, 0, 0, FLAT_NORMAL)
    result.extend(vertex * (33 * 33))
    result.extend(struct.pack("<H", packed_tile) * (16 * 16))
    return bytes(result)


def build_archive(members: Iterable[tuple[str, bytes]], timestamp: int = 0) -> bytes:
    records: list[dict[str, Any]] = []
    result = bytearray(16)
    for name, payload in members:
        encoded_name = name.encode("cp1251")
        offset = len(result)
        result.extend(payload)
        result.extend(b"\0" * ((16 - len(result) % 16) % 16))
        records.append({"name": encoded_name, "payload": payload, "offset": offset})
    table_offset = len(result)
    names = bytearray()
    for record in records:
        encoded_name = record["name"]
        result.extend(struct.pack(
            "<iIIIHI", -1, len(record["payload"]), record["offset"],
            timestamp, len(encoded_name), len(names)
        ))
        names.extend(encoded_name)
    result.extend(names)
    struct.pack_into("<4I", result, 0, RES_SIGNATURE, len(records), table_offset, len(names))
    return bytes(result)


def collection_text(mpr_reference: str, mob_reference: str) -> str:
    return (
        "# EI Mapper Endurance Collection\n"
        "[collection]\n"
        "version=1\n"
        'name="Particle catalog - flat Zone 1"\n'
        'profile="jamais-vu"\n'
        'map="gz1g"\n'
        f'mpr="{mpr_reference}"\n'
        f'mob="{mob_reference}"\n'
    )


def build(catalog_mob: Path, output_root: Path) -> dict[str, Any]:
    mob_bytes = catalog_mob.read_bytes()
    if len(mob_bytes) != REFERENCE_MOB_SIZE:
        raise ValueError(
            f"particle catalog MOB size mismatch: {len(mob_bytes)} != {REFERENCE_MOB_SIZE}"
        )
    mob_hash = sha256(mob_bytes)
    if mob_hash != REFERENCE_MOB_SHA256:
        raise ValueError(
            f"particle catalog MOB hash mismatch: {mob_hash} != {REFERENCE_MOB_SHA256}"
        )
    mpr_bytes = build_archive([
        ("zone1.mp", synthetic_zone1_mp()),
        ("zone1000000.sec", flat_sector()),
    ])

    scene_directory = output_root / "test-scenes" / "particle-catalog-grid"
    collection_directory = output_root / "collections"
    scene_directory.mkdir(parents=True, exist_ok=True)
    collection_directory.mkdir(parents=True, exist_ok=True)
    mpr_path = scene_directory / "particle-zone1-flat-1x1.mpr"
    mob_path = scene_directory / "all-particles-grid.mob"
    manifest_path = scene_directory / "particle-catalog-collection.json"
    collection_path = collection_directory / "particle-catalog.collection"
    mpr_path.write_bytes(mpr_bytes)
    if catalog_mob.resolve() != mob_path.resolve():
        shutil.copyfile(catalog_mob, mob_path)
    collection_path.write_text(collection_text(
        "../test-scenes/particle-catalog-grid/particle-zone1-flat-1x1.mpr",
        "../test-scenes/particle-catalog-grid/all-particles-grid.mob",
    ), encoding="utf-8", newline="\n")

    manifest: dict[str, Any] = {
        "format": "ei-particle-catalog-collection-v1",
        "inputs": {
            "particleMob": {
                "fileName": catalog_mob.name,
                "size": len(mob_bytes),
                "sha256": mob_hash,
            },
            "terrainAtlas": {
                "profile": "jamais-vu",
                "innerNamespace": "zone1",
                "embeddedImages": 0,
            },
        },
        "layout": {
            "sectorCount": [SECTOR_COUNT_X, SECTOR_COUNT_Y],
            "worldExtent": [SECTOR_WORLD_SIZE, SECTOR_WORLD_SIZE],
            "rawHeight": 0,
            "packedTile": ZONE1_GROUND_PACKED_TILE,
            "atlasSlot": ZONE1_GROUND_ATLAS_SLOT,
            "rotation": 0,
        },
        "outputs": {
            "collection": {"path": "collections/particle-catalog.collection"},
            "mpr": {
                "path": "test-scenes/particle-catalog-grid/particle-zone1-flat-1x1.mpr",
                "size": len(mpr_bytes),
                "sha256": sha256(mpr_bytes),
            },
            "mob": {
                "path": "test-scenes/particle-catalog-grid/all-particles-grid.mob",
                "size": len(mob_bytes),
                "sha256": mob_hash,
            },
        },
    }
    manifest_path.write_text(
        json.dumps(manifest, indent=2, ensure_ascii=False, sort_keys=True) + "\n",
        encoding="utf-8", newline="\n",
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("particle_catalog_mob", type=Path)
    parser.add_argument("output_root", type=Path)
    args = parser.parse_args(argv)
    try:
        result = build(args.particle_catalog_mob, args.output_root)
        print(json.dumps(result["outputs"], indent=2, sort_keys=True))
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
