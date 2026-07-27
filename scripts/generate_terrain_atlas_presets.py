#!/usr/bin/env python3
"""Build compact semantic-atlas presets from reviewed derived metadata.

The output contains semantic IDs, names, corner signatures, and hashes only.
It never embeds source textures, masks, recipes, or installed archives.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import json
from pathlib import Path
import struct

import numpy as np


RES_SIGNATURE = 0x019CE23C
MMP_SIGNATURE = 0x00504D4D
DXT1 = 0x31545844
MMP_HEADER_SIZE = 76


def res_index(path: Path) -> dict[str, dict]:
    with path.open("rb") as stream:
        signature, count, table_offset, names_size = struct.unpack("<4I", stream.read(16))
        if signature != RES_SIGNATURE:
            raise ValueError(f"not an EI RES archive: {path}")
        entry_size = struct.calcsize("<iIIIHI")
        stream.seek(table_offset)
        table = stream.read(count * entry_size)
        names = stream.read(names_size)
    result = {}
    for index in range(count):
        _next, size, offset, timestamp, name_len, name_offset = struct.unpack_from(
            "<iIIIHI", table, index * entry_size)
        name = names[name_offset:name_offset + name_len].decode("cp1251", errors="replace")
        result[name.lower()] = {"name": name, "size": size, "offset": offset}
    return result


def read_member(path: Path, record: dict) -> bytes:
    with path.open("rb") as stream:
        stream.seek(record["offset"])
        result = stream.read(record["size"])
    if len(result) != record["size"]:
        raise ValueError(f"truncated member {record['name']} in {path}")
    return result


def rgb565(values: np.ndarray) -> np.ndarray:
    values = values.astype(np.uint16)
    # Match resourcemanager.cpp's extractColor exactly. Its operands are
    # integers, so the division truncates before the nominal +0.5 is applied.
    # Rounding here produces visually indistinguishable pixels but a different
    # exact binding hash, causing every shipped preset to be rejected at run
    # time and silently replaced by inference or a legacy namespace table.
    red = (((values >> 11) & 31).astype(np.uint16) * 255) // 31
    green = (((values >> 5) & 63).astype(np.uint16) * 255) // 63
    blue = ((values & 31).astype(np.uint16) * 255) // 31
    alpha = np.full_like(red, 255)
    return np.stack((red, green, blue, alpha), axis=1).astype(np.uint8)


def decode_dxt1(mmp: bytes) -> np.ndarray:
    signature, width, height, _mips, texture_format = struct.unpack_from("<5I", mmp)
    if signature != MMP_SIGNATURE or texture_format != DXT1:
        raise ValueError("preset hashing requires a DXT1 MMP")
    blocks_wide, blocks_high = (width + 3) // 4, (height + 3) // 4
    size = blocks_wide * blocks_high * 8
    blocks = np.frombuffer(mmp[MMP_HEADER_SIZE:MMP_HEADER_SIZE + size],
                           dtype=np.uint8).reshape(-1, 8)
    color0 = blocks[:, 0].astype(np.uint16) | blocks[:, 1].astype(np.uint16) << 8
    color1 = blocks[:, 2].astype(np.uint16) | blocks[:, 3].astype(np.uint16) << 8
    first, second = rgb565(color0), rgb565(color1)
    palette = np.empty((len(blocks), 4, 4), dtype=np.uint8)
    palette[:, 0], palette[:, 1] = first, second
    opaque = color0 > color1
    palette[opaque, 2] = ((2 * first[opaque].astype(np.uint16)
                           + second[opaque].astype(np.uint16)) // 3).astype(np.uint8)
    palette[opaque, 3] = ((first[opaque].astype(np.uint16)
                           + 2 * second[opaque].astype(np.uint16)) // 3).astype(np.uint8)
    palette[~opaque, 2] = ((first[~opaque].astype(np.uint16)
                            + second[~opaque].astype(np.uint16)) // 2).astype(np.uint8)
    palette[~opaque, 3] = 0
    # The editor's terrain DXT1 decoder intentionally emits opaque RGBA even
    # for the DXT1 three-colour code path.
    palette[:, :, 3] = 255
    indices = (blocks[:, 4].astype(np.uint32)
               | blocks[:, 5].astype(np.uint32) << 8
               | blocks[:, 6].astype(np.uint32) << 16
               | blocks[:, 7].astype(np.uint32) << 24)
    selectors = ((indices[:, None] >> (2 * np.arange(16, dtype=np.uint32))) & 3).astype(np.intp)
    pixels = palette[np.arange(len(blocks))[:, None], selectors]
    image = pixels.reshape(blocks_high, blocks_wide, 4, 4, 4).transpose(0, 2, 1, 3, 4)
    return image.reshape(blocks_high * 4, blocks_wide * 4, 4)[:height, :width]


def pixel_hash(archive: Path, index: dict[str, dict], namespace: str,
               atlas_parts: int) -> str | None:
    names = [f"{namespace}{part:03d}.mmp".lower() for part in range(atlas_parts)]
    if any(name not in index for name in names):
        return None
    digest = hashlib.sha256()
    digest.update(str(atlas_parts * 64).encode("ascii") + b"\n")
    for name in names:
        atlas = np.flipud(decode_dxt1(read_member(archive, index[name])))
        if atlas.shape[0] != atlas.shape[1] or atlas.shape[0] % 8:
            raise ValueError(f"unexpected atlas dimensions for {name}")
        tile_size = atlas.shape[0] // 8
        for column in range(8):
            for row in range(8):
                tile = np.flipud(atlas[column * tile_size:(column + 1) * tile_size,
                                       row * tile_size:(row + 1) * tile_size])
                digest.update(f"{tile_size}x{tile_size}\n".encode("ascii"))
                digest.update(tile.tobytes(order="C"))
    return digest.hexdigest()


def underwater_role(recipe: dict, family_id: int) -> str:
    land = below = 0
    for slot in recipe["slots"]:
        if family_id not in slot["signature"].values():
            continue
        land += slot["observedUsage"]["land"]
        below += slot["observedUsage"]["belowOverlay"]
    if land and below == land:
        return "underwater-only"
    if below:
        return "mixed"
    return "surface"


def editor_material_is_paintable(recipe: dict, family_id: int) -> bool:
    """Return whether the exact corner component can form a useful brush.

    The first pass preserves every source component in every slot signature.
    The editor palette is a second pass: a component is selectable only when
    the atlas contains either a uniform tile or a one-on-three vertex stamp.
    Other components remain available as transition support in the atlas
    editor, but are not presented as brushes that cannot paint coherently.
    """
    for slot in recipe["slots"]:
        corners = list(slot["signature"].values())
        count = corners.count(family_id)
        if count == 4:
            return True
        if count == 1 and len(set(corner for corner in corners
                                  if corner != family_id)) == 1:
            return True
    return False


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiled", type=Path, required=True)
    parser.add_argument("--hd", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    compiled = json.loads(args.compiled.read_text(encoding="utf-8"))
    hd_index, original_index = res_index(args.hd), res_index(args.original)
    presets = []
    for recipe in compiled["recipes"]:
        namespace = recipe["namespace"]
        atlas_parts = recipe["slotCount"] // 64
        bindings = []
        for variant, archive, index in (("hd", args.hd, hd_index),
                                        ("original", args.original, original_index)):
            value = pixel_hash(archive, index, namespace, atlas_parts)
            if value:
                bindings.append({"variant": variant, "pixelSha256": value})
        signatures = bytearray()
        for slot in recipe["slots"]:
            signature = slot["signature"]
            signatures.extend(signature[name] for name in
                              ("northWest", "northEast", "southEast", "southWest"))
        families = []
        for family in recipe["families"]:
            families.append({
                "id": family["id"],
                "name": family["suggestedName"],
                "allowedLayers": family["allowedLayers"],
                "underwaterRole": underwater_role(recipe, family["id"]),
                "tileType": family["tileType"],
                "thumbnailSlot": family["thumbnailSlot"],
                "paintSelectable": editor_material_is_paintable(
                    recipe, family["id"]),
            })
        presets.append({
            "namespace": namespace, "slotCount": recipe["slotCount"],
            "bindings": bindings, "families": families,
            "signaturesBase64": base64.b64encode(signatures).decode("ascii"),
        })
    result = {
        "schemaVersion": 1,
        "provenance": "built-in-material-atlas-v2",
        "presets": presets,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":"), ensure_ascii=False) + "\n",
                           encoding="utf-8")
    print(json.dumps({"presets": len(presets),
                      "bindings": sum(len(item["bindings"]) for item in presets),
                      "bytes": args.output.stat().st_size}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
