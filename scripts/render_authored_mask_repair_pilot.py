#!/usr/bin/env python3
"""Render a small review pilot for conservative authored mask repair.

The input is the pre-R42 stock-derived review index.  The renderer deliberately
does not call the canonical geometric fallback: one real authored mask is
selected per family/topology, repaired within a strict change budget, and then
shown beside its source and several concrete material outcomes.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import re
from collections import defaultdict, deque
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter

from render_blend_mask_questionnaire import interaction_script


TILE_SIZE = 256
CORNER_NAMES = ("NW", "NE", "SE", "SW")
PILOT_CASES = (
    ("grass-strands", "outer-1of4-v1"),
    ("grass-strands", "edge-2of4-v1"),
    ("grass-strands", "diagonal-2of4"),
    ("soil-mottle", "outer-1of4-v2"),
    ("soil-mottle", "edge-2of4-v2"),
    ("soil-mottle", "inside-3of4-v1"),
    ("stone-fracture", "outer-1of4-v1"),
    ("stone-fracture", "edge-2of4-v1"),
    ("stone-fracture", "diagonal-2of4"),
    ("sand-grain", "outer-1of4-v1"),
    ("sand-grain", "edge-2of4-v2"),
    ("sand-grain", "inside-3of4-v2"),
)
SAND_PILOT_CASES = tuple(
    item for item in PILOT_CASES if item[0] == "sand-grain")

# One unresolved baseline per hard-material family and corner topology.  Do not
# repeat masks already approved in R44/R48, and do not send Sand, Snow, Ice, or
# Water through this route: those continuous/soft families require the detailed
# opacity workflow (or an explicit A/B review) instead.
REMAINING_HARD_BASELINE_CASES = (
    ("grass-strands", "inside-3of4-v1"),
    ("stone-fracture", "inside-3of4-v1"),
    ("gravel-chunks", "outer-1of4-v1"),
    ("gravel-chunks", "edge-2of4-v1"),
    ("gravel-chunks", "diagonal-2of4"),
    ("gravel-chunks", "inside-3of4-v1"),
    ("paving-courses", "outer-1of4-v1"),
    ("paving-courses", "edge-2of4-v1"),
    ("paving-courses", "diagonal-2of4"),
    ("paving-courses", "inside-3of4-v1"),
)

# Both genuine Soil diagonal sources are near-solid ownership fields and fail
# the user's protected-quarter rule. They are recorded as a source gap instead
# of asking the reviewer to approve an obviously invalid mask.
HARD_BASELINE_SOURCE_GAPS = (
    ("soil-mottle", "diagonal-2of4"),
)


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def _sha256_bytes(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _image_hash(image: Image.Image) -> str:
    return _sha256_bytes(image.tobytes())


@lru_cache(maxsize=16)
def _sheet(path_text: str) -> Image.Image:
    with Image.open(path_text) as image:
        image.load()
        return image.copy()


def _cell(review: Path, file_name: str, row: int, column: int,
          mode: str = "RGB") -> Image.Image:
    source = _sheet(str((review / file_name).resolve()))
    if (source.width % TILE_SIZE or source.height % TILE_SIZE or
            (column + 1) * TILE_SIZE > source.width or
            (row + 1) * TILE_SIZE > source.height):
        raise ValueError(
            f"invalid review cell {file_name}:{row},{column} in {source.size}")
    return source.crop((column * TILE_SIZE, row * TILE_SIZE,
                        (column + 1) * TILE_SIZE,
                        (row + 1) * TILE_SIZE)).convert(mode)


def expected_owner_corners(topology: str) -> tuple[bool, bool, bool, bool]:
    if topology.startswith("outer-1of4"):
        return True, False, False, False
    if topology.startswith("edge-2of4"):
        return True, True, False, False
    if topology == "diagonal-2of4":
        return True, False, True, False
    if topology.startswith("inside-3of4"):
        return True, True, True, False
    raise ValueError(f"unsupported pilot topology: {topology}")


def _quadrants(height: int, width: int):
    half_y, half_x = height // 2, width // 2
    return (
        (slice(0, half_y), slice(0, half_x)),
        (slice(0, half_y), slice(half_x, width)),
        (slice(half_y, height), slice(half_x, width)),
        (slice(half_y, height), slice(0, half_x)),
    )


def _corner_patch(array: np.ndarray, corner: int, size: int) -> np.ndarray:
    if corner == 0:
        return array[:size, :size]
    if corner == 1:
        return array[:size, -size:]
    if corner == 2:
        return array[-size:, -size:]
    return array[-size:, :size]


def mask_metrics(source: Image.Image, repaired: Image.Image,
                 topology: str) -> dict:
    raw = np.asarray(source.convert("L"), dtype=np.float32) / 255.0
    result = np.asarray(repaired.convert("L"), dtype=np.float32) / 255.0
    if raw.shape != result.shape:
        raise ValueError("source and repaired mask sizes differ")
    expected = expected_owner_corners(topology)
    raw_strong = raw >= 0.5
    result_strong = result >= 0.5
    intersection = int(np.logical_and(raw_strong, result_strong).sum())
    union = int(np.logical_or(raw_strong, result_strong).sum())
    quarter_strong: dict[str, float] = {}
    protected_presence: dict[str, float] = {}
    for index, area in enumerate(_quadrants(*result.shape)):
        values = result[area]
        if expected[index]:
            quarter_strong[CORNER_NAMES[index]] = round(
                float((values >= 0.8).mean()), 4)
        else:
            protected_presence[CORNER_NAMES[index]] = round(
                float((values >= 0.2).mean()), 4)
    patch_size = max(4, min(result.shape) // 16)
    corner_means = {
        CORNER_NAMES[index]: round(
            float(_corner_patch(result, index, patch_size).mean()), 4)
        for index in range(4)
    }
    structural = (
        all(value >= 0.70 for value in quarter_strong.values()) and
        all(value <= 0.30 for value in protected_presence.values()) and
        all((corner_means[CORNER_NAMES[index]] >= 0.80 if owned else
             corner_means[CORNER_NAMES[index]] <= 0.20)
            for index, owned in enumerate(expected))
    )
    return {
        "expectedOwnerCorners": [
            CORNER_NAMES[index] for index, value in enumerate(expected) if value],
        "strongIoU": round(intersection / max(1, union), 4),
        "changedStrongFraction": round(
            float(np.not_equal(raw_strong, result_strong).mean()), 4),
        "meanAbsoluteChange": round(float(np.abs(raw - result).mean()), 4),
        "affectedQuarterStrongCoverage": quarter_strong,
        "protectedQuarterOwnerPresence": protected_presence,
        "cornerPatchMeans": corner_means,
        "structuralTargetMet": structural,
    }


def _gaussian(array: np.ndarray, radius: float) -> np.ndarray:
    image = Image.fromarray(
        np.clip(np.rint(array * 255.0), 0, 255).astype(np.uint8), "L")
    return (np.asarray(image.filter(ImageFilter.GaussianBlur(radius)),
                       dtype=np.float32) / 255.0)


def _sigmoid(value: np.ndarray) -> np.ndarray:
    clipped = np.clip(value, -12.0, 12.0)
    return 1.0 / (1.0 + np.exp(-clipped))


def _coherent_noise(height: int, width: int, seed: int) -> np.ndarray:
    rng = np.random.default_rng(seed)
    layers = []
    for cells, weight in ((7, 0.68), (15, 0.32)):
        small = rng.normal(0.0, 1.0, (cells, cells)).astype(np.float32)
        image = Image.fromarray(small, "F").resize(
            (width, height), Image.Resampling.BICUBIC)
        layers.append(np.asarray(image, dtype=np.float32) * weight)
    result = layers[0] + layers[1]
    result -= float(result.mean())
    deviation = float(result.std())
    return result / deviation if deviation > 1e-6 else result


def _anchor_adjust(probability: np.ndarray,
                   expected: tuple[bool, bool, bool, bool]) -> np.ndarray:
    height, width = probability.shape
    yy, xx = np.mgrid[0:height, 0:width]
    corners = ((0, 0), (width - 1, 0),
               (width - 1, height - 1), (0, height - 1))
    radius = 0.22 * min(height, width)
    result = probability.copy()
    for index, (corner_x, corner_y) in enumerate(corners):
        distance = np.sqrt((xx - corner_x) ** 2 + (yy - corner_y) ** 2)
        influence = np.clip(1.0 - distance / radius, 0.0, 1.0) ** 1.35
        if expected[index]:
            result += (1.0 - result) * influence
        else:
            result *= 1.0 - influence
    return np.clip(result, 0.0, 1.0)


def _enforce_exact_corners(array: np.ndarray,
                           expected: tuple[bool, bool, bool, bool]) -> None:
    points = ((0, 0), (0, array.shape[1] - 1),
              (array.shape[0] - 1, array.shape[1] - 1),
              (array.shape[0] - 1, 0))
    for index, point in enumerate(points):
        array[point] = 1.0 if expected[index] else 0.0


def repair_authored_mask(source: Image.Image, topology: str,
                         seed: int) -> tuple[Image.Image, dict]:
    """Repair an authored mask while retaining its macro silhouette.

    Coverage and corner requirements influence the existing probability field;
    they never define a replacement shape.  The final interpolation is capped
    at 22% strong-pixel change and 18% mean alpha change.
    """
    raw = np.asarray(source.convert("L"), dtype=np.float32) / 255.0
    if raw.ndim != 2 or raw.shape[0] != raw.shape[1] or raw.shape[0] < 16:
        raise ValueError("pilot masks must be square grayscale images")
    height, width = raw.shape
    expected = expected_owner_corners(topology)

    fine = _gaussian(raw, 1.7)
    coarse = _gaussian(raw, 5.2)
    yy, xx = np.mgrid[0:height, 0:width]
    edge_distance = np.minimum.reduce(
        (xx, width - 1 - xx, yy, height - 1 - yy)).astype(np.float32)
    inward = np.clip(edge_distance / (0.34 * min(height, width)), 0.0, 1.0)
    smoothed = 0.14 * raw + 0.86 * (
        fine * (1.0 - 0.62 * inward) + coarse * 0.62 * inward)
    logits = np.log(np.clip(smoothed, 0.01, 0.99) /
                    np.clip(1.0 - smoothed, 0.01, 0.99))

    u = xx.astype(np.float32) / max(1, width - 1)
    v = yy.astype(np.float32) / max(1, height - 1)
    bases = ((1.0 - u) * (1.0 - v), u * (1.0 - v),
             u * v, (1.0 - u) * v)
    quadrants = _quadrants(height, width)
    # Smooth corner-basis biases nudge the inherited silhouette toward the
    # requested quarter coverage. They cannot introduce a standalone lobe.
    for _ in range(7):
        probability = _sigmoid(logits)
        for index, area in enumerate(quadrants):
            values = probability[area]
            if expected[index]:
                deficit = 0.70 - float((values >= 0.8).mean())
                if deficit > 0.0:
                    logits += min(0.42, 1.15 * deficit) * bases[index] * 2.4
            else:
                excess = float((values >= 0.2).mean()) - 0.30
                if excess > 0.0:
                    logits -= min(0.42, 1.15 * excess) * bases[index] * 2.4

    probability = _anchor_adjust(_sigmoid(logits), expected)
    owner_points = [
        (0, 0), (width - 1, 0),
        (width - 1, height - 1), (0, height - 1)]
    owner_points = [owner_points[index]
                    for index, value in enumerate(expected) if value]
    distances = [np.sqrt((xx - x) ** 2 + (yy - y) ** 2)
                 for x, y in owner_points]
    from_origin = np.minimum.reduce(distances)
    origin_ramp = np.clip(
        from_origin / (0.62 * math.sqrt(width * width + height * height)),
        0.0, 1.0)
    wall_envelope = np.sin(math.pi * u) * np.sin(math.pi * v)
    edge_band = 4.0 * probability * (1.0 - probability)
    probability += (_coherent_noise(height, width, seed) * 0.085 *
                    origin_ramp * wall_envelope * edge_band)
    probability = np.clip(probability, 0.0, 1.0)

    selected = None
    for amount in np.linspace(1.0, 0.05, 20):
        candidate = raw * (1.0 - amount) + probability * amount
        candidate = _anchor_adjust(candidate, expected)
        _enforce_exact_corners(candidate, expected)
        image = Image.fromarray(
            np.clip(np.rint(candidate * 255.0), 0, 255).astype(np.uint8), "L")
        metrics = mask_metrics(source, image, topology)
        if (metrics["changedStrongFraction"] <= 0.22 and
                metrics["meanAbsoluteChange"] <= 0.18):
            selected = image, metrics
            break
    if selected is None:
        candidate = _anchor_adjust(raw, expected)
        _enforce_exact_corners(candidate, expected)
        image = Image.fromarray(
            np.clip(np.rint(candidate * 255.0), 0, 255).astype(np.uint8), "L")
        selected = image, mask_metrics(source, image, topology)
    return selected


def _manhattan_distance_to(seeds: np.ndarray) -> np.ndarray:
    """Return a faceted distance field without rounding authored corners."""
    if seeds.ndim != 2:
        raise ValueError("distance seeds must be a two-dimensional mask")
    height, width = seeds.shape
    unreachable = height + width + 1
    distance = np.full((height, width), unreachable, dtype=np.int32)
    pending: deque[tuple[int, int]] = deque()
    for y, x in np.argwhere(seeds):
        distance[y, x] = 0
        pending.append((int(y), int(x)))
    if not pending:
        return distance
    while pending:
        y, x = pending.popleft()
        next_distance = distance[y, x] + 1
        for next_y, next_x in ((y - 1, x), (y + 1, x),
                               (y, x - 1), (y, x + 1)):
            if (0 <= next_y < height and 0 <= next_x < width and
                    next_distance < distance[next_y, next_x]):
                distance[next_y, next_x] = next_distance
                pending.append((next_y, next_x))
    return distance


def repair_sand_banded_mask(source: Image.Image, topology: str,
                            seed: int) -> tuple[Image.Image, dict]:
    """Rebuild sand depth as nested opacity bands around the authored shape.

    Sand is intentionally not sent through the globally blurred probability
    repair. Its >=128 silhouette remains pixel-exact, including the authored
    star/crystal facets. One subtle exterior band and two interior shoulders
    provide the reviewed three-step depth cue before the opaque core.
    Manhattan distance keeps offsets faceted instead of rounding the points.
    """
    del seed  # Reserved for a future reviewed band-variation contract.
    raw = np.asarray(source.convert("L"), dtype=np.uint8)
    if raw.ndim != 2 or raw.shape[0] != raw.shape[1] or raw.shape[0] < 16:
        raise ValueError("pilot masks must be square grayscale images")
    owner = raw >= 128
    outside_distance = _manhattan_distance_to(owner)
    inside_distance = _manhattan_distance_to(~owner)
    band_width = max(2, min(raw.shape) // 64)

    result = np.zeros(raw.shape, dtype=np.uint8)
    # A single subtle exterior ledge stays below the protected-quarter 20%
    # influence threshold. Two interior shoulders then create the visible
    # stepped depth without expanding the authored ownership silhouette.
    result[np.logical_and(~owner,
                          outside_distance <= band_width)] = 48
    result[owner] = 255
    result[np.logical_and(owner, inside_distance <= band_width)] = 216
    result[np.logical_and(owner,
                          inside_distance <= max(1, band_width // 2))] = 144

    expected = expected_owner_corners(topology)
    points = ((0, 0), (0, result.shape[1] - 1),
              (result.shape[0] - 1, result.shape[1] - 1),
              (result.shape[0] - 1, 0))
    for index, point in enumerate(points):
        result[point] = 255 if expected[index] else 0

    image = Image.fromarray(result, "L")
    metrics = mask_metrics(source, image, topology)
    metrics["repairMethod"] = "faceted-opacity-bands"
    metrics["bandWidthPixels"] = band_width
    metrics["opacityLevels"] = [0, 48, 144, 216, 255]
    metrics["distinctEdgeBands"] = sum(
        int(np.any(result == value)) for value in (48, 144, 216))
    metrics["strongSilhouettePixelExact"] = bool(np.array_equal(
        raw >= 128, result >= 128))
    ownership_coverage = {}
    protected_ownership = {}
    for index, area in enumerate(_quadrants(*owner.shape)):
        value = round(float(owner[area].mean()), 4)
        if expected[index]:
            ownership_coverage[CORNER_NAMES[index]] = value
        else:
            protected_ownership[CORNER_NAMES[index]] = value
    metrics["ownershipQuarterCoverage"] = ownership_coverage
    metrics["protectedQuarterOwnership"] = protected_ownership
    metrics["ownershipTargetMet"] = (
        all(value >= 0.70 for value in ownership_coverage.values()) and
        all(value <= 0.30 for value in protected_ownership.values()))
    return image, metrics


def repair_reviewed_mask(source: Image.Image, family: str, topology: str,
                         seed: int) -> tuple[Image.Image, dict]:
    """Apply only the methods authorized by the explicit R44 review."""
    if family == "sand-grain":
        return repair_sand_banded_mask(source, topology, seed)
    return repair_authored_mask(source, topology, seed)


def band_visualization(mask: Image.Image) -> Image.Image:
    """Color the discrete sand opacity plateaus for questionnaire review."""
    values = np.asarray(mask.convert("L"), dtype=np.uint8)
    result = np.empty((*values.shape, 3), dtype=np.uint8)
    result[values == 0] = (30, 42, 67)
    result[np.logical_and(values > 0, values < 96)] = (58, 126, 164)
    result[np.logical_and(values >= 96, values < 180)] = (75, 182, 170)
    result[np.logical_and(values >= 180, values < 240)] = (238, 166, 65)
    result[values >= 240] = (248, 242, 218)
    return Image.fromarray(result, "RGB")


def _source_quality(mask: Image.Image, topology: str) -> float:
    metrics = mask_metrics(mask, mask, topology)
    expected = expected_owner_corners(topology)
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    score = 0.0
    for value in metrics["affectedQuarterStrongCoverage"].values():
        score += max(0.0, 0.70 - value) * 8.0
    for value in metrics["protectedQuarterOwnerPresence"].values():
        score += max(0.0, value - 0.30) * 8.0
    for index, owned in enumerate(expected):
        mean = metrics["cornerPatchMeans"][CORNER_NAMES[index]]
        score += (max(0.0, 0.80 - mean) if owned
                  else max(0.0, mean - 0.20)) * 8.0
    blurred = _gaussian(values, 4.0)
    score += float(np.abs(values - blurred).mean()) * 2.0
    expected_area = sum(expected) / 4.0
    score += abs(float((values >= 0.5).mean()) - expected_area)
    return score


def align_authored_mask(mask: Image.Image, topology: str) -> tuple[Image.Image, dict]:
    """Quarter-turn the requested owner corners into the canonical layout."""
    expected = expected_owner_corners(topology)
    candidates = []
    for turns in range(4):
        rotated = mask.convert("L").rotate(90 * turns)
        points = ((0, 0), (rotated.width - 1, 0),
                  (rotated.width - 1, rotated.height - 1),
                  (0, rotated.height - 1))
        actual = tuple(rotated.getpixel(point) >= 128 for point in points)
        if actual == expected:
            candidates.append((
                round(_source_quality(rotated, topology), 6), turns, rotated))
    if not candidates:
        raise ValueError(
            f"authored mask corners do not match {topology} under rotation")
    score, turns, aligned = min(
        candidates, key=lambda value: (value[0], value[1]))
    return aligned, {
        "rotationCounterClockwise": turns * 90,
        "inverted": False,
        "alignmentScore": score,
    }


def _save(image: Image.Image, path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    return {
        "file": f"images/{path.name}",
        "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "pixelSha256": _image_hash(image),
        "size": list(image.size),
    }


def _difference_image(raw: Image.Image, repaired: Image.Image) -> Image.Image:
    before = np.asarray(raw.convert("L"), dtype=np.int16)
    after = np.asarray(repaired.convert("L"), dtype=np.int16)
    delta = after - before
    base = np.repeat((before[..., None] * 0.34 + 40).astype(np.uint8), 3, axis=2)
    added = np.clip(delta, 0, 255).astype(np.uint8)
    removed = np.clip(-delta, 0, 255).astype(np.uint8)
    base[..., 0] = np.maximum(base[..., 0], removed)
    base[..., 1] = np.maximum(base[..., 1], added)
    base[..., 2] = np.maximum(base[..., 2], removed)
    return Image.fromarray(base, "RGB")


def _heatmap(mask: Image.Image) -> Image.Image:
    values = np.asarray(mask.convert("L"), dtype=np.uint8)
    result = np.empty((*values.shape, 3), dtype=np.uint8)
    result[values >= 204] = (42, 184, 92)
    result[values <= 51] = (35, 52, 96)
    mixed = np.logical_and(values > 51, values < 204)
    result[mixed] = (236, 154, 52)
    return Image.fromarray(result, "RGB")


def _representatives(rows: list[dict[str, str]], limit: int = 3):
    ordered = sorted(rows, key=lambda row: (
        row["support_scenario"], row["owner_material"].casefold(),
        row["support_material"].casefold(), row["provenance"]))
    selected = []
    seen = set()
    for scenario in ("cross-style", "same-style"):
        for row in ordered:
            key = (row["owner_material_id"], row["support_material_id"])
            if row["support_scenario"] == scenario and key not in seen:
                selected.append(row)
                seen.add(key)
                break
    for row in ordered:
        key = (row["owner_material_id"], row["support_material_id"])
        if key not in seen:
            selected.append(row)
            seen.add(key)
        if len(selected) >= limit:
            break
    return selected[:limit]


def _topology_label(topology: str) -> str:
    return topology.replace("-", " ").replace("of4", "of 4")


def render(review: Path, output: Path,
           checkpoint: str = "R44-AUTHORED-PILOT",
           cases: tuple[tuple[str, str], ...] = PILOT_CASES,
           reviewed_methods: bool = False) -> dict:
    review = review.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"pilot output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    required = {
        "sheet", "row", "column", "owner_material_id", "owner_material",
        "owner_style", "support_scenario", "support_material_id",
        "support_material", "topology", "variation", "provenance",
        "mask_sha256", "source_example_sheet", "owner_uniform",
        "support_uniform",
    }
    if not rows or required.difference(rows[0]):
        raise ValueError(
            f"authored review index is missing {sorted(required.difference(rows[0] if rows else {}))}")
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["owner_style"], row["topology"])].append(row)

    questions = []
    cards = []
    for number, logical in enumerate(cases, 1):
        group_rows = grouped.get(logical, [])
        if not group_rows:
            raise ValueError(f"pilot group is absent from authored evidence: {logical}")
        unique_candidates = {}
        for row in group_rows:
            raw_mask = _cell(review, row["sheet"], int(row["row"]),
                             int(row["column"]), "L")
            raw_hash = _image_hash(raw_mask)
            if raw_hash.lower() != row["mask_sha256"].lower():
                raise ValueError(
                    f"authored mask hash mismatch for {row['provenance']}")
            aligned_mask, alignment = align_authored_mask(raw_mask, logical[1])
            candidate_seed = int(_sha256_bytes(
                f"{logical[0]}|{logical[1]}|{row['provenance']}".encode()
            )[:8], 16)
            _, candidate_metrics = (repair_reviewed_mask(
                aligned_mask, logical[0], logical[1], candidate_seed)
                if reviewed_methods else repair_authored_mask(
                    aligned_mask, logical[1], candidate_seed))
            gate_failures = sum((
                candidate_metrics.get("strongIoU", 0.0) < 0.68,
                candidate_metrics.get("changedStrongFraction", 1.0) > 0.22,
                candidate_metrics.get("meanAbsoluteChange", 1.0) > 0.18,
            ))
            candidate = (gate_failures,
                         round(_source_quality(aligned_mask, logical[1]), 6),
                         row["provenance"], row, raw_mask, aligned_mask,
                         alignment)
            previous = unique_candidates.get(raw_hash)
            if previous is None or candidate[:3] < previous[:3]:
                unique_candidates[raw_hash] = candidate
        (_, selected_score, _, selected_row, extracted_mask, aligned_mask,
          alignment) = min(
            unique_candidates.values(), key=lambda value: value[:3])
        seed = int(_sha256_bytes(
            f"{logical[0]}|{logical[1]}|{selected_row['provenance']}".encode())[:8], 16)
        repaired, metrics = (repair_reviewed_mask(
            aligned_mask, logical[0], logical[1], seed)
            if reviewed_methods else
            repair_authored_mask(aligned_mask, logical[1], seed))
        key = f"authored-repair-{_slug(logical[0])}-{_slug(logical[1])}"
        prefix = f"{number:02d}-{_slug(logical[0])}-{_slug(logical[1])}"
        source_tile = _cell(
            review, selected_row["source_example_sheet"],
            int(selected_row["row"]), int(selected_row["column"]), "RGB")
        files = {
            "authoredSource": _save(source_tile, images / f"{prefix}-source.png"),
            "rawMask": _save(
                extracted_mask, images / f"{prefix}-raw-mask.png"),
            "alignedMask": _save(
                aligned_mask, images / f"{prefix}-aligned-mask.png"),
            "repairedMask": _save(
                repaired, images / f"{prefix}-repaired-mask.png"),
            "difference": _save(
                _difference_image(aligned_mask, repaired),
                images / f"{prefix}-difference.png"),
            "contractHeatmap": _save(
                _heatmap(repaired), images / f"{prefix}-heatmap.png"),
        }
        if metrics.get("repairMethod") == "faceted-opacity-bands":
            files["bandVisualization"] = _save(
                band_visualization(repaired),
                images / f"{prefix}-opacity-bands.png")
        examples = []
        examples_html = []
        for example_number, row in enumerate(_representatives(group_rows), 1):
            owner = Image.open(review / row["owner_uniform"]).convert("RGB")
            support = Image.open(review / row["support_uniform"]).convert("RGB")
            raw_result = Image.composite(owner, support, aligned_mask)
            repaired_result = Image.composite(owner, support, repaired)
            example_prefix = f"{prefix}-example-{example_number}"
            example_files = {
                "owner": _save(owner, images / f"{example_prefix}-owner.png"),
                "rawResult": _save(
                    raw_result, images / f"{example_prefix}-raw-result.png"),
                "repairedResult": _save(
                    repaired_result,
                    images / f"{example_prefix}-repaired-result.png"),
                "support": _save(
                    support, images / f"{example_prefix}-support.png"),
            }
            examples.append({
                "ownerMaterial": row["owner_material"],
                "supportMaterial": row["support_material"],
                "scenario": row["support_scenario"],
                "files": example_files,
            })
            examples_html.append(f'''<section class="example">
<h3>{html.escape(row["owner_material"])} + {html.escape(row["support_material"])}</h3>
<p class="muted">{html.escape(row["support_scenario"])}</p><div class="example-grid">
<figure><img src="{example_files['owner']['file']}" alt="Requested uniform"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{example_files['rawResult']['file']}" alt="Aligned authored-mask result"><figcaption>Aligned authored-mask result</figcaption></figure>
<figure><img src="{example_files['repairedResult']['file']}" alt="Repaired authored-mask result"><figcaption><strong>Proposed repair result</strong></figcaption></figure>
<figure><img src="{example_files['support']['file']}" alt="Support uniform"><figcaption>Support uniform</figcaption></figure>
</div></section>''')
        metric_text = (
            f"Authored IoU {metrics['strongIoU']:.1%} · changed strong pixels "
            f"{metrics['changedStrongFraction']:.1%} · mean alpha change "
            f"{metrics['meanAbsoluteChange']:.1%} · structural target "
            f"{'met' if metrics['structuralTargetMet'] else 'not fully met'}")
        if metrics.get("repairMethod") == "faceted-opacity-bands":
            metric_text += (
                f" | {metrics['distinctEdgeBands']} explicit edge bands | "
                f"{metrics['bandWidthPixels']} px per band | strong silhouette "
                f"{'pixel-exact' if metrics['strongSilhouettePixelExact'] else 'changed'} | "
                f"70/30 ownership target "
                f"{'met' if metrics['ownershipTargetMet'] else 'not fully met by authored shape'}")
        owner_corner_text = ", ".join(metrics["expectedOwnerCorners"])
        answer_options = (
            "Still uncertain",
            "Approve repaired authored mask",
            "Prefer raw authored mask",
            "Repair is too strong",
            "Repair is too weak",
            "Choose a different authored source",
            "Reject this source/topology",
        )
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in answer_options)
        band_figure = ""
        if "bandVisualization" in files:
            band_figure = f'''<figure><img src="{files['bandVisualization']['file']}" alt="Separated opacity bands"><figcaption><strong>Opacity bands</strong><br>blue outer | teal inner | amber shoulder | ivory core</figcaption></figure>'''
        repair_caption = ("Band-preserving faceted repair" if
                          metrics.get("repairMethod") == "faceted-opacity-bands"
                          else "Conservatively repaired authored mask")
        cards.append(f'''<article class="pilot-question" data-key="{key}">
<h2>{number}. {html.escape(logical[0])} · {html.escape(_topology_label(logical[1]))}</h2>
<p class="muted">Owner corner assignment: <strong>{html.escape(owner_corner_text)}</strong> · selected authored source {html.escape(selected_row['provenance'])} from {len(unique_candidates)} distinct candidates · legal alignment {alignment['rotationCounterClockwise']}° CCW{' + polarity inversion' if alignment['inverted'] else ''} · conservative source score {selected_score:.3f}</p>
<p class="metric">{html.escape(metric_text)}</p>
<div class="evidence-grid">
<figure><img src="{files['authoredSource']['file']}" alt="Authored transition tile"><figcaption>Authored transition tile<br><strong>{html.escape(selected_row['provenance'])}</strong></figcaption></figure>
<figure><img src="{files['rawMask']['file']}" alt="Raw extracted authored mask"><figcaption>Raw extracted mask</figcaption></figure>
<figure><img src="{files['alignedMask']['file']}" alt="Legally aligned authored mask"><figcaption>Aligned authored mask<br><strong>{alignment['rotationCounterClockwise']}° CCW{' + inverted' if alignment['inverted'] else ''}</strong></figcaption></figure>
<figure><img src="{files['repairedMask']['file']}" alt="{html.escape(repair_caption)}"><figcaption><strong>{html.escape(repair_caption)}</strong></figcaption></figure>
{band_figure}
<figure><img src="{files['difference']['file']}" alt="Difference heatmap"><figcaption>Difference · green added · magenta removed</figcaption></figure>
<figure><img src="{files['contractHeatmap']['file']}" alt="Coverage heatmap"><figcaption>Contract · green strong owner · navy support · orange feather</figcaption></figure>
</div><div class="examples">{''.join(examples_html)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional source or repair detail"></label></div>
</article>''')
        questions.append({
            "key": key,
            "maskFamily": logical[0],
            "topology": logical[1],
            "sourceKind": "authored-stock-mask",
            "selectedProvenance": selected_row["provenance"],
            "alignment": alignment,
            "candidateCount": len(unique_candidates),
            "sourceScore": selected_score,
            "seed": seed,
            "repairMethod": metrics.get("repairMethod", "single-field-authored"),
            "files": files,
            "metrics": metrics,
            "examples": examples,
        })

    script = interaction_script(checkpoint)
    structural_count = sum(
        bool(question["metrics"]["structuralTargetMet"])
        for question in questions)
    sand_only = bool(cases) and all(item[0] == "sand-grain" for item in cases)
    warning_text = ("This is a three-case sand depth pilot, not runtime behavior. "
        "Each proposal starts from the legally aligned authored silhouette, "
        "keeps its strong faceted boundary pixel-exact, and reconstructs three "
        "separate opacity edge bands. No global blur or geometric replacement is used."
        if sand_only else
        "This is a 12-case pilot, not runtime behavior. Every proposal starts "
        "from a real pre-R42 authored JMV mask. The geometric R42 fallback is not used.")
    summary_text = (f"Each of the {len(questions)} sand questions shows the authored "
        "transition, extracted and aligned silhouette, separated opacity bands, "
        "the band-preserving mask, a difference map, and three full material-pair outcomes."
        if sand_only else
        f"Each question shows the selected authored transition, its extracted mask, "
        f"the legally aligned mask, the conservative repair, a difference map, a "
        f"contract heatmap, and three full material-pair outcomes. {structural_count} "
        f"of {len(questions)} proposals meet every 70/30 structural target.")
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} authored repair pilot</title>
<style>
:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}
*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px;margin:0}}.muted{{color:var(--muted)}}.warning{{padding:12px;border:1px solid #d29922;background:var(--panel)}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:320px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.metric{{font:600 13px/1.4 Consolas,monospace}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(5,minmax(150px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(100px,256px));gap:8px}}.export-panel{{margin:14px 0}}#answer-export-text{{width:100%;min-height:160px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:1100px){{.evidence-grid{{grid-template-columns:repeat(3,minmax(150px,256px))}}.example-grid{{grid-template-columns:repeat(2,minmax(100px,256px))}}}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid{{grid-template-columns:1fr}}.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}
</style></head><body><h1>Blend-mask {html.escape(checkpoint)} · authored repair pilot</h1>
<p class="warning"><strong>{html.escape(warning_text)}</strong></p>
<p class="muted">{html.escape(summary_text)} “Still uncertain” is the safe default.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section>
<main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_name = f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path = output / html_name
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-authored-mask-repair-pilot-v1",
        "checkpoint": checkpoint,
        "sourceReview": f"{review.parent.name}/{review.name}",
        "runtimeChanged": False,
        "reviewedMethods": reviewed_methods,
        "questionCount": len(questions),
        "questions": questions,
    }
    (output / "authored-mask-repair-analysis.json").write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    validation = validate_output(output)
    return {
        "html": str(html_path),
        "analysis": str(output / "authored-mask-repair-analysis.json"),
        **validation,
    }


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads(
        (output / "authored-mask-repair-analysis.json").read_text(
            encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError(f"expected one pilot HTML file, found {len(html_files)}")
    document = html_files[0].read_text(encoding="utf-8")
    errors = []
    questions = analysis.get("questions", [])
    if not questions or analysis.get("questionCount") != len(questions):
        errors.append("pilot question count is empty or inconsistent")
    if document.count('class="pilot-question"') != len(questions):
        errors.append("HTML question count differs from analysis")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    for reference in references:
        if (reference.startswith(("/", "\\")) or ":" in reference or
                not (output / reference).is_file()):
            errors.append(f"missing or non-relocatable image: {reference}")
    verified_images = 0
    for question in questions:
        if question.get("sourceKind") != "authored-stock-mask":
            errors.append(f"{question.get('key')} is not authored-source based")
        if "canonical-generated" in question.get("selectedProvenance", ""):
            errors.append(f"{question.get('key')} uses rejected geometry provenance")
        metrics = question.get("metrics", {})
        if metrics.get("strongIoU", 0.0) < 0.68:
            errors.append(f"{question.get('key')} lost its authored silhouette")
        if metrics.get("changedStrongFraction", 1.0) > 0.22:
            errors.append(f"{question.get('key')} exceeds strong change budget")
        if metrics.get("meanAbsoluteChange", 1.0) > 0.18:
            errors.append(f"{question.get('key')} exceeds alpha change budget")
        if question.get("repairMethod") == "faceted-opacity-bands":
            if not metrics.get("strongSilhouettePixelExact"):
                errors.append(
                    f"{question.get('key')} changed its faceted strong silhouette")
            if metrics.get("distinctEdgeBands") != 3:
                errors.append(
                    f"{question.get('key')} does not expose three opacity bands")
            if "bandVisualization" not in question.get("files", {}):
                errors.append(
                    f"{question.get('key')} lacks a band visualization")
        if len(question.get("examples", [])) != 3:
            errors.append(f"{question.get('key')} does not show three outcomes")
        file_entries = list(question.get("files", {}).values())
        for example in question.get("examples", []):
            file_entries.extend(example.get("files", {}).values())
        for entry in file_entries:
            path = output / entry["file"]
            if not path.is_file():
                errors.append(f"missing indexed image: {entry['file']}")
                continue
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                errors.append(f"file hash mismatch: {entry['file']}")
            verified_images += 1
        try:
            aligned = Image.open(
                output / question["files"]["alignedMask"]["file"]).convert("L")
            repaired = Image.open(
                output / question["files"]["repairedMask"]["file"]).convert("L")
            for example in question.get("examples", []):
                example_files = example["files"]
                owner = Image.open(
                    output / example_files["owner"]["file"]).convert("RGB")
                support = Image.open(
                    output / example_files["support"]["file"]).convert("RGB")
                expected_raw = Image.composite(owner, support, aligned)
                expected_repaired = Image.composite(owner, support, repaired)
                if (_image_hash(expected_raw) !=
                        example_files["rawResult"]["pixelSha256"]):
                    errors.append(
                        f"{question.get('key')} raw outcome does not match its uniforms")
                if (_image_hash(expected_repaired) !=
                        example_files["repairedResult"]["pixelSha256"]):
                    errors.append(
                        f"{question.get('key')} repaired outcome does not match its uniforms")
        except (KeyError, OSError) as exception:
            errors.append(
                f"{question.get('key')} outcome verification failed: {exception}")
        if f'data-key="{question.get("key")}"' not in document:
            errors.append(f"HTML missing key {question.get('key')}")
    for marker in ('class="answer form-select"', 'class="note form-control"',
                   'id="submit-answers"', "window.openai.sendFollowUpMessage",
                   "localStorage.setItem"):
        if marker not in document:
            errors.append(f"HTML missing interaction marker {marker}")
    if errors:
        raise ValueError("\n".join(errors[:80]))
    return {
        "status": "PASS",
        "questions": len(questions),
        "examples": sum(len(value["examples"]) for value in questions),
        "verifiedImages": verified_images,
        "htmlImageReferences": len(references),
        "structuralTargetsMet": sum(
            bool(value["metrics"]["structuralTargetMet"])
            for value in questions),
        "ownershipTargetsMet": sum(
            bool(value["metrics"].get("ownershipTargetMet"))
            for value in questions),
        "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R44-AUTHORED-PILOT")
    parser.add_argument("--sand-band-pilot", action="store_true",
                        help="render only the three R44-rejected sand cases "
                             "with the reviewed faceted opacity-band method")
    parser.add_argument("--remaining-hard-baselines", action="store_true",
                        help="render the compact unresolved hard-material "
                             "family/topology baseline batch")
    args = parser.parse_args()
    if args.sand_band_pilot and args.remaining_hard_baselines:
        parser.error("select only one focused batch")
    cases = (SAND_PILOT_CASES if args.sand_band_pilot else
             REMAINING_HARD_BASELINE_CASES
             if args.remaining_hard_baselines else PILOT_CASES)
    print(json.dumps(render(
        args.review, args.output, args.checkpoint, cases,
        reviewed_methods=args.sand_band_pilot),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
