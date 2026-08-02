#!/usr/bin/env python3
"""Build compact semantic-atlas presets from reviewed derived metadata.

The output contains semantic IDs, names, corner signatures, and hashes only.
It never embeds source textures, masks, recipes, or installed archives.
"""

from __future__ import annotations

import argparse
import base64
import copy
import hashlib
import json
from pathlib import Path
import struct
from collections import Counter, defaultdict

import numpy as np
from PIL import Image


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


def editor_tiles(archive: Path, index: dict[str, dict], namespace: str,
                 atlas_parts: int) -> list[np.ndarray]:
    """Decode tiles in the exact QImage order/orientation used by CTileForm."""
    result = []
    for part in range(atlas_parts):
        name = f"{namespace}{part:03d}.mmp".lower()
        atlas = np.flipud(decode_dxt1(read_member(archive, index[name])))
        tile_size = atlas.shape[0] // 8
        for column in range(8):
            for row in range(8):
                result.append(np.flipud(
                    atlas[column * tile_size:(column + 1) * tile_size,
                          row * tile_size:(row + 1) * tile_size]).copy())
    return result


def reviewed_zone2_stone_families(recipe: dict,
                                  tiles: list[np.ndarray]) -> dict:
    """Separate Zone 2's three visible stone identities.

    EI-HD recipe components are exact atlas-construction evidence, but several
    Zone 2 component names cover more than one visible stone colour and
    `ground/gravel` duplicates the pink stone.  Classify only those reviewed
    stone-component corners against stable uniform atlas anchors.  Grass,
    ground, overlay, and every other authored corner remain untouched.
    """
    if recipe["namespace"].lower() != "zone2":
        return recipe
    result = copy.deepcopy(recipe)
    corner_names = ("northWest", "northEast", "southEast", "southWest")
    candidate_ids = {4, 5, 6, 10, 11, 19}
    anchor_slots = {
        4: (113, 114, 134, 138, 324),       # pale limestone
        6: (164, 205, 206, 162, 279, 496), # pink stone
        8: tuple(next(family for family in result["families"]
                      if family["id"] == 8)["uniformSlots"]),  # brown ground
        23: (285, 339, 401),                # moss-covered gray stone
    }

    def feature(slot: int, corner: int) -> np.ndarray:
        image = tiles[slot]
        height, width = image.shape[:2]
        # Material identity is encoded at the vertex.  A quarter-tile probe
        # reaches into the transition body and can classify a valid limestone
        # corner as its mossy neighbour (or vice versa).  Match the audit's
        # vertex-local sampling window.
        sample = max(8, min(height, width) // 8)
        y = 0 if corner in (0, 1) else height - sample
        x = 0 if corner in (0, 3) else width - sample
        patch = image[y:y + sample, x:x + sample, :3]
        rgb = np.asarray(Image.fromarray(patch.astype(np.uint8), "RGB").resize(
            (12, 12), Image.Resampling.LANCZOS), dtype=np.float64) / 255.0
        mean = rgb.mean(axis=(0, 1))
        deviation = rgb.std(axis=(0, 1))
        maximum = rgb.max(axis=2)
        minimum = rgb.min(axis=2)
        chroma = maximum - minimum
        hue_x = np.zeros_like(maximum)
        hue_y = np.zeros_like(maximum)
        valid = chroma > 1e-5
        red = valid & (maximum == rgb[:, :, 0])
        green = valid & (maximum == rgb[:, :, 1])
        blue = valid & (maximum == rgb[:, :, 2])
        hue = np.zeros_like(maximum)
        hue[red] = ((rgb[:, :, 1] - rgb[:, :, 2])[red] / chroma[red]) % 6.0
        hue[green] = ((rgb[:, :, 2] - rgb[:, :, 0])[green] / chroma[green]) + 2.0
        hue[blue] = ((rgb[:, :, 0] - rgb[:, :, 1])[blue] / chroma[blue]) + 4.0
        angle = hue * (np.pi / 3.0)
        hue_x[valid] = np.cos(angle[valid]) * chroma[valid]
        hue_y[valid] = np.sin(angle[valid]) * chroma[valid]
        gray = rgb @ np.asarray((0.2126, 0.7152, 0.0722))
        gradient = np.concatenate((np.abs(np.diff(gray, axis=0)).ravel(),
                                   np.abs(np.diff(gray, axis=1)).ravel()))
        texture = (gray - gray.mean()) / (gray.std() + 0.03)
        return np.concatenate((mean * 5.0, deviation * 3.0,
                               np.asarray((chroma.mean(), chroma.std(),
                                           hue_x.mean(), hue_y.mean(),
                                           gradient.mean(),
                                           gradient.std())) * 3.0,
                               texture[::2, ::2].ravel() * 0.16))

    def distance(value: np.ndarray, prototypes: list[np.ndarray]) -> float:
        return min(float(np.sqrt(np.square(value - candidate).mean()))
                   for candidate in prototypes)

    eligible = [
        (slot, corner_index, name)
        for slot in result["slots"]
        for corner_index, name in enumerate(corner_names)
        if slot["signature"][name] in candidate_ids
    ]
    seed_anchors = {
        family_id: [feature(slot, corner)
                    for slot in slots for corner in range(4)]
        for family_id, slots in anchor_slots.items()
    }
    anchors = copy.deepcopy(seed_anchors)
    converged = False
    for _ in range(8):
        changed = 0
        for slot, corner_index, name in eligible:
            value = feature(slot["slot"], corner_index)
            replacement = min(
                anchors,
                key=lambda family_id: distance(value, anchors[family_id]))
            changed += slot["signature"][name] != replacement
            slot["signature"][name] = replacement

        # A classification that creates a complete four-corner material gives
        # the next pass additional atlas-native variants. This prevents the
        # seed set from classifying a later byte/texture-equivalent variant as
        # another family merely because that variant was absent from the hand-
        # selected anchors. Rebuild from seeds each pass; never bootstrap from
        # a transition corner or retain a family that ceased to be uniform.
        anchors = copy.deepcopy(seed_anchors)
        for slot in result["slots"]:
            values = list(slot["signature"].values())
            if len(set(values)) != 1 or values[0] not in anchors:
                continue
            anchors[values[0]].extend(
                feature(slot["slot"], corner) for corner in range(4))
        if changed == 0:
            converged = True
            break
    if not converged:
        raise ValueError("Zone 2 stone identity refinement did not converge")

    by_id = {family["id"]: family for family in result["families"]}
    limestone = copy.deepcopy(by_id[4])
    # Human vocabulary review standardized this reused material identity with
    # the Zone 1 baseline and the rest of the atlas catalog.
    limestone["suggestedName"] = "Stone / Lime"
    limestone["thumbnailSlot"] = 113
    pink = copy.deepcopy(by_id[6])
    pink["suggestedName"] = "Stone / Pink"
    pink["thumbnailSlot"] = 164
    mossy = copy.deepcopy(by_id[6])
    mossy["id"] = 23
    mossy["sourceFamily"] = "reviewed/stone/mossy"
    mossy["suggestedName"] = "Stone / Mossy"
    mossy["thumbnailSlot"] = 285
    result["families"] = [family for family in result["families"]
                          if family["id"] not in candidate_ids]
    result["families"].extend((limestone, pink, mossy))
    result["families"].sort(key=lambda family: family["id"])
    return result


def reviewed_unresolved_questionnaire_corrections(
        recipe: dict) -> tuple[dict[tuple[int, int], int],
                               dict[int, int], dict[int, str]]:
    """Resolve the completed bz3g questionnaire without guessing follow-ups."""
    decisions_path = Path(__file__).with_name(
        "semantic_atlas_unresolved_review_decisions.json")
    decisions = json.loads(decisions_path.read_text(encoding="utf-8"))
    if recipe["namespace"].lower() != decisions["namespace"].lower():
        return {}, {}, {}

    by_name = {
        family["suggestedName"]: int(family["id"])
        for family in recipe["families"]
        if family.get("suggestedName")
    }
    required_names = {
        decision["family"]
        for decision in (
            decisions["cornerDecisions"]
            + decisions.get("verifiedSupplementalCorrections", []))
    } | {"Rock / Dark", "Ground / Mountainous"}
    if not (required_names - {
            "Rock / Dark", "Ground / Mountainous"}).issubset(by_name):
        return {}, {}, {}
    if "Rock / Dark" not in by_name and "Ground / Dark" in by_name:
        by_name["Rock / Dark"] = by_name["Ground / Dark"]
    corner_index = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}
    corrections = {}
    for decision in (
            decisions["cornerDecisions"]
            + decisions.get("verifiedSupplementalCorrections", [])):
        if decision.get("status") == "needs-follow-up":
            continue
        corrections[(int(decision["slot"]),
                     corner_index[decision["corner"]])] = by_name[
                         decision["family"]]

    # The completed family-level review merges the visually indistinguishable
    # Rock / Dark and Ground / Mountainous groups. Existing presets already use
    # Ground / Dark as the user-facing vocabulary; Ground / Dark underwater is
    # a separate map-use classification and remains distinct.
    aliases = {
        (by_name["Ground / Mountainous"]
         if "Ground / Mountainous" in by_name else 7):
        by_name["Rock / Dark"]
    }
    renames = (
        {by_name["Rock / Dark"]: "Ground / Dark"}
        if "Ground / Dark" not in by_name else {})
    return corrections, aliases, renames


def reviewed_fragment_corrections(recipe: dict) -> dict:
    """Apply corner-level corrections proven against decoded atlas pixels.

    Recipe component labels describe construction inputs, not necessarily a
    paint material.  These corrections are deliberately explicit: each entry
    was reviewed at the vertex-local corner, against uniform anchors and the
    surrounding signature.  No nearest-colour result writes semantics here.
    """
    namespace = recipe["namespace"].lower()
    corner_names = ("northWest", "northEast", "southEast", "southWest")
    corrections: dict[tuple[int, int], int] = {}
    removed_ids: set[int] = set()
    family_renames: dict[int, str] = {}
    family_aliases: dict[int, int] = {}

    if namespace == "zone2":
        corrections = {
            # Exact decoded-pixel conflicts in the close Zone 2 grass hues.
            # Each source corner below is byte-identical to a vertex-local
            # patch from exactly one differently named uniform family.  The
            # same check reports zero conflicts for the known-good Zone 1
            # table.  Slot 169 SW remains untouched because its patch is
            # shared by two uniform anchors and is therefore ambiguous.
            (5, 1): 7, (16, 1): 7, (24, 1): 7, (25, 1): 7,
            (34, 1): 7, (38, 0): 3, (52, 1): 7, (53, 2): 1,
            (55, 1): 7, (81, 2): 1, (93, 0): 1,
            (105, 0): 2, (105, 3): 7, (110, 3): 7,
            (131, 2): 1, (141, 3): 7, (148, 1): 7,
            (155, 0): 2, (157, 3): 7, (186, 0): 1,
            (210, 2): 1, (215, 0): 2, (242, 0): 2,
            (246, 2): 1, (267, 3): 7, (271, 3): 23,
            (283, 0): 2, (309, 3): 14, (360, 0): 2,
            (294, 3): 14, (353, 1): 3,
            (371, 2): 1, (382, 1): 2, (387, 0): 2,
            (393, 1): 7, (406, 0): 2, (410, 3): 3,
            (412, 2): 1,
            (428, 0): 2, (431, 0): 14, (442, 0): 3,
            (449, 2): 1, (450, 0): 2, (453, 3): 7,
            (464, 2): 1, (467, 0): 2, (471, 1): 7,
            (472, 1): 7, (488, 3): 7,
            (124, 3): 3,

            # Grass / Irregular remains a real yellow transition-only family.
            # These seven occurrences are visibly other established families.
            (54, 3): 23, (77, 0): 8, (433, 0): 8, (434, 0): 8,
            (458, 0): 8, (458, 2): 7, (464, 3): 6,

            # Misleading recipe fragments: resolve each vertex-local sample to
            # its established limestone, mossy stone, grass, ground, or pink
            # stone identity, then remove the empty phantom family.
            (78, 3): 23, (137, 3): 23, (172, 3): 4, (187, 3): 23,
            (262, 0): 4, (295, 3): 23, (304, 0): 7,
            (320, 0): 23, (320, 1): 23, (320, 3): 4,
            (334, 1): 23, (334, 2): 23, (334, 3): 4,
            (359, 3): 4, (367, 2): 7, (367, 3): 4, (370, 3): 4,
            (376, 0): 4, (393, 0): 4, (395, 0): 4, (395, 3): 23,
            (398, 2): 9, (398, 3): 4, (447, 1): 4, (469, 3): 6,

            (333, 0): 6, (333, 3): 6, (336, 1): 6, (336, 3): 6,
            (405, 0): 4, (405, 3): 23, (503, 0): 4,
            (384, 2): 4, (384, 3): 4,
            (403, 1): 4, (403, 2): 23, (403, 3): 4,
            (460, 2): 8, (460, 3): 6,
            (448, 1): 8, (448, 2): 8, (448, 3): 8, (502, 0): 8,
            (485, 0): 8, (485, 2): 15,
            (506, 0): 4, (506, 3): 4,

            # Historical grouped review provisionally retained Grass / Faded
            # for the close Faded/Burnt set. The later in-game checkerboard
            # and explicit corner review below supersede these provisional
            # values. Slot 211 was reviewed as Common/Dark/Mossy/Mossy.
            # Slot 115 SW and the west side of slot 190 are the brighter Fresh
            # family, not Common. A later in-game checkerboard split the
            # contaminated Faded/Burnt anchors; slot 309 SW is byte-identical
            # to the clean Burnt anchor in both original and HD atlases.
            (169, 3): 7,
            (142, 0): 7, (142, 1): 7, (142, 2): 7, (142, 3): 7,
            (195, 0): 7, (195, 1): 7, (195, 2): 7, (195, 3): 7,
            (198, 1): 7, (198, 2): 7, (198, 3): 7,
            (299, 0): 7, (299, 1): 7, (299, 2): 7, (299, 3): 7,
            (493, 0): 7, (493, 1): 7, (493, 3): 7,
            (494, 0): 7, (494, 1): 7, (494, 3): 7,
            (211, 0): 2, (211, 1): 1, (211, 2): 23, (211, 3): 23,
            (115, 3): 3, (190, 0): 3, (190, 3): 3,

            # The grouped high-resolution follow-up resolved the remaining
            # shared Burnt/Faded/Brown pixel ambiguity at these six SW
            # corners as Grass / Faded.
            (201, 3): 7, (219, 3): 7, (270, 3): 7,
            (485, 3): 7, (491, 3): 7, (505, 3): 7,

            # Grouped high-resolution review showed that slots 268 and 284
            # are three-corner Faded transitions around Ground / Brown, not
            # uniform Faded interiors. Preserve the authored orientation:
            # Brown is NW in 268 and SE in 284.
            (268, 0): 8,
            (284, 2): 8,

            # The in-game three-slot checkerboard separated the two close
            # grass hues that the static sheet had collapsed as Faded. Slot
            # 26 remains the clean Faded anchor. Slot 195 is the clean Burnt
            # anchor: its NE/SE/SW vertex patches are byte-identical to the
            # independently reviewed Burnt corners of slots 212 and 297.
            # Human review then resolved the connected mixed tiles below.
            # A labeled follow-up then resolved every remaining corner:
            # 494 NE is Brown; 299 NE/SE and 198 SE/SW are Burnt. The complete
            # post-review exact-pixel audit additionally proves slot 309 SW is
            # Burnt in both archive variants.
            (142, 1): 14, (142, 2): 14, (142, 3): 14,
            (195, 0): 14, (195, 1): 14, (195, 2): 14, (195, 3): 14,
            (299, 0): 8, (299, 1): 14, (299, 2): 14, (299, 3): 14,
            (198, 1): 8, (198, 2): 14, (198, 3): 14,
            (493, 1): 14, (493, 3): 14,
            (494, 1): 8, (494, 3): 14,

            # Slot 423 falsely validated itself as uniform Faded. Its SW
            # corner is byte-identical in both original and HD atlases to two
            # independent Zone 2 Fresh anchors and Fresh anchors in bz4g,
            # Zones 6, 6_2, and 7.
            (423, 3): 3,

            # The refreshed false-uniform audit found two more exact grass
            # identities in the close Zone 2 hue set. Slot 98 NE is byte-
            # identical to the independent Grass / Dark slot 0 anchor, while
            # slot 273 SW is byte-identical to the independent Grass / Fresh
            # slot 27 anchor. Their other, merely similar quarters remain
            # untouched for grouped visual review. The same independent
            # original+HD match proves slot 115 NE is Dark, as recorded by the
            # earlier false-uniform audit but previously omitted from output.
            (98, 1): 1,
            (115, 1): 1,
            (273, 3): 3,

            # Slot 199 is visibly a limestone/grass transition, not a uniform
            # Fresh anchor. Its NW patch independently ranks the clean Stone /
            # Limestone anchor at slot 324 NW first (distance 0.041395; anchor
            # risk 0.143537). Correct only that stone quadrant.
            (199, 0): 4,

            # The refreshed all-profile false-uniform audit found three more
            # visibly mixed Zone 2 tiles. Original and HD pixels agree on all
            # four replacements: 427 SE is Faded grass; 193 NE and 476 NW are
            # Limestone; and 476 SW is Fresh grass. The remaining corners stay
            # unchanged until the close grass-hue component is resolved.
            (427, 2): 7,
            (193, 1): 4,
            (476, 0): 4, (476, 3): 3,

            # The original and HD contact sheets expose three more Faded
            # tiles that had falsely validated themselves as uniform. Slot
            # 204 alternates Limestone/Common/Limestone/Faded around the
            # four corners. Slot 304 is the same one-Limestone/three-Faded
            # construction already established at slot 212. Slot 473 has
            # Common at NW, Limestone at SE, and Faded on the other two
            # corners. The complete texture shapes agree in both variants;
            # this also supersedes the earlier contaminated-anchor rewrite
            # of slot 304 NW to Faded.
            (204, 0): 4, (204, 1): 2,
            (204, 2): 4, (204, 3): 7,
            (304, 0): 4,
            (473, 0): 2, (473, 2): 4,

            # Slot 499 is visibly mixed in both original and HD atlases:
            # Brown occupies the north edge, Ground / Dark is at SE, and the
            # SW patch is an exact Grass / Faded anchor match. It must not
            # remain a uniform Brown anchor.
            (499, 0): 8, (499, 1): 8,
            (499, 2): 15, (499, 3): 7,

            # Slot 408 is a uniform Fresh variant, not Common. Its SE patch
            # is byte-identical to the clean Fresh slot 27 anchor, every
            # corner prefers Fresh with the contaminated duplicate group
            # excluded, and both original and HD full tiles have the brighter
            # Fresh hue. Correcting the whole anchor preserves the distinct
            # Common/Fresh families while removing their false exact overlap.
            (408, 0): 3, (408, 1): 3,
            (408, 2): 3, (408, 3): 3,

            # The intra-tile audit exposed two nominally uniform Mossy
            # anchors with the same authored north/south construction. Slots
            # 339 and 401 retain Mossy stone across the north; their pale
            # cracked south corners are the established Limestone surface.
            (339, 2): 4, (339, 3): 4,
            (401, 3): 4,

            # Human review of the high-resolution splatter constructions uses
            # the material touching each vertex, not the misleading centre
            # body. Slots 128/129 alternate Common and Dark around the tile;
            # slot 500 is Faded on the west and Common on the east. These are
            # explicit per-corner decisions and must not be generalized to
            # merely similar grass variants.
            (128, 0): 2, (128, 1): 1, (128, 2): 2, (128, 3): 1,
            (129, 0): 2, (129, 1): 1, (129, 2): 2, (129, 3): 1,
            (500, 0): 7, (500, 1): 2, (500, 2): 2, (500, 3): 7,

            # Whole-map continuity, leave-group-out pixels in both source
            # variants, and complete-tile review agree that slot 188 is not
            # uniform Fresh: its SE vertex is Grass / Common.
            (188, 2): 2,
        }
        removed_ids = {13, 16, 17, 18, 20, 21, 22}

    if namespace == "zone8":
        corrections = {
            # Slot 227 is not a uniform Sand / Wet anchor. Its NW vertex is
            # the established Rock / Wet surface in both original and HD
            # pixels. Authored Zone 8 map continuity independently agrees at
            # all 46 occurrences; the other three wet-sand corners remain
            # unchanged because their supporting map votes are not unanimous.
            (227, 0): 1,

            # Human-reviewed shared-bz5g ambiguity. These pale corners are
            # Stone / Light; slot 341 SW is Ground / Gravel.
            (62, 1): 4, (210, 1): 4, (232, 1): 4, (271, 1): 4,
            (289, 1): 4, (325, 1): 4, (344, 1): 4, (356, 1): 4,
            (422, 1): 4, (450, 1): 4, (453, 1): 4,
            (341, 3): 5,

            # Grouped review of the mixed uniform anchors and nearby
            # transitions. Sand / Common and Sand / Light remain distinct:
            # slots 105/107 are Common on the north edge and Light on the
            # south edge. Slot 19 is wholly Ground / Gravel, while slot 436 is
            # Gravel/Common/Dark/Dark.
            (19, 0): 5, (19, 1): 5, (19, 2): 5, (19, 3): 5,
            (105, 0): 7, (105, 1): 7, (105, 2): 12, (105, 3): 12,
            (107, 0): 7, (107, 1): 7, (107, 2): 12, (107, 3): 12,
            (436, 0): 5, (436, 1): 7, (436, 2): 3, (436, 3): 3,
            (74, 3): 3,

            # Slot 437 falsely validated itself as uniform Slabs / Square.
            # Its SE patch is byte-identical to independent Stone / Light
            # anchors in this atlas (slot 4 SE) and Zone 7 (slot 128 SE).
            # Only that proven corner is changed; the other three remain for
            # grouped visual resolution.
            (437, 2): 4,

            # Slot 443 was another visibly mixed tile presented as a uniform
            # Grass / Common anchor.  Its NE patch is byte-identical to the
            # established Grass / Dark uniform anchors (slots 111/112) and is
            # visibly the same dark grass.  The other three corners remain
            # unchanged until their close Common/Burnt evidence is reviewed.
            (443, 1): 13,

            # Slot 377 is not uniform Sand / Gravel. Its NW patch independently
            # matches the clean Sand / Dark anchor at slot 3 NW (distance
            # 0.008734; anchor risk 0.077088), and the full tile shows that
            # darker sand only in the north-west corner.
            (377, 0): 3,

            # The shared bz5g transition-only Grass / Irregular fragment was
            # re-audited after the Zone 2 classifier ownership fix. Across
            # three vertex scales and the complete high-resolution tiles,
            # these fourteen corners are the established yellow-brown Grass /
            # Burnt identity, while 308 NW and 369 SE are Ground / Gravel.
            (269, 1): 8,
            (364, 1): 8, (364, 2): 8,
            (375, 3): 8, (376, 3): 8,
            (384, 0): 8, (388, 3): 8,
            (396, 1): 8, (406, 2): 8,
            (421, 0): 8,
            (472, 2): 8, (472, 3): 8,
            (478, 0): 8, (504, 3): 8,
            (308, 0): 5, (369, 2): 5,

            # The seven initially split-vote corners were then reviewed at
            # 320 px per tile beside the established Burnt, Gravel, and Light
            # anchors. Their visible vertex material completes the same split:
            # 384 SW and 388 SE are Gravel; the other five are Burnt. This
            # removes the empty fragment family rather than leaving an
            # unpaintable internal label in the atlas editor.
            (307, 0): 8,
            (384, 1): 8, (384, 3): 5,
            (388, 2): 5,
            (417, 0): 8, (417, 2): 8,
            (495, 2): 8,

            # Re-auditing the corrected uniform-anchor graph exposed slot 332
            # as another false uniform. Its NE patch independently matches the
            # clean Sand / Common anchor at slot 12 NE (distance 0.008432;
            # anchor risk 0.089563) and is visibly the warmer Common sand.
            (332, 1): 7,

            # Removing slot 332 from the Light anchor set exposed another
            # dependent false uniform: slot 294 SE matches the independent
            # Sand / Light anchor at slot 122 SE (distance 0.006538; anchor
            # risk 0.120689), consistent with its visibly pale south edge.
            (294, 2): 12,

            # Slot 323 is not uniform Ground / Dry. Its SW quadrant is visibly
            # dark sand and independently ranks the clean Sand / Dark slot 3
            # anchor first (distance 0.038383; anchor risk 0.077088).
            (323, 3): 3,

            # Slot 438 is visibly mixed, not a uniform Ground / Common anchor.
            # Its NW quadrant is the same pale stone texture as the clean
            # Stone / Light uniform slots 4/5. The zero-distance Common match
            # at slot 265 is another contaminated uniform anchor, so it is not
            # accepted as semantic evidence here.
            (438, 0): 4,

            # Complete-tile comparison against the already-authored
            # transition set removes three more false uniform anchors. Slot
            # 420 repeats the Irregular/Light-stone/Light-stone/Gravel
            # construction at slot 478. Slots 428 and 445 complete the same
            # Common/Gravel/Irregular graph used by 257-258 and 297/342/388.
            (420, 0): 8, (420, 1): 4, (420, 2): 4,
            (428, 0): 10, (428, 1): 5,
            (445, 0): 10, (445, 1): 5,
            (445, 2): 10, (445, 3): 5,

            # Grouped mixed-tile review identifies Gravel at three corners of
            # each false-uniform slot and the remaining NE corner as Stone /
            # Light. The NE identity was deliberately left untouched until
            # the titled high-resolution follow-up named it explicitly.
            (273, 0): 5, (273, 1): 4, (273, 2): 5, (273, 3): 5,
            (274, 0): 5, (274, 1): 4, (274, 2): 5, (274, 3): 5,
        }
        removed_ids = {14, 20}
        # The nominal Ground / Brown family (20) has only three corners.
        # Two are byte-identical to Grass / Irregular (8), the third selects
        # the same anchor in both original and HD audits, and complete-tile
        # review shows the same dark green irregular material at every vertex.
        family_aliases = {20: 8}
        family_renames = {8: "Grass / Irregular"}

    if namespace == "bz4g":
        corrections = {
            # The visually mixed slots previously validated themselves as
            # uniform Cobblestone anchors. Keep only their reviewed paved
            # sides and assign the visible sand/ground halves explicitly.
            (498, 0): 8, (498, 1): 15, (498, 2): 15, (498, 3): 14,
            (501, 0): 15, (501, 1): 15, (501, 2): 5, (501, 3): 14,

            # Uniform-overlap review proved that the focused corners in the
            # nominally uniform source tiles belong to the established Lime
            # and Fresh families.
            (261, 2): 2,
            (116, 3): 17, (117, 3): 17,

            # Human review rejected the old all-Fresh signature: the complete
            # tile is Irregular across its north half and Fresh across its
            # south half.  Encode all four corners so regeneration cannot
            # silently restore the false uniform source assignment.
            (474, 0): 4, (474, 1): 4, (474, 2): 17, (474, 3): 17,

            # Slot 154 falsely presented four Ground / Gray corners. Its SE
            # patch independently matches the clean Sand / Dark anchor at
            # slot 6 SE (distance 0.009068; anchor risk 0.077088), consistent
            # with the visibly sandy south-east quadrant.
            (154, 2): 5,

            # Slot 155 is also visibly mixed. Its SE quadrant is clean dark
            # sand and independently ranks the low-variance Sand / Dark slot 6
            # anchor first (distance 0.053405; anchor risk 0.077088).
            (155, 2): 5,

            # Human-reviewed four-corner resolution. Sand / Light is a real
            # distinct component in this atlas, not Ground / Light or Common
            # sand; it is introduced below as reviewed family 25.
            (496, 0): 8, (496, 1): 25,
            (496, 2): 14, (496, 3): 15,
            (492, 0): 3, (492, 1): 25,
            (492, 2): 25, (492, 3): 8,

            # Slot 507 is not a uniform Ground / Gray anchor. Its NE corner
            # is visibly dark sand and independently ranks the clean slot 6
            # Sand / Dark anchor first in both original and HD pixels
            # (0.082517 / 0.083247). Keep its other close-ranked corners
            # unchanged.
            (507, 1): 5,

            # Slots 58/59 are repeated one-corner Common-on-Dark grass
            # stamps, not uniform Dark anchors.  Their NW quadrants are
            # visibly brighter and every authored-map occurrence resolves
            # that vertex to Grass / Common (26/26 and 27/27 supported
            # vertices); the other three corners retain Grass / Dark.
            (58, 0): 1,
            (59, 0): 1,

            # Grouped mixed-tile review resolves all four visible grass
            # corners. The titled follow-up names the previously unresolved
            # SW quadrants as Fresh in slot 382 and Irregular in slot 434.
            (382, 0): 1, (382, 1): 1, (382, 2): 9, (382, 3): 17,
            (434, 0): 9, (434, 1): 1, (434, 2): 1, (434, 3): 4,
        }
        # Ground / Light (6) and Ground / Trampled (12) were false
        # transition-only splits. Across all 19 and 21 occurrences
        # respectively, both original and HD decoded pixels independently
        # choose Ground / Common (3), and complete contact sheets show one
        # coherent brown material rather than distinct light/trampled ones.
        family_aliases = {6: 3, 11: 2, 12: 3, 16: 2}
        removed_ids.update({6, 11, 12, 16})

    if namespace == "zone14":
        corrections = {
            # The reviewed corners are the same black stone texture as the
            # established Stone / Black anchors. Zone14 is the canonical
            # pixel identity also used by bz11k.
            (35, 2): 8,
            (297, 1): 8,

            # Slot 274 falsely validated itself as a uniform Mountainous
            # anchor.  With the complete slot excluded, NW and SW are exact
            # Trampled matches and NE/SE independently rank Trampled first;
            # the full-tile comparison shows the same ground with footprint
            # decoration as the established Trampled variants.
            (274, 0): 16, (274, 1): 16,
            (274, 2): 16, (274, 3): 16,

            # Slot 419 is a visibly mixed tile, not a uniform Mountainous
            # anchor. Its SW patch independently matches the clean Rock /
            # Greenish anchor at slot 24 SW (distance 0.002346; anchor risk
            # 0.143808). The other three corners remain unresolved here.
            (419, 3): 10,

            # The refreshed false-uniform audit found the same mossy-stone
            # quadrant embedded in two nominally uniform anchors. Both SW
            # patches independently rank the clean Stone / Moss slot 491
            # anchor first (distances 0.001270 and 0.002600), and the complete
            # tiles visibly show the shared green/moss texture only there.
            (25, 3): 7,
            (327, 3): 7,

            # Slot 497 completes the north/south inverse of the established
            # Moss/Grayish edge at slots 221/223/224. Its north half is Stone
            # / Grayish and its south half is Stone / Moss; it must not remain
            # a uniform Mountainous anchor.
            (497, 0): 11, (497, 1): 11,
            (497, 2): 7, (497, 3): 7,

            # Slot 506 is a one-corner Stone / Moss stamp on Rock /
            # Greenish, not a uniform Greenish variant. The HD quadrant is
            # visually identical to the clean Moss family and the authored
            # bz11k map independently resolves its NW vertex to family 7
            # under the convention calibrated by 3,117/3,969 unanimous map
            # vertices (vertical preview mirror, clockwise packed rotation).
            (506, 0): 7,

            # Slot 64 is another contaminated Grayish anchor.  Its NE
            # quadrant is visibly the established Stone / Dark texture and
            # thirteen authored-map vertices select family 1 there, with a
            # single disagreement.  The other three quadrants remain
            # Grayish pending stronger evidence.
            (64, 1): 1,

            # Slot 32 is a Stone / Dark NW stamp on Mountainous ground. The
            # visible cracked quadrant, HD pixel owner, and all 15 supported
            # bz11k map vertices agree.
            (32, 0): 1,
        }
        # Family 2 (Ice / Cracked) is not a separate paint identity. Its
        # uniform slots 3 and 30, plus every mixed occurrence, resolve to the
        # established Ice / Common family in the authored bz11k and Zone 14
        # maps. Zone 14 supplies especially strong coverage: slots 3 and 30
        # have 106 and 19 occurrences, while every supported corner from the
        # eight mixed slots votes for family 3. The full original and HD tiles
        # show texture variation within one ice surface, with no 2/3
        # transition topology that would justify two brushes.
        family_aliases = {2: 3}
        removed_ids = {2}

    if namespace == "bz9k":
        corrections = {
            # The shared Zone 11 snow atlas contains four repeated
            # Common-snow-on-Dark-snow stamps.  The pale crystal-pattern NW
            # quadrant is visibly Snow / Common and the authored map resolves
            # all 182 supported NW vertices to family 3 with no disagreement;
            # the other corners remain Snow / Dark.
            (37, 0): 3,
            (40, 0): 3,
            (43, 0): 3,
            (45, 0): 3,

            # These three nominally uniform Ground / Light tiles contain the
            # same unmistakable Paving / Square SW quadrant.  Authored-map
            # continuity independently selects family 10 at that corner for
            # every used variant, with no disagreement.
            (371, 3): 10,
            (372, 3): 10,
            (374, 3): 10,

            # Slot 368 is not uniform paving. Its NE quadrant is the brown
            # Ground / Light surface represented by the clean slot 128
            # anchor; pixel and authored-map evidence agree.
            (368, 1): 6,
        }

    if namespace == "zone10":
        corrections = {
            # The grouped irregular-grass comparison retained the normal
            # Irregular identity at all four focused SE corners.
            (439, 2): 11, (136, 2): 11,
            (137, 2): 11, (176, 2): 11,

            # That SE retention did not make slot 176 uniform: its visibly
            # brown NW quadrant independently ranks the clean Ground / Dirt
            # anchor at slot 8 NW first (distance 0.029322; anchor risk
            # 0.132165). Preserve the reviewed SE and correct only this NW.
            (176, 0): 5,

            # Slot 316 is not a uniform Ground / Common anchor. SE is an
            # almost exact Winter-normal match (0.003313), while SW
            # independently ranks Winter-faded first. NW/NE remain Common:
            # despite broader quadrant variation, their vertex-local bytes
            # are exact Common matches, which is the semantic paint boundary.
            (316, 2): 9, (316, 3): 10,

            # Slot 153 is visibly a three-family tile, not a uniform Stone /
            # Black anchor.  Under the corpus-wide calibrated map convention,
            # all 17 occurrences resolve NW to Stone / Greenish and NE to
            # Grass / Sparse dark; the south edge remains Stone / Black.
            (153, 0): 2, (153, 1): 1,

            # The strict topology audit proposed several replacements, but
            # only slot 298 SW survives complete-tile review in both source
            # variants. The pale quadrant is visibly the established Slabs /
            # Square surface (uniform slot 41), not Ground / Dry. Slot 342's
            # tempting Slabs / Carved proposal fails that visual comparison
            # and is deliberately left unchanged.
            (298, 3): 6,

            # Slot 308 is a Faces-on-Carved construction, not a uniform
            # Carved anchor. Its NE quadrant matches Slabs / Faces in both
            # source variants and in authored-map continuity.
            (308, 1): 7,

            # The post-R44 map/pixel intersection exposes two more false
            # uniform anchors. Slot 97 carries Stone / Black at NE/SW, and
            # slot 137 carries Ground / Dirt at NE. Both source variants,
            # complete-tile review, and independent map vertices agree.
            (97, 1): 3, (97, 3): 3,
            (137, 1): 5,

            # Focused full-tile review established that the former Ground / Dry
            # anchor at slot 398 is the plain member of Slabs / Square. Apply
            # only transition corners explicitly reviewed with it. R58 then
            # reviewed the final 20 exact-pixel overlaps individually and
            # assigned every one to Slabs / Square. Other Ground / Dry corners
            # remain untouched: they do not share this exact candidate and
            # still require separate evidence before family 12 can be removed.
            (178, 3): 6,
            (211, 1): 6, (211, 2): 6, (211, 3): 6,
            (260, 1): 6, (260, 2): 6, (260, 3): 6,
            (326, 1): 6, (326, 2): 6, (326, 3): 6,
            (398, 0): 6, (398, 1): 6, (398, 2): 6, (398, 3): 6,
            (491, 2): 6, (491, 3): 6,
            (503, 2): 6, (503, 3): 6,
            (180, 3): 6,
            (212, 1): 6, (212, 2): 6, (212, 3): 6,
            (217, 3): 6,
            (220, 3): 6,
            (276, 2): 6,
            (324, 3): 6,
            (361, 2): 6,
            (391, 3): 6,
            (392, 3): 6,
            (412, 3): 6,
            (427, 2): 6,
            (476, 3): 6,
            (481, 3): 6,
            (483, 3): 6,
            (487, 3): 6,
            (489, 3): 6,
            (495, 3): 6,
            (500, 2): 6,

            # R59 reviewed every remaining Ground / Dry corner. Thirty-nine
            # belong to Slabs / Square; slot 495 NW and slot 502 NE belong to
            # Slabs / Carved. This exhausts family 12 exactly, so it is removed
            # below rather than left as an ownerless internal palette family.
            (144, 3): 6, (145, 3): 6, (146, 3): 6,
            (147, 3): 6, (148, 3): 6,
            (217, 2): 6,
            (219, 2): 6, (219, 3): 6,
            (220, 2): 6,
            (276, 1): 6, (276, 3): 6,
            (294, 1): 6, (294, 3): 6,
            (295, 1): 6, (295, 3): 6,
            (297, 3): 6,
            (317, 2): 6, (317, 3): 6,
            (332, 2): 6, (332, 3): 6,
            (333, 2): 6, (333, 3): 6,
            (346, 3): 6,
            (353, 3): 6,
            (361, 3): 6,
            (381, 3): 6,
            (382, 3): 6,
            (399, 3): 6,
            (410, 3): 6,
            (427, 3): 6,
            (433, 3): 6,
            (458, 3): 6,
            (463, 3): 6,
            (487, 2): 6,
            (489, 2): 6,
            (495, 0): 9, (495, 2): 6,
            (500, 3): 6,
            (502, 1): 9, (502, 2): 6, (502, 3): 6,
        }
        # The grouped comparison explicitly merged Irregular dark into the
        # established Grass / Irregular family. Rewrite every occurrence so
        # no hidden duplicate identity survives outside the reviewed tiles.
        for slot in recipe["slots"]:
            for corner_index, name in enumerate(corner_names):
                if slot["signature"][name] == 8:
                    corrections[(slot["slot"], corner_index)] = 11
        removed_ids = {8, 12}

    if namespace == "zone3obr":
        # Three labels were created from isolated pale-stone corners, not
        # from distinct uniform materials. In both source variants every one
        # of their 19 occurrences selects the established Stone / Lime
        # identity, and the complete tiles show the same cracked surface.
        corrections = {
            # The Ground / Common thumbnail is a mixed tile. Its NW corner
            # is the established Ground / Dry surface in both complete-tile
            # review and authored-map continuity.
            (96, 0): 4,
        }
        family_aliases = {8: 1, 11: 1, 12: 1}
        removed_ids = {8, 11, 12}

    if namespace == "zone6":
        corrections = {
            # Authored-map vertex continuity completes the earlier human SW
            # decisions. Slot 148 is the Common/Dark checker construction,
            # not a uniform Dark anchor. Slot 216 is Common ground across the
            # north and Gravel across the south. Slot 318 has one Dark SE
            # corner on otherwise Common grass.
            (148, 0): 4, (148, 1): 2, (148, 2): 4, (148, 3): 2,
            (216, 0): 8, (216, 1): 8, (216, 2): 12, (216, 3): 12,
            (318, 2): 2,
            (269, 3): 2,


            # Slots 230/231 are one-corner Common-ground stamps on Gravel,
            # not uniform Gravel anchors. Their NW patch is visibly distinct
            # and resolves to family 8 at 16/22 authored-map vertices; only
            # one occurrence disagrees across both variants.
            (230, 0): 8,
            (231, 0): 8,

            # Slot 60 is a Mountainous-ground SW stamp on Sparse-dark grass.
            # The complete tile and both source variants agree with 114
            # independent authored-map votes and no Sparse-dark votes.
            (60, 3): 5,

            # Family-catalog review of each displayed thumbnail recovered
            # mixed corners that the recipe had treated as uniform. These
            # exact corners are independently supported by authored-map
            # continuity; no neighbouring corners are inferred from them.
            (28, 3): 5,
            (71, 0): 8,
            (394, 0): 8, (394, 1): 3,
            (401, 0): 1, (401, 2): 1, (401, 3): 5,

            # R50 resolved the matching one-corner Ground / Light fragment
            # as the established Ground / Common material. This is the only
            # family-19 occurrence in Zone6.
            (485, 1): 8,
        }
        # Greenish, Grayish, and Gray were transition-only fragments of the
        # established pale cracked Stone / Lime surface. Every occurrence
        # independently chooses Lime in both original and HD pixels, and the
        # complete contact sheets show no distinct material boundary.
        # The reviewed family catalog also found no visible or structural
        # boundary between Rock / Dark (6) and Ground / Mountainous (5).
        # Fold the duplicate brush into the established Ground family.
        family_aliases = {6: 5, 14: 1, 16: 1, 20: 1}
        removed_ids = {6, 14, 16, 20}

    if namespace == "zone6_2":
        corrections = {
            (148, 0): 4, (148, 1): 2, (148, 2): 4, (148, 3): 2,
            (216, 0): 8, (216, 1): 8, (216, 2): 15, (216, 3): 15,
            (318, 2): 2,
            (269, 3): 2,

            (230, 0): 8,
            (231, 0): 8,

            # Exact thumbnail-corner decisions corroborated by map
            # continuity. The R49 focused review selected Ground / Common
            # for slot 261 NE. Slot 394 SW was explicitly confirmed as its
            # existing Stone / Lime identity.
            (28, 3): 5,
            (71, 0): 8,
            (261, 1): 8,
            (394, 1): 3,

            # R50 reviewed every remaining Ground / Light corner at the same
            # scale. Slot 401's pale rocky NW/SE quadrants are Stone / Lime;
            # slot 485 NE is Ground / Common. These are all three family-19
            # occurrences, so the empty fragment family disappears.
            (401, 0): 1, (401, 2): 1,
            (485, 1): 8,
        }
        # Zone6_2 uses the same decoded stone fragments with shifted family
        # identifiers. Keep its semantic result aligned with Zone6. Cracked
        # ice (9) is also a visual variant of Common ice (10), not a separate
        # brush identity: the atlas contains no 9/10 transition, all 50 uses
        # of uniform slot 50 meet family 10 at every supported vertex, and
        # both Ice corners of slot 60 do so at all nine authored-map uses.
        family_aliases = {6: 5, 9: 10, 17: 1, 20: 1, 23: 1}
        removed_ids = {6, 9, 17, 20, 23}

    if namespace == "zone7":
        corrections = {
            # Slot 44 occurs 673 times. Its north corners resolve to Common
            # grass (436/626 unanimous votes) and its south corners to Dark
            # grass (665/670), completing the earlier reviewed SW identity.
            (44, 0): 3, (44, 1): 3, (44, 2): 2, (44, 3): 2,
            (42, 3): 2,

            # Four nominally uniform Common-grass variants each contain one
            # visibly dark quadrant. The stated corners receive 15-23
            # unanimous authored-map votes and complete the existing
            # Common/Dark transition set.
            (171, 1): 2,
            (173, 0): 2,
            (193, 2): 2,
            (199, 0): 2,

            # Slot 168 SW is the established Ground / Trampled surface in
            # both original and HD atlases (0.013665 nearest-anchor score,
            # 0.062094 margin). Other topology suggestions in slots 86/87
            # and 226 fail full-quadrant comparison and remain unchanged.
            (168, 3): 17,

            # Three more nominally uniform anchors are mixed tiles. Slot 149
            # carries Common sand at NW; slot 322 carries Ground / Brown at
            # NW and SE. HD/original pixels and authored-map votes agree.
            (149, 0): 10,
            (322, 0): 13, (322, 2): 13,

            # Reviewed catalog thumbnails expose five additional mixed
            # corners. The R49 same-scale comparison resolved slot 151 NW as
            # Ground / Brown, matching the 46 authored-map neighbour votes.
            (151, 0): 13,
            (159, 0): 6,
            (163, 2): 10,
            (256, 0): 13,
            (257, 3): 17,
            (495, 1): 10,

            # R50 resolved the complete Ground / Light and Ground / Dirt
            # fragment sets corner by corner. No neighbouring tile is
            # propagated: these are precisely the seven reviewed positions.
            (412, 3): 17,
            (226, 3): 17,
            (329, 0): 13, (329, 2): 13, (329, 3): 17,
            (335, 2): 13,
            (480, 3): 17,
        }
        # Stone / Gray (8) is another false transition-only split. Its five
        # occurrences are the same pale cracked surface as the independent
        # Stone / Light (6) uniform anchors in both original and HD atlases;
        # one occurrence is byte-identical and all five agree visually.
        family_aliases = {8: 6}
        removed_ids = {8}
        family_renames = {
            1: "Rock / Underwater emerald",
            4: "Sand / Underwater",
            14: "Ground / Cracked",
        }

    if namespace == "zone12":
        # Stone / Light was a false fragment family. Its complete set of
        # eight occurrences (slots 49-52/75 SW, 230/233 NW, and 235 NE)
        # visibly matches the cracked Stone / Gray anchor at slot 13. Across
        # the authored bz10k and Zone12 maps the high-usage corners receive
        # 55-207 independent Gray votes and zero Light votes; the remaining
        # low-usage corners agree in complete-tile review. No distinct Light
        # material or Light/Gray boundary survives.
        family_aliases = {4: 3}
        removed_ids = {4}

    if namespace == "zone13":
        corrections = {
            # Slot 312 is a one-corner Ground / Light stamp on Stone / Gray,
            # not a uniform gray-stone anchor.  Its NW patch is visibly brown
            # and all 20 authored-map occurrences independently resolve that
            # vertex to family 3; the remaining quadrants stay Stone / Gray.
            (312, 0): 3,

            # Slots 166/170 are Light-ground NW stamps on Trampled ground;
            # slots 240/401 carry Paving / Square at SW on Light ground.
            # Complete HD tiles, leave-group-out pixels, and authored-map
            # continuity independently agree on all four corners.
            (166, 0): 3, (170, 0): 3,
            (240, 3): 8, (401, 3): 8,

            # High-resolution family-catalog review corrected the displayed
            # thumbnail corners below. Each accepted corner has authored-map
            # support or agrees in both decoded source variants. Slot 63 SW
            # had no map vote and conflicting decoded rankings, so it was
            # resolved only after the R49 same-scale human comparison selected
            # the existing Grass / Tundra family.
            (15, 3): 6,
            (63, 1): 3, (63, 2): 4, (63, 3): 4,
            (164, 0): 3, (164, 1): 4,
            (112, 0): 3,
            (279, 3): 7,
            (324, 0): 3,
            (354, 1): 6, (354, 2): 1, (354, 3): 6,
            (445, 3): 7,
            (508, 3): 7,
        }
        family_renames = {4: "Grass / Tundra"}
        # Ground / Common has no four-corner tile in this atlas. Slot 164 was
        # merely the old lowest mixed occurrence (2x Common + 2x Tundra), so
        # it made a misleading family thumbnail. Slot 289 is the lowest
        # three-of-four occurrence and therefore the truthful stamp fallback.
        next(family for family in recipe["families"]
             if family["id"] == 9)["thumbnailSlot"] = 289

    if namespace == "zone19":
        corrections = {
            # Slots 282/285 are not uniform Stone / Orange: their NW corner is
            # Sand / Common and their other three corners are Stone / Orange.
            (282, 0): 1, (282, 1): 2, (282, 2): 2, (282, 3): 2,
            (285, 0): 1, (285, 1): 2, (285, 2): 2, (285, 3): 2,

            # High-usage authored-map continuity exposes five more false
            # uniform anchors. Their visible quadrants agree with the named
            # existing families and every reported corner has unanimous map
            # support under the globally calibrated corner convention.
            (152, 0): 1, (152, 1): 1, (152, 2): 1, (152, 3): 3,
            (180, 0): 10, (180, 1): 10, (180, 2): 10, (180, 3): 2,
            (193, 0): 10, (193, 1): 2, (193, 2): 2, (193, 3): 2,
            (292, 0): 10, (292, 1): 4, (292, 2): 4, (292, 3): 4,
            (445, 0): 6, (445, 1): 1, (445, 2): 3, (445, 3): 3,

            # The duplicate-anchor review explicitly merged Stone / Beige
            # into Stone / Desert. Slot 43 is byte-identical to a Desert
            # uniform tile and slot 330 carries the same connected identity.
            # Latest full-tile review corrected the earlier tentative
            # orientation: slot 181 SE is Orange and SW is Desert; the two
            # northern corners remain Stone / Desert.
            (43, 0): 10, (43, 1): 10, (43, 2): 10, (43, 3): 10,
            (181, 2): 2, (181, 3): 10,
            (330, 0): 10, (330, 1): 10, (330, 2): 10,

            # Slot 189 is a Desert-on-Orange stamp rather than a uniform
            # Orange anchor. Its visibly darker NW quadrant receives 65
            # Desert votes in the authored map; the other three quadrants
            # retain Orange.
            (189, 0): 10,

            # The Gravel family contained a large repeated set of false
            # uniform anchors. These are exact recurring stamp forms visible
            # in the complete tiles and independently supported by authored
            # map vertices. Preserve the Gravel background while recovering
            # the Common-sand, Sand/Grass, Orange-stone, Desert-stone, and
            # Lime-rock patches at their actual corners.
            (65, 0): 1, (65, 1): 1,
            (68, 0): 1, (68, 1): 1,
            (70, 0): 1, (70, 1): 1,
            (72, 0): 1, (72, 1): 1,
            (153, 0): 1, (154, 0): 1, (155, 0): 1,
            (156, 0): 1, (157, 0): 1, (158, 0): 1,
            (159, 0): 1, (160, 0): 1, (161, 0): 1,
            (226, 0): 6, (226, 1): 6,
            (231, 0): 6, (231, 1): 6,
            (241, 0): 2, (244, 0): 2, (245, 0): 2,
            (252, 0): 6, (254, 0): 6, (255, 0): 6,
            (256, 0): 6, (257, 0): 6,
            (296, 0): 10,
            (435, 0): 7,

            # The whole-map/pixel intersection exposes nine additional false
            # uniform sand and rock anchors. Each named corner visibly agrees
            # with the existing target family; the authored map supplies
            # 3-196 target votes and at most one current-family vote.
            (57, 3): 5,
            (100, 0): 1,
            (118, 3): 4, (124, 3): 4,
            (214, 0): 1, (216, 0): 1,
            (309, 1): 4, (310, 1): 4,
            (446, 1): 1,
        }
        removed_ids = {9}
        family_renames = {7: "Paving / Sand ruins"}

    if namespace == "zone20":
        corrections = {
            # Ten nominally uniform Stone / Black variants are the same
            # one-corner Stone / Greenish stamp.  The green NW quadrant is
            # visible in every full tile and authored-map neighbour voting
            # selects family 2 at that corner for every variant; all other
            # corners retain Stone / Black.
            (43, 0): 2, (45, 0): 2, (49, 0): 2, (51, 0): 2,
            (53, 0): 2, (55, 0): 2, (56, 0): 2, (58, 0): 2,
            (60, 0): 2, (62, 0): 2,


            # Reviewed family thumbnails are mixed rather than uniform.
            # Authored-map vertices and decoded quadrants agree on these
            # exact assignments.
            (37, 0): 2,
            (158, 2): 2, (158, 3): 3,
        }

    if namespace == "zone15":
        corrections = {
            # Slots 161/335 repeat the already-authored slot 163 construction:
            # Ground / Red on three corners and Rubble / Orange at SW. The
            # complete tiles in both Zone15 and bz13h visibly share that
            # rubble quadrant and must not remain uniform Red anchors.
            (161, 3): 5,
            (335, 3): 5,

            # Slot 361 completes the same Red/Rubble/Orange transition set.
            # NW/SE carry the established Rubble / Orange texture, NE is Red,
            # and SW is the pale Stone / Orange texture. Neighbouring authored
            # signatures 458/499/501/502/509 independently establish all
            # three family appearances and orientations.
            (361, 0): 5, (361, 1): 1,
            (361, 2): 5, (361, 3): 4,

            # These nominally uniform sand anchors contain repeated visible
            # corner stamps. Slot 106 has a Light-sand SW patch; slots
            # 170-172 have a Common-sand NW patch; and 201/203-206 carry the
            # established Gravel texture at SW. Every accepted corner has
            # unanimous authored-map continuity. We intentionally leave the
            # lower-confidence additional quadrants unchanged.
            (106, 3): 7,
            (170, 0): 3, (171, 0): 3, (172, 0): 3,
            (201, 3): 11, (203, 3): 11, (204, 3): 11,
            (205, 3): 11, (206, 3): 11,

            # Three more false uniform anchors survive the same independent
            # map/pixel/full-tile gate: slot 131 NW is Ground / Red and slots
            # 219/221 NW are Sand / Common.
            (131, 0): 1,
            (219, 0): 3, (221, 0): 3,

            # Slot 222 is not a uniform Sand / Gravel anchor. Its smooth NW
            # quadrant is Sand / Common in original and HD pixels, and all
            # seven supported authored-map vertices select Common.
            (222, 0): 3,
        }

    if namespace == "zone18":
        corrections = {
            # These nominally uniform sand tiles are repeated copies of
            # already-correct transition motifs in the same atlas. Slots
            # 137/138/140/141/143 match 139/142/144 (one Sand / Grass NW
            # corner over Common); slot 156 matches 155/157-160 (Sand /
            # Grass north over Common); and slot 337 matches 335/336
            # (Common, Grass, Common, Dark). Complete-tile comparison agrees
            # in both original and HD variants, so no nearest-colour guess is
            # involved.
            (137, 0): 10, (138, 0): 10, (140, 0): 10,
            (141, 0): 10, (143, 0): 10,
            (156, 0): 10, (156, 1): 10,
            (156, 2): 1, (156, 3): 1,
            (337, 0): 1, (337, 1): 10,
            (337, 2): 1, (337, 3): 3,

            # The adjoining Rock / Lime motif has the same duplicated-anchor
            # defect. Slots 229/233 duplicate the Common-sand NW form used by
            # 228/230-234, while 338/339 duplicate slot 340's Sand / Grass NW
            # form. The other three corners remain Rock / Lime.
            (229, 0): 1, (233, 0): 1,
            (338, 0): 10, (339, 0): 10,

            # Zone18 and bz16h share this complete four-family construction.
            # The already-reviewed bz16h tile provides the corresponding
            # semantic identity: Red at NW/SE, Weathered at NE, and Plant
            # soil at SW. Zone18's tile has the same visible quadrants and
            # must not remain a uniform Plant-soil anchor.
            (404, 0): 4, (404, 1): 8,
            (404, 2): 4, (404, 3): 2,

            # Four additional false Red anchors complete the rotated
            # one-Desert-algae-on-three-Red stamp set. Both the Zone18 pixels
            # and the paired bz16h HD atlas place green Desert algae at the
            # stated corner; existing slots 221/352 supply the opposite SW/NE
            # rotations. The remaining corners keep the clean Red texture.
            (397, 0): 5,
            (400, 0): 5,
            (413, 1): 5,
            (441, 2): 5,
        }

    if namespace == "bz16h":
        corrections = {
            # These five repeated variants are one-corner Sand / Grass stamps
            # over Sand / Common, not uniform Common anchors. Full-tile review
            # shows the green Sand / Grass texture only at NW; the remaining
            # three vertex-local quadrants are the pale Common sand.
            (137, 0): 10,
            (138, 0): 10,
            (140, 0): 10,
            (141, 0): 10,
            (143, 0): 10,

            # With the contaminated Common anchors removed, slot 337 resolves
            # as a genuine three-family transition: Common sand at NW/SE,
            # Sand / Grass at NE, and the existing Dark sand at SW. Each
            # candidate ranks first against an independent low-risk anchor and
            # the full-tile quadrants visibly follow that arrangement.
            (337, 0): 1,
            (337, 1): 10,
            (337, 2): 1,
            (337, 3): 3,

            # Slot 404 is a four-corner transition, not a uniform Plant-soil
            # anchor. NW/SE independently match Ground / Red, NE matches the
            # clean Stone / Weathered slot 15 anchor, and only SW retains
            # Ground / Plant soil. The full tile visibly confirms all four.
            (404, 0): 4,
            (404, 1): 8,
            (404, 2): 4,
            (404, 3): 2,

            # Slot 156 is the same north-edge construction as Zone18 and the
            # neighbouring reviewed variants: Sand / Grass across the north,
            # pale Sand / Common across the south. The complete-tile pair
            # rules out its former uniform/Dark north identity.
            (156, 0): 10,
            (156, 1): 10,
            (156, 2): 1,
            (156, 3): 1,

            # The Rock / Lime false-uniform pattern found in Zone18 is also
            # present here. Full-tile pairs show Common sand at NW for
            # 229/233 and Sand / Grass at NW for 338/339; the other three
            # corners remain Rock / Lime.
            (229, 0): 1,
            (233, 0): 1,
            (338, 0): 10,
            (339, 0): 10,

            # Paired Zone18/bz16h false Red anchors. The green Desert-algae
            # quadrant rotates NW/NW/NE/SE across these four variants; the
            # other three corners are visibly the clean Red material.
            (397, 0): 5,
            (400, 0): 5,
            (413, 1): 5,
            (441, 2): 5,
        }

    if namespace == "bz3g":
        corrections = {
            # The current all-profile uniform-anchor audit exposed a cluster
            # of visibly mixed tiles that the authored table had treated as
            # A/A/A/A.  Every entry below is either an exact vertex-local
            # match to an independent, low-risk family anchor or (slot 354
            # SE) the unmistakable radial paving quadrant.  Correcting the
            # complete false anchors before the generic exact-identity pass
            # prevents their contaminated patches from relabelling other
            # transitions.
            (17, 1): 4, (17, 3): 4,
            (18, 1): 4, (18, 3): 4,
            (110, 3): 6,
            (115, 0): 5, (115, 3): 6,
            (129, 1): 3,
            (204, 1): 2,
            (210, 1): 2, (210, 3): 8,
            (276, 1): 2,
            (329, 0): 3, (329, 2): 3,
            (354, 2): 15, (354, 3): 7,
            (394, 0): 14,
            (432, 1): 3,
            (442, 1): 3,
            (445, 1): 6,
            (453, 3): 3,
            (465, 0): 3, (465, 2): 6,
            (468, 0): 6, (468, 2): 6,

            # Paving / Radial slots 335 and 481 are false uniform anchors in
            # both HD and original atlases. Slot 335 carries radial paving at
            # NW/NE, Sparse-dark grass at SE (an exact cross-atlas corner
            # match), and Mountainous ground at SW. Slot 481 carries Grayish
            # stone at NW, radial paving at NE/SE, and Mountainous ground at
            # SW. Clean slots 29/2/6/9 and connected paving transitions fix
            # the identities and orientation independently.
            (335, 0): 11, (335, 1): 11,
            (335, 2): 5, (335, 3): 7,
            (481, 0): 3, (481, 1): 11,
            (481, 2): 11, (481, 3): 7,

            # Three more A/A/A/A entries are complete transitions. Slot 312
            # is Radial paving over three Rock / Dark corners (the user had
            # already identified it as non-uniform); 388 is
            # Gravel/Common/Dark/Common; and 510 is
            # Common/Common/Trampled/Dark.
            (312, 0): 11,
            (388, 0): 8, (388, 2): 1,
            (510, 2): 13, (510, 3): 1,

            # HD and original full tiles expose four Grayish/Sparse-dark
            # transitions which had also validated themselves as uniform
            # Grayish anchors. Together with slots 24-26 and 52-53 they form
            # the missing one-corner, north-edge, south-edge, and rotated
            # one-corner signatures without any colour inference.
            (23, 0): 5,
            (109, 2): 5, (109, 3): 5,
            (387, 0): 5, (387, 1): 5,
            (389, 1): 5,

            # Slot 330 is a one-corner Grass / Dark stamp on Grass / Sparse
            # dark, not a uniform Sparse-dark anchor. The NW quadrant is
            # visually the established green Dark family; its authored bz3g
            # map occurrence independently resolves NW to family 1 and NE to
            # family 5 under the globally calibrated map-corner convention.
            (330, 0): 1,

            # Human-reviewed retentions: these exact-pixel ambiguities remain
            # visibly Stone / Grayish despite sharing anchor bytes with other
            # families.
            (106, 3): 3, (257, 3): 3, (307, 3): 3, (308, 3): 3,
            (314, 3): 3, (356, 3): 3, (497, 3): 3,
            (60, 1): 3, (106, 1): 3, (168, 1): 3,
            (255, 1): 3, (436, 1): 3,
            (26, 3): 3, (61, 3): 3, (334, 3): 3,

            # Human-reviewed corrections from the grouped image review.
            (383, 1): 2,  # Grass / Sparse light
            (186, 1): 8,  # Ground / Gravel
            (291, 1): 2,  # Grass / Sparse light

            # Slot 273 SW is visibly the same pale cracked stone as the clean
            # Stone / Grayish anchor at slot 2. The tiny vertex patch is
            # byte-identical to both Sparse light and Grayish anchors, so the
            # complete quadrant supplies the deciding evidence. The expanded
            # same-hash comparison shows the same pale cracked quadrant at
            # slot 472 SW; neither matches the brown dotted Mountainous anchor.
            (273, 3): 3, (472, 3): 3,

            # The grouped reviewer identified every displayed corner in this
            # nine-slot set as Ground / Gravel. Slot 264 was discussed
            # separately and was identified as Stone / Grayish.
            (30, 3): 8, (34, 3): 8, (36, 3): 8,
            (41, 3): 8, (114, 3): 8, (255, 3): 8,
            (324, 3): 8, (488, 3): 8, (498, 3): 8,
            (264, 3): 3,

            # Cross-atlas exact anchors reproduced in both original and HD.
            # These corners were self-validating through nominally uniform
            # mixed tiles; independent maps consistently identify them as
            # Common ground, Sparse dark grass, or Rock / Dark respectively.
            (56, 1): 12,
            (108, 1): 5, (321, 0): 5, (404, 0): 5,
            (410, 2): 5, (504, 0): 5,
            (297, 3): 6,

            # Slot 462 is not uniform Stone / Grayish. Its SE patch matches the
            # clean Grass / Sparse dark anchor at slot 32 SE (distance
            # 0.004189; anchor risk 0.123465), and full-tile inspection shows
            # the same dark sparse grass in that quadrant.
            (462, 2): 5,

            # The next grouped review found a second false-uniform cluster in
            # Rock / Dark and Ground / Mountainous. The later completed family
            # questionnaire supersedes the earlier undecided state and merges
            # these groups below after applying the explicit corner decisions.
            (229, 2): 5, (229, 3): 7,
            (417, 1): 3,
            (104, 1): 3,
            (163, 2): 6, (163, 3): 6,
            (298, 0): 13, (298, 1): 2,
            (298, 2): 2, (298, 3): 12,
            (300, 0): 5, (300, 1): 6, (300, 3): 6,
            (343, 3): 3,
            (422, 1): 13,
            (450, 1): 3, (450, 2): 13,
            (472, 0): 6, (472, 2): 2,
            (494, 0): 15, (494, 2): 5,
            (496, 0): 3, (496, 1): 3,

            # Human review identifies slot 83 as the missing rotated
            # Common/Dark checker: Common at NW/SE and Dark at NE/SW. The
            # catalog already contains the complementary diagonal at slots
            # 17/18, so this completes a real connected topology rather than
            # introducing an isolated label.
            (83, 0): 4, (83, 1): 1, (83, 2): 4, (83, 3): 1,

            # The high-resolution unresolved-corner follow-up names slot 414
            # as Gravel/Sparse-light/Sparse-light/Grayish around NW/NE/SE/SW.
            # Slot 390 remains untouched: its brownish quadrants still need a
            # titled comparison against Trampled and other ground families.
            (414, 0): 8, (414, 1): 2,
            (414, 2): 2, (414, 3): 3,

            # Slot 45 is not uniform Mountainous ground: its NW quadrant is
            # Rock / Dark in both atlas variants and at 70 authored vertices,
            # while the other three corners remain Mountainous.
            (45, 0): 6,

            # Slot 222 is not uniform sparse-dark grass. Its brown NW corner
            # is Rock / Dark in both variants and at every supported authored
            # vertex; the other three corners remain Sparse dark.
            (222, 0): 6,
        }

        questionnaire, aliases, renames = (
            reviewed_unresolved_questionnaire_corrections(recipe))
        corrections.update(questionnaire)
        family_aliases.update(aliases)
        family_renames.update(renames)

    if namespace in {"bz14h", "bz18h", "zone16"}:
        # These recipe fragments are sides of the established beige/red stone
        # transition or tan ground, not independent paint materials.
        corrections = {
            (21, 0): 1, (21, 1): 1, (31, 0): 1,
            (30, 0): 1,
            (179, 0): 4, (187, 1): 4,

            # The nominally uniform Rubble / Red group contains repeated
            # rotated stamps.  Complete tiles visibly expose the non-red
            # quadrants, while the authored Zone 16 map independently votes
            # for Ground / Tan (4) or Stone / Red (2) at every listed corner.
            (118, 0): 4, (119, 0): 4, (125, 0): 4,
            (160, 3): 2,
            (238, 2): 4, (238, 3): 2,
            (255, 1): 2, (255, 3): 2,

            # Slot 186 is Stone / Red on three corners with Ground / Tan at
            # NE. The complete tile and independent HD/map evidence agree.
            (186, 1): 4,
        }
        removed_ids = {6, 7, 8}

    if namespace in {"bz15h", "zone17"}:
        # Slot 134 is the only occurrence of the recipe's Ground / Tan
        # fragment.  Multi-scale vertex-local pixel comparison and visual
        # review both identify it as Ground / Red.  Folding it into that
        # established family leaves the authored 1x Ground / Red + 3x Rock /
        # Orange junction available as a paintable stamp/support pair.
        corrections = {
            (134, 0): 3,

            # Slot 130 is visibly red ground on its north edge rather than a
            # uniform Rock / Orange tile. Its NE patch ranks the clean Ground /
            # Red slot 32 anchor first (distance 0.044614; anchor risk
            # 0.137994). Other corners remain unchanged pending re-audit.
            (130, 1): 3,

            # Two complementary repeated stamp rows were mislabeled as
            # uniform anchors.  Slots 20/22/24/28/29 place Ground / Red at NW
            # over Rubble / Orange; slots 30/31/32/33/34/38/39 place Rubble /
            # Orange at SW under Ground / Red.  Each corrected corner has
            # 33-65 authored-map consensus votes and at most one disagreement,
            # and the full-tile contact sheets show the same rotated motifs.
            (20, 0): 3, (22, 0): 3, (24, 0): 3,
            (28, 0): 3, (29, 0): 3,
            (30, 3): 1, (31, 3): 1, (32, 3): 1,
            (33, 3): 1, (34, 3): 1, (38, 3): 1, (39, 3): 1,
        }
        removed_ids = {6}

    # These archives contain byte-identical complete material tiles. The Zone
    # 10 grouped review already selected the user-facing Irregular name, so use
    # it consistently in Zone 5_1 as well; this is a display-name correction,
    # not a cross-namespace ID merge.
    if namespace == "zone5_1":
        family_renames = {
            4: "Grass / Irregular",
            5: "Stone / Underwater",
        }

    # R59 global-confidence review. Family-wide selections are aliases;
    # "Only this corner" and explicitly named additional corners are exact
    # edits. Notes containing uncertainty (might/probably/needs review) are
    # deliberately excluded and remain questionnaire work.
    r59_aliases = {
        "bz11k": {18: 16},
        # Zone14 is the canonical decoded identity for the content-identical
        # bz11k atlas. The rule must live on both inputs because canonical
        # substitution occurs after namespace-local review.
        "zone14": {18: 16},
        "bz13h": {12: 1, 13: 2},
        # Zone15 likewise owns the decoded identity shared by bz13h.
        "zone15": {12: 1, 13: 2},
        "zone1": {11: 8, 12: 7},
        "zone10": {18: 6, 20: 6, 21: 9},
    }
    r59_corrections = {
        "bz11k": {
            (235, 3): 16,
            (300, 3): 19,
        },
        "zone14": {
            (235, 3): 16,
            (300, 3): 19,
        },
        "bz13h": {
            (13, 0): 8,
        },
        "zone15": {
            (13, 0): 8,
        },
        "bz16h": {
            (95, 0): 6,
            (449, 0): 13,
        },
        "bz3g": {
            (265, 3): 10,
            (383, 0): 5,
        },
        "bz4g": {
            (174, 2): 3,
            (280, 3): 5,
            (294, 0): 3,
            (370, 0): 9, (370, 1): 9,
            (370, 2): 1, (370, 3): 3,
            (437, 3): 5,
        },
        "bz5g": {
            (310, 2): 8,
            (317, 3): 4,
        },
        # Zone8 is the canonical owner for the identical bz5g/bz6g pages.
        "zone8": {
            (310, 2): 8,
            (317, 3): 4,
        },
        "bz7g": {
            (162, 2): 1,
        },
        "bz8k": {
            (323, 2): 2, (323, 3): 2,
        },
        # bz9k is the canonical owner for identical bz8k/Zone11 pages.
        "bz9k": {
            (323, 2): 2, (323, 3): 2,
        },
        "zone1": {
            (338, 3): 7,
        },
        "zone10": {
            (287, 0): 3, (287, 3): 6,
            (323, 3): 6,
            # Exact decoded SW pixels shared with reviewed slot 323. These
            # copies are structural identity, not nearest-colour inference.
            (354, 3): 6, (416, 3): 6, (479, 3): 6,
            (465, 2): 2,
            (471, 3): 6,
            (486, 3): 6,
        },
        "zone13": {
            (178, 1): 6, (178, 2): 6, (178, 3): 6,
            (283, 3): 7,
            (450, 3): 7,
            (477, 0): 6, (477, 2): 6,
            (497, 3): 3,
        },
        "zone20": {
            (189, 0): 2,
        },
        "zone6": {
            (278, 0): 8,
            (471, 0): 3,
        },
        "zone6_2": {
            (278, 0): 8,
            (471, 0): 3,
        },
        "zone7": {
            (333, 3): 1,
        },
    }
    corrections.update(r59_corrections.get(namespace, {}))
    selected_aliases = r59_aliases.get(namespace, {})
    family_aliases.update(selected_aliases)
    removed_ids.update(selected_aliases)

    # R62 focused review. Apply only direct selections. Notes asking for more
    # visual evidence remain questionnaire work: the tentative Zone10
    # Ground / Mountainous merge, bz8k Sand / Dark rename, Zone13 ground
    # comparison, and cross-atlas water/sand/rock comparisons are not inferred.
    r62_aliases = {
        "bz4g": {25: 8},
        # Zone4 owns the decoded identity shared by bz4g.
        "zone4": {25: 8},
        "zone10": {10: 5},
        "zone3obr": {10: 4},
    }
    r62_corrections = {
        "bz11k": {
            (235, 2): 16,
        },
        # Zone14 owns the decoded identity shared by bz11k.
        "zone14": {
            (235, 2): 16,
        },
        "bz8k": {
            (428, 3): 7,
            (444, 3): 7,
        },
        # bz9k owns the decoded identity shared by bz8k and Zone11.
        "bz9k": {
            (428, 3): 7,
            (444, 3): 7,
        },
        "zone20": {
            # The reviewer identified both highlighted greenish corners. Slot
            # 38 NW independently ranks Stone / Greenish first; its other
            # three corners remain Stone / Black.
            (38, 0): 2,
            (190, 0): 2,
        },
    }
    corrections.update(r62_corrections.get(namespace, {}))
    selected_aliases = r62_aliases.get(namespace, {})
    family_aliases.update(selected_aliases)
    removed_ids.update(selected_aliases)

    # R63 focused review. Cross-atlas statements establish semantic naming
    # evidence but do not merge namespace-local family IDs. Unanswered water,
    # sand, and Zone10 slab comparisons remain explicit follow-up work.
    r63_aliases = {
        "zone13": {14: 3},
    }
    r63_corrections = {
        "bz11k": {
            (33, 0): 1,
            (33, 1): 5,
            (33, 2): 5,
            (33, 3): 5,
            (55, 3): 5,
            (58, 3): 5,
        },
        # Zone14 owns the decoded identity shared by bz11k.
        "zone14": {
            (33, 0): 1,
            (33, 1): 5,
            (33, 2): 5,
            (33, 3): 5,
            (55, 3): 5,
            (58, 3): 5,
        },
        "zone10": {
            (291, 1): 5,
        },
        "zone13": {
            # Apply this exact exception before merging the remaining former
            # Ground / Dry references into Ground / Light.
            (325, 0): 9,
        },
    }
    r63_renames = {
        "bz8k": {7: "Grass / Yellow lichen"},
        # bz9k owns the decoded identity shared by bz8k and Zone11.
        "bz9k": {7: "Grass / Yellow lichen"},
    }
    corrections.update(r63_corrections.get(namespace, {}))
    selected_aliases = r63_aliases.get(namespace, {})
    family_aliases.update(selected_aliases)
    removed_ids.update(selected_aliases)
    family_renames.update(r63_renames.get(namespace, {}))

    # R64 focused review. Apply only exact corner selections. The review's
    # "420W" shorthand does not identify one of the four stored corners, and
    # slot 445 was described as NW even though its unresolved family is NE;
    # both remain unchanged for a marked-corner follow-up. Slot 452 was
    # explicitly excluded from the Ground / Dirt selections.
    r64_corrections = {
        "zone10": {
            (213, 3): 6,
            (292, 1): 5,
            (293, 1): 5,
            (306, 0): 5,
            (313, 0): 5,
            (314, 0): 5,
            (322, 1): 5,
            (375, 1): 5,
            (419, 0): 5,
            (486, 2): 1,
        },
    }
    r64_renames = {
        "bz5g": {14: "Sand / Grainy"},
        "bz6g": {14: "Sand / Grainy"},
        # Zone8 owns the decoded identity shared by bz5g and bz6g. Its family
        # 11 is the same slot-98 material under the canonical table.
        "zone8": {11: "Sand / Grainy"},
    }
    corrections.update(r64_corrections.get(namespace, {}))
    family_renames.update(r64_renames.get(namespace, {}))

    # R65 focused review classified every corner remaining in the local
    # Rock / Dark bucket as Stone / Black. Zone14 owns the byte-identical
    # decoded atlas used by bz11k, so preserve the decision on both namespace
    # inputs. The separately questioned slots 36 and 61 belong to other family
    # IDs and are intentionally unaffected by this alias.
    r65_aliases = {
        # BaseGipat's seven transition-only "Still" corners match Zone1's
        # single water family and use the same Swamp boundary topology as the
        # local Waves variations. They are authored variations of one water
        # material, not a third overlay material.
        "basegipat": {10: 5},
        "bz11k": {5: 8},
        "zone14": {5: 8},
    }
    r65_renames = {
        "basegipat": {5: "Water / Still"},
    }
    selected_aliases = r65_aliases.get(namespace, {})
    family_aliases.update(selected_aliases)
    removed_ids.update(selected_aliases)
    family_renames.update(r65_renames.get(namespace, {}))

    # R66 exact-corner review. These replace two false-uniform bz11k/Zone14
    # tiles and the remaining mixed Zone10 bucket members. Names in the review
    # are mapped only to existing, independently reviewed local identities;
    # adjacent or same-named corners are not inferred.
    r66_corrections = {
        "bz11k": {
            (36, 0): 1, (36, 1): 8, (36, 2): 8, (36, 3): 8,
            (61, 0): 8, (61, 1): 1, (61, 2): 8, (61, 3): 16,
        },
        "zone14": {
            (36, 0): 1, (36, 1): 8, (36, 2): 8, (36, 3): 8,
            (61, 0): 8, (61, 1): 1, (61, 2): 8, (61, 3): 16,
        },
        "zone10": {
            (293, 0): 1,
            (306, 1): 1,
            (419, 2): 1,
            (420, 0): 5, (420, 2): 1,
            (445, 1): 5,
            (452, 1): 3, (452, 2): 1,
            (214, 0): 1, (214, 1): 1, (214, 2): 1, (214, 3): 6,
            (375, 0): 2, (375, 1): 5, (375, 2): 2, (375, 3): 3,
        },
    }
    corrections.update(r66_corrections.get(namespace, {}))

    # R67 cross-atlas identity review. These families have byte-identical
    # uniform authored tiles across the named atlases. Unify only their
    # user-facing names; per-map allowed layers and underwater availability
    # remain owned by each existing family record.
    r67_renames = {
        "bz3g": {6: "Ground / Dark"},
        "zone6": {5: "Ground / Dark"},
        "zone6_2": {5: "Ground / Dark"},
        "bz5g": {
            1: "Rock / Underwater emerald",
            2: "Sand / Underwater",
            14: "Sand / Grainy",
        },
        "bz6g": {
            1: "Rock / Underwater emerald",
            2: "Sand / Underwater",
            14: "Sand / Grainy",
        },
        "zone8": {
            1: "Rock / Underwater emerald",
            2: "Sand / Underwater",
            11: "Sand / Grainy",
        },
        "zone7": {
            1: "Rock / Underwater emerald",
            4: "Sand / Underwater",
            15: "Sand / Grainy",
        },
    }
    family_renames.update(r67_renames.get(namespace, {}))

    # R69 marked-tile review. These are the reviewer's explicit four-corner
    # classifications from the real-map stripe/circle collections. Keep-current
    # answers require no rewrite; notes requesting side-by-side checks remain
    # outside this table. Byte-identical atlas owners receive the same exact
    # corrections so canonical substitution cannot discard a reviewed result.
    r69_corrections = {
        "bz10k": {
            (316, 0): 5, (316, 2): 9, (316, 3): 10,
        },
        "zone12": {
            (316, 0): 5, (316, 2): 9, (316, 3): 10,
        },
        "bz11k": {
            (246, 1): 8, (246, 2): 1, (246, 3): 8,
            (287, 0): 1, (287, 1): 8,
            (309, 0): 8, (309, 1): 1,
            (310, 0): 8, (310, 1): 1,
            (311, 0): 8, (311, 1): 1,
            (346, 0): 8, (346, 1): 8, (346, 3): 1,
            (450, 0): 1, (450, 2): 8, (450, 3): 8,
            (477, 0): 6, (477, 1): 6, (477, 2): 10, (477, 3): 8,
            (509, 0): 16, (509, 1): 1, (509, 2): 8, (509, 3): 16,
        },
        "zone14": {
            (246, 1): 8, (246, 2): 1, (246, 3): 8,
            (287, 0): 1, (287, 1): 8,
            (309, 0): 8, (309, 1): 1,
            (310, 0): 8, (310, 1): 1,
            (311, 0): 8, (311, 1): 1,
            (346, 0): 8, (346, 1): 8, (346, 3): 1,
            (450, 0): 1, (450, 2): 8, (450, 3): 8,
            (477, 0): 6, (477, 1): 6, (477, 2): 10, (477, 3): 8,
            (509, 0): 16, (509, 1): 1, (509, 2): 8, (509, 3): 16,
        },
        "bz13h": {
            (227, 3): 3,
            (248, 0): 4, (248, 1): 1, (248, 2): 4, (248, 3): 2,
            (288, 0): 4, (288, 1): 4,
            (341, 3): 7, (343, 3): 7,
            (369, 2): 3, (370, 2): 3, (410, 2): 3,
            (449, 3): 7, (495, 0): 3, (495, 3): 6,
        },
        "zone15": {
            (227, 3): 3,
            (248, 0): 4, (248, 1): 1, (248, 2): 4, (248, 3): 2,
            (288, 0): 4, (288, 1): 4,
            (341, 3): 7, (343, 3): 7,
            (369, 2): 3, (370, 2): 3, (410, 2): 3,
            (449, 3): 7, (495, 0): 3, (495, 3): 6,
        },
        "bz16h": {(420, 3): 2, (470, 0): 8},
        "zone18": {(420, 3): 2, (445, 2): 1, (470, 0): 8},
        "bz2g": {
            (38, 0): 2, (93, 0): 2,
            (98, 0): 2, (98, 2): 1, (98, 3): 3,
            (120, 2): 4, (120, 3): 4,
            (145, 1): 1, (146, 0): 1,
            (244, 3): 3, (306, 3): "Grass / Burnt", (441, 3): 3,
        },
        "zone2": {
            (38, 0): 2, (93, 0): 2,
            (98, 0): 2, (98, 2): 1, (98, 3): 3,
            (120, 2): 4, (120, 3): 4,
            (145, 1): 1, (146, 0): 1,
            (244, 3): 3, (306, 3): "Grass / Burnt", (441, 3): 3,
        },
        "bz3g": {
            (172, 1): 1, (172, 3): 14,
            (192, 2): 12, (220, 0): 12, (244, 1): 12,
            (267, 0): 12, (271, 3): 17,
            (283, 0): 2, (283, 3): 14,
            (307, 0): 5, (347, 3): 17, (504, 1): 11,
        },
        "bz4g": {(158, 3): 17, (192, 3): 17},
        "zone4": {(158, 3): 17, (192, 3): 17},
        "bz5g": {
            (318, 0): 6, (318, 1): 2, (318, 2): 5, (318, 3): 5,
            (355, 2): 7, (358, 0): 7,
            (462, 3): "Sand / Grainy", (494, 3): "Sand / Grainy",
        },
        "zone8": {
            (318, 0): 6, (318, 1): 2, (318, 2): 5, (318, 3): 5,
            (355, 2): 7, (358, 0): 7,
            (462, 3): "Sand / Grainy", (494, 3): "Sand / Grainy",
        },
        "bz7g": {
            (125, 2): 4, (133, 3): 2, (134, 0): 4,
            (198, 1): 4, (199, 1): 4, (229, 2): 6,
        },
        "bz8k": {(323, 1): 6},
        "bz9k": {(323, 1): 6},
        "zone11": {(323, 1): 6},
        "zone1": {(377, 3): 1},
        "zone10": {
            (87, 3): 3, (88, 3): 3, (89, 3): 3,
            (99, 3): 3, (100, 3): 3, (102, 3): 3,
            (224, 1): 3, (451, 0): 5,
        },
        "zone13": {
            (364, 3): 4, (369, 3): 4,
            (439, 0): 3, (440, 0): 3, (505, 1): 4,
        },
        "zone19": {
            (310, 3): 4, (353, 3): 5, (354, 3): 5,
            (472, 3): 5, (494, 0): 1,
        },
        "zone3obr": {
            (38, 3): 13, (39, 3): 13, (218, 3): 13,
            (347, 2): 4,
        },
        "zone5_1": {(167, 2): 2},
        "zone6": {
            (265, 3): 15, (268, 3): 15, (291, 3): 15,
            (314, 2): 15, (314, 3): 15, (317, 3): 15,
            (336, 1): 4, (336, 3): 15,
        },
        "zone6_2": {
            (266, 3): 18, (269, 2): 8,
            (314, 2): 18, (314, 3): 18,
        },
        "zone7": {(43, 1): 3},
    }
    corrections.update(r69_corrections.get(namespace, {}))

    # R70 follow-up review.  Only explicit corner identities are recorded
    # here.  The reviewer's deliberately uncertain ground/soil corners and the
    # contradictory bz11k slot 480 SE answer remain unchanged for the next
    # visual questionnaire.  Byte-identical atlas owners receive the same
    # corrections; visually related but byte-distinct atlases do not.
    r70_corrections = {
        "bz11k": {
            (246, 0): 16, (346, 2): 1, (450, 1): 6,
            (281, 0): 8,
            (377, 0): 1, (377, 1): 16, (377, 2): 16, (377, 3): 16,
            (480, 0): 16, (480, 1): 1, (480, 3): 10,
            (491, 2): 1, (491, 3): 10,
        },
        "zone14": {
            (246, 0): 16, (346, 2): 1, (450, 1): 6,
            (281, 0): 8,
            (377, 0): 1, (377, 1): 16, (377, 2): 16, (377, 3): 16,
            (480, 0): 16, (480, 1): 1, (480, 3): 10,
            (491, 2): 1, (491, 3): 10,
        },
        "bz13h": {
            (12, 0): 4, (12, 1): 2, (12, 2): 2, (12, 3): 2,
            (408, 0): 3, (408, 1): 11, (408, 2): 3, (408, 3): 11,
        },
        "zone15": {
            (12, 0): 4, (12, 1): 2, (12, 2): 2, (12, 3): 2,
            (408, 0): 3, (408, 1): 11, (408, 2): 3, (408, 3): 11,
        },
        "bz4g": {
            (204, 0): 18, (204, 1): 18, (204, 2): 18, (204, 3): 18,
        },
        "zone4": {
            (204, 0): 18, (204, 1): 18, (204, 2): 18, (204, 3): 18,
        },
        "bz16h": {
            (480, 1): 8, (480, 2): 8,
        },
        "bz5g": {
            (212, 3): 2, (229, 0): 1,
        },
        "zone8": {
            (212, 3): 2, (229, 0): 1,
        },
        "zone10": {(439, 0): 1},
        "zone18": {
            (480, 1): 8, (480, 2): 8,
        },
        "zone19": {
            (46, 0): 1, (46, 1): 1, (46, 2): 5, (46, 3): 5,
            (59, 0): 1, (59, 1): 1, (59, 2): 1, (59, 3): 5,
        },
    }
    corrections.update(r70_corrections.get(namespace, {}))

    # The R70 reviewer identified this Zone18 visual identity as the existing
    # Paving / Sand ruins material.  Other similarly named atlases are
    # byte-distinct and remain unchanged pending direct comparison.
    if namespace == "zone18":
        family_renames[7] = "Paving / Sand ruins"

    # R71 anchor review.  Mixed/false-uniform families are not globally
    # propagated from a single corner choice: only the explicitly named
    # quarters and byte-identical atlas owners are changed here.  The next
    # questionnaire expands the remaining Stone / Grayish ownership and the
    # purported Ground / Mountainous and Rock / Black uniform anchors.
    r71_corrections = {
        "bz11k": {
            (182, 3): 16,
            (431, 0): 1,
            (470, 0): 1,
            (480, 2): 8,
        },
        "zone14": {
            (182, 3): 16,
            (431, 0): 1,
            (470, 0): 1,
            (480, 2): 8,
        },
        "bz4g": {
            (169, 3): 18, (185, 3): 18, (195, 3): 18,
        },
        "zone4": {
            (169, 3): 18, (185, 3): 18, (195, 3): 18,
        },
        "bz16h": {(480, 3): 2},
        "zone18": {(480, 0): 4, (480, 3): 2},
    }
    corrections.update(r71_corrections.get(namespace, {}))

    r71_renames = {
        "bz11k": {10: "Rock / Black"},
        "zone14": {10: "Rock / Black"},
        "bz13h": {7: "Sand / Yellow"},
        "zone15": {7: "Sand / Yellow"},
        "zone19": {5: "Sand / Yellow"},
    }
    family_renames.update(r71_renames.get(namespace, {}))

    if not corrections and not family_renames and not family_aliases:
        return recipe
    result = copy.deepcopy(recipe)
    by_slot = {slot["slot"]: slot for slot in result["slots"]}
    for (slot_index, corner_index), family_id in corrections.items():
        # Focused unit fixtures may intentionally model only the first part of
        # an atlas. Real compiled recipes still contain every authored slot.
        if slot_index not in by_slot:
            continue
        if isinstance(family_id, str):
            matches = [
                family["id"] for family in result["families"]
                if family_renames.get(
                    family["id"], family.get("suggestedName", "")
                ).casefold()
                == family_id.casefold()
            ]
            if len(matches) != 1 and "slotCount" in result:
                raise ValueError(
                    f"{result['namespace']}: reviewed family name "
                    f"{family_id!r} resolved to {matches}")
            if len(matches) != 1:
                continue
            family_id = matches[0]
        by_slot[slot_index]["signature"][corner_names[corner_index]] = family_id
    if family_aliases:
        for slot in result["slots"]:
            for corner_name, family_id in slot["signature"].items():
                slot["signature"][corner_name] = family_aliases.get(
                    family_id, family_id)
    if namespace == "bz4g" and not any(
            family["id"] == 25 for family in result["families"]):
        sand_light = copy.deepcopy(next(
            family for family in result["families"] if family["id"] == 8))
        sand_light["id"] = 25
        sand_light["sourceFamily"] = "reviewed/sand/light"
        sand_light["suggestedName"] = "Sand / Light"
        sand_light["thumbnailSlot"] = 492
        sand_light["uniformSlots"] = []
        sand_light["memberSlots"] = [492, 496]
        result["families"].append(sand_light)
        result["families"].sort(key=lambda family: family["id"])
    if namespace == "zone3obr" and not any(
            family["id"] == 13 for family in result["families"]):
        # R69 identified the SW quarters of slots 38/39/218 as a distinct
        # greenish stone. It has no honest four-corner tile in this atlas, so
        # keep it as a transition-only contextual family rather than borrowing
        # a misleading uniform thumbnail from Stone / Lime.
        stone_greenish = copy.deepcopy(next(
            family for family in result["families"] if family["id"] == 1))
        stone_greenish["id"] = 13
        stone_greenish["sourceFamily"] = "reviewed/stone/greenish"
        stone_greenish["suggestedName"] = "Stone / Greenish"
        stone_greenish["thumbnailSlot"] = 38
        stone_greenish["uniformSlots"] = []
        stone_greenish["memberSlots"] = [38, 39, 218]
        result["families"].append(stone_greenish)
        result["families"].sort(key=lambda family: family["id"])
    referenced = {
        family_id
        for slot in result["slots"]
        for family_id in slot["signature"].values()
    }
    result["families"] = [
        family for family in result["families"]
        if family["id"] in referenced and family["id"] not in removed_ids
    ]
    for family in result["families"]:
        if family["id"] in family_renames:
            family["suggestedName"] = family_renames[family["id"]]
    if namespace == "zone2":
        irregular = next(family for family in result["families"]
                         if family["id"] == 9)
        irregular["thumbnailSlot"] = 117
        burnt = next(family for family in result["families"]
                     if family["id"] == 14)
        # The in-game checkerboard established slot 195 as the first truthful
        # four-corner Burnt preview; the old slot 122 was a mixed transition.
        burnt["thumbnailSlot"] = 195
    return result


def exact_pixel_identity_evidence(recipe: dict,
                                  tiles: list[np.ndarray]
                                  ) -> tuple[dict, int, int, list[dict]]:
    """Report corners that exactly equal one established uniform family.

    Exact local pixels are useful corroborating evidence, but they do not own
    semantic identity.  A masked transition corner can share its vertex-local
    bytes with a uniform tile while retaining a different authored source
    family.  Consequently this function never rewrites ``recipe``.  It reports
    unique candidates and ambiguities for audit only; explicit reviewed
    corrections remain the only post-compile identity overrides.
    """
    corner_names = ("northWest", "northEast", "southEast", "southWest")

    def patch_digest(tile: np.ndarray, corner: int) -> bytes:
        height, width = tile.shape[:2]
        size = max(8, min(height, width) // 8)
        if corner == 0:
            patch = tile[:size, :size, :3]
        elif corner == 1:
            patch = tile[:size, width - size:, :3]
        elif corner == 2:
            patch = tile[height - size:, width - size:, :3]
        else:
            patch = tile[height - size:, :size, :3]
        return hashlib.sha256(patch.tobytes(order="C")).digest()

    anchors: dict[bytes, set[int]] = defaultdict(set)
    for slot in recipe["slots"]:
        values = list(slot["signature"].values())
        if len(set(values)) != 1:
            continue
        for corner in range(4):
            anchors[patch_digest(tiles[slot["slot"]], corner)].add(values[0])

    result = copy.deepcopy(recipe)
    suggestions = ambiguous = 0
    decisions: list[dict] = []
    for slot in recipe["slots"]:
        for corner, name in enumerate(corner_names):
            current = slot["signature"][name]
            candidates = anchors.get(
                patch_digest(tiles[slot["slot"]], corner), set())
            if current in candidates or not candidates:
                continue
            if len(candidates) != 1:
                ambiguous += 1
                decisions.append({
                    "kind": "ambiguous",
                    "namespace": recipe["namespace"],
                    "slot": slot["slot"],
                    "corner": name,
                    "currentFamilyId": current,
                    "candidateFamilyIds": sorted(candidates),
                })
                continue
            replacement = next(iter(candidates))
            decisions.append({
                "kind": "suggestion",
                "namespace": recipe["namespace"],
                "slot": slot["slot"],
                "corner": name,
                "currentFamilyId": current,
                "candidateFamilyIds": [replacement],
            })
            suggestions += 1

    return result, suggestions, ambiguous, decisions


def finalize_identity_decisions(decisions: list[dict],
                                recipes: list[dict]) -> None:
    """Attach the effective post-canonical family to every evidence record.

    Evidence is collected independently for each authored namespace before
    byte-identical atlases adopt their reviewed canonical identity. Reporting
    the pre-canonical family as final makes already-resolved alias corners look
    unresolved. Resolve the log only after canonical propagation, against the
    same recipes that are serialized into the built-in preset catalog.
    """
    by_namespace = {
        recipe["namespace"].lower(): {
            slot["slot"]: slot for slot in recipe["slots"]
        }
        for recipe in recipes
    }
    for decision in decisions:
        slots = by_namespace[decision["namespace"].lower()]
        final_family = slots[decision["slot"]]["signature"][
            decision["corner"]]
        decision["finalFamilyId"] = final_family
        decision["appliedToReviewedRecipe"] = (
            final_family != decision["currentFamilyId"])


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


# These namespaces decode to byte-for-byte identical atlas pixels.  Their
# independently authored EI-HD recipes disagree about corner identity, which
# is impossible for an atlas-driven paint resolver: the same tile cannot mean
# two different things merely because another map references it.  The choices
# below are backed by decoded-pixel review rather than namespace precedence.
# Zone 2 is the explicitly corrected table; bz4g correctly labels the pale
# halves of slots 114/115; bz9k is the reviewed Zone 11 snow table; and Zone 8
# removes visibly heterogeneous fragment families from the shared bz5g/bz6g
# atlas.
CANONICAL_IDENTITY_NAMESPACE = {
    "bz2g": "zone2",
    "zone2": "zone2",
    "bz4g": "bz4g",
    "zone4": "bz4g",
    "bz5g": "zone8",
    "bz6g": "zone8",
    "zone8": "zone8",
    "bz8k": "bz9k",
    "bz9k": "bz9k",
    "zone11": "bz9k",
    "bz3g": "bz3g",
    "zone9": "bz3g",
    "bz10k": "zone12",
    "zone12": "zone12",
    "bz11k": "zone14",
    "zone14": "zone14",
    "bz13h": "zone15",
    "zone15": "zone15",
    "bz14h": "zone16",
    "bz18h": "zone16",
    "zone16": "zone16",
    "bz15h": "zone17",
    "zone17": "zone17",
}


def apply_canonical_identity(target: dict, canonical: dict) -> dict:
    """Use canonical pixel identity while retaining target-map usage evidence."""
    if target["slotCount"] != canonical["slotCount"]:
        raise ValueError("canonical atlas identity has a different slot count")
    result = copy.deepcopy(target)
    canonical_slots = {slot["slot"]: slot for slot in canonical["slots"]}
    for slot in result["slots"]:
        identity = canonical_slots[slot["slot"]]
        slot["signature"] = copy.deepcopy(identity["signature"])
        slot["sourceFamilies"] = copy.deepcopy(identity.get("sourceFamilies", {}))

    families = []
    for source in canonical["families"]:
        family = copy.deepcopy(source)
        family_id = family["id"]
        member_slots = []
        uniform_slots = []
        layer_names = set()
        tile_types = Counter()
        land = below = 0
        for slot in result["slots"]:
            corner_ids = list(slot["signature"].values())
            if family_id not in corner_ids:
                continue
            member_slots.append(slot["slot"])
            if len(set(corner_ids)) == 1:
                uniform_slots.append(slot["slot"])
            layer_names.update(slot["allowedLayers"])
            tile_types[slot["tileType"]] += 1
            land += slot["observedUsage"]["land"]
            below += slot["observedUsage"]["belowOverlay"]
        if not member_slots:
            raise ValueError(
                f"canonical family {family_id} has no members in {target['namespace']}")
        family["memberSlots"] = member_slots
        family["uniformSlots"] = uniform_slots
        family["allowedLayers"] = sorted(layer_names)
        family["tileType"] = tile_types.most_common(1)[0][0]
        family["underwater"] = bool(
            "land" in layer_names and land > 0 and below == land)
        families.append(family)
    result["families"] = families
    return result


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


def resolved_thumbnail_slot(recipe: dict, family: dict) -> int:
    """Preserve a valid authored choice, otherwise choose a truthful fallback.

    Reviewed/manual thumbnails remain stable as long as the chosen slot still
    contains the family after canonicalization and corner corrections. A stale
    thumbnail must never display an unrelated tile. Prefer the lowest uniform
    tile, then a one-on-three stamp, then the lowest remaining occurrence.
    """
    family_id = family["id"]
    by_slot = {slot["slot"]: list(slot["signature"].values())
               for slot in recipe["slots"]}
    selected = family.get("thumbnailSlot", -1)
    if family_id in by_slot.get(selected, []):
        return selected
    occurrences = [(slot, corners) for slot, corners in by_slot.items()
                   if family_id in corners]
    if not occurrences:
        raise ValueError(
            f"family {family_id} has no thumbnail candidate in {recipe['namespace']}")
    uniform = [slot for slot, corners in occurrences
               if corners.count(family_id) == 4]
    if uniform:
        return min(uniform)
    stamps = [slot for slot, corners in occurrences
              if corners.count(family_id) == 1
              and len(set(corner for corner in corners
                          if corner != family_id)) == 1]
    if stamps:
        return min(stamps)
    return min(slot for slot, _ in occurrences)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compiled", type=Path, required=True)
    parser.add_argument("--hd", type=Path, required=True)
    parser.add_argument("--original", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--identity-log", type=Path,
                        help="write every exact correction and ambiguity")
    args = parser.parse_args()
    compiled = json.loads(args.compiled.read_text(encoding="utf-8"))
    hd_index, original_index = res_index(args.hd), res_index(args.original)
    recipes = []
    exact_suggestions = 0
    exact_ambiguities = 0
    identity_decisions: list[dict] = []
    for recipe in compiled["recipes"]:
        namespace = recipe["namespace"]
        atlas_parts = recipe["slotCount"] // 64
        hd_tiles = editor_tiles(args.hd, hd_index, namespace, atlas_parts)
        recipe = reviewed_zone2_stone_families(recipe, hd_tiles)
        recipe = reviewed_fragment_corrections(recipe)
        recipe, suggested, ambiguous, decisions = exact_pixel_identity_evidence(
            recipe, hd_tiles)
        # Hand-reviewed namespace corrections are authoritative where a
        # replacement atlas intentionally differs from the original pixels.
        # Reapply them after evidence collection to keep the ownership order
        # explicit if the audit implementation changes later.
        recipe = reviewed_fragment_corrections(recipe)
        exact_suggestions += suggested
        exact_ambiguities += ambiguous
        identity_decisions.extend(decisions)
        recipes.append(recipe)

    by_namespace = {recipe["namespace"].lower(): recipe for recipe in recipes}
    recipes = [apply_canonical_identity(
        recipe,
        by_namespace[CANONICAL_IDENTITY_NAMESPACE.get(
            recipe["namespace"].lower(), recipe["namespace"].lower())])
        for recipe in recipes]
    finalize_identity_decisions(identity_decisions, recipes)

    presets = []
    for recipe in recipes:
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
                "thumbnailSlot": resolved_thumbnail_slot(recipe, family),
                "paintSelectable": editor_material_is_paintable(
                    recipe, family["id"]),
            })
        presets.append({
            "namespace": namespace, "slotCount": recipe["slotCount"],
            "bindings": bindings, "families": families,
            "signaturesBase64": base64.b64encode(signatures).decode("ascii"),
        })

    identities_by_binding = {}
    for preset in presets:
        family_identity = tuple(
            (family["id"], family["name"], family["thumbnailSlot"],
             family["paintSelectable"])
            for family in preset["families"])
        identity = (preset["signaturesBase64"], family_identity)
        for binding in preset["bindings"]:
            key = (binding["variant"], binding["pixelSha256"],
                   preset["slotCount"])
            previous = identities_by_binding.setdefault(key, identity)
            if previous != identity:
                raise ValueError(
                    "identical atlas binding has conflicting material identity: "
                    f"{preset['namespace']} {binding['variant']} "
                    f"{binding['pixelSha256']}")
    result = {
        "schemaVersion": 1,
        "provenance": "built-in-material-atlas-v2",
        "presets": presets,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":"), ensure_ascii=False) + "\n",
                           encoding="utf-8")
    if args.identity_log:
        args.identity_log.parent.mkdir(parents=True, exist_ok=True)
        args.identity_log.write_text(json.dumps({
            "schema": "ei-semantic-atlas-exact-identity-log-v2",
            "suggestions": [item for item in identity_decisions
                            if item["kind"] == "suggestion"],
            "ambiguities": [item for item in identity_decisions
                            if item["kind"] == "ambiguous"],
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"presets": len(presets),
                      "bindings": sum(len(item["bindings"]) for item in presets),
                      "exactPixelAmbiguities": exact_ambiguities,
                      "exactPixelSuggestions": exact_suggestions,
                      "bytes": args.output.stat().st_size}, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
