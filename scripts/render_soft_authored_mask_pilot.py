#!/usr/bin/env python3
"""Reconstruct painterly sand ownership from authored transition artwork.

This pilot deliberately bypasses the legacy per-pixel uniform-distance mask and
its binary topology cleanup. It learns both visual regimes from the known full-
strength corners of the authored transition itself, classifies the tile at
multiple spatial scales, and leaves the resulting ownership field continuous.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import re
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image, ImageFilter, ImageOps

from render_authored_mask_repair_pilot import (
    CORNER_NAMES,
    TILE_SIZE,
    _cell,
    _difference_image,
    _image_hash,
    _representatives,
    _save,
    _slug,
    align_authored_mask,
)
from render_blend_mask_questionnaire import interaction_script


OUTER = (True, False, False, False)
EDGE = (True, True, False, False)
INSIDE = (True, True, True, False)


def _blur_rgb(image: Image.Image, radius: float) -> np.ndarray:
    blurred = image.convert("RGB").filter(ImageFilter.GaussianBlur(radius))
    return np.asarray(blurred, dtype=np.float32) / 255.0


def _blur_gray(values: np.ndarray, radius: float) -> np.ndarray:
    image = Image.fromarray(
        np.uint8(np.clip(np.rint(values * 255.0), 0, 255)), "L")
    return np.asarray(
        image.filter(ImageFilter.GaussianBlur(radius)),
        dtype=np.float32) / 255.0


def _rgb_features(rgb: np.ndarray) -> np.ndarray:
    """Convert sRGB to a compact perceptual Lab-like feature space."""
    linear = np.where(rgb <= 0.04045, rgb / 12.92,
                      ((rgb + 0.055) / 1.055) ** 2.4)
    xyz = np.empty_like(linear)
    xyz[..., 0] = (0.4124564 * linear[..., 0] +
                   0.3575761 * linear[..., 1] +
                   0.1804375 * linear[..., 2]) / 0.95047
    xyz[..., 1] = (0.2126729 * linear[..., 0] +
                   0.7151522 * linear[..., 1] +
                   0.0721750 * linear[..., 2])
    xyz[..., 2] = (0.0193339 * linear[..., 0] +
                   0.1191920 * linear[..., 1] +
                   0.9503041 * linear[..., 2]) / 1.08883
    epsilon = 216.0 / 24389.0
    kappa = 24389.0 / 27.0
    f = np.where(xyz > epsilon, np.cbrt(xyz),
                 (kappa * xyz + 16.0) / 116.0)
    result = np.empty_like(rgb)
    result[..., 0] = (116.0 * f[..., 1] - 16.0) / 100.0
    result[..., 1] = (500.0 * (f[..., 0] - f[..., 1])) / 128.0
    result[..., 2] = (200.0 * (f[..., 1] - f[..., 2])) / 128.0
    return result


def _corner_training_mask(height: int, width: int, corner: int,
                          patch: int) -> np.ndarray:
    result = np.zeros((height, width), dtype=bool)
    if corner == 0:
        result[:patch, :patch] = True
    elif corner == 1:
        result[:patch, width - patch:] = True
    elif corner == 2:
        result[height - patch:, width - patch:] = True
    else:
        result[height - patch:, :patch] = True
    return result


def _palette(features: np.ndarray, selected: np.ndarray,
             cluster_count: int = 10) -> np.ndarray:
    samples = features[selected].reshape(-1, features.shape[-1])
    if not len(samples):
        raise ValueError("ownership class has no authored corner samples")
    stride = max(1, len(samples) // 4096)
    samples = samples[::stride]
    count = min(cluster_count, len(samples))
    mean = samples.mean(axis=0)
    centers = [samples[np.argmax(np.sum((samples - mean) ** 2, axis=1))]]
    while len(centers) < count:
        current = np.asarray(centers)
        distance = np.min(np.sum(
            (samples[:, None, :] - current[None, :, :]) ** 2, axis=2),
            axis=1)
        centers.append(samples[int(np.argmax(distance))])
    centers = np.asarray(centers, dtype=np.float32)
    for _ in range(8):
        distance = np.sum(
            (samples[:, None, :] - centers[None, :, :]) ** 2, axis=2)
        labels = np.argmin(distance, axis=1)
        updated = centers.copy()
        for index in range(len(centers)):
            members = samples[labels == index]
            if len(members):
                updated[index] = members.mean(axis=0)
        if np.allclose(updated, centers, atol=1e-5):
            break
        centers = updated
    return centers


def _palette_ownership(features: np.ndarray, owner: np.ndarray,
                       support: np.ndarray) -> np.ndarray:
    flat = features.reshape(-1, features.shape[-1])
    owner_distance = np.min(np.sum(
        (flat[:, None, :] - owner[None, :, :]) ** 2, axis=2), axis=1)
    support_distance = np.min(np.sum(
        (flat[:, None, :] - support[None, :, :]) ** 2, axis=2), axis=1)
    denominator = owner_distance + support_distance
    probability = np.divide(
        support_distance, denominator,
        out=np.full_like(denominator, 0.5), where=denominator > 1e-8)
    return probability.reshape(features.shape[:2])


def _bilinear_corner_prior(height: int, width: int,
                           owner_corners: tuple[bool, bool, bool, bool]):
    yy, xx = np.mgrid[0:height, 0:width]
    u = xx.astype(np.float32) / max(1, width - 1)
    v = yy.astype(np.float32) / max(1, height - 1)
    bases = ((1.0 - u) * (1.0 - v), u * (1.0 - v),
             u * v, (1.0 - u) * v)
    result = np.zeros((height, width), dtype=np.float32)
    for index, owned in enumerate(owner_corners):
        if owned:
            result += bases[index]
    return result


def reconstruct_soft_ownership(
        authored_transition: Image.Image,
        owner_corners: tuple[bool, bool, bool, bool]) -> tuple[Image.Image, dict]:
    """Recover a continuous ownership field from transition-local evidence."""
    image = authored_transition.convert("RGB")
    width, height = image.size
    if width != height or width < 16 or len(owner_corners) != 4:
        raise ValueError("soft reconstruction requires a square tile and four corners")
    patch = max(10, min(width, height) // 5)
    owner_training = np.zeros((height, width), dtype=bool)
    support_training = np.zeros((height, width), dtype=bool)
    for corner, owned in enumerate(owner_corners):
        target = owner_training if owned else support_training
        target |= _corner_training_mask(height, width, corner, patch)

    estimates = []
    for radius, weight in ((0.0, 0.12), (2.0, 0.56), (6.0, 0.32)):
        rgb = (np.asarray(image, dtype=np.float32) / 255.0 if radius == 0.0
               else _blur_rgb(image, radius))
        features = _rgb_features(rgb)
        owner_palette = _palette(features, owner_training)
        support_palette = _palette(features, support_training)
        estimates.append((_palette_ownership(
            features, owner_palette, support_palette), weight))
    ownership = sum(value * weight for value, weight in estimates)
    ownership = _blur_gray(ownership, max(1.5, min(width, height) / 40.0))

    # The topology prior resolves only genuinely ambiguous colors. It cannot
    # replace the transition-local painterly field or create a binary lobe.
    confidence = np.abs(ownership - 0.5) * 2.0
    prior_weight = 0.08 * (1.0 - confidence) ** 2
    prior = _bilinear_corner_prior(height, width, owner_corners)
    ownership = ownership * (1.0 - prior_weight) + prior * prior_weight
    ownership = np.clip(ownership, 0.0, 1.0)

    points = ((0, 0), (0, width - 1),
              (height - 1, width - 1), (height - 1, 0))
    for index, point in enumerate(points):
        ownership[point] = 1.0 if owner_corners[index] else 0.0
    result = Image.fromarray(
        np.uint8(np.clip(np.rint(ownership * 255.0), 0, 255)), "L")
    values = np.asarray(result, dtype=np.uint8)
    metrics = {
        "method": "transition-local-multiscale-palettes",
        "usesExternalUniformPixels": False,
        "usesBinaryCleanup": False,
        "trainingPatchPixels": patch,
        "softPixelFraction": round(float(np.logical_and(
            values > 25, values < 230).mean()), 4),
        "distinctAlphaValues": int(len(np.unique(values))),
        "cornerValues": {
            CORNER_NAMES[index]: int(values[point])
            for index, point in enumerate(points)
        },
    }
    return result, metrics


def derive_inside_from_outer(outer: Image.Image) -> Image.Image:
    """Map NW 1-of-4 to SW-negative 3-of-4 by rotation then inversion."""
    return ImageOps.invert(outer.convert("L").rotate(90))


def _alpha_heatmap(mask: Image.Image) -> Image.Image:
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    stops = (
        (0.0, np.array((27, 39, 70), dtype=np.float32)),
        (0.35, np.array((45, 135, 160), dtype=np.float32)),
        (0.65, np.array((231, 153, 61), dtype=np.float32)),
        (1.0, np.array((249, 241, 211), dtype=np.float32)),
    )
    result = np.empty((*values.shape, 3), dtype=np.float32)
    for (left_value, left_color), (right_value, right_color) in zip(
            stops[:-1], stops[1:]):
        selected = np.logical_and(values >= left_value, values <= right_value)
        amount = np.clip((values - left_value) /
                         (right_value - left_value), 0.0, 1.0)
        result[selected] = (left_color[None, :] *
                            (1.0 - amount[selected, None]) +
                            right_color[None, :] * amount[selected, None])
    return Image.fromarray(np.uint8(np.clip(np.rint(result), 0, 255)), "RGB")


def _bounded_support_holes(mask: Image.Image) -> dict:
    support = np.asarray(mask.convert("L"), dtype=np.uint8) < 128
    height, width = support.shape
    visited = np.zeros_like(support, dtype=bool)
    holes = []
    for start_y in range(height):
        for start_x in range(width):
            if visited[start_y, start_x] or not support[start_y, start_x]:
                continue
            pending = [(start_y, start_x)]
            visited[start_y, start_x] = True
            area = 0
            touches_edge = False
            while pending:
                y, x = pending.pop()
                area += 1
                touches_edge = touches_edge or (
                    y == 0 or x == 0 or y + 1 == height or x + 1 == width)
                for next_y, next_x in ((y - 1, x), (y + 1, x),
                                       (y, x - 1), (y, x + 1)):
                    if (0 <= next_y < height and 0 <= next_x < width and
                            support[next_y, next_x] and
                            not visited[next_y, next_x]):
                        visited[next_y, next_x] = True
                        pending.append((next_y, next_x))
            if not touches_edge:
                holes.append(area)
    substantial = [area for area in holes if area >= 4]
    return {
        "countAtLeast4Pixels": len(substantial),
        "pixelsAtLeast4": sum(substantial),
        "largestPixels": max(substantial, default=0),
    }


def _find_source(rows: list[dict[str, str]], topology: str,
                 provenance: str) -> dict[str, str]:
    for row in rows:
        if (row["owner_style"] == "sand-grain" and
                row["topology"] == topology and
                row["provenance"] == provenance):
            return row
    raise ValueError(f"missing authored source {provenance} {topology}")


def _outcomes(review: Path, rows: list[dict[str, str]], mask: Image.Image,
              legacy: Image.Image, images: Path, prefix: str) -> tuple[list, str]:
    entries = []
    sections = []
    for number, row in enumerate(_representatives(rows), 1):
        owner = Image.open(review / row["owner_uniform"]).convert("RGB")
        support = Image.open(review / row["support_uniform"]).convert("RGB")
        legacy_result = Image.composite(owner, support, legacy)
        soft_result = Image.composite(owner, support, mask)
        item_prefix = f"{prefix}-example-{number}"
        files = {
            "owner": _save(owner, images / f"{item_prefix}-owner.png"),
            "legacyResult": _save(
                legacy_result, images / f"{item_prefix}-legacy-result.png"),
            "softResult": _save(
                soft_result, images / f"{item_prefix}-soft-result.png"),
            "support": _save(
                support, images / f"{item_prefix}-support.png"),
        }
        entries.append({
            "ownerMaterial": row["owner_material"],
            "supportMaterial": row["support_material"],
            "scenario": row["support_scenario"],
            "files": files,
        })
        sections.append(f'''<section class="example"><h3>{html.escape(row['owner_material'])} + {html.escape(row['support_material'])}</h3>
<p class="muted">{html.escape(row['support_scenario'])}</p><div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Requested uniform"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{files['legacyResult']['file']}" alt="Rejected extracted-mask outcome"><figcaption>Rejected extracted-mask outcome</figcaption></figure>
<figure><img src="{files['softResult']['file']}" alt="Soft source-reconstructed outcome"><figcaption><strong>Soft source-reconstructed outcome</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform</figcaption></figure>
</div></section>''')
    return entries, "".join(sections)


def render(review: Path, output: Path,
           checkpoint: str = "R46-SOFT-SOURCE") -> dict:
    review = review.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"pilot output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["owner_style"], row["topology"])].append(row)

    outer_row = _find_source(rows, "outer-1of4-v1", "Zone7:130")
    outer_raw = _cell(review, outer_row["sheet"], int(outer_row["row"]),
                      int(outer_row["column"]), "L")
    outer_legacy, outer_alignment = align_authored_mask(
        outer_raw, "outer-1of4-v1")
    outer_source = _cell(
        review, outer_row["source_example_sheet"], int(outer_row["row"]),
        int(outer_row["column"]), "RGB")
    outer_source_aligned = outer_source.rotate(
        outer_alignment["rotationCounterClockwise"])
    outer_soft, outer_metrics = reconstruct_soft_ownership(
        outer_source_aligned, OUTER)
    legacy_inside = derive_inside_from_outer(outer_legacy)
    soft_inside = derive_inside_from_outer(outer_soft)

    edge_row = _find_source(rows, "edge-2of4-v2", "Zone7:232")
    edge_raw = _cell(review, edge_row["sheet"], int(edge_row["row"]),
                     int(edge_row["column"]), "L")
    edge_legacy, edge_alignment = align_authored_mask(edge_raw, "edge-2of4-v2")
    edge_source = _cell(
        review, edge_row["source_example_sheet"], int(edge_row["row"]),
        int(edge_row["column"]), "RGB")
    edge_source_aligned = edge_source.rotate(
        edge_alignment["rotationCounterClockwise"])
    edge_soft, edge_metrics = reconstruct_soft_ownership(
        edge_source_aligned, EDGE)
    edge_metrics["legacyBoundedSupportHoles"] = _bounded_support_holes(edge_legacy)
    edge_metrics["softBoundedSupportHoles"] = _bounded_support_holes(edge_soft)

    outer_prefix = "01-sand-grain-outer-inside"
    outer_files = {
        "source": _save(outer_source, images / f"{outer_prefix}-source.png"),
        "alignedSource": _save(
            outer_source_aligned, images / f"{outer_prefix}-aligned-source.png"),
        "legacyOuter": _save(
            outer_legacy, images / f"{outer_prefix}-legacy-outer.png"),
        "softOuter": _save(
            outer_soft, images / f"{outer_prefix}-soft-outer.png"),
        "outerHeatmap": _save(
            _alpha_heatmap(outer_soft),
            images / f"{outer_prefix}-soft-outer-heatmap.png"),
        "difference": _save(
            _difference_image(outer_legacy, outer_soft),
            images / f"{outer_prefix}-difference.png"),
        "legacyInside": _save(
            legacy_inside, images / f"{outer_prefix}-legacy-inside.png"),
        "softInside": _save(
            soft_inside, images / f"{outer_prefix}-soft-inside.png"),
    }
    outer_outcomes, outer_outcomes_html = _outcomes(
        review, grouped[("sand-grain", "outer-1of4-v1")], outer_soft,
        outer_legacy, images, f"{outer_prefix}-outer")
    inside_outcomes, inside_outcomes_html = _outcomes(
        review, grouped[("sand-grain", "inside-3of4-v2")], soft_inside,
        legacy_inside, images, f"{outer_prefix}-inside")

    edge_prefix = "02-sand-grain-edge"
    edge_files = {
        "source": _save(edge_source, images / f"{edge_prefix}-source.png"),
        "alignedSource": _save(
            edge_source_aligned, images / f"{edge_prefix}-aligned-source.png"),
        "legacy": _save(edge_legacy, images / f"{edge_prefix}-legacy.png"),
        "soft": _save(edge_soft, images / f"{edge_prefix}-soft.png"),
        "heatmap": _save(
            _alpha_heatmap(edge_soft), images / f"{edge_prefix}-heatmap.png"),
        "difference": _save(
            _difference_image(edge_legacy, edge_soft),
            images / f"{edge_prefix}-difference.png"),
    }
    edge_outcomes, edge_outcomes_html = _outcomes(
        review, grouped[("sand-grain", "edge-2of4-v2")], edge_soft,
        edge_legacy, images, edge_prefix)

    options = "".join(f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
                      for value in (
                          "Still uncertain",
                          "Approve soft source reconstruction",
                          "Too soft",
                          "Not soft enough",
                          "Wrong ownership shape",
                          "Unwanted holes remain",
                          "Reject this reconstruction"))
    cards = f'''<article class="pilot-question" data-key="soft-source-sand-grain-outer-inside-v1">
<h2>1. sand-grain · outer 1 of 4 v1 + exact inside 3 of 4 v2 inverse</h2>
<p class="metric">{outer_metrics['softPixelFraction']:.1%} intermediate-alpha pixels · {outer_metrics['distinctAlphaValues']} alpha values · no binary cleanup · inside derived as rotate 90° CCW then invert</p>
<div class="evidence-grid">
<figure><img src="{outer_files['source']['file']}" alt="Authored transition"><figcaption>Authored transition · Zone7:130</figcaption></figure>
<figure><img src="{outer_files['alignedSource']['file']}" alt="Aligned authored transition"><figcaption>Aligned authored transition used for reconstruction</figcaption></figure>
<figure><img src="{outer_files['legacyOuter']['file']}" alt="Rejected legacy extraction"><figcaption>Rejected legacy extraction</figcaption></figure>
<figure><img src="{outer_files['softOuter']['file']}" alt="Soft outer reconstruction"><figcaption><strong>Soft outer reconstruction</strong></figcaption></figure>
<figure><img src="{outer_files['outerHeatmap']['file']}" alt="Continuous-alpha heatmap"><figcaption>Continuous alpha · navy support · teal/amber transition · ivory owner</figcaption></figure>
<figure><img src="{outer_files['difference']['file']}" alt="Legacy to soft difference"><figcaption>Legacy → soft difference</figcaption></figure>
<figure><img src="{outer_files['legacyInside']['file']}" alt="Rejected binary inside inverse"><figcaption>Rejected binary inside inverse</figcaption></figure>
<figure><img src="{outer_files['softInside']['file']}" alt="Exact soft inside inverse"><figcaption><strong>Exact soft inside inverse</strong></figcaption></figure>
</div><h3>Outer outcomes</h3><div class="examples">{outer_outcomes_html}</div>
<h3>Inside outcomes · same mask, exact transformed polarity</h3><div class="examples">{inside_outcomes_html}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional reconstruction detail"></label></div></article>
<article class="pilot-question" data-key="soft-source-sand-grain-edge-v2">
<h2>2. sand-grain · edge 2 of 4 v2</h2>
<p class="metric">{edge_metrics['softPixelFraction']:.1%} intermediate-alpha pixels · {edge_metrics['distinctAlphaValues']} alpha values · bounded support holes ≥4 px: legacy {edge_metrics['legacyBoundedSupportHoles']['countAtLeast4Pixels']} → soft {edge_metrics['softBoundedSupportHoles']['countAtLeast4Pixels']}</p>
<div class="evidence-grid">
<figure><img src="{edge_files['source']['file']}" alt="Authored transition"><figcaption>Authored transition · Zone7:232</figcaption></figure>
<figure><img src="{edge_files['alignedSource']['file']}" alt="Aligned authored transition"><figcaption>Aligned authored transition used for reconstruction</figcaption></figure>
<figure><img src="{edge_files['legacy']['file']}" alt="Rejected legacy extraction"><figcaption>Rejected extraction · hard islands and false holes</figcaption></figure>
<figure><img src="{edge_files['soft']['file']}" alt="Soft edge reconstruction"><figcaption><strong>Soft edge reconstruction</strong></figcaption></figure>
<figure><img src="{edge_files['heatmap']['file']}" alt="Continuous-alpha heatmap"><figcaption>Continuous-alpha heatmap</figcaption></figure>
<figure><img src="{edge_files['difference']['file']}" alt="Legacy to soft difference"><figcaption>Legacy → soft difference</figcaption></figure>
</div><div class="examples">{edge_outcomes_html}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional reconstruction detail"></label></div></article>'''

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} soft source pilot</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.warning{{padding:12px;border:1px solid #d29922;background:var(--panel)}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:320px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.metric{{font:600 13px/1.4 Consolas,monospace}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(100px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · soft authored-source reconstruction</h1>
<p class="warning"><strong>R45 is rejected and is not runtime behavior.</strong> This two-question replacement reconstructs continuous ownership directly from the authored transition’s full-strength corner regimes. It does not compare transition pixels with unrelated uniform pixels, sharpen the field, or run binary component cleanup.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{cards}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-soft-authored-mask-pilot-v1",
        "checkpoint": checkpoint,
        "runtimeChanged": False,
        "questionCount": 2,
        "questions": [
            {
                "key": "soft-source-sand-grain-outer-inside-v1",
                "source": "Zone7:130",
                "alignment": outer_alignment,
                "files": outer_files,
                "metrics": outer_metrics,
                "outerOutcomes": outer_outcomes,
                "insideOutcomes": inside_outcomes,
                "insideDerivation": "invert(rotate_ccw_90(softOuter))",
            },
            {
                "key": "soft-source-sand-grain-edge-v2",
                "source": "Zone7:232",
                "alignment": edge_alignment,
                "files": edge_files,
                "metrics": edge_metrics,
                "outcomes": edge_outcomes,
            },
        ],
    }
    analysis_path = output / "soft-authored-mask-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {
        "html": str(html_path),
        "analysis": str(analysis_path),
        **validate_output(output),
    }


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "soft-authored-mask-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("soft pilot must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    errors = []
    questions = analysis.get("questions", [])
    if len(questions) != 2 or analysis.get("questionCount") != 2:
        errors.append("soft pilot must contain exactly two questions")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    for reference in references:
        if (reference.startswith(("/", "\\")) or ":" in reference or
                not (output / reference).is_file()):
            errors.append(f"missing or non-relocatable image: {reference}")
    verified_images = 0
    for question in questions:
        entries = list(question.get("files", {}).values())
        outcomes = (question.get("outcomes", []) +
                    question.get("outerOutcomes", []) +
                    question.get("insideOutcomes", []))
        for outcome in outcomes:
            entries.extend(outcome["files"].values())
        for entry in entries:
            path = output / entry["file"]
            if not path.is_file():
                errors.append(f"missing indexed image: {entry['file']}")
                continue
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                errors.append(f"file hash mismatch: {entry['file']}")
            verified_images += 1
    outer = questions[0]
    outer_mask = Image.open(output / outer["files"]["softOuter"]["file"])
    inside_mask = Image.open(output / outer["files"]["softInside"]["file"])
    if derive_inside_from_outer(outer_mask).tobytes() != inside_mask.tobytes():
        errors.append("inside mask is not the exact rotated polarity inverse")
    for question in questions:
        metrics = question["metrics"]
        if metrics["usesBinaryCleanup"] or metrics["usesExternalUniformPixels"]:
            errors.append(f"{question['key']} used the rejected extraction path")
        if metrics["softPixelFraction"] < 0.10:
            errors.append(f"{question['key']} lost its continuous soft field")
    edge_metrics = questions[1]["metrics"]
    if (edge_metrics["softBoundedSupportHoles"]["countAtLeast4Pixels"] >=
            edge_metrics["legacyBoundedSupportHoles"]["countAtLeast4Pixels"]):
        errors.append("edge reconstruction did not reduce bounded support holes")
    for question in questions:
        outcome_groups = []
        if "outcomes" in question:
            outcome_groups.append((question["outcomes"],
                                   question["files"]["legacy"]["file"],
                                   question["files"]["soft"]["file"]))
        else:
            outcome_groups.extend((
                (question["outerOutcomes"],
                 question["files"]["legacyOuter"]["file"],
                 question["files"]["softOuter"]["file"]),
                (question["insideOutcomes"],
                 question["files"]["legacyInside"]["file"],
                 question["files"]["softInside"]["file"]),
            ))
        for outcomes, legacy_file, soft_file in outcome_groups:
            legacy = Image.open(output / legacy_file).convert("L")
            soft = Image.open(output / soft_file).convert("L")
            for outcome in outcomes:
                files = outcome["files"]
                owner = Image.open(output / files["owner"]["file"]).convert("RGB")
                support = Image.open(output / files["support"]["file"]).convert("RGB")
                if (_image_hash(Image.composite(owner, support, legacy)) !=
                        files["legacyResult"]["pixelSha256"]):
                    errors.append("legacy outcome/uniform mismatch")
                if (_image_hash(Image.composite(owner, support, soft)) !=
                        files["softResult"]["pixelSha256"]):
                    errors.append("soft outcome/uniform mismatch")
    for marker in ('class="pilot-question"', 'class="answer form-select"',
                   'class="note form-control"', 'id="submit-answers"',
                   "window.openai.sendFollowUpMessage", "localStorage.setItem"):
        if marker not in document:
            errors.append(f"HTML missing interaction marker {marker}")
    if document.count('class="pilot-question"') != 2:
        errors.append("HTML question count differs from analysis")
    if errors:
        raise ValueError("\n".join(errors[:80]))
    return {
        "status": "PASS",
        "questions": 2,
        "outcomes": 9,
        "verifiedImages": verified_images,
        "htmlImageReferences": len(references),
        "insideExactInverse": True,
        "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R46-SOFT-SOURCE")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
