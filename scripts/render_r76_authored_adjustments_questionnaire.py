#!/usr/bin/env python3
"""Render the 20 explicit R75 authored-mask adjustment requests.

This pass never searches for or reorients a source.  It reuses the exact R75
authored transition, uniforms, alignment, recovered ownership, and restored
opacity.  Each adjustment is a deterministic operation named by the reviewer:
recovered ownership, a disclosed recovered/restored hybrid, support-edge
cleanup, or the previously reviewed continuous owner-opacity reach.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from render_authored_mask_repair_pilot import (
    CORNER_NAMES,
    expected_owner_corners,
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script
from render_r75_authored_restoration_questionnaire import _shared_edge_mask
from render_semantic_atlas_review import common_style
from render_snow_ice_continuous_owner_review import (
    apply_continuous_owner_opacity_reach,
)
from render_soft_authored_mask_pilot import _alpha_heatmap


RECOVERED_KEYS = {
    "mask-family-ice-fracture-diagonal-2of4",
    "mask-family-ice-fracture-inside-3of4-v1",
    "mask-family-ice-fracture-inside-3of4-v2",
    "mask-family-ice-fracture-outer-1of4-v1",
    "mask-family-ice-fracture-outer-1of4-v2",
    "mask-family-sand-grain-inside-3of4-v2",
    "mask-family-sand-grain-outer-1of4-v1",
    "mask-family-snow-clumps-diagonal-2of4",
    "mask-family-snow-clumps-inside-3of4-v1",
    "mask-family-snow-clumps-inside-3of4-v2",
    "mask-family-snow-clumps-outer-1of4-v1",
    "mask-family-snow-clumps-outer-1of4-v2",
}
HYBRID_KEYS = {
    "mask-family-ice-fracture-edge-2of4-v1",
    "mask-family-ice-fracture-edge-2of4-v2",
    "mask-family-snow-clumps-edge-2of4-v2",
}
SUPPORT_CLEANUP_KEYS = {
    "mask-family-paving-courses-edge-2of4-v2",
    "mask-family-paving-courses-outer-1of4-v2",
    "mask-family-sand-grain-outer-1of4-v2",
}
OWNER_REACH_KEYS = {
    "mask-family-gravel-chunks-inside-3of4-v2",
    "mask-family-stone-fracture-inside-3of4-v2",
}
EXPECTED_KEYS = (
    RECOVERED_KEYS | HYBRID_KEYS | SUPPORT_CLEANUP_KEYS | OWNER_REACH_KEYS
)


def _pixel_sha(image: Image.Image) -> str:
    return hashlib.sha256(image.tobytes()).hexdigest()


def _load(root: Path, record: dict, mode: str) -> Image.Image:
    path = root / record["file"]
    if hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
        raise ValueError(f"R75 file hash mismatch: {record['file']}")
    with Image.open(path) as source:
        image = source.convert(mode)
        image.load()
    if _pixel_sha(image) != record["pixelSha256"]:
        raise ValueError(f"R75 pixel hash mismatch: {record['file']}")
    return image


def _save(image: Image.Image, path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    return {
        "file": f"images/{path.name}",
        "pixelSha256": _pixel_sha(image),
        "fileSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def _smoothstep(values: np.ndarray) -> np.ndarray:
    values = np.clip(values, 0.0, 1.0)
    return values * values * (3.0 - 2.0 * values)


def _hybrid(recovered: Image.Image, restored: Image.Image,
            recovered_weight: float = 0.72) -> tuple[Image.Image, dict]:
    if not 0.0 < recovered_weight < 1.0:
        raise ValueError("hybrid weight must be between zero and one")
    a = np.asarray(recovered.convert("L"), dtype=np.float32)
    b = np.asarray(restored.convert("L"), dtype=np.float32)
    values = np.rint(a * recovered_weight + b * (1.0 - recovered_weight))
    result = Image.fromarray(np.uint8(np.clip(values, 0, 255)), "L")
    return result, {
        "method": "disclosed-recovered-restored-hybrid",
        "recoveredWeight": recovered_weight,
        "restoredWeight": round(1.0 - recovered_weight, 6),
    }


def clean_support_outer_edges(
        mask: Image.Image, topology: str,
        reach_pixels: int = 48) -> tuple[Image.Image, dict]:
    """Fade owner opacity to zero only near support-owned outer edges."""
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    height, width = values.shape
    if not 0 < reach_pixels < min(height, width) // 2:
        raise ValueError("support cleanup reach is invalid")
    yy, xx = np.mgrid[0:height, 0:width]
    half_y, half_x = height // 2, width // 2
    quadrants = (
        (slice(0, half_y), slice(0, half_x), xx, yy),
        (slice(0, half_y), slice(half_x, width), width - 1 - xx, yy),
        (slice(half_y, height), slice(half_x, width),
         width - 1 - xx, height - 1 - yy),
        (slice(half_y, height), slice(0, half_x), xx, height - 1 - yy),
    )
    result = values.copy()
    changed_region = np.zeros(values.shape, dtype=np.bool_)
    owner = expected_owner_corners(topology)
    for index, owned in enumerate(owner):
        if owned:
            continue
        ys, xs, inward_x, inward_y = quadrants[index]
        # Distance from the nearest one of this corner's two outer edges.
        # The fade is exactly zero on those support edges and reaches one
        # smoothly before the authored transition interior.
        factor = _smoothstep(
            np.minimum(inward_x, inward_y) / float(reach_pixels))
        before = result[ys, xs].copy()
        result[ys, xs] *= factor[ys, xs]
        changed_region[ys, xs] = np.abs(result[ys, xs] - before) > 1e-6
    output = Image.fromarray(
        np.uint8(np.clip(np.rint(result * 255.0), 0, 255)), "L")
    return output, {
        "method": "support-owned-outer-edge-smooth-cleanup",
        "reachPixels": reach_pixels,
        "supportCorners": [
            CORNER_NAMES[index] for index, owned in enumerate(owner)
            if not owned
        ],
        "changedPixels": int(changed_region.sum()),
        "changesOwnerQuadrants": False,
        "supportOuterEdgeTarget": 0.0,
        "usesHardQuadrantFill": False,
    }


def _difference(before: Image.Image, after: Image.Image) -> Image.Image:
    a = np.asarray(before.convert("L"), dtype=np.int16)
    b = np.asarray(after.convert("L"), dtype=np.int16)
    delta = b - a
    rgb = np.zeros((*delta.shape, 3), dtype=np.uint8)
    rgb[..., 1] = np.uint8(np.clip(delta, 0, 255))
    rgb[..., 0] = np.uint8(np.clip(-delta, 0, 255))
    rgb[..., 2] = np.uint8(np.clip(np.abs(delta) // 3, 0, 255))
    return Image.fromarray(rgb, "RGB")


def construct_adjustment(key: str, topology: str,
                         recovered: Image.Image,
                         restored: Image.Image) -> tuple[Image.Image, dict]:
    if key in RECOVERED_KEYS:
        candidate = recovered.copy()
        process = {"method": "recovered-authored-ownership"}
    elif key in HYBRID_KEYS:
        candidate, process = _hybrid(recovered, restored)
        if key == "mask-family-snow-clumps-edge-2of4-v2":
            candidate, cleanup = clean_support_outer_edges(candidate, topology)
            process["supportCleanup"] = cleanup
    elif key in SUPPORT_CLEANUP_KEYS:
        base = (restored if key.startswith("mask-family-paving-courses-")
                else recovered)
        candidate, process = clean_support_outer_edges(base, topology)
        process["input"] = (
            "restored-authored-opacity" if base is restored
            else "recovered-authored-ownership")
    elif key in OWNER_REACH_KEYS:
        candidate, process = apply_continuous_owner_opacity_reach(
            recovered, recovered, topology)
    else:
        raise ValueError(f"unexpected R75 adjustment key: {key}")
    effective = _shared_edge_mask(candidate, topology)
    process["runtimeTransform"] = "shared-edge-continuity-only"
    return effective, process


def _source_files(r75: Path, question: dict, images: Path,
                  prefix: str) -> tuple[dict, dict[str, Image.Image]]:
    modes = {
        "transition": "RGB", "owner": "RGB", "support": "RGB",
        "recovered": "L", "restored": "L", "effective": "L",
    }
    loaded = {
        name: _load(r75, question["files"][name], mode)
        for name, mode in modes.items()
    }
    files = {
        name: _save(image, images / f"{prefix}-{name}.png")
        for name, image in loaded.items()
    }
    return files, loaded


def _audit_contact_sheets(output: Path, questions: list[dict]) -> list[dict]:
    by_family: dict[str, list[dict]] = {}
    for question in questions:
        by_family.setdefault(question["family"], []).append(question)
    columns = (
        ("recovered", "recovered"),
        ("restored", "restored"),
        ("R75", "effective"),
        ("adjusted", "adjusted"),
        ("difference", "difference"),
        ("R75 outcome", "r75"),
        ("adjusted outcome", "adjusted"),
    )
    records = []
    tile = 128
    column_width = 136
    row_height = 172
    header_height = 52
    for family, items in sorted(by_family.items()):
        canvas = Image.new(
            "RGB", (column_width * len(columns),
                    header_height + row_height * len(items)), (24, 24, 24))
        draw = ImageDraw.Draw(canvas)
        draw.text((6, 5), f"R76 authored adjustments: {family}",
                  fill=(240, 240, 240))
        for column, (label, _) in enumerate(columns):
            draw.text((column * column_width + 4, 27), label,
                      fill=(185, 205, 230))
        for row, question in enumerate(items):
            y = header_height + row * row_height
            first_outcome = question["outcomes"][0]
            for column, (_, name) in enumerate(columns):
                if column < 5:
                    relative = question["files"][name]["file"]
                else:
                    relative = first_outcome["files"][name]["file"]
                image = Image.open(output / relative).convert("RGB")
                image.thumbnail((tile, tile), Image.Resampling.LANCZOS)
                canvas.paste(image, (column * column_width + 4, y))
            draw.text((4, y + 132), question["topology"],
                      fill=(240, 240, 240))
            draw.text((4, y + 148), question["process"]["method"],
                      fill=(170, 190, 210))
        path = output / f"audit-contact-{family}.png"
        canvas.save(path, "PNG", optimize=True)
        records.append({
            "family": family,
            "file": path.name,
            "fileSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        })
    return records


def render(r75: Path, decisions_path: Path, output: Path,
           checkpoint: str = "R76-AUTHORED-ADJUSTMENTS") -> dict:
    r75 = r75.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"R76 output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()

    source_bytes = (r75 / "authored-restoration-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    decisions_bytes = decisions_path.read_bytes()
    decisions = json.loads(decisions_bytes.decode("utf-8"))
    pending = decisions["adjustmentRequests"]
    approved = set(decisions["approvedRestorations"])
    if set(pending) != EXPECTED_KEYS or len(pending) != 20:
        raise ValueError("R75 adjustment requests do not match the 20 routes")
    if approved & set(pending):
        raise ValueError("approved and pending R75 masks overlap")
    by_key = {question["key"]: question for question in source["questions"]}
    if not EXPECTED_KEYS <= set(by_key):
        raise ValueError("R75 analysis is missing adjustment evidence")

    cards = []
    questions = []
    for number, key in enumerate(pending, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        source_files, loaded = _source_files(r75, prior, images, prefix)
        adjusted, process = construct_adjustment(
            key, topology, loaded["recovered"], loaded["restored"])
        metrics = mask_metrics(loaded["recovered"], adjusted, topology)
        adjusted_files = {
            "adjusted": _save(adjusted, images / f"{prefix}-adjusted.png"),
            "heatmap": _save(
                _alpha_heatmap(adjusted), images / f"{prefix}-heatmap.png"),
            "difference": _save(
                _difference(loaded["effective"], adjusted),
                images / f"{prefix}-difference.png"),
        }
        outcomes = []
        outcome_html = []
        for outcome_number, prior_outcome in enumerate(prior["outcomes"], 1):
            owner = _load(r75, prior_outcome["files"]["owner"], "RGB")
            support = _load(r75, prior_outcome["files"]["support"], "RGB")
            r75_result = Image.composite(owner, support, loaded["effective"])
            adjusted_result = Image.composite(owner, support, adjusted)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(owner, images / f"{outcome_prefix}-owner.png"),
                "r75": _save(r75_result, images / f"{outcome_prefix}-r75.png"),
                "adjusted": _save(
                    adjusted_result, images / f"{outcome_prefix}-adjusted.png"),
                "support": _save(
                    support, images / f"{outcome_prefix}-support.png"),
            }
            outcomes.append({
                "ownerMaterial": prior_outcome["ownerMaterial"],
                "supportMaterial": prior_outcome["supportMaterial"],
                "scenario": prior_outcome["scenario"],
                "files": files,
            })
            outcome_html.append(f'''<section class="outcome"><h3>{html.escape(prior_outcome["ownerMaterial"])} + {html.escape(prior_outcome["supportMaterial"])}</h3><div class="visuals four">
<figure><img src="{files['owner']['file']}"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{files['r75']['file']}"><figcaption>R75 candidate</figcaption></figure>
<figure><img src="{files['adjusted']['file']}"><figcaption><strong>Adjusted candidate</strong></figcaption></figure>
<figure><img src="{files['support']['file']}"><figcaption>Support uniform</figcaption></figure>
</div></section>''')

        options = (
            "Still uncertain",
            "Approve adjusted mask",
            "Use recovered authored ownership exactly",
            "Needs further adjustment",
            "Keep R75 candidate",
        )
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in options)
        owner_names = [
            CORNER_NAMES[index] for index, owned in enumerate(
                expected_owner_corners(topology)) if owned]
        support_names = [
            CORNER_NAMES[index] for index, owned in enumerate(
                expected_owner_corners(topology)) if not owned]
        cards.append(f'''<article class="card" data-key="{key}"><h2>{number}. {html.escape(prior['family'])} &middot; {html.escape(topology)}</h2>
<p><strong>Your R75 adjustment:</strong> {html.escape(pending[key])}</p>
<p><strong>Exact canonical ownership:</strong> owner {html.escape(' + '.join(owner_names))}; support {html.escape(' + '.join(support_names))}. Source {html.escape(prior['source']['source'])}; original R75 alignment CCW {prior['alignment']['rotationCounterClockwise']}&deg;. No source search, scoring, rotation, mirror, or polarity change occurs in this pass.</p>
<p><strong>Construction:</strong> {html.escape(process['method'])}. {html.escape(json.dumps(process, sort_keys=True))}</p>
<div class="visuals source">
<figure><img src="{source_files['transition']['file']}"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{source_files['owner']['file']}"><figcaption>Same-atlas owner uniform</figcaption></figure>
<figure><img src="{source_files['support']['file']}"><figcaption>Same-atlas support uniform</figcaption></figure>
<figure><img src="{source_files['recovered']['file']}"><figcaption>Recovered authored ownership</figcaption></figure>
<figure><img src="{source_files['restored']['file']}"><figcaption>R75 restored opacity</figcaption></figure>
<figure><img src="{source_files['effective']['file']}"><figcaption>R75 runtime candidate</figcaption></figure>
<figure><img src="{adjusted_files['adjusted']['file']}"><figcaption><strong>Adjusted runtime candidate</strong></figcaption></figure>
<figure><img src="{adjusted_files['heatmap']['file']}"><figcaption>Adjusted opacity heatmap</figcaption></figure>
<figure><img src="{adjusted_files['difference']['file']}"><figcaption>R75 → adjusted: green adds owner, red removes owner</figcaption></figure>
</div>{''.join(outcome_html)}
<div class="answer-row"><label>Decision<select class="answer">{option_html}</select></label><label>Note<input class="note" type="text"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "adjustmentRequest": pending[key],
            "source": prior["source"],
            "alignment": prior["alignment"],
            "process": process,
            "metrics": metrics,
            "files": {**source_files, **adjusted_files},
            "outcomes": outcomes,
        })

    style = common_style() + """
.card{padding:18px 0;border-top:2px solid var(--border)}.visuals{display:grid;gap:10px}.visuals.source{grid-template-columns:repeat(5,minmax(150px,256px))}.visuals.four{grid-template-columns:repeat(4,minmax(150px,256px))}.outcome{padding:10px;background:var(--panel);margin:12px 0}figure{margin:0}figure img{width:256px;height:256px;max-width:100%;object-fit:contain}figcaption{color:var(--muted)}.answer-row{display:flex;gap:12px;flex-wrap:wrap}.answer-row label{display:grid;gap:4px;min-width:320px}.answer-row select,.answer-row input{padding:8px}@media(max-width:1100px){.visuals.source{grid-template-columns:repeat(3,1fr)}.visuals.four{grid-template-columns:repeat(2,1fr)}}
"""
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(checkpoint)} authored adjustments</title><style>{style}</style></head><body><h1>Blend-mask {html.escape(checkpoint)} focused adjustment review</h1><p class="notice"><strong>Review-only; production contains only the 27 R75 approvals.</strong> These are exactly the 20 R75 adjustment requests. Every source, uniform, mirror, and exact corner rotation is frozen to the R75 evidence. The only changes are the explicitly named opacity repairs shown per card.</p><div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status"></span></div><textarea id="answer-export-text" readonly></textarea><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    audit_contact_sheets = _audit_contact_sheets(output, questions)
    analysis = {
        "schema": "ei-authored-mask-adjustment-questionnaire-v1",
        "checkpoint": checkpoint,
        "r75AnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "decisionsSha256": hashlib.sha256(decisions_bytes).hexdigest(),
        "questionCount": len(questions),
        "questions": questions,
        "auditContactSheets": audit_contact_sheets,
    }
    (output / "authored-adjustment-analysis.json").write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return validate(output, decisions_path)


def validate(output: Path, decisions_path: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "authored-adjustment-analysis.json")
                          .read_text(encoding="utf-8"))
    decisions = json.loads(decisions_path.read_text(encoding="utf-8"))
    expected = set(decisions["adjustmentRequests"])
    approved = set(decisions["approvedRestorations"])
    document = next(output.glob("*.html")).read_text(encoding="utf-8")
    errors = []
    keys = [question["key"] for question in analysis["questions"]]
    if len(keys) != 20 or set(keys) != expected or len(set(keys)) != 20:
        errors.append("question keys differ from the 20 R75 adjustments")
    if set(keys) & approved:
        errors.append("approved R75 mask leaked into adjustment review")
    indexed = []
    for question in analysis["questions"]:
        if question["alignment"]["orientationSelection"] != (
                "exact-corner-assignment-only"):
            errors.append(f"{question['key']} orientation was reselected")
        if tuple(question["alignment"]["canonicalOwnerCorners"]) != (
                expected_owner_corners(question["topology"])):
            errors.append(f"{question['key']} ownership differs")
        indexed.extend(question["files"].values())
        adjusted = Image.open(
            output / question["files"]["adjusted"]["file"]).convert("L")
        for outcome in question["outcomes"]:
            indexed.extend(outcome["files"].values())
            owner = Image.open(
                output / outcome["files"]["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / outcome["files"]["support"]["file"]).convert("RGB")
            actual = Image.open(
                output / outcome["files"]["adjusted"]["file"]).convert("RGB")
            if _pixel_sha(Image.composite(owner, support, adjusted)) != (
                    _pixel_sha(actual)):
                errors.append(f"{question['key']} adjusted outcome mismatch")
    for record in indexed:
        path = output / record["file"]
        if not path.is_file():
            errors.append(f"missing image {record['file']}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
            errors.append(f"file hash mismatch {record['file']}")
    for record in analysis.get("auditContactSheets", []):
        path = output / record["file"]
        if not path.is_file():
            errors.append(f"missing contact sheet {record['file']}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
            errors.append(f"contact hash mismatch {record['file']}")
    for marker in ('class="answer"', 'class="note"',
                   'id="submit-answers"',
                   'window.openai.sendFollowUpMessage'):
        if marker not in document:
            errors.append(f"HTML missing {marker}")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS",
        "questions": len(keys),
        "images": len(indexed),
        "missingImages": 0,
        "html": str(next(output.glob("*.html"))),
        "analysis": str(output / "authored-adjustment-analysis.json"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r75", required=True, type=Path)
    parser.add_argument("--decisions", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R76-AUTHORED-ADJUSTMENTS")
    args = parser.parse_args()
    print(json.dumps(render(args.r75, args.decisions, args.output,
                            args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
