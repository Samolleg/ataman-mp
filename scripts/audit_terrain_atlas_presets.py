#!/usr/bin/env python3
"""Audit semantic-atlas family identity against decoded atlas pixels.

This is a review tool, not an automatic preset rewriter. It treats uniform
tiles in each atlas as visual anchors, measures every assigned corner against
those anchors, and emits explicit suspect-family evidence and contact sheets.
"""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import json
import shutil
from pathlib import Path
from collections import defaultdict

import numpy as np
from PIL import Image, ImageDraw, ImageFont

from generate_terrain_atlas_presets import editor_tiles, res_index


CORNER_NAMES = ("NW", "NE", "SE", "SW")


def corner_patch(image: np.ndarray, corner: int) -> np.ndarray:
    height, width = image.shape[:2]
    # A semantic corner describes the material at the tile vertex. Sampling a
    # third of the tile also captures the transition body and can make a valid
    # corner family appear to contain its neighbours. Keep this probe local to
    # the vertex while retaining enough texels to compare hue and texture.
    size = max(8, min(height, width) // 8)
    if corner == 0:
        return image[:size, :size, :3]
    if corner == 1:
        return image[:size, width - size:, :3]
    if corner == 2:
        return image[height - size:, width - size:, :3]
    return image[height - size:, :size, :3]


def feature(patch: np.ndarray) -> np.ndarray:
    image = Image.fromarray(patch.astype(np.uint8), "RGB").resize(
        (12, 12), Image.Resampling.LANCZOS)
    rgb = np.asarray(image, dtype=np.float64) / 255.0
    mean = rgb.mean(axis=(0, 1))
    deviation = rgb.std(axis=(0, 1))
    maximum = rgb.max(axis=2)
    minimum = rgb.min(axis=2)
    chroma = maximum - minimum
    hue_x = np.zeros_like(maximum)
    hue_y = np.zeros_like(maximum)
    valid = chroma > 1e-5
    # Circular hue representation avoids a red wrap discontinuity.
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
                                       gradient.mean(), gradient.std())) * 3.0,
                           texture[::2, ::2].ravel() * 0.16))


def distance(sample: np.ndarray, prototypes: list[np.ndarray]) -> float:
    return min(float(np.sqrt(np.square(sample - candidate).mean()))
               for candidate in prototypes)


def ranked_anchor_candidates(
        sample: np.ndarray,
        prototypes_by_slot: dict[int, dict[int, list[np.ndarray]]],
        family_names: dict[int, str], excluded_slot: int,
        anchor_risk_by_slot: dict[int, float] | None = None,
        verified_anchor_slots: dict[int, set[int]] | None = None,
        limit: int = 4) -> list[dict]:
    """Rank uniform anchors without allowing the source tile to prove itself.

    These rankings are review evidence only.  A small feature distance never
    authorizes a semantic rewrite.  Excluding the complete source slot is
    essential when an A/A/A/A tile is visibly mixed: none of its four current
    corners may become circular evidence for that label.
    """
    ranked = []
    for family_id, by_slot in prototypes_by_slot.items():
        candidates = []
        for slot, values in by_slot.items():
            if slot == excluded_slot:
                continue
            if (verified_anchor_slots is not None
                    and slot not in verified_anchor_slots.get(family_id, set())):
                continue
            candidates.extend((
                float(np.sqrt(np.square(sample - candidate).mean())),
                slot, corner)
                for corner, candidate in enumerate(values))
        if not candidates:
            continue
        score, anchor_slot, anchor_corner = min(candidates)
        ranked.append({
            "familyId": family_id,
            "family": family_names.get(family_id, str(family_id)),
            "score": round(score, 6),
            "anchorSlot": anchor_slot,
            "anchorCorner": CORNER_NAMES[anchor_corner],
            "anchorMaxCornerDistance": round(
                (anchor_risk_by_slot or {}).get(anchor_slot, 0.0), 6),
        })
    ranked.sort(key=lambda value: (
        value["score"], value["family"].casefold(), value["familyId"],
        value["anchorSlot"]))
    return ranked[:limit]


def verified_anchor_prototypes(
        prototypes_by_slot: dict[int, dict[int, list[np.ndarray]]],
        verified_anchor_slots: dict[int, set[int]],
) -> dict[int, dict[int, list[np.ndarray]]]:
    """Return only explicitly reviewed reference tiles.

    A current A/A/A/A signature is merely a nominal uniform assignment.  It is
    not evidence that the decoded tile is visually one material, and therefore
    must never acquire a human-readable candidate label unless the whole tile
    has been reviewed as a clean anchor.
    """
    return {
        family_id: {
            slot: values for slot, values in by_slot.items()
            if slot in verified_anchor_slots.get(family_id, set())
        }
        for family_id, by_slot in prototypes_by_slot.items()
        if any(slot in verified_anchor_slots.get(family_id, set())
               for slot in by_slot)
    }


def intra_tile_corner_distances(
        image: np.ndarray) -> list[tuple[float, int, int]]:
    """Return all six feature distances between one tile's four corners."""
    corner_features = [
        feature(corner_patch(image, corner)) for corner in range(4)
    ]
    return [
        (float(np.sqrt(np.square(corner_features[first_corner]
                                 - corner_features[second_corner]).mean())),
         first_corner, second_corner)
        for first_corner in range(4)
        for second_corner in range(first_corner + 1, 4)
    ]


def cross_atlas_uniform_anchor_groups(
        records: list[dict]) -> tuple[list[dict], list[dict]]:
    """Group exact uniform-corner pixels reused by independent namespaces."""
    by_hash: dict[str, list[dict]] = defaultdict(list)
    for record in records:
        by_hash[record["pixelSha256"]].append(record)
    corroborated: list[dict] = []
    review: list[dict] = []
    for pixel_hash, values in sorted(by_hash.items()):
        namespaces = sorted({value["namespace"] for value in values},
                            key=str.casefold)
        if len({value.casefold() for value in namespaces}) < 2:
            continue
        # Layer and underwater role are observed map usage, not intrinsic
        # cross-namespace pixel identity. The same exact material may be land
        # in one profile, overlay in another, or used below an overlay only in
        # a third. Preserve those contexts without manufacturing a cross-atlas
        # naming conflict. Layer compatibility is still validated inside each
        # profile by audit_semantic_atlas_catalog.py.
        identity_keys = sorted({value["family"].casefold() for value in values})
        family_keys = set(identity_keys)
        reasons = []
        if len(family_keys) > 1:
            reasons.append("name")
        entry = {
            "pixelSha256": pixel_hash,
            "namespaces": namespaces,
            "familyNames": sorted({value["family"] for value in values},
                                  key=str.casefold),
            "classifications": [
                {"family": family,
                 "allowedLayerSets": [list(layers) for layers in sorted({
                     tuple(value["allowedLayers"]) for value in values
                     if value["family"].casefold() == family
                 })],
                 "underwaterRoles": sorted({
                     value["underwaterRole"] for value in values
                     if value["family"].casefold() == family
                 })}
                for family in identity_keys
            ],
            "reviewReasons": reasons,
            "occurrences": sorted(values, key=lambda value: (
                value["namespace"].casefold(), value["familyId"],
                value["slot"], value["corner"])),
        }
        if len(identity_keys) == 1:
            corroborated.append(entry)
        else:
            review.append(entry)
    review.sort(key=lambda value: (
        -len(value["namespaces"]), -len(value["occurrences"]),
        value["pixelSha256"]))
    corroborated.sort(key=lambda value: (
        -len(value["namespaces"]), -len(value["occurrences"]),
        value["pixelSha256"]))
    return corroborated, review


def uniform_exact_owner_review(
        namespace_contexts: dict[str, dict],
        global_uniform_anchor_records: list[dict]) -> list[dict]:
    """Find false-uniform tiles contradicted by independent exact anchors.

    A decorated or splattered tile can look coherent in its centre while the
    material touching each vertex alternates.  Feature distance is too weak for
    this case.  This pass hashes the vertex-local pixels of every A/A/A/A tile,
    excludes the source tile itself, and reports a corner only when every
    remaining exact uniform-anchor owner agrees on one different family name.
    The result is review evidence and never rewrites a preset automatically.
    """
    owners_by_hash: dict[str, list[dict]] = defaultdict(list)
    for record in global_uniform_anchor_records:
        owners_by_hash[record["pixelSha256"]].append(record)

    review: list[dict] = []
    for namespace, context in namespace_contexts.items():
        table = context["table"]
        tiles = context["tiles"]
        family_by_id = context["familyById"]
        for slot, signature in enumerate(table):
            if len(set(signature)) != 1:
                continue
            current_family_id = signature[0]
            current_family = family_by_id[current_family_id]["name"]
            conflicts = []
            for corner in range(4):
                patch = corner_patch(tiles[slot], corner)
                pixel_hash = hashlib.sha256(
                    patch.tobytes(order="C")).hexdigest()
                independent = [
                    owner for owner in owners_by_hash.get(pixel_hash, [])
                    if not (owner["namespace"].casefold()
                            == namespace.casefold()
                            and owner["slot"] == slot)
                ]
                candidate_names = sorted(
                    {owner["family"] for owner in independent},
                    key=str.casefold)
                if (len({value.casefold() for value in candidate_names}) != 1
                        or candidate_names[0].casefold()
                        == current_family.casefold()):
                    continue
                conflicts.append({
                    "corner": CORNER_NAMES[corner],
                    "pixelSha256": pixel_hash,
                    "candidateFamily": candidate_names[0],
                    "independentOwners": sorted(independent, key=lambda value: (
                        value["namespace"].casefold(), value["slot"],
                        value["corner"])),
                })
            if not conflicts:
                continue
            review.append({
                "namespace": namespace,
                "slot": slot,
                "currentFamilyId": current_family_id,
                "currentFamily": current_family,
                "conflictingCorners": conflicts,
                "distinctCandidateFamilies": sorted({
                    value["candidateFamily"] for value in conflicts
                }, key=str.casefold),
                "requiredAction": (
                    "Inspect the complete tile and assign the material touching "
                    "each vertex. Centre splatter is not corner identity."),
            })
    review.sort(key=lambda value: (
        -len(value["conflictingCorners"]),
        -len(value["distinctCandidateFamilies"]),
        value["namespace"].casefold(), value["slot"]))
    for rank, record in enumerate(review, 1):
        record["rank"] = rank
    return review


def cross_atlas_review_components(review: list[dict]) -> list[dict]:
    """Collapse repeated exact hashes with the same semantic disagreement."""
    grouped: dict[tuple, list[dict]] = defaultdict(list)
    for entry in review:
        key = tuple((value["family"], tuple(tuple(layers) for layers in
                                            value["allowedLayerSets"]),
                     tuple(value["underwaterRoles"]))
                    for value in entry["classifications"])
        grouped[key].append(entry)
    components = []
    for key, entries in grouped.items():
        occurrences = [value for entry in entries
                       for value in entry["occurrences"]]
        components.append({
            "componentId": "cross-atlas-" + hashlib.sha256(
                json.dumps(key, sort_keys=True).encode("utf-8")
            ).hexdigest()[:12],
            "reviewReasons": sorted({reason for entry in entries
                                     for reason in entry["reviewReasons"]}),
            "familyNames": sorted({value["family"] for value in occurrences},
                                  key=str.casefold),
            "classifications": entries[0]["classifications"],
            "pixelHashCount": len(entries),
            "namespaces": sorted({value["namespace"] for value in occurrences},
                                 key=str.casefold),
            "occurrenceCount": len(occurrences),
            "pixelSha256": [entry["pixelSha256"] for entry in entries],
            "occurrences": sorted(occurrences, key=lambda value: (
                value["namespace"].casefold(), value["familyId"],
                value["slot"], value["corner"])),
        })
    components.sort(key=lambda value: (
        0 if "name" in value["reviewReasons"] else 1,
        -value["pixelHashCount"], -len(value["namespaces"]),
        value["componentId"]))
    for rank, component in enumerate(components, 1):
        component["rank"] = rank
    return components


def signatures(preset: dict) -> list[tuple[int, int, int, int]]:
    raw = base64.b64decode(preset["signaturesBase64"])
    return [tuple(raw[index:index + 4])
            for index in range(0, len(raw), 4)]


def preset_atlas_members(preset: dict) -> list[str]:
    """Return every atlas page required to decode a compact preset."""
    slot_count = preset["slotCount"]
    if slot_count % 64:
        raise ValueError(
            f"{preset['namespace']} slotCount is not page aligned: {slot_count}")
    return [f"{preset['namespace']}{part:03d}.mmp".lower()
            for part in range(slot_count // 64)]


def archive_contains_preset(archive_index: dict[str, dict],
                            preset: dict) -> bool:
    """Whether the selected archive contains every page needed by a preset."""
    return all(name in archive_index for name in preset_atlas_members(preset))


def contact_sheet(path: Path, namespace: str, family: dict,
                  occurrences: list[dict], tiles: list[np.ndarray],
                  family_names: dict[int, str]) -> None:
    tile_size = 104
    patch_size = 72
    cell_width, cell_height = 240, 190
    columns = 5
    rows = max(1, (len(occurrences) + columns - 1) // columns)
    canvas = Image.new("RGB", (columns * cell_width, 48 + rows * cell_height),
                       (30, 30, 30))
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 8), f"{namespace}  ID {family['id']}  {family['name']}",
              fill=(255, 255, 255))
    for index, occurrence in enumerate(occurrences):
        x = (index % columns) * cell_width
        y = 48 + (index // columns) * cell_height
        image = Image.fromarray(tiles[occurrence["slot"]][:, :, :3], "RGB")
        image.thumbnail((tile_size, tile_size), Image.Resampling.LANCZOS)
        canvas.paste(image, (x, y))
        crop = Image.fromarray(corner_patch(
            tiles[occurrence["slot"]], occurrence["corner"]), "RGB")
        crop = crop.resize((patch_size, patch_size), Image.Resampling.LANCZOS)
        canvas.paste(crop, (x + tile_size + 8, y))
        draw.rectangle((x + tile_size + 7, y - 1,
                        x + tile_size + 8 + patch_size,
                        y + patch_size), outline=(255, 80, 80), width=2)
        draw.text((x, y + 108),
                  f"slot {occurrence['slot']} {CORNER_NAMES[occurrence['corner']]}",
                  fill=(255, 220, 120))
        draw.text((x, y + 124), f"signature {occurrence['signature']}",
                  fill=(210, 210, 210))
        best = occurrence.get("bestAnchorId")
        label = family_names.get(best, "no uniform anchor") if best else "no anchor"
        draw.text((x, y + 140), f"nearest: {label}", fill=(220, 220, 220))
        draw.text((x, y + 156),
                  f"score {occurrence.get('bestScore', 0):.4f} margin {occurrence.get('margin', 0):.4f}",
                  fill=(180, 180, 180))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def uniform_contact_sheet(path: Path, namespace: str, family: dict,
                          slots: list[int], tiles: list[np.ndarray]) -> None:
    if not slots:
        return
    tile_size = 128
    cell_width, cell_height = 160, 164
    columns = 6
    rows = (len(slots) + columns - 1) // columns
    canvas = Image.new("RGB", (columns * cell_width, 32 + rows * cell_height),
                       (28, 28, 28))
    draw = ImageDraw.Draw(canvas)
    draw.text((8, 8), f"{namespace} ID {family['id']} {family['name']} uniform variants",
              fill=(238, 238, 238))
    for index, slot in enumerate(slots):
        x = (index % columns) * cell_width + 8
        y = 32 + (index // columns) * cell_height
        image = Image.fromarray(tiles[slot][:, :, :3], "RGB").resize(
            (tile_size, tile_size), Image.Resampling.LANCZOS)
        canvas.paste(image, (x, y))
        draw.text((x, y + tile_size + 5), f"uniform slot {slot}",
                  fill=(245, 220, 120))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def review_font(size: int, bold: bool = False) -> ImageFont.ImageFont:
    names = (("arialbd.ttf", "DejaVuSans-Bold.ttf") if bold
             else ("arial.ttf", "DejaVuSans.ttf"))
    for name in names:
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def overlap_review_sheet(path: Path, namespace: str, anchor_ids: list[int],
                         occurrences: list[dict], context: dict) -> None:
    """Render exact-anchor overlap evidence without deciding family identity."""
    tiles = context["tiles"]
    family_by_id = context["familyById"]
    uniform_slots = context["uniformSlots"]
    columns = 4
    cell_width, cell_height = 300, 252
    tile_size = 180
    header_height = 112
    occurrence_rows = max(1, (len(occurrences) + columns - 1) // columns)
    candidate_rows = sum(max(1, (min(12, len(uniform_slots.get(family_id, [])))
                                 + columns - 1) // columns)
                         for family_id in anchor_ids)
    candidate_header_height = 54 * len(anchor_ids)
    height = (header_height + occurrence_rows * cell_height
              + candidate_header_height + candidate_rows * (tile_size + 38) + 24)
    canvas = Image.new("RGB", (columns * cell_width, height), (25, 25, 25))
    draw = ImageDraw.Draw(canvas)
    title_font = review_font(25, True)
    label_font = review_font(18, True)
    body_font = review_font(16)
    small_font = review_font(14)
    family_label = " | ".join(
        f"{family_id}: {family_by_id[family_id]['name']}" for family_id in anchor_ids)
    draw.text((14, 12), f"{namespace} exact uniform-anchor overlap",
              fill=(255, 255, 255), font=title_font)
    draw.text((14, 47), family_label, fill=(255, 220, 125), font=label_font)
    draw.text((14, 76),
              "Red frame = the exact corner under review. Current assignment is evidence, not truth.",
              fill=(220, 220, 220), font=body_font)
    for index, occurrence in enumerate(occurrences):
        x = (index % columns) * cell_width + 10
        y = header_height + (index // columns) * cell_height
        slot = int(occurrence["slot"])
        corner = CORNER_NAMES.index(occurrence["corner"])
        image = Image.fromarray(tiles[slot][:, :, :3], "RGB").resize(
            (tile_size, tile_size), Image.Resampling.NEAREST)
        canvas.paste(image, (x, y))
        half = tile_size // 2
        corner_boxes = ((x, y, x + half, y + half),
                        (x + half, y, x + tile_size, y + half),
                        (x + half, y + half, x + tile_size, y + tile_size),
                        (x, y + half, x + half, y + tile_size))
        draw.rectangle(corner_boxes[corner], outline=(255, 35, 35), width=5)
        draw.text((x + tile_size + 10, y + 4),
                  f"slot {slot}\n{occurrence['corner']}",
                  fill=(255, 230, 140), font=label_font, spacing=4)
        draw.text((x, y + tile_size + 7),
                  f"current: {occurrence['familyId']} {occurrence['family']}",
                  fill=(235, 235, 235), font=body_font)
        draw.text((x, y + tile_size + 30),
                  f"tile signature: {occurrence['signature']}",
                  fill=(185, 185, 185), font=small_font)
    y = header_height + occurrence_rows * cell_height
    for family_id in anchor_ids:
        family = family_by_id[family_id]
        slots = uniform_slots.get(family_id, [])[:12]
        draw.text((14, y + 8),
                  f"Candidate {family_id}: {family['name']} — uniform anchor tiles",
                  fill=(140, 210, 255), font=label_font)
        y += 54
        if not slots:
            draw.text((14, y), "No uniform tile in this family.",
                      fill=(255, 150, 150), font=body_font)
            y += tile_size + 38
            continue
        for index, slot in enumerate(slots):
            x = (index % columns) * cell_width + 10
            row_y = y + (index // columns) * (tile_size + 38)
            image = Image.fromarray(tiles[slot][:, :, :3], "RGB").resize(
                (tile_size, tile_size), Image.Resampling.NEAREST)
            canvas.paste(image, (x, row_y))
            draw.text((x, row_y + tile_size + 6), f"uniform slot {slot}",
                      fill=(245, 220, 120), font=body_font)
        y += max(1, (len(slots) + columns - 1) // columns) * (tile_size + 38)
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.crop((0, 0, canvas.width, min(canvas.height, y + 18))).save(path)


def cross_atlas_review_sheet(path: Path, component: dict,
                             contexts: dict[str, dict]) -> None:
    """Render exact cross-atlas name conflicts at readable tile scale."""
    grouped: dict[tuple[str, int, int], dict] = {}
    for occurrence in component["occurrences"]:
        key = (occurrence["namespace"], occurrence["familyId"],
               occurrence["slot"])
        value = grouped.setdefault(key, {
            "namespace": occurrence["namespace"],
            "familyId": occurrence["familyId"],
            "family": occurrence["family"],
            "slot": occurrence["slot"],
            "corners": [],
        })
        value["corners"].append(occurrence["corner"])
    values = sorted(grouped.values(), key=lambda value: (
        value["family"].casefold(), value["namespace"].casefold(),
        value["slot"]))
    columns = 4
    tile_size = 192
    cell_width, cell_height = 300, 258
    header_height = 88
    rows = max(1, (len(values) + columns - 1) // columns)
    canvas = Image.new("RGB", (columns * cell_width,
                               header_height + rows * cell_height),
                       (27, 27, 27))
    draw = ImageDraw.Draw(canvas)
    draw.text((12, 10), "Exact cross-atlas pixel identity conflict",
              fill=(245, 245, 245), font=review_font(22, bold=True))
    draw.text((12, 42), " / ".join(component["familyNames"]),
              fill=(255, 220, 120), font=review_font(17))
    for index, value in enumerate(values):
        x = (index % columns) * cell_width + 12
        y = header_height + (index // columns) * cell_height
        tile = Image.fromarray(
            contexts[value["namespace"]]["tiles"][value["slot"]][:, :, :3],
            "RGB").resize((tile_size, tile_size), Image.Resampling.NEAREST)
        canvas.paste(tile, (x, y))
        marker = 38
        for corner in sorted(set(value["corners"])):
            corner_index = CORNER_NAMES.index(corner)
            left = x if corner_index in (0, 3) else x + tile_size - marker
            top = y if corner_index in (0, 1) else y + tile_size - marker
            draw.rectangle((left, top, left + marker - 1, top + marker - 1),
                           outline=(255, 48, 48), width=5)
        draw.text((x, y + tile_size + 7),
                  f"{value['namespace']} tile {value['slot']} "
                  + "/".join(sorted(set(value["corners"]))),
                  fill=(245, 220, 120), font=review_font(15))
        draw.text((x, y + tile_size + 29), value["family"],
                  fill=(230, 230, 230), font=review_font(15))
    path.parent.mkdir(parents=True, exist_ok=True)
    canvas.save(path)


def connected_overlap_components(queue: list[dict],
                                 contexts: dict[str, dict]) -> list[dict]:
    """Join pairwise exact-anchor groups that share a family in one atlas."""
    result: list[dict] = []
    by_namespace: dict[str, list[dict]] = defaultdict(list)
    for group in queue:
        by_namespace[group["namespace"]].append(group)
    for namespace, groups in sorted(by_namespace.items()):
        parent: dict[int, int] = {}

        def find(value: int) -> int:
            parent.setdefault(value, value)
            while parent[value] != value:
                parent[value] = parent[parent[value]]
                value = parent[value]
            return value

        def union(first: int, second: int) -> None:
            first_root, second_root = find(first), find(second)
            if first_root != second_root:
                parent[second_root] = first_root

        for group in groups:
            family_ids = group["anchorFamilyIds"]
            for family_id in family_ids:
                find(family_id)
            for family_id in family_ids[1:]:
                union(family_ids[0], family_id)
        grouped: dict[int, list[dict]] = defaultdict(list)
        for group in groups:
            grouped[find(group["anchorFamilyIds"][0])].append(group)
        family_by_id = contexts[namespace]["familyById"]
        for component_groups in grouped.values():
            family_ids = sorted({
                family_id for group in component_groups
                for family_id in group["anchorFamilyIds"]
            })
            focused: dict[tuple[int, str, int], dict] = {}
            for group in component_groups:
                for corner in group["focusedCorners"]:
                    key = (corner["slot"], corner["corner"],
                           corner["currentFamilyId"])
                    focused[key] = corner
            safe_ids = "-".join(str(value) for value in family_ids)
            result.append({
                "componentId": f"{namespace.lower()}-component-{safe_ids}",
                "namespace": namespace,
                "familyIds": family_ids,
                "families": [family_by_id[value]["name"]
                             for value in family_ids],
                "groupCount": len(component_groups),
                "reviewIds": [group["reviewId"] for group in component_groups],
                "severities": sorted({group["severity"]
                                      for group in component_groups}),
                "uniqueFocusedCorners": len(focused),
                "focusedCorners": [focused[key] for key in sorted(focused)],
                "requiredAction": (
                    "Review this connected family set together. A decision in "
                    "one pair can change the meaning of every other overlap "
                    "linked through a shared family."),
            })
    result.sort(key=lambda value: (-value["uniqueFocusedCorners"],
                                   -value["groupCount"],
                                   value["namespace"], value["componentId"]))
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--all-contact-sheets", action="store_true",
                        help="also render visually consistent families")
    parser.add_argument("--namespace", action="append", default=[],
                        help="limit the audit to one or more namespaces")
    args = parser.parse_args()
    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    archive_index = res_index(args.archive)
    args.output.mkdir(parents=True, exist_ok=True)
    # These directories are wholly owned generated evidence. Remove them before
    # rendering so a deleted/merged family cannot leave a stale contact sheet
    # that is later counted or reviewed as if it belonged to the current run.
    for managed_name in ("contact-sheets", "exact-conflict-contact-sheets",
                         "uniform-contact-sheets",
                         "overlap-review-contact-sheets",
                         "cross-atlas-review-contact-sheets"):
        managed_path = args.output / managed_name
        if managed_path.is_dir():
            shutil.rmtree(managed_path)
    reviewed_tables: set[tuple[int, str]] = set()
    records: list[dict] = []
    occurrence_records: list[dict] = []
    uniform_duplicate_records: list[dict] = []
    uniform_intra_tile_records: list[dict] = []
    global_uniform_anchor_records: list[dict] = []
    global_uniform_tile_records: list[dict] = []
    namespace_reports: list[dict] = []
    namespace_contexts: dict[str, dict] = {}
    skipped_namespaces: list[dict] = []
    available_namespaces: list[str] = []
    reused_table_namespaces: list[dict] = []
    reviewed_table_owner: dict[tuple[int, str], str] = {}
    for preset in catalog["presets"]:
        if args.namespace and preset["namespace"].lower() not in {
                value.lower() for value in args.namespace}:
            continue
        namespace = preset["namespace"]
        required_members = preset_atlas_members(preset)
        missing_members = [name for name in required_members
                           if name not in archive_index]
        if missing_members:
            skipped_namespaces.append({
                "namespace": namespace,
                "reason": "atlas pages are absent from the selected archive",
                "missingMembers": missing_members,
            })
            continue
        available_namespaces.append(namespace)
        table_key = (preset["slotCount"], preset["signaturesBase64"])
        if table_key in reviewed_tables:
            reused_table_namespaces.append({
                "namespace": namespace,
                "sharesSignatureTableWith": reviewed_table_owner[table_key],
            })
            continue
        reviewed_tables.add(table_key)
        reviewed_table_owner[table_key] = namespace
        table = signatures(preset)
        tiles = editor_tiles(args.archive, archive_index, namespace,
                             preset["slotCount"] // 64)
        family_by_id = {item["id"]: item for item in preset["families"]}
        family_names = {key: item["name"] for key, item in family_by_id.items()}
        tile_hashes = [hashlib.sha256(
            tile[:, :, :3].tobytes(order="C")).hexdigest() for tile in tiles]
        occurrences: dict[int, list[dict]] = defaultdict(list)
        uniform_slots: dict[int, list[int]] = defaultdict(list)
        for slot, signature in enumerate(table):
            if len(set(signature)) == 1:
                uniform_slots[signature[0]].append(slot)
            for corner, family_id in enumerate(signature):
                occurrences[family_id].append({"slot": slot, "corner": corner,
                                               "signature": list(signature)})
        prototypes: dict[int, list[np.ndarray]] = defaultdict(list)
        prototypes_by_slot: dict[int, dict[int, list[np.ndarray]]] = defaultdict(
            lambda: defaultdict(list))
        exact_pixel_anchors: dict[str, set[int]] = defaultdict(set)
        uniform_tile_owners: dict[str, set[int]] = defaultdict(set)
        for family_id, slots in uniform_slots.items():
            for slot in slots:
                uniform_tile_owners[tile_hashes[slot]].add(family_id)
                global_uniform_tile_records.append({
                    "pixelSha256": tile_hashes[slot],
                    "namespace": namespace,
                    "familyId": family_id,
                    "family": family_by_id[family_id]["name"],
                    "allowedLayers": sorted(
                        family_by_id[family_id].get("allowedLayers", [])),
                    "underwaterRole": family_by_id[family_id].get(
                        "underwaterRole", ""),
                    "slot": slot,
                    "corner": "ALL",
                })
                for corner in range(4):
                    patch = corner_patch(tiles[slot], corner)
                    patch_feature = feature(patch)
                    pixel_hash = hashlib.sha256(
                        patch.tobytes(order="C")).hexdigest()
                    prototypes[family_id].append(patch_feature)
                    prototypes_by_slot[family_id][slot].append(patch_feature)
                    exact_pixel_anchors[pixel_hash].add(family_id)
                    global_uniform_anchor_records.append({
                        "pixelSha256": pixel_hash,
                        "namespace": namespace,
                        "familyId": family_id,
                        "family": family_by_id[family_id]["name"],
                        "allowedLayers": sorted(
                            family_by_id[family_id].get("allowedLayers", [])),
                        "underwaterRole": family_by_id[family_id].get(
                            "underwaterRole", ""),
                        "slot": slot,
                        "corner": CORNER_NAMES[corner],
                    })
        namespace_contexts[namespace] = {
            "tiles": tiles,
            "table": table,
            "familyById": family_by_id,
            "uniformSlots": uniform_slots,
        }
        uniform_anchor_risk_by_slot = {
            slot: max((value[0] for value in
                       intra_tile_corner_distances(tiles[slot])), default=0.0)
            for slots in uniform_slots.values()
            for slot in slots
        }
        for tile_hash, owner_ids in sorted(uniform_tile_owners.items()):
            if len(owner_ids) < 2:
                continue
            sorted_owner_ids = sorted(owner_ids)
            owner_slots = {
                family_id: sorted(
                    slot for slot in uniform_slots[family_id]
                    if tile_hashes[slot] == tile_hash)
                for family_id in sorted_owner_ids
            }
            contacts_by_family: dict[int, set[int]] = {
                family_id: set() for family_id in sorted_owner_ids}
            cooccurrence_slots: set[int] = set()
            for slot, signature in enumerate(table):
                present = set(signature) & owner_ids
                if len(present) > 1:
                    cooccurrence_slots.add(slot)
                for family_id in present:
                    contacts_by_family[family_id].update(
                        set(signature) - {family_id})
            contact_sets = [contacts_by_family[family_id]
                            for family_id in sorted_owner_ids]
            uniform_duplicate_records.append({
                "namespace": namespace,
                "tileSha256": tile_hash,
                "familyIds": "/".join(str(value)
                                      for value in sorted_owner_ids),
                "families": "/".join(family_names[value]
                                      for value in sorted_owner_ids),
                "uniformSlotsByFamily": "; ".join(
                    f"{family_names[family_id]}="
                    + "/".join(str(slot) for slot in owner_slots[family_id])
                    for family_id in sorted_owner_ids),
                "cooccurrenceSlots": "/".join(
                    str(slot) for slot in sorted(cooccurrence_slots)),
                "sameContactSet": all(
                    contact_set == contact_sets[0]
                    for contact_set in contact_sets[1:]),
                "contactsByFamily": "; ".join(
                    f"{family_names[family_id]}=" + "/".join(
                        family_names.get(value, str(value))
                        for value in sorted(contacts_by_family[family_id]))
                    for family_id in sorted_owner_ids),
            })
        family_reports = []
        for family_id, family in sorted(family_by_id.items()):
            values = occurrences.get(family_id, [])
            nearest_counts: dict[int, int] = defaultdict(int)
            own_scores: list[float] = []
            exact_pixel_conflicts = 0
            leave_one_out_disagreements = 0
            leave_duplicate_group_out_disagreements = 0
            leave_duplicate_group_out_exact_ambiguities = 0
            cross_family_identical_uniform_slots = 0
            cross_family_identical_uniform_ids: set[int] = set()
            uniform_variant_distances: list[float] = []
            uniform_variant_pairs: list[tuple[float, int, int]] = []
            uniform_intra_tile_distances: list[float] = []
            uniform_intra_tile_pairs: list[tuple[float, int, int, int]] = []
            uniform_feature_by_slot = {
                slot: np.mean([
                    feature(corner_patch(tiles[slot], corner))
                    for corner in range(4)], axis=0)
                for slot in uniform_slots.get(family_id, [])
            }
            uniform_feature_slots = sorted(uniform_feature_by_slot)
            for first_index, first_slot in enumerate(uniform_feature_slots):
                for second_slot in uniform_feature_slots[first_index + 1:]:
                    pair_distance = float(np.sqrt(np.square(
                        uniform_feature_by_slot[first_slot]
                        - uniform_feature_by_slot[second_slot]).mean()))
                    uniform_variant_distances.append(pair_distance)
                    uniform_variant_pairs.append(
                        (pair_distance, first_slot, second_slot))
            maximum_variant_pair = max(uniform_variant_pairs, default=(0.0, -1, -1))
            for slot in uniform_feature_slots:
                slot_corner_pairs = intra_tile_corner_distances(tiles[slot])
                for corner_distance, first_corner, second_corner in slot_corner_pairs:
                    uniform_intra_tile_distances.append(corner_distance)
                    uniform_intra_tile_pairs.append((
                        corner_distance, slot, first_corner, second_corner))
                slot_maximum = max(
                    slot_corner_pairs, default=(0.0, -1, -1))
                uniform_intra_tile_records.append({
                    "namespace": namespace,
                    "familyId": family_id,
                    "family": family["name"],
                    "slot": slot,
                    "tileSha256": tile_hashes[slot],
                    "maxDistance": round(slot_maximum[0], 6),
                    "meanDistance": round(float(np.mean([
                        value[0] for value in slot_corner_pairs])), 6)
                        if slot_corner_pairs else 0.0,
                    "maxDistanceCorners": [
                        CORNER_NAMES[slot_maximum[1]],
                        CORNER_NAMES[slot_maximum[2]],
                    ] if slot_maximum[1] >= 0 else [],
                    "cornerCandidates": {
                        CORNER_NAMES[corner]: ranked_anchor_candidates(
                            feature(corner_patch(tiles[slot], corner)),
                            prototypes_by_slot, family_names, slot,
                            uniform_anchor_risk_by_slot)
                        for corner in range(4)
                    },
                })
            maximum_intra_tile_pair = max(
                uniform_intra_tile_pairs, default=(0.0, -1, -1, -1))
            for occurrence in values:
                sample_patch = corner_patch(
                    tiles[occurrence["slot"]], occurrence["corner"])
                sample = feature(sample_patch)
                sample_pixel_hash = hashlib.sha256(
                    sample_patch.tobytes(order="C")).hexdigest()
                exact_ids = sorted(exact_pixel_anchors.get(
                    sample_pixel_hash, set()))
                conflicting_exact_ids = [anchor_id for anchor_id in exact_ids
                                         if anchor_id != family_id]
                exact_conflict = bool(
                    conflicting_exact_ids and family_id not in exact_ids)
                occurrence["exactPixelAnchorIds"] = exact_ids
                occurrence["exactPixelConflict"] = exact_conflict
                if exact_conflict:
                    exact_pixel_conflicts += 1
                ranked = sorted((distance(sample, candidates), anchor_id)
                                for anchor_id, candidates in prototypes.items())
                if ranked:
                    occurrence["bestScore"], occurrence["bestAnchorId"] = ranked[0]
                    occurrence["secondScore"] = ranked[1][0] if len(ranked) > 1 else ranked[0][0]
                    occurrence["margin"] = occurrence["secondScore"] - occurrence["bestScore"]
                    nearest_counts[occurrence["bestAnchorId"]] += 1
                if family_id in prototypes:
                    own_scores.append(distance(sample, prototypes[family_id]))
                leave_one_out_ranked = []
                leave_duplicate_group_out_ranked = []
                if occurrence["slot"] in uniform_slots.get(family_id, []):
                    for anchor_id, candidates in prototypes.items():
                        compare_candidates = candidates
                        if anchor_id == family_id:
                            compare_candidates = [
                                candidate
                                for slot, slot_candidates in
                                prototypes_by_slot[family_id].items()
                                if slot != occurrence["slot"]
                                for candidate in slot_candidates
                            ]
                        if compare_candidates:
                            leave_one_out_ranked.append(
                                (distance(sample, compare_candidates), anchor_id))
                        duplicate_group_candidates = candidates
                        if anchor_id == family_id:
                            duplicate_group_candidates = [
                                candidate
                                for slot, slot_candidates in
                                prototypes_by_slot[family_id].items()
                                if tile_hashes[slot]
                                != tile_hashes[occurrence["slot"]]
                                for candidate in slot_candidates
                            ]
                        if duplicate_group_candidates:
                            leave_duplicate_group_out_ranked.append((
                                distance(sample, duplicate_group_candidates),
                                anchor_id))
                    leave_one_out_ranked.sort()
                    if leave_one_out_ranked:
                        occurrence["leaveOneOutScore"], occurrence[
                            "leaveOneOutAnchorId"] = leave_one_out_ranked[0]
                        occurrence["leaveOneOutSecondScore"] = (
                            leave_one_out_ranked[1][0]
                            if len(leave_one_out_ranked) > 1
                            else leave_one_out_ranked[0][0])
                        occurrence["leaveOneOutMargin"] = (
                            occurrence["leaveOneOutSecondScore"]
                            - occurrence["leaveOneOutScore"])
                        leave_one_out_disagreements += (
                            occurrence["leaveOneOutAnchorId"] != family_id)
                    leave_duplicate_group_out_ranked.sort()
                    if leave_duplicate_group_out_ranked:
                        occurrence["leaveDuplicateGroupOutScore"], occurrence[
                            "leaveDuplicateGroupOutAnchorId"] = (
                                leave_duplicate_group_out_ranked[0])
                        occurrence["leaveDuplicateGroupOutSecondScore"] = (
                            leave_duplicate_group_out_ranked[1][0]
                            if len(leave_duplicate_group_out_ranked) > 1
                            else leave_duplicate_group_out_ranked[0][0])
                        occurrence["leaveDuplicateGroupOutMargin"] = (
                            occurrence["leaveDuplicateGroupOutSecondScore"]
                            - occurrence["leaveDuplicateGroupOutScore"])
                        leave_duplicate_group_out_disagreements += (
                            occurrence["leaveDuplicateGroupOutAnchorId"]
                            != family_id)
                        exact_other_family = (
                            occurrence["leaveDuplicateGroupOutAnchorId"]
                            != family_id
                            and occurrence["leaveDuplicateGroupOutScore"]
                            <= 1e-12)
                        occurrence["leaveDuplicateGroupOutExactOtherFamily"] = (
                            exact_other_family)
                        leave_duplicate_group_out_exact_ambiguities += int(
                            exact_other_family)
                    duplicate_tile_ids = sorted(
                        uniform_tile_owners.get(
                            tile_hashes[occurrence["slot"]], set())
                        - {family_id})
                    occurrence["uniformWholeTileDuplicateFamilyIds"] = (
                        duplicate_tile_ids)
                    if duplicate_tile_ids:
                        cross_family_identical_uniform_slots += 1
                        cross_family_identical_uniform_ids.update(
                            duplicate_tile_ids)
                occurrence_records.append({
                    "namespace": namespace,
                    "familyId": family_id,
                    "family": family["name"],
                    "slot": occurrence["slot"],
                    "corner": CORNER_NAMES[occurrence["corner"]],
                    "pixelSha256": sample_pixel_hash,
                    "signature": "/".join(str(value) for value in
                                            occurrence["signature"]),
                    "nearestAnchorId": occurrence.get("bestAnchorId", 0),
                    "nearestAnchor": family_names.get(
                        occurrence.get("bestAnchorId", 0), ""),
                    "nearestScore": round(occurrence.get("bestScore", 0.0), 6),
                    "runnerUpScore": round(occurrence.get("secondScore", 0.0), 6),
                    "margin": round(occurrence.get("margin", 0.0), 6),
                    "exactPixelAnchorIds": "/".join(str(value)
                                                      for value in exact_ids),
                    "exactPixelAnchors": "/".join(family_names.get(value, "")
                                                    for value in exact_ids),
                    "exactPixelConflict": exact_conflict,
                    "leaveOneOutAnchorId": occurrence.get(
                        "leaveOneOutAnchorId", 0),
                    "leaveOneOutAnchor": family_names.get(
                        occurrence.get("leaveOneOutAnchorId", 0), ""),
                    "leaveOneOutScore": round(
                        occurrence.get("leaveOneOutScore", 0.0), 6),
                    "leaveOneOutRunnerUpScore": round(
                        occurrence.get("leaveOneOutSecondScore", 0.0), 6),
                    "leaveOneOutMargin": round(
                        occurrence.get("leaveOneOutMargin", 0.0), 6),
                    "leaveDuplicateGroupOutAnchorId": occurrence.get(
                        "leaveDuplicateGroupOutAnchorId", 0),
                    "leaveDuplicateGroupOutAnchor": family_names.get(
                        occurrence.get("leaveDuplicateGroupOutAnchorId", 0), ""),
                    "leaveDuplicateGroupOutScore": round(
                        occurrence.get("leaveDuplicateGroupOutScore", 0.0), 6),
                    "leaveDuplicateGroupOutRunnerUpScore": round(
                        occurrence.get(
                            "leaveDuplicateGroupOutSecondScore", 0.0), 6),
                    "leaveDuplicateGroupOutMargin": round(
                        occurrence.get("leaveDuplicateGroupOutMargin", 0.0), 6),
                    "leaveDuplicateGroupOutExactOtherFamily": occurrence.get(
                        "leaveDuplicateGroupOutExactOtherFamily", False),
                    "uniformWholeTileDuplicateFamilyIds": "/".join(
                        str(value) for value in occurrence.get(
                            "uniformWholeTileDuplicateFamilyIds", [])),
                    "uniformWholeTileDuplicateFamilies": "/".join(
                        family_names.get(value, "") for value in occurrence.get(
                            "uniformWholeTileDuplicateFamilyIds", [])),
                })
            total = len(values)
            dominant_anchor = max(nearest_counts, key=nearest_counts.get) if nearest_counts else 0
            dominant_share = nearest_counts.get(dominant_anchor, 0) / total if total else 0.0
            report = {
                "namespace": namespace,
                "familyId": family_id,
                "family": family["name"],
                "memberSlots": len({item["slot"] for item in values}),
                "cornerOccurrences": total,
                "uniformSlots": len(uniform_slots.get(family_id, [])),
                "paintSelectable": family.get("paintSelectable", True),
                "dominantVisualAnchorId": dominant_anchor,
                "dominantVisualAnchor": family_names.get(dominant_anchor, ""),
                "dominantShare": round(dominant_share, 4),
                "meanOwnAnchorScore": round(float(np.mean(own_scores)), 6)
                    if own_scores else None,
                "maxOwnAnchorScore": round(float(np.max(own_scores)), 6)
                    if own_scores else None,
                "meanUniformVariantDistance": round(float(np.mean(
                    uniform_variant_distances)), 6)
                    if uniform_variant_distances else 0.0,
                "maxUniformVariantDistance": round(float(np.max(
                    uniform_variant_distances)), 6)
                    if uniform_variant_distances else 0.0,
                "maxUniformVariantSlotA": maximum_variant_pair[1],
                "maxUniformVariantSlotB": maximum_variant_pair[2],
                "meanUniformIntraTileCornerDistance": round(float(np.mean(
                    uniform_intra_tile_distances)), 6)
                    if uniform_intra_tile_distances else 0.0,
                "maxUniformIntraTileCornerDistance": round(float(np.max(
                    uniform_intra_tile_distances)), 6)
                    if uniform_intra_tile_distances else 0.0,
                "maxUniformIntraTileSlot": maximum_intra_tile_pair[1],
                "maxUniformIntraTileCornerA": (
                    CORNER_NAMES[maximum_intra_tile_pair[2]]
                    if maximum_intra_tile_pair[2] >= 0 else ""),
                "maxUniformIntraTileCornerB": (
                    CORNER_NAMES[maximum_intra_tile_pair[3]]
                    if maximum_intra_tile_pair[3] >= 0 else ""),
                "exactPixelConflicts": exact_pixel_conflicts,
                "leaveOneOutUniformDisagreements":
                    leave_one_out_disagreements,
                "leaveDuplicateGroupOutUniformDisagreements":
                    leave_duplicate_group_out_disagreements,
                "leaveDuplicateGroupOutExactAmbiguities":
                    leave_duplicate_group_out_exact_ambiguities,
                "crossFamilyIdenticalUniformSlots":
                    cross_family_identical_uniform_slots // 4,
                "crossFamilyIdenticalUniformIds": "/".join(
                    str(value) for value in sorted(
                        cross_family_identical_uniform_ids)),
                "crossFamilyIdenticalUniformFamilies": "/".join(
                    family_names.get(value, "") for value in sorted(
                        cross_family_identical_uniform_ids)),
            }
            family_reports.append(report)
            records.append(report)
            suspect = (not uniform_slots.get(family_id)
                       or dominant_anchor != family_id
                       or dominant_share < 0.80)
            if (suspect or args.all_contact_sheets) and values:
                safe_name = "".join(character.lower() if character.isalnum() else "-"
                                    for character in family["name"]).strip("-")
                contact_sheet(args.output / "contact-sheets" / namespace.lower()
                              / f"id-{family_id}-{safe_name}.png",
                              namespace, family, values, tiles, family_names)
            exact_conflict_values = [value for value in values
                                     if value.get("exactPixelConflict")]
            if exact_conflict_values:
                safe_name = "".join(character.lower() if character.isalnum() else "-"
                                    for character in family["name"]).strip("-")
                contact_sheet(args.output / "exact-conflict-contact-sheets"
                              / namespace.lower()
                              / f"id-{family_id}-{safe_name}.png",
                              namespace, family, exact_conflict_values, tiles,
                              family_names)
            if uniform_slots.get(family_id):
                safe_name = "".join(character.lower() if character.isalnum() else "-"
                                    for character in family["name"]).strip("-")
                uniform_contact_sheet(
                    args.output / "uniform-contact-sheets" / namespace.lower()
                    / f"id-{family_id}-{safe_name}.png",
                    namespace, family, uniform_slots[family_id], tiles)
        namespace_reports.append({"namespace": namespace,
                                  "families": family_reports})
    fieldnames = list(records[0].keys())
    with (args.output / "family-audit.csv").open("w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=fieldnames)
        writer.writeheader()
        writer.writerows(records)
    occurrence_fields = list(occurrence_records[0].keys())
    with (args.output / "corner-audit.csv").open(
            "w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=occurrence_fields)
        writer.writeheader()
        writer.writerows(occurrence_records)
    exact_ambiguity_groups: dict[tuple[str, str], list[dict]] = defaultdict(list)
    for occurrence in occurrence_records:
        if "/" in occurrence["exactPixelAnchorIds"]:
            exact_ambiguity_groups[(
                occurrence["namespace"],
                occurrence["exactPixelAnchorIds"],
            )].append(occurrence)
    exact_ambiguity_records: list[dict] = []
    overlap_review_queue: list[dict] = []
    for (namespace, anchor_ids), values in sorted(
            exact_ambiguity_groups.items()):
        owner_counts: dict[str, int] = defaultdict(int)
        uniform_occurrences = 0
        for occurrence in values:
            owner_counts[f"{occurrence['familyId']}:{occurrence['family']}"] += 1
            signature_ids = occurrence["signature"].split("/")
            uniform_occurrences += int(len(set(signature_ids)) == 1)
        anchor_id_set = set(anchor_ids.split("/"))
        observed_owner_ids = {
            owner.split(":", 1)[0] for owner in owner_counts}
        exact_ambiguity_records.append({
            "namespace": namespace,
            "anchorFamilyIds": anchor_ids,
            "anchorFamilies": values[0]["exactPixelAnchors"],
            "occurrences": len(values),
            "uniformOccurrences": uniform_occurrences,
            "transitionOccurrences": len(values) - uniform_occurrences,
            "allAnchorFamiliesObserved":
                observed_owner_ids == anchor_id_set,
            "owners": "; ".join(
                f"{owner}={count}" for owner, count in sorted(
                    owner_counts.items())),
            "slotsAndCorners": "/".join(
                f"{value['slot']}{value['corner']}" for value in values),
            "containsWholeTileDuplicate": any(
                value["uniformWholeTileDuplicateFamilyIds"]
                for value in values),
        })
        if uniform_occurrences:
            anchor_id_values = [int(value) for value in anchor_ids.split("/")]
            whole_tile_duplicate = any(
                value["uniformWholeTileDuplicateFamilyIds"] for value in values)
            all_observed = observed_owner_ids == anchor_id_set
            if whole_tile_duplicate:
                severity = "whole-tile-duplicate"
                priority = 0
            elif not all_observed:
                severity = "shared-anchor-with-third-party-owners"
                priority = 1
            elif len(anchor_id_values) > 2:
                severity = "multi-family-overlap"
                priority = 2
            else:
                severity = "two-family-overlap"
                priority = 3
            safe_ids = "-".join(str(value) for value in anchor_id_values)
            relative_sheet = (Path("overlap-review-contact-sheets")
                              / namespace.lower() / f"ids-{safe_ids}.png")
            overlap_review_sheet(args.output / relative_sheet, namespace,
                                 anchor_id_values, values,
                                 namespace_contexts[namespace])
            overlap_review_queue.append({
                "reviewId": f"{namespace.lower()}-ids-{safe_ids}",
                "namespace": namespace,
                "severity": severity,
                "priority": priority,
                "anchorFamilyIds": anchor_id_values,
                "anchorFamilies": [namespace_contexts[namespace]["familyById"][
                    family_id]["name"] for family_id in anchor_id_values],
                "occurrences": len(values),
                "uniformOccurrences": uniform_occurrences,
                "transitionOccurrences": len(values) - uniform_occurrences,
                "allAnchorFamiliesObserved": all_observed,
                "owners": [{
                    "familyId": int(owner.split(":", 1)[0]),
                    "family": owner.split(":", 1)[1],
                    "occurrences": count,
                } for owner, count in sorted(owner_counts.items())],
                "candidateUniformSlots": {
                    str(family_id): namespace_contexts[namespace]["uniformSlots"].get(
                        family_id, [])
                    for family_id in anchor_id_values
                },
                "focusedCorners": [{
                    "slot": value["slot"],
                    "corner": value["corner"],
                    "currentFamilyId": value["familyId"],
                    "currentFamily": value["family"],
                    "signature": [int(item) for item in value["signature"].split("/")],
                    "isUniform": len(set(value["signature"].split("/"))) == 1,
                } for value in values],
                "contactSheet": relative_sheet.as_posix(),
                "requiredAction": (
                    "Review the red-framed exact corner against every candidate "
                    "uniform family. Split or merge only with explicit visual and "
                    "structural evidence; do not auto-apply this ambiguity."),
            })
    overlap_review_queue.sort(key=lambda value: (
        value["priority"], -value["uniformOccurrences"],
        -value["occurrences"], value["namespace"], value["reviewId"]))
    overlap_components = connected_overlap_components(
        overlap_review_queue, namespace_contexts)
    cross_atlas_corroborated, cross_atlas_review = (
        cross_atlas_uniform_anchor_groups(global_uniform_anchor_records))
    cross_atlas_components = cross_atlas_review_components(cross_atlas_review)
    cross_atlas_tile_corroborated, cross_atlas_tile_review = (
        cross_atlas_uniform_anchor_groups(global_uniform_tile_records))
    cross_atlas_tile_components = cross_atlas_review_components(
        cross_atlas_tile_review)
    uniform_exact_owner_queue = uniform_exact_owner_review(
        namespace_contexts, global_uniform_anchor_records)
    for component in cross_atlas_components:
        if "name" not in component["reviewReasons"]:
            continue
        relative_sheet = (Path("cross-atlas-review-contact-sheets")
                          / f"{component['componentId']}.png")
        component["contactSheet"] = relative_sheet.as_posix()
        cross_atlas_review_sheet(
            args.output / relative_sheet, component, namespace_contexts)
    uniform_variance_queue = []
    for record in records:
        if record["uniformSlots"] < 2:
            continue
        safe_name = "".join(
            character.lower() if character.isalnum() else "-"
            for character in record["family"]).strip("-")
        uniform_variance_queue.append({
            "namespace": record["namespace"],
            "familyId": record["familyId"],
            "family": record["family"],
            "uniformSlots": record["uniformSlots"],
            "memberSlots": record["memberSlots"],
            "maxDistance": record["maxUniformVariantDistance"],
            "meanDistance": record["meanUniformVariantDistance"],
            "maxDistanceSlots": [record["maxUniformVariantSlotA"],
                                 record["maxUniformVariantSlotB"]],
            "leaveDuplicateGroupOutDisagreements":
                record["leaveDuplicateGroupOutUniformDisagreements"],
            "exactOtherFamilyAmbiguities":
                record["leaveDuplicateGroupOutExactAmbiguities"],
            "exactOverlapAlreadyQueued":
                record["leaveDuplicateGroupOutExactAmbiguities"] > 0,
            "contactSheet": (Path("uniform-contact-sheets")
                             / record["namespace"].lower()
                             / f"id-{record['familyId']}-{safe_name}.png").as_posix(),
            "requiredAction": (
                "Compare the two max-distance slots with every uniform variant. "
                "A high distance is a review priority, not proof of a split."),
        })
    uniform_variance_queue.sort(key=lambda value: (
        -value["maxDistance"], -value["meanDistance"],
        -value["uniformSlots"], value["namespace"], value["familyId"]))
    for rank, record in enumerate(uniform_variance_queue, 1):
        record["rank"] = rank
    uniform_intra_tile_queue = []
    for record in records:
        if record["uniformSlots"] < 1:
            continue
        safe_name = "".join(
            character.lower() if character.isalnum() else "-"
            for character in record["family"]).strip("-")
        uniform_intra_tile_queue.append({
            "namespace": record["namespace"],
            "familyId": record["familyId"],
            "family": record["family"],
            "uniformSlots": record["uniformSlots"],
            "memberSlots": record["memberSlots"],
            "maxDistance": record["maxUniformIntraTileCornerDistance"],
            "meanDistance": record["meanUniformIntraTileCornerDistance"],
            "maxDistanceSlot": record["maxUniformIntraTileSlot"],
            "maxDistanceCorners": [record["maxUniformIntraTileCornerA"],
                                   record["maxUniformIntraTileCornerB"]],
            "exactOverlapAlreadyQueued": bool(
                record["leaveDuplicateGroupOutExactAmbiguities"]),
            "contactSheet": (Path("uniform-contact-sheets")
                             / record["namespace"].lower()
                             / f"id-{record['familyId']}-{safe_name}.png").as_posix(),
            "requiredAction": (
                "Inspect the named corners of the highest-variance A/A/A/A tile. "
                "Visible material differences indicate a likely bad uniform "
                "signature; texture decoration alone does not authorize a split."),
        })
    uniform_intra_tile_queue.sort(key=lambda value: (
        -value["maxDistance"], -value["meanDistance"],
        value["namespace"], value["familyId"]))
    for rank, record in enumerate(uniform_intra_tile_queue, 1):
        record["rank"] = rank
    uniform_intra_tile_records.sort(key=lambda value: (
        -value["maxDistance"], -value["meanDistance"],
        value["namespace"], value["familyId"], value["slot"]))
    for rank, record in enumerate(uniform_intra_tile_records, 1):
        record["rank"] = rank
    exact_ambiguity_fields = [
        "namespace", "anchorFamilyIds", "anchorFamilies", "occurrences",
        "uniformOccurrences", "transitionOccurrences",
        "allAnchorFamiliesObserved", "owners", "slotsAndCorners",
        "containsWholeTileDuplicate"]
    with (args.output / "exact-ambiguity-groups.csv").open(
            "w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=exact_ambiguity_fields)
        writer.writeheader()
        writer.writerows(exact_ambiguity_records)
    uniform_duplicate_fields = [
        "namespace", "tileSha256", "familyIds", "families",
        "uniformSlotsByFamily", "cooccurrenceSlots", "sameContactSet",
        "contactsByFamily"]
    with (args.output / "uniform-duplicate-groups.csv").open(
            "w", newline="", encoding="utf-8-sig") as stream:
        writer = csv.DictWriter(stream, fieldnames=uniform_duplicate_fields)
        writer.writeheader()
        writer.writerows(uniform_duplicate_records)
    (args.output / "overlap-review-queue.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-overlap-review-queue-v1",
            "description": (
                "Human-review queue for exact pixel anchors owned by multiple "
                "families. Entries are evidence only and never automatic labels."),
            "groups": overlap_review_queue,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "overlap-review-components.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-overlap-components-v1",
            "description": (
                "Namespace-local connected components of exact-anchor review "
                "groups. Components prevent pairwise decisions from being "
                "interpreted in isolation."),
            "components": overlap_components,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "uniform-variance-review-queue.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-uniform-variance-review-v1",
            "description": (
                "Ranked visual-review queue for families with two or more "
                "four-corner members. Distances prioritize review and never "
                "authorize an automatic split."),
            "families": uniform_variance_queue,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "uniform-intra-tile-review-queue.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-uniform-intra-tile-review-v1",
            "description": (
                "Ranked visual-review queue for tiles whose current signature "
                "assigns one family to all four corners. It detects visibly "
                "different corners inside an A/A/A/A assignment; distances "
                "prioritize review and never authorize an automatic rewrite."),
            "families": uniform_intra_tile_queue,
            "tiles": uniform_intra_tile_records,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "uniform-exact-owner-review-queue.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-uniform-exact-owner-review-v1",
            "description": (
                "A/A/A/A tiles whose vertex-local pixels have one independent "
                "exact uniform-anchor owner with a different family name. "
                "The source tile is excluded so it cannot validate itself. "
                "Entries require complete-tile visual review and never rewrite "
                "a preset automatically."),
            "tiles": uniform_exact_owner_queue,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "archive-coverage.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-audit-archive-coverage-v1",
            "archive": str(args.archive),
            "catalogNamespaces": [
                preset["namespace"] for preset in catalog["presets"]
                if not args.namespace or preset["namespace"].lower() in {
                    value.lower() for value in args.namespace}],
            "availableNamespaces": available_namespaces,
            "auditedNamespaces": sorted(namespace_contexts,
                                        key=str.casefold),
            "reusedSignatureTableNamespaces": reused_table_namespaces,
            "skippedNamespaces": skipped_namespaces,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "cross-atlas-uniform-anchor-review.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-cross-atlas-uniform-anchors-v1",
            "description": (
                "Exact vertex-local pixels reused by uniform tiles in two or "
                "more namespaces. These are supporting evidence only: a local "
                "vertex match never authorizes a semantic rewrite. Complete-"
                "tile conflicts are reported separately."),
            "corroborated": cross_atlas_corroborated,
            "review": cross_atlas_review,
            "reviewComponents": cross_atlas_components,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "cross-atlas-uniform-tile-review.json").write_text(
        json.dumps({
            "schema": "ei-semantic-atlas-cross-atlas-uniform-tiles-v1",
            "description": (
                "Byte-identical complete uniform tiles reused by two or more "
                "namespaces. Different user-facing names are actionable "
                "cross-atlas identity conflicts; layer and underwater usage "
                "remain namespace-local context."),
            "corroborated": cross_atlas_tile_corroborated,
            "review": cross_atlas_tile_review,
            "reviewComponents": cross_atlas_tile_components,
        }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    (args.output / "family-audit.json").write_text(
        json.dumps({"schema": "ei-semantic-atlas-family-audit-v1",
                    "uniqueTables": len(reviewed_tables),
                    "exactAmbiguityGroups": exact_ambiguity_records,
                    "overlapReviewQueue": overlap_review_queue,
                    "overlapReviewComponents": overlap_components,
                    "uniformVarianceReviewQueue": uniform_variance_queue,
                    "uniformIntraTileReviewQueue": uniform_intra_tile_queue,
                    "uniformIntraTileReviewTiles": uniform_intra_tile_records,
                    "uniformExactOwnerReviewQueue": uniform_exact_owner_queue,
                    "archiveCoverage": {
                        "availableNamespaces": available_namespaces,
                        "auditedNamespaces": sorted(namespace_contexts,
                                                    key=str.casefold),
                        "reusedSignatureTableNamespaces":
                            reused_table_namespaces,
                        "skippedNamespaces": skipped_namespaces,
                    },
                    "crossAtlasUniformAnchorCorroborated":
                        cross_atlas_corroborated,
                    "crossAtlasUniformAnchorReview": cross_atlas_review,
                    "crossAtlasUniformAnchorReviewComponents":
                        cross_atlas_components,
                    "crossAtlasUniformTileCorroborated":
                        cross_atlas_tile_corroborated,
                    "crossAtlasUniformTileReview": cross_atlas_tile_review,
                    "crossAtlasUniformTileReviewComponents":
                        cross_atlas_tile_components,
                    "uniformDuplicateGroups": uniform_duplicate_records,
                    "namespaces": namespace_reports}, indent=2,
                   ensure_ascii=False) + "\n", encoding="utf-8")
    print(json.dumps({"uniqueTables": len(reviewed_tables),
                      "availableNamespaces": len(available_namespaces),
                      "auditedNamespaces": len(namespace_contexts),
                      "reusedTableNamespaces":
                          len(reused_table_namespaces),
                      "skippedNamespaces": len(skipped_namespaces),
                      "families": len(records),
                      "contactSheets": len(list((args.output / 'contact-sheets').rglob('*.png'))),
                      "overlapReviewGroups": len(overlap_review_queue),
                      "overlapReviewComponents": len(overlap_components),
                      "uniformVarianceFamilies": len(uniform_variance_queue),
                      "uniformExactOwnerTiles": len(uniform_exact_owner_queue),
                      "uniformIntraTileFamilies": len(uniform_intra_tile_queue),
                      "crossAtlasUniformCorroborated":
                          len(cross_atlas_corroborated),
                      "crossAtlasUniformReview": len(cross_atlas_review),
                      "crossAtlasUniformReviewComponents":
                          len(cross_atlas_components),
                      "crossAtlasUniformTileCorroborated":
                          len(cross_atlas_tile_corroborated),
                      "crossAtlasUniformTileReview":
                          len(cross_atlas_tile_review),
                      "crossAtlasUniformTileReviewComponents":
                          len(cross_atlas_tile_components)},
                     sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
