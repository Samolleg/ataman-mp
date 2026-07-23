#!/usr/bin/env python3
"""Read-only report for EI MPR liquid/lava authorship relevant to particle studies."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
from typing import Any


RES_SIGNATURE = 0x019CE23C
MP_SIGNATURE = 0xCE4AF672
SEC_SIGNATURE = 0xCF4BF774
LAVA_TILE_TYPE = 13


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest().upper()


def res_members(data: bytes) -> list[dict[str, Any]]:
    if len(data) < 16:
        raise ValueError("MPR/RES header is truncated")
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != RES_SIGNATURE:
        raise ValueError("not an EI MPR/RES archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names_end = table_end + names_size
    if table_offset < 16 or table_end > len(data) or names_end > len(data):
        raise ValueError("MPR member table is out of bounds")
    names = data[table_end:names_end]
    members = []
    for index in range(count):
        _, size, offset, timestamp, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size
        )
        if name_offset + name_len > len(names) or offset + size > len(data):
            raise ValueError(f"MPR member {index} is out of bounds")
        name = names[name_offset:name_offset + name_len].decode("cp1251", errors="replace")
        payload = data[offset:offset + size]
        members.append({
            "index": index, "name": name, "offset": offset, "size": size,
            "timestamp": timestamp, "sha256": sha256(payload), "payload": payload,
        })
    return members


def parse_mp(data: bytes) -> dict[str, Any]:
    if len(data) < 38:
        raise ValueError("MP header is truncated")
    values = struct.unpack_from("<If6IH I".replace(" ", ""), data)
    (signature, max_z, x_sectors, y_sectors, texture_count, texture_size,
     tile_count, tile_size, material_count, animation_count) = values
    if signature != MP_SIGNATURE:
        raise ValueError("invalid MP signature")
    offset = 38
    materials = []
    for index in range(material_count):
        if offset + 44 > len(data):
            raise ValueError("MP material table is truncated")
        material = struct.unpack_from("<i10f", data, offset)
        materials.append({
            "index": index, "terrain_type": material[0],
            "rgba": list(material[1:5]), "self_illumination": material[5],
            "wave_multiplier": material[6], "warp_speed": material[7],
            "reserved": list(material[8:11]), "raw_hex": data[offset:offset + 44].hex().upper(),
        })
        offset += 44
    if offset + tile_count * 4 + animation_count * 4 > len(data):
        raise ValueError("MP tile or animation table is truncated")
    tile_types = list(struct.unpack_from(f"<{tile_count}i", data, offset))
    offset += tile_count * 4
    animations = []
    for index in range(animation_count):
        tile_index, phase_count = struct.unpack_from("<HH", data, offset)
        animations.append({"index": index, "tile_index": tile_index, "phase_count": phase_count})
        offset += 4
    return {
        "header": {
            "max_z": max_z, "x_sectors": x_sectors, "y_sectors": y_sectors,
            "texture_count": texture_count, "texture_size": texture_size,
            "tile_count": tile_count, "tile_size": tile_size,
            "material_count": material_count, "animation_count": animation_count,
        },
        "materials": materials,
        "tile_types": tile_types,
        "lava_tile_indices": [index for index, value in enumerate(tile_types) if value == LAVA_TILE_TYPE],
        "animated_tiles": animations,
        "parsed_bytes": offset,
        "trailing_bytes": len(data) - offset,
    }


def parse_sector(data: bytes, tile_types: list[int], member_name: str) -> dict[str, Any]:
    if len(data) < 5:
        raise ValueError(f"sector {member_name} is truncated")
    signature, sector_type = struct.unpack_from("<IB", data)
    if signature != SEC_SIGNATURE:
        raise ValueError(f"sector {member_name} has an invalid signature")
    offset = 5 + 33 * 33 * 8
    has_liquid = sector_type == 3
    if has_liquid:
        offset += 33 * 33 * 8
    land_tiles_end = offset + 16 * 16 * 2
    if land_tiles_end > len(data):
        raise ValueError(f"sector {member_name} land tiles are truncated")
    offset = land_tiles_end
    liquid_tiles = []
    if has_liquid:
        liquid_end = offset + 16 * 16 * 2
        materials_end = liquid_end + 16 * 16 * 2
        if materials_end > len(data):
            raise ValueError(f"sector {member_name} liquid records are truncated")
        for index in range(16 * 16):
            packed = struct.unpack_from("<H", data, offset + index * 2)[0]
            material_index = struct.unpack_from("<h", data, liquid_end + index * 2)[0]
            atlas_tile = (packed & 63) + (((packed >> 6) & 255) * 64)
            tile_type = tile_types[atlas_tile] if atlas_tile < len(tile_types) else None
            liquid_tiles.append({
                "row": index // 16, "column": index % 16, "packed": packed,
                "atlas_tile_index": atlas_tile, "rotation": (packed >> 14) & 3,
                "tile_type": tile_type, "material_index": material_index,
                "is_lava": tile_type == LAVA_TILE_TYPE,
            })
        offset = materials_end
    return {
        "member": member_name, "sector_type": sector_type, "has_liquid": has_liquid,
        "liquid_tiles": liquid_tiles, "lava_tile_count": sum(tile["is_lava"] for tile in liquid_tiles),
        "parsed_bytes": offset, "trailing_bytes": len(data) - offset,
    }


def report(path: Path) -> dict[str, Any]:
    archive = path.read_bytes()
    members = res_members(archive)
    mp_members = [member for member in members if member["name"].lower().endswith(".mp")]
    if len(mp_members) != 1:
        raise ValueError(f"expected one MP member, found {len(mp_members)}")
    mp_member = mp_members[0]
    mp = parse_mp(mp_member["payload"])
    sectors = [
        parse_sector(member["payload"], mp["tile_types"], member["name"])
        for member in members if member["name"].lower().endswith(".sec")
    ]
    public_members = [{key: value for key, value in member.items() if key != "payload"} for member in members]
    return {
        "format": "ei-particle-mpr-report-v1",
        "source": {"path": str(path.resolve()), "size": len(archive), "sha256": sha256(archive)},
        "members": public_members, "map_header_member": mp_member["name"], "map": mp,
        "sectors": sectors,
        "summary": {
            "sector_count": len(sectors),
            "liquid_sector_count": sum(sector["has_liquid"] for sector in sectors),
            "lava_tile_count": sum(sector["lava_tile_count"] for sector in sectors),
            "particle_records": 0,
        },
    }


def compact_report(path: Path) -> dict[str, Any]:
    result = report(path)
    result["sectors"] = [
        {
            "member": sector["member"],
            "sector_type": sector["sector_type"],
            "lava_tile_count": sector["lava_tile_count"],
            "lava_atlas_tile_indices": sorted({
                tile["atlas_tile_index"] for tile in sector["liquid_tiles"] if tile["is_lava"]
            }),
            "lava_material_indices": sorted({
                tile["material_index"] for tile in sector["liquid_tiles"] if tile["is_lava"]
            }),
        }
        for sector in result["sectors"] if sector["lava_tile_count"]
    ]
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mpr", type=Path)
    parser.add_argument("--output", type=Path, help="JSON destination; stdout when omitted")
    parser.add_argument("--compact", action="store_true",
                        help="retain exact map/material tables and aggregate lava sector records")
    args = parser.parse_args(argv)
    try:
        result = compact_report(args.mpr) if args.compact else report(args.mpr)
        encoded = json.dumps(result, indent=2, ensure_ascii=False, sort_keys=True) + "\n"
        if args.output:
            args.output.write_text(encoded, encoding="utf-8", newline="\n")
        else:
            sys.stdout.write(encoded)
    except (OSError, ValueError, struct.error) as error:
        parser.error(str(error))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
