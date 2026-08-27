#!/usr/bin/env python3
"""Render an MPR land-tile grid from copied decoded atlas-part PNGs.

This is a renderer-independent regression aid for semantic paint fixtures. It
does not modify the MPR or installed resources and intentionally omits height,
lighting, objects, overlays, and water.
"""

from __future__ import annotations

import argparse
from pathlib import Path
import re
import struct

from PIL import Image

from generate_terrain_atlas_presets import editor_tiles, res_index


RES_SIGNATURE = 0x019CE23C
MP_SIGNATURE = 0xCE4AF672
SEC_SIGNATURE = 0xCF4BF774


def members(data: bytes) -> dict[str, bytes]:
    signature, count, table_offset, names_size = struct.unpack_from("<4I", data)
    if signature != RES_SIGNATURE:
        raise ValueError("not an EI MPR archive")
    entry_size = struct.calcsize("<iIIIHI")
    table_end = table_offset + count * entry_size
    names = data[table_end:table_end + names_size]
    result = {}
    for index in range(count):
        _, size, offset, _, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", data, table_offset + index * entry_size)
        name = names[name_offset:name_offset + name_len].decode(
            "cp1251", errors="replace")
        result[name] = data[offset:offset + size]
    return result


def dimensions(payload: bytes) -> tuple[int, int, int]:
    values = struct.unpack_from("<If6IHI", payload)
    if values[0] != MP_SIGNATURE:
        raise ValueError("invalid MP member")
    return values[2], values[3], values[4]


def land_tiles(payload: bytes) -> tuple[int, ...]:
    signature, sector_type = struct.unpack_from("<IB", payload)
    if signature != SEC_SIGNATURE:
        raise ValueError("invalid SEC member")
    offset = 5 + 33 * 33 * 8
    if sector_type == 3:
        offset += 33 * 33 * 8
    return struct.unpack_from("<256H", payload, offset)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mpr", type=Path)
    parser.add_argument("atlas_source", type=Path,
                        help="terrain RES archive, or directory containing part0.png, ...")
    parser.add_argument("output", type=Path)
    parser.add_argument("--tile-pixels", type=int, default=16)
    args = parser.parse_args()

    archive = members(args.mpr.read_bytes())
    mp_names = [name for name in archive if name.lower().endswith(".mp")]
    if len(mp_names) != 1:
        raise ValueError(f"expected one MP member, found {len(mp_names)}")
    sectors_x, sectors_y, atlas_count = dimensions(archive[mp_names[0]])
    sector_pattern = re.compile(r"^(.*?)(\d{3})(\d{3})\.sec$", re.I)
    sectors = {}
    for name, payload in archive.items():
        match = sector_pattern.match(name)
        if not match:
            continue
        sectors[(int(match.group(2)), int(match.group(3)))] = land_tiles(payload)
    if len(sectors) != sectors_x * sectors_y:
        raise ValueError(f"expected {sectors_x * sectors_y} sectors, found {len(sectors)}")

    if args.atlas_source.is_file():
        namespace = Path(mp_names[0]).stem
        index = res_index(args.atlas_source)
        tiles = [Image.fromarray(tile[:, :, :3], "RGB") for tile in
                 editor_tiles(args.atlas_source, index, namespace, atlas_count)]
    else:
        parts = []
        index = 0
        while (args.atlas_source / f"part{index}.png").exists():
            parts.append(Image.open(
                args.atlas_source / f"part{index}.png").convert("RGB"))
            index += 1
        if not parts:
            raise ValueError("no decoded atlas part PNGs found")
        tile_size = parts[0].width // 8
        tiles = []
        for part in parts:
            if part.size != parts[0].size or part.width != part.height:
                raise ValueError("atlas part sizes differ")
            for row in range(8):
                for column in range(8):
                    tile = part.crop((column * tile_size, row * tile_size,
                                      (column + 1) * tile_size,
                                      (row + 1) * tile_size))
                    tiles.append(tile)

    output = Image.new("RGB", (sectors_x * 16 * args.tile_pixels,
                               sectors_y * 16 * args.tile_pixels))
    for (sector_x, sector_y), packed_tiles in sectors.items():
        for local_index, packed in enumerate(packed_tiles):
            slot = (packed & 63) + (((packed >> 6) & 255) * 64)
            rotation = (packed >> 14) & 3
            if slot >= len(tiles):
                raise ValueError(f"atlas slot {slot} is unavailable")
            tile = tiles[slot]
            if rotation:
                tile = tile.rotate(-90 * rotation)
            tile = tile.resize((args.tile_pixels, args.tile_pixels),
                               Image.Resampling.LANCZOS)
            local_x, local_y = local_index % 16, local_index // 16
            output.paste(tile, ((sector_x * 16 + local_x) * args.tile_pixels,
                                (sector_y * 16 + local_y) * args.tile_pixels))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    output.save(args.output)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
