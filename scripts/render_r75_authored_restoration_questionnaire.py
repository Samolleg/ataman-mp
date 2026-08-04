#!/usr/bin/env python3
"""Build one authored-restoration candidate for every R74-rejected mask.

Candidates come only from provenance-qualified, same-atlas two-family JMV
transitions.  Exact semantic corners determine mirror/rotation/polarity; source
quality never chooses orientation.  Natural hard masks use the approved R44
repair, soft masks use the approved transition-local detailed-opacity route,
and paving preserves the aligned authored field with corner-only cleanup.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import math
import re
from collections import defaultdict
from functools import lru_cache
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw, ImageFilter, ImageOps

from render_authored_mask_repair_pilot import (
    CORNER_NAMES,
    expected_owner_corners,
    mask_metrics,
    repair_authored_mask,
)
from render_blend_mask_questionnaire import (
    _representatives,
    _sheet_cell,
    interaction_script,
)
from render_semantic_atlas_review import common_style
from render_soft_authored_mask_pilot import (
    _alpha_heatmap,
    reconstruct_soft_ownership,
)


TILE_SIZE = 256
SOFT_FAMILIES = {
    "sand-grain", "snow-clumps", "ice-fracture", "water-ripples"
}
SOURCE_OVERRIDES = {
    "mask-family-gravel-chunks-outer-1of4-v1": "bz13h:213",
    "mask-family-gravel-chunks-inside-3of4-v1": "bz13h:213",
    "mask-family-water-ripples-outer-1of4-v1": "Zone3Obr:235",
    "mask-family-water-ripples-edge-2of4-v1": "BaseGipat:190",
    "mask-family-water-ripples-diagonal-2of4": "Zone1:351",
    "mask-family-water-ripples-inside-3of4-v1": "Zone3Obr:435",
}


def _sha(image: Image.Image) -> str:
    return hashlib.sha256(image.tobytes()).hexdigest()


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def _key_parts(key: str) -> tuple[str, str]:
    match = re.fullmatch(
        r"mask-family-(grass-strands|gravel-chunks|ice-fracture|"
        r"paving-courses|sand-grain|snow-clumps|soil-mottle|"
        r"stone-fracture|water-ripples)-(.+)", key)
    if not match:
        raise ValueError(f"invalid R74 mask key: {key}")
    return match.group(1), match.group(2)


def _base_topology(topology: str) -> str:
    return re.sub(r"-v[12]$", "", topology)


def _variant_index(topology: str) -> int:
    match = re.search(r"-v([12])$", topology)
    return int(match.group(1)) - 1 if match else 0


@lru_cache(maxsize=512)
def _load(path_text: str, mode: str = "RGB") -> Image.Image:
    with Image.open(path_text) as image:
        image.load()
        return image.convert(mode)


def _inventory_image(root: Path, relative: str, mode: str = "RGB") -> Image.Image:
    return _load(str((root / relative).resolve()), mode).copy()


def _rotate_pattern_ccw(values: tuple[bool, bool, bool, bool]):
    return values[1], values[2], values[3], values[0]


def _align_candidate(root: Path, candidate: dict, topology: str):
    expected = expected_owner_corners(topology)
    pattern = tuple(bool(value) for value in candidate["owner_corners"])
    turns = None
    rotated_pattern = pattern
    for value in range(4):
        if rotated_pattern == expected:
            turns = value
            break
        rotated_pattern = _rotate_pattern_ccw(rotated_pattern)
    if turns is None:
        raise ValueError(
            f'{candidate["source"]} cannot exactly supply {topology}')

    def aligned(relative: str) -> Image.Image:
        # Inventory images are physical atlas previews.  Cross the engine-grid
        # boundary exactly once, then apply only the proven legal quarter turn.
        image = ImageOps.flip(_inventory_image(root, relative, "RGB"))
        return image.rotate(90 * turns)

    return (
        aligned(candidate["transition_file"]),
        aligned(candidate["owner_uniform_file"]),
        aligned(candidate["support_uniform_file"]),
        {
            "physicalPreviewToGridMirrorY": True,
            "rotationCounterClockwise": turns * 90,
            "orientationSelection": "exact-corner-assignment-only",
            "sourceGridOwnerCorners": list(pattern),
            "canonicalOwnerCorners": list(expected),
        },
    )


def _blur_gray(values: np.ndarray, radius: float) -> np.ndarray:
    image = Image.fromarray(
        np.uint8(np.clip(np.rint(values * 255.0), 0, 255)), "L")
    return np.asarray(
        image.filter(ImageFilter.GaussianBlur(radius)), dtype=np.float32) / 255.0


def _extract_ownership(transition: Image.Image, owner: Image.Image,
                       support: Image.Image, topology: str) -> Image.Image:
    actual = np.asarray(transition.convert("RGB"), dtype=np.float32) / 255.0
    a = np.asarray(owner.convert("RGB"), dtype=np.float32) / 255.0
    b = np.asarray(support.convert("RGB"), dtype=np.float32) / 255.0
    direction = a - b
    length = np.sum(direction * direction, axis=2)
    projection = np.divide(
        np.sum((actual - b) * direction, axis=2), length,
        out=np.full_like(length, 0.5), where=length > 1e-7)
    owner_distance = np.sum((actual - a) ** 2, axis=2)
    support_distance = np.sum((actual - b) ** 2, axis=2)
    total = owner_distance + support_distance
    distance = np.divide(
        support_distance, total, out=np.full_like(total, 0.5),
        where=total > 1e-7)
    ownership = np.clip(0.78 * projection + 0.22 * distance, 0.0, 1.0)
    ownership = _blur_gray(ownership, 0.65)
    points = ((0, 0), (0, ownership.shape[1] - 1),
              (ownership.shape[0] - 1, ownership.shape[1] - 1),
              (ownership.shape[0] - 1, 0))
    for index, point in enumerate(points):
        ownership[point] = 1.0 if expected_owner_corners(topology)[index] else 0.0
    return Image.fromarray(
        np.uint8(np.clip(np.rint(ownership * 255.0), 0, 255)), "L")


def _paving_corner_cleanup(mask: Image.Image, topology: str) -> tuple[Image.Image, dict]:
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    height, width = values.shape
    yy, xx = np.mgrid[0:height, 0:width]
    corners = ((0, 0), (width - 1, 0),
               (width - 1, height - 1), (0, height - 1))
    expected = expected_owner_corners(topology)
    result = values.copy()
    radius = 0.12 * min(height, width)
    for index, (corner_x, corner_y) in enumerate(corners):
        distance = np.hypot(xx - corner_x, yy - corner_y)
        influence = np.clip(1.0 - distance / radius, 0.0, 1.0) ** 1.7
        if expected[index]:
            result += (1.0 - result) * influence
        else:
            result *= 1.0 - influence
    image = Image.fromarray(
        np.uint8(np.clip(np.rint(result * 255.0), 0, 255)), "L")
    return image, {
        "method": "raw-authored-with-corner-only-cleanup",
        "cleanupRadiusPixels": round(radius, 3),
        "usesGlobalBlur": False,
        "preservesAuthoredInterior": True,
    }


def _shared_edge_mask(mask: Image.Image, topology: str) -> Image.Image:
    """Keep restored authored opacity; normalize only the one-texel seam ring."""
    values = np.asarray(mask.convert("L"), dtype=np.uint8).copy()
    owner = expected_owner_corners(topology)
    height, width = values.shape

    def smooth(position: float) -> int:
        value = min(1.0, max(0.0, (position - 0.38) / 0.24))
        value = value * value * (3.0 - 2.0 * value)
        return int(round(value * 255.0))

    def edge(first: bool, second: bool, position: float) -> int:
        if first == second:
            return 255 if first else 0
        second_weight = smooth(position)
        return 255 - second_weight if first else second_weight

    for x in range(width):
        position = 0.0 if width == 1 else x / (width - 1)
        values[0, x] = edge(owner[0], owner[1], position)
        values[-1, x] = edge(owner[3], owner[2], position)
    for y in range(height):
        position = 0.0 if height == 1 else y / (height - 1)
        values[y, 0] = edge(owner[0], owner[3], position)
        values[y, -1] = edge(owner[1], owner[2], position)
    return Image.fromarray(values, "L")


def _restore(family: str, topology: str, transition: Image.Image,
             owner: Image.Image, support: Image.Image, seed: int):
    recovered = _extract_ownership(transition, owner, support, topology)
    if family in SOFT_FAMILIES:
        restored, process = reconstruct_soft_ownership(
            transition, expected_owner_corners(topology),
            remove_bounded_holes=family in {"snow-clumps", "ice-fracture"})
        route = "transition-local-detailed-opacity"
    elif family == "paving-courses":
        restored, process = _paving_corner_cleanup(recovered, topology)
        route = "raw-authored-corner-cleanup"
    else:
        restored, metrics = repair_authored_mask(recovered, topology, seed)
        process = {"method": "approved-r44-conservative-repair",
                   "repairMetrics": metrics}
        route = "conservative-authored-repair"
    return recovered, restored, _shared_edge_mask(restored, topology), route, process


def _quick_score(root: Path, candidate: dict, topology: str) -> tuple:
    transition, owner, support, _ = _align_candidate(root, candidate, topology)
    transition = transition.resize((64, 64), Image.Resampling.BILINEAR)
    owner = owner.resize((64, 64), Image.Resampling.BILINEAR)
    support = support.resize((64, 64), Image.Resampling.BILINEAR)
    raw = _extract_ownership(transition, owner, support, topology)
    metrics = mask_metrics(raw, raw, topology)
    expected = expected_owner_corners(topology)
    penalty = 0.0
    penalty += sum(max(0.0, 0.70 - value) * 30.0
                   for value in metrics["affectedQuarterStrongCoverage"].values())
    penalty += sum(max(0.0, value - 0.30) * 30.0
                   for value in metrics["protectedQuarterOwnerPresence"].values())
    for index, owned in enumerate(expected):
        value = metrics["cornerPatchMeans"][CORNER_NAMES[index]]
        penalty += (max(0.0, 0.80 - value) if owned
                    else max(0.0, value - 0.20)) * 25.0
    values = np.asarray(raw, dtype=np.float32) / 255.0
    actual = np.asarray(transition, dtype=np.float32) / 255.0
    a = np.asarray(owner, dtype=np.float32) / 255.0
    b = np.asarray(support, dtype=np.float32) / 255.0
    recomposed = a * values[..., None] + b * (1.0 - values[..., None])
    error = float(np.abs(actual - recomposed).mean())
    expected_area = sum(expected) / 4.0
    area_error = abs(float((values >= 0.5).mean()) - expected_area)
    return round(penalty + error * 8.0 + area_error * 5.0, 6), candidate["source"]


def _ranked_sources(inventory_root: Path, candidates: list[dict],
                    family: str, topology: str) -> list[dict]:
    base = _base_topology(topology)
    filtered = [item for item in candidates
                if item["owner_style"] == family and item["topology"] == base]
    unique = {}
    for item in filtered:
        identity = (item["transition_pixel_sha256"], item["owner_material_id"],
                    item["support_material_id"], tuple(item["owner_corners"]))
        previous = unique.get(identity)
        if previous is None or item["source"].casefold() < previous["source"].casefold():
            unique[identity] = item
    ordered = sorted(unique.values(), key=lambda item: (
        item["support_style"], item["source"].casefold(),
        item["transition_pixel_sha256"]))
    # Preserve broad atlas/support diversity without loading thousands of
    # duplicate candidates.  Source overrides are reinserted below.
    sampled = []
    buckets = defaultdict(list)
    for item in ordered:
        buckets[(item["support_style"], item["source_namespace"])].append(item)
    while len(sampled) < 160 and any(buckets.values()):
        for bucket in sorted(buckets):
            if buckets[bucket] and len(sampled) < 160:
                sampled.append(buckets[bucket].pop(0))
    scored = [(_quick_score(inventory_root, item, topology), item)
              for item in sampled]
    return [item for _, item in sorted(scored, key=lambda value: value[0])]


def _save(image: Image.Image, path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    return {"file": f"images/{path.name}", "pixelSha256": _sha(image),
            "fileSha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def _audit_contact_sheets(output: Path, questions: list[dict]) -> list[dict]:
    """Render compact family-wide evidence sheets for visual QA."""
    by_family = defaultdict(list)
    for question in questions:
        by_family[question["family"]].append(question)
    records = []
    columns = (
        ("source", "transition"),
        ("recovered", "recovered"),
        ("restored", "restored"),
        ("effective", "effective"),
        ("rejected", "current"),
        ("candidate", "candidate"),
    )
    tile = 128
    column_width = 136
    row_height = 184
    header_height = 54
    for family, family_questions in sorted(by_family.items()):
        canvas = Image.new(
            "RGB", (column_width * len(columns),
                    header_height + row_height * len(family_questions)),
            (24, 24, 24))
        draw = ImageDraw.Draw(canvas)
        draw.text((8, 6), f"R75 authored restoration audit: {family}",
                  fill=(240, 240, 240))
        for column, (label, _) in enumerate(columns):
            draw.text((column * column_width + 4, 28), label,
                      fill=(190, 210, 235))
        for row, question in enumerate(family_questions):
            y = header_height + row * row_height
            first_outcome = question["outcomes"][0]
            for column, (_, key) in enumerate(columns):
                if key in question["files"]:
                    relative = question["files"][key]["file"]
                else:
                    relative = first_outcome["files"][key]["file"]
                image = Image.open(output / relative).convert("RGB")
                image.thumbnail((tile, tile), Image.Resampling.LANCZOS)
                canvas.paste(image, (column * column_width + 4, y))
            topology = question["topology"]
            source = question["source"]["source"]
            draw.text((4, y + 132), topology, fill=(240, 240, 240))
            draw.text((4, y + 148), source, fill=(170, 190, 210))
            draw.text((4, y + 164), question["route"],
                      fill=(170, 190, 210))
        path = output / f"audit-contact-{family}.png"
        canvas.save(path, "PNG", optimize=True)
        records.append({
            "family": family,
            "file": path.name,
            "fileSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    return records


def render(inventory_root: Path, review: Path, decisions_path: Path,
           output: Path, checkpoint: str) -> dict:
    inventory_root, review, output = (
        inventory_root.resolve(), review.resolve(), output.resolve())
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"restoration output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    inventory_path = inventory_root / "authored-mask-source-inventory.json"
    inventory = json.loads(inventory_path.read_text(encoding="utf-8"))
    decisions = json.loads(decisions_path.read_text(encoding="utf-8"))
    rejected = decisions["replaceWithAuthoredRestoration"]
    if len(rejected) != 47 or len(set(rejected)) != 47:
        raise ValueError("R74 decisions must contain 47 unique replacements")
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        review_rows = list(csv.DictReader(handle))
    rows_by_group = defaultdict(list)
    for row in review_rows:
        rows_by_group[(row["mask_family"], row["topology"])].append(row)

    rank_cache = {}
    selected_by_base = defaultdict(set)
    cards = []
    questions = []
    for number, key in enumerate(rejected, 1):
        family, topology = _key_parts(key)
        base = _base_topology(topology)
        cache_key = (family, base)
        if cache_key not in rank_cache:
            rank_cache[cache_key] = _ranked_sources(
                inventory_root, inventory["candidates"], family, topology)
        ranked = rank_cache[cache_key]
        override = SOURCE_OVERRIDES.get(key)
        selected = None
        if override:
            matching = [item for item in inventory["candidates"]
                        if item["owner_style"] == family
                        and item["topology"] == base
                        and item["source"].casefold() == override.casefold()]
            if matching:
                selected = min(
                    matching, key=lambda item: _quick_score(
                        inventory_root, item, topology))
        if selected is None:
            desired = _variant_index(topology)
            available = [item for item in ranked
                         if item["transition_pixel_sha256"]
                         not in selected_by_base[cache_key]]
            pool = available or ranked
            if not pool:
                raise ValueError(f"no authored source for {key}")
            selected = pool[min(desired, len(pool) - 1)]
        selected_by_base[cache_key].add(selected["transition_pixel_sha256"])

        transition, source_owner, source_support, alignment = _align_candidate(
            inventory_root, selected, topology)
        seed = int(hashlib.sha256(key.encode()).hexdigest()[:8], 16)
        recovered, restored, effective, route, process = _restore(
            family, topology, transition, source_owner, source_support, seed)
        metrics = mask_metrics(recovered, restored, topology)
        prefix = f"{number:02d}-{family}-{topology}"
        source_files = {
            "transition": _save(transition, images / f"{prefix}-source.png"),
            "owner": _save(source_owner, images / f"{prefix}-source-owner.png"),
            "support": _save(source_support, images / f"{prefix}-source-support.png"),
            "recovered": _save(recovered, images / f"{prefix}-recovered.png"),
            "restored": _save(restored, images / f"{prefix}-restored.png"),
            "effective": _save(effective, images / f"{prefix}-effective.png"),
            "heatmap": _save(_alpha_heatmap(effective),
                             images / f"{prefix}-effective-heatmap.png"),
        }
        representatives = _representatives(rows_by_group[(family, topology)], 4)
        outcomes = []
        outcome_html = []
        for example_index, row in enumerate(representatives, 1):
            owner = ImageOps.flip(_inventory_image(
                review, row["owner_uniform"], "RGB"))
            support = ImageOps.flip(_inventory_image(
                review, row["support_uniform"], "RGB"))
            current = ImageOps.flip(_sheet_cell(
                review, row["result_example_sheet"], int(row["row"]),
                int(row["column"]), "RGB"))
            candidate = Image.composite(owner, support, effective)
            outcome_prefix = f"{prefix}-example-{example_index}"
            files = {
                "owner": _save(owner, images / f"{outcome_prefix}-owner.png"),
                "current": _save(current, images / f"{outcome_prefix}-current.png"),
                "candidate": _save(candidate, images / f"{outcome_prefix}-candidate.png"),
                "support": _save(support, images / f"{outcome_prefix}-support.png"),
            }
            outcomes.append({
                "ownerMaterial": row["owner_material"],
                "supportMaterial": row["support_material"],
                "scenario": row["support_scenario"], "files": files})
            outcome_html.append(f'''<section class="outcome"><h3>{html.escape(row["owner_material"])} + {html.escape(row["support_material"])}</h3><div class="visuals four">
<figure><img src="{files['owner']['file']}"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{files['current']['file']}"><figcaption>Rejected R74 outcome</figcaption></figure>
<figure><img src="{files['candidate']['file']}"><figcaption><strong>Restored candidate</strong></figcaption></figure>
<figure><img src="{files['support']['file']}"><figcaption>Support uniform</figcaption></figure>
</div></section>''')
        options = (
            "Still uncertain",
            "Approve restored authored mask",
            "Needs another authored source",
            "Needs restoration adjustment",
            "Keep R74 current after all",
        )
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in options)
        owner_corner_names = [
            CORNER_NAMES[index] for index, value in enumerate(
                expected_owner_corners(topology)) if value]
        support_corner_names = [
            CORNER_NAMES[index] for index, value in enumerate(
                expected_owner_corners(topology)) if not value]
        owner_coverage = metrics["affectedQuarterStrongCoverage"]
        support_presence = metrics["protectedQuarterOwnerPresence"]
        gate_label = ("PASS" if metrics["structuralTargetMet"]
                      else "REVIEW WARNING")
        cards.append(f'''<article class="card" data-key="{key}"><h2>{number}. {html.escape(family)} &middot; {html.escape(topology)}</h2>
<p><strong>Exact canonical ownership:</strong> owner {html.escape(' + '.join(owner_corner_names))}; support {html.escape(' + '.join(support_corner_names))}. <strong>No orientation scoring.</strong></p>
<p><strong>Source:</strong> {html.escape(selected['source'])} &middot; owner {html.escape(selected['owner_material'])} &middot; support {html.escape(selected['support_material'])} &middot; route {html.escape(route)} &middot; aligned CCW {alignment['rotationCounterClockwise']}&deg;</p>
<p class="gate {'pass' if metrics['structuralTargetMet'] else 'warning'}"><strong>70/30 structural gate: {gate_label}.</strong> Owner strong coverage {html.escape(json.dumps(owner_coverage, sort_keys=True))}; protected owner presence {html.escape(json.dumps(support_presence, sort_keys=True))}. A warning is evidence for review, not an automatic rejection of authored detail.</p>
<div class="visuals source"><figure><img src="{source_files['transition']['file']}"><figcaption>Aligned authored transition</figcaption></figure><figure><img src="{source_files['owner']['file']}"><figcaption>Same-atlas owner uniform</figcaption></figure><figure><img src="{source_files['support']['file']}"><figcaption>Same-atlas support uniform</figcaption></figure><figure><img src="{source_files['recovered']['file']}"><figcaption>Recovered authored ownership</figcaption></figure><figure><img src="{source_files['restored']['file']}"><figcaption>Restored authored opacity</figcaption></figure><figure><img src="{source_files['effective']['file']}"><figcaption><strong>Runtime candidate</strong><br>authored opacity + seam ring only</figcaption></figure><figure><img src="{source_files['heatmap']['file']}"><figcaption>Candidate opacity heatmap</figcaption></figure></div>
{''.join(outcome_html)}<div class="answer-row"><label>Decision<select class="answer">{option_html}</select></label><label>Note<input class="note" type="text"></label></div></article>''')
        questions.append({
            "key": key, "family": family, "topology": topology,
            "route": route, "source": selected, "alignment": alignment,
            "process": process, "metrics": metrics,
            "files": source_files, "outcomes": outcomes,
            "preservesAuthoredOpacity": True,
            "runtimeTransform": "shared-edge-continuity-only",
        })

    script = interaction_script(checkpoint)
    style = common_style() + """
.card{padding:18px 0;border-top:2px solid var(--border)}.visuals{display:grid;gap:10px}.visuals.source{grid-template-columns:repeat(7,minmax(130px,256px))}.visuals.four{grid-template-columns:repeat(4,minmax(130px,256px))}.outcome{padding:10px;background:var(--panel);margin:12px 0}figure{margin:0}figure img{width:256px;height:256px;max-width:100%;object-fit:contain}figcaption{color:var(--muted)}.answer-row{display:flex;gap:12px;flex-wrap:wrap}.answer-row label{display:grid;gap:4px;min-width:320px}.answer-row select,.answer-row input{padding:8px}.gate{padding:8px 10px;border-left:4px solid #789}.gate.pass{border-color:#4a6}.gate.warning{border-color:#d90;background:#2b2414}@media(max-width:1100px){.visuals.source{grid-template-columns:repeat(3,1fr)}.visuals.four{grid-template-columns:repeat(2,1fr)}}
"""
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(checkpoint)} authored restoration</title><style>{style}</style></head><body><h1>Blend-mask {html.escape(checkpoint)} authored restoration review</h1><p class="notice">47 R74-rejected masks only. Every candidate comes from a genuine same-atlas two-family transition with full owner/support uniforms. Orientation is exact corner replay, never scored. Restored opacity is preserved; only the one-texel shared-edge continuity ring is applied.</p><div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status"></span></div><textarea id="answer-export-text" readonly></textarea><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    audit_contact_sheets = _audit_contact_sheets(output, questions)
    analysis = {
        "schema": "ei-authored-mask-restoration-questionnaire-v1",
        "checkpoint": checkpoint,
        "decisionsSha256": hashlib.sha256(decisions_path.read_bytes()).hexdigest(),
        "inventorySha256": hashlib.sha256(inventory_path.read_bytes()).hexdigest(),
        "questionCount": len(questions), "questions": questions,
        "auditContactSheets": audit_contact_sheets,
    }
    analysis_path = output / "authored-restoration-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return validate(output)


def validate(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "authored-restoration-analysis.json")
                          .read_text(encoding="utf-8"))
    html_path = next(output.glob("*.html"))
    document = html_path.read_text(encoding="utf-8")
    errors = []
    keys = []
    indexed_files = []
    for question in analysis["questions"]:
        keys.append(question["key"])
        expected = expected_owner_corners(question["topology"])
        if tuple(question["alignment"]["canonicalOwnerCorners"]) != expected:
            errors.append(f'{question["key"]} corner alignment differs')
        if question["alignment"]["orientationSelection"] != (
                "exact-corner-assignment-only"):
            errors.append(f'{question["key"]} used orientation scoring')
        indexed_files.extend(question["files"].values())
        for outcome in question["outcomes"]:
            indexed_files.extend(outcome["files"].values())
            owner = Image.open(output / outcome["files"]["owner"]["file"]).convert("RGB")
            support = Image.open(output / outcome["files"]["support"]["file"]).convert("RGB")
            effective = Image.open(output / question["files"]["effective"]["file"]).convert("L")
            actual = Image.open(output / outcome["files"]["candidate"]["file"]).convert("RGB")
            if _sha(Image.composite(owner, support, effective)) != _sha(actual):
                errors.append(f'{question["key"]} candidate outcome mismatch')
    if len(keys) != 47 or len(set(keys)) != 47:
        errors.append("question keys are not exactly 47 unique replacements")
    for record in indexed_files:
        path = output / record["file"]
        if not path.is_file():
            errors.append(f'missing image {record["file"]}')
        elif hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
            errors.append(f'image hash mismatch {record["file"]}')
    for record in analysis.get("auditContactSheets", []):
        path = output / record["file"]
        if not path.is_file():
            errors.append(f'missing audit contact sheet {record["file"]}')
        elif hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
            errors.append(f'audit contact hash mismatch {record["file"]}')
    for marker in ('class="answer"', 'class="note"',
                   'id="submit-answers"', 'window.openai.sendFollowUpMessage'):
        if marker not in document:
            errors.append(f"HTML missing {marker}")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS", "questions": len(keys),
        "images": len(indexed_files), "missingImages": 0,
        "html": str(html_path),
        "analysis": str(output / "authored-restoration-analysis.json"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", required=True, type=Path)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--decisions", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R75-AUTHORED-RESTORATION")
    args = parser.parse_args()
    print(json.dumps(render(args.inventory, args.review, args.decisions,
                            args.output, args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
