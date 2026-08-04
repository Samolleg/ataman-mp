#!/usr/bin/env python3
"""Render the four explicit R76 non-rectilinear mask adjustments.

This pass freezes every R76 source, uniform, corner assignment, rotation, and
polarity.  It only replaces the rejected rectangular support cleanup with
disclosed, deterministic fades applied to recovered authored opacity.  The
four candidates remain review-only until the user explicitly approves them.
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
from render_r76_authored_adjustments_questionnaire import (
    _alpha_heatmap,
    _difference,
    _hybrid,
    _load,
    _pixel_sha,
    _save,
)
from render_semantic_atlas_review import common_style


EXPECTED_KEYS = (
    "mask-family-paving-courses-edge-2of4-v2",
    "mask-family-paving-courses-outer-1of4-v2",
    "mask-family-sand-grain-outer-1of4-v2",
    "mask-family-snow-clumps-edge-2of4-v2",
)


def _smoothstep(values: np.ndarray) -> np.ndarray:
    values = np.clip(values, 0.0, 1.0)
    return values * values * (3.0 - 2.0 * values)


def _coherent_noise(height: int, width: int, seed: int) -> np.ndarray:
    """Return deterministic low-frequency noise, never per-pixel speckle."""
    rng = np.random.default_rng(seed)
    layers = []
    for cells, weight in ((6, 0.72), (13, 0.28)):
        small = rng.normal(0.0, 1.0, (cells, cells)).astype(np.float32)
        expanded = Image.fromarray(small, "F").resize(
            (width, height), Image.Resampling.BICUBIC)
        layers.append(np.asarray(expanded, dtype=np.float32) * weight)
    result = layers[0] + layers[1]
    result -= float(result.mean())
    deviation = float(result.std())
    return result / deviation if deviation > 1e-6 else result


def _from_float(values: np.ndarray) -> Image.Image:
    return Image.fromarray(
        np.uint8(np.clip(np.rint(values * 255.0), 0, 255)), "L")


def _noisy_south_feather(mask: Image.Image, topology: str, *,
                         reach_pixels: float, noise_pixels: float,
                         seed: int, method: str) -> tuple[Image.Image, dict]:
    """Fade owner opacity from a wavy boundary to zero at the south edge."""
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    height, width = values.shape
    noise = np.clip(_coherent_noise(height, width, seed), -1.8, 1.8)
    yy = np.arange(height, dtype=np.float32)[:, None]
    distance_from_south = (height - 1.0) - yy
    local_reach = np.clip(
        reach_pixels + noise * noise_pixels,
        reach_pixels * 0.68,
        reach_pixels * 1.32,
    )
    factor = _smoothstep(distance_from_south / local_reach)
    output = _shared_edge_mask(_from_float(values * factor), topology)
    crossing = []
    for x in range(width):
        ys = np.flatnonzero(factor[:, x] >= 0.5)
        crossing.append(int(ys[-1]) if len(ys) else 0)
    process = {
        "method": method,
        "input": "recovered-authored-ownership",
        "supportEdge": "south",
        "reachPixels": reach_pixels,
        "reachFractionOfHeight": round(reach_pixels / height, 4),
        "noisePixels": noise_pixels,
        "noiseType": "deterministic-low-frequency-bicubic",
        "noiseSeed": seed,
        "halfOpacityBoundaryYRange": [min(crossing), max(crossing)],
        "halfOpacityBoundaryYStdDev": round(float(np.std(crossing)), 3),
        "southEdgeTarget": 0.0,
        "usesStraightInteriorBoundary": False,
        "usesHardQuadrantFill": False,
        "runtimeTransform": "shared-edge-continuity-only",
    }
    return output, process


def _support_corner_radial_fade(
        mask: Image.Image, topology: str, *, radius_pixels: float,
        noise_fraction: float, seed: int, method: str) -> tuple[Image.Image, dict]:
    """Attenuate recovered owner opacity around support corners with arcs."""
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    height, width = values.shape
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    noise = np.clip(_coherent_noise(height, width, seed), -1.8, 1.8)
    corners = (
        (0.0, 0.0),
        (width - 1.0, 0.0),
        (width - 1.0, height - 1.0),
        (0.0, height - 1.0),
    )
    owner = expected_owner_corners(topology)
    factor = np.ones(values.shape, dtype=np.float32)
    support_names = []
    for index, owned in enumerate(owner):
        if owned:
            continue
        support_names.append(CORNER_NAMES[index])
        cx, cy = corners[index]
        distance = np.sqrt((xx - cx) ** 2 + (yy - cy) ** 2)
        local_radius = radius_pixels * (1.0 + noise * noise_fraction)
        local_radius = np.clip(local_radius,
                               radius_pixels * 0.76,
                               radius_pixels * 1.24)
        factor = np.minimum(factor, _smoothstep(distance / local_radius))
    output = _shared_edge_mask(_from_float(values * factor), topology)
    process = {
        "method": method,
        "input": "recovered-authored-ownership",
        "supportCorners": support_names,
        "radiusPixels": radius_pixels,
        "radiusFractionOfTile": round(radius_pixels / min(width, height), 4),
        "noiseFraction": noise_fraction,
        "noiseType": ("none" if noise_fraction == 0.0
                      else "deterministic-low-frequency-bicubic"),
        "noiseSeed": seed,
        "shape": "support-corner radial arcs",
        "usesStraightInteriorBoundary": False,
        "usesHardQuadrantFill": False,
        "runtimeTransform": "shared-edge-continuity-only",
    }
    return output, process


def construct_candidate(key: str, topology: str,
                        recovered: Image.Image,
                        restored: Image.Image) -> tuple[Image.Image, dict]:
    if key == "mask-family-paving-courses-edge-2of4-v2":
        return _noisy_south_feather(
            recovered, topology, reach_pixels=102.0, noise_pixels=8.0,
            seed=0x77A1, method="recovered-opacity-noisy-south-feather")
    if key == "mask-family-paving-courses-outer-1of4-v2":
        return _support_corner_radial_fade(
            recovered, topology, radius_pixels=164.0, noise_fraction=0.0,
            seed=0x77A2, method="recovered-opacity-circular-support-fade")
    if key == "mask-family-sand-grain-outer-1of4-v2":
        return _support_corner_radial_fade(
            recovered, topology, radius_pixels=126.0, noise_fraction=0.09,
            seed=0x77A3, method="recovered-opacity-uneven-support-arcs")
    if key == "mask-family-snow-clumps-edge-2of4-v2":
        hybrid, blend = _hybrid(recovered, restored)
        candidate, process = _noisy_south_feather(
            hybrid, topology, reach_pixels=74.0, noise_pixels=12.0,
            seed=0x77A4,
            method="authored-hybrid-noisy-south-support-feather")
        process["input"] = "72-percent-recovered-28-percent-restored"
        process["hybrid"] = blend
        return candidate, process
    raise ValueError(f"unexpected R76 adjustment key: {key}")


def _audit_contact_sheet(output: Path, questions: list[dict]) -> dict:
    columns = (
        ("transition", "transition"),
        ("recovered", "recovered"),
        ("restored", "restored"),
        ("R76", "r76"),
        ("R77", "candidate"),
        ("difference", "difference"),
        ("R76 outcome", "r76"),
        ("R77 outcome", "candidate"),
    )
    tile = 128
    column_width = 136
    row_height = 166
    header_height = 54
    canvas = Image.new(
        "RGB", (column_width * len(columns),
                header_height + row_height * len(questions)), (24, 24, 24))
    draw = ImageDraw.Draw(canvas)
    draw.text((6, 5), "R77 non-rectilinear authored adjustments",
              fill=(240, 240, 240))
    for column, (label, _) in enumerate(columns):
        draw.text((column * column_width + 4, 29), label,
                  fill=(185, 205, 230))
    for row, question in enumerate(questions):
        y = header_height + row * row_height
        first_outcome = question["outcomes"][0]
        for column, (_, name) in enumerate(columns):
            record = (question["files"][name] if column < 6
                      else first_outcome["files"][name])
            image = Image.open(output / record["file"]).convert("RGB")
            image.thumbnail((tile, tile), Image.Resampling.LANCZOS)
            canvas.paste(image, (column * column_width + 4, y))
        draw.text((4, y + 132), question["family"], fill=(240, 240, 240))
        draw.text((4, y + 148), question["topology"], fill=(170, 190, 210))
    path = output / "audit-contact-r77-nonrectilinear.png"
    canvas.save(path, "PNG", optimize=True)
    return {
        "file": path.name,
        "fileSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
    }


def render(r76: Path, decisions_path: Path, output: Path,
           checkpoint: str = "R77-NONRECTILINEAR-ADJUSTMENTS") -> dict:
    r76 = r76.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"R77 output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()

    analysis_bytes = (r76 / "authored-adjustment-analysis.json").read_bytes()
    source = json.loads(analysis_bytes.decode("utf-8"))
    decisions_bytes = decisions_path.read_bytes()
    decisions = json.loads(decisions_bytes.decode("utf-8"))
    requests = decisions["adjustmentRequests"]
    approved = set(decisions["approvedAdjustments"])
    if tuple(requests) != EXPECTED_KEYS:
        raise ValueError("R76 adjustment requests differ from the four routes")
    if approved & set(requests):
        raise ValueError("approved and pending R76 masks overlap")
    by_key = {question["key"]: question for question in source["questions"]}
    if not set(EXPECTED_KEYS) <= set(by_key):
        raise ValueError("R76 analysis is missing adjustment evidence")

    cards = []
    questions = []
    for number, key in enumerate(EXPECTED_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        modes = {
            "transition": "RGB", "owner": "RGB", "support": "RGB",
            "recovered": "L", "restored": "L", "effective": "L",
            "adjusted": "L",
        }
        loaded = {
            name: _load(r76, prior["files"][name], mode)
            for name, mode in modes.items()
        }
        files = {
            "transition": _save(loaded["transition"], images / f"{prefix}-transition.png"),
            "owner": _save(loaded["owner"], images / f"{prefix}-owner.png"),
            "support": _save(loaded["support"], images / f"{prefix}-support.png"),
            "recovered": _save(loaded["recovered"], images / f"{prefix}-recovered.png"),
            "restored": _save(loaded["restored"], images / f"{prefix}-restored.png"),
            "r76": _save(loaded["adjusted"], images / f"{prefix}-r76.png"),
        }
        candidate, process = construct_candidate(
            key, topology, loaded["recovered"], loaded["restored"])
        files.update({
            "candidate": _save(candidate, images / f"{prefix}-candidate.png"),
            "heatmap": _save(_alpha_heatmap(candidate), images / f"{prefix}-heatmap.png"),
            "difference": _save(_difference(loaded["adjusted"], candidate),
                                images / f"{prefix}-difference.png"),
        })

        outcomes = []
        outcome_html = []
        for outcome_number, old_outcome in enumerate(prior["outcomes"], 1):
            owner = _load(r76, old_outcome["files"]["owner"], "RGB")
            support = _load(r76, old_outcome["files"]["support"], "RGB")
            r76_result = Image.composite(owner, support, loaded["adjusted"])
            candidate_result = Image.composite(owner, support, candidate)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            outcome_files = {
                "owner": _save(owner, images / f"{outcome_prefix}-owner.png"),
                "r76": _save(r76_result, images / f"{outcome_prefix}-r76.png"),
                "candidate": _save(candidate_result,
                                   images / f"{outcome_prefix}-candidate.png"),
                "support": _save(support, images / f"{outcome_prefix}-support.png"),
            }
            outcomes.append({
                "ownerMaterial": old_outcome["ownerMaterial"],
                "supportMaterial": old_outcome["supportMaterial"],
                "scenario": old_outcome["scenario"],
                "files": outcome_files,
            })
            outcome_html.append(f'''<section class="outcome"><h3>{html.escape(old_outcome["ownerMaterial"])} + {html.escape(old_outcome["supportMaterial"])}</h3><div class="visuals four">
<figure><img src="{outcome_files['owner']['file']}"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{outcome_files['r76']['file']}"><figcaption>Rejected R76 candidate</figcaption></figure>
<figure><img src="{outcome_files['candidate']['file']}"><figcaption><strong>R77 candidate</strong></figcaption></figure>
<figure><img src="{outcome_files['support']['file']}"><figcaption>Support uniform</figcaption></figure>
</div></section>''')

        owner_names = [
            CORNER_NAMES[index] for index, owned in enumerate(
                expected_owner_corners(topology)) if owned]
        support_names = [
            CORNER_NAMES[index] for index, owned in enumerate(
                expected_owner_corners(topology)) if not owned]
        options = (
            "Still uncertain",
            "Approve non-rectilinear adjustment",
            "Needs further adjustment",
            "Use recovered authored ownership exactly",
            "Keep R76 candidate",
        )
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in options)
        cards.append(f'''<article class="card" data-key="{key}"><h2>{number}. {html.escape(prior['family'])} &middot; {html.escape(topology)}</h2>
<p><strong>Your R76 adjustment:</strong> {html.escape(requests[key])}</p>
<p><strong>Exact canonical ownership:</strong> owner {html.escape(' + '.join(owner_names))}; support {html.escape(' + '.join(support_names))}. Source {html.escape(prior['source']['source'])}; frozen R75 alignment CCW {prior['alignment']['rotationCounterClockwise']}&deg;. No source search, scoring, rotation, mirror, or polarity change occurs.</p>
<p><strong>Construction:</strong> {html.escape(json.dumps(process, sort_keys=True))}</p>
<div class="visuals source">
<figure><img src="{files['transition']['file']}"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{files['owner']['file']}"><figcaption>Same-atlas owner uniform</figcaption></figure>
<figure><img src="{files['support']['file']}"><figcaption>Same-atlas support uniform</figcaption></figure>
<figure><img src="{files['recovered']['file']}"><figcaption>Recovered authored ownership</figcaption></figure>
<figure><img src="{files['restored']['file']}"><figcaption>R75 restored opacity</figcaption></figure>
<figure><img src="{files['r76']['file']}"><figcaption>Rejected R76 candidate</figcaption></figure>
<figure><img src="{files['candidate']['file']}"><figcaption><strong>R77 review-only candidate</strong></figcaption></figure>
<figure><img src="{files['heatmap']['file']}"><figcaption>R77 opacity heatmap</figcaption></figure>
<figure><img src="{files['difference']['file']}"><figcaption>R76 to R77: green adds owner, red removes owner</figcaption></figure>
</div>{''.join(outcome_html)}
<div class="answer-row"><label>Decision<select class="answer">{option_html}</select></label><label>Note<input class="note" type="text"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "adjustmentRequest": requests[key],
            "source": prior["source"],
            "alignment": prior["alignment"],
            "process": process,
            "metrics": mask_metrics(loaded["recovered"], candidate, topology),
            "files": files,
            "outcomes": outcomes,
        })

    style = common_style() + """
.card{padding:18px 0;border-top:2px solid var(--border)}.visuals{display:grid;gap:10px}.visuals.source{grid-template-columns:repeat(5,minmax(150px,256px))}.visuals.four{grid-template-columns:repeat(4,minmax(150px,256px))}.outcome{padding:10px;background:var(--panel);margin:12px 0}figure{margin:0}figure img{width:256px;height:256px;max-width:100%;object-fit:contain}figcaption{color:var(--muted)}.answer-row{display:flex;gap:12px;flex-wrap:wrap}.answer-row label{display:grid;gap:4px;min-width:320px}.answer-row select,.answer-row input{padding:8px}@media(max-width:1100px){.visuals.source{grid-template-columns:repeat(3,1fr)}.visuals.four{grid-template-columns:repeat(2,1fr)}}
"""
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(checkpoint)}</title><style>{style}</style></head><body><h1>Blend-mask {html.escape(checkpoint)} focused review</h1><p class="notice"><strong>Review-only; production contains only the 16 explicit R76 approvals.</strong> These are exactly the four R76 adjustment requests. Source artwork, uniforms, corner ownership, rotation, and polarity are frozen. Only the disclosed non-rectilinear attenuation differs.</p><div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status"></span></div><textarea id="answer-export-text" readonly></textarea><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    contact = _audit_contact_sheet(output, questions)
    analysis = {
        "schema": "ei-authored-mask-nonrectilinear-questionnaire-v1",
        "checkpoint": checkpoint,
        "r76AnalysisSha256": hashlib.sha256(analysis_bytes).hexdigest(),
        "decisionsSha256": hashlib.sha256(decisions_bytes).hexdigest(),
        "questionCount": len(questions),
        "questions": questions,
        "auditContactSheet": contact,
    }
    (output / "nonrectilinear-adjustment-analysis.json").write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return validate(output, decisions_path)


def validate(output: Path, decisions_path: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "nonrectilinear-adjustment-analysis.json")
                          .read_text(encoding="utf-8"))
    decisions = json.loads(decisions_path.read_text(encoding="utf-8"))
    keys = [question["key"] for question in analysis["questions"]]
    errors = []
    if tuple(keys) != EXPECTED_KEYS or len(set(keys)) != 4:
        errors.append("question keys differ from the four R76 adjustments")
    if set(keys) & set(decisions["approvedAdjustments"]):
        errors.append("approved R76 mask leaked into follow-up review")
    indexed = []
    for question in analysis["questions"]:
        if question["alignment"]["orientationSelection"] != (
                "exact-corner-assignment-only"):
            errors.append(f"{question['key']} orientation was reselected")
        if tuple(question["alignment"]["canonicalOwnerCorners"]) != (
                expected_owner_corners(question["topology"])):
            errors.append(f"{question['key']} ownership differs")
        indexed.extend(question["files"].values())
        candidate = Image.open(
            output / question["files"]["candidate"]["file"]).convert("L")
        for outcome in question["outcomes"]:
            indexed.extend(outcome["files"].values())
            owner = Image.open(output / outcome["files"]["owner"]["file"]).convert("RGB")
            support = Image.open(output / outcome["files"]["support"]["file"]).convert("RGB")
            actual = Image.open(output / outcome["files"]["candidate"]["file"]).convert("RGB")
            if _pixel_sha(Image.composite(owner, support, candidate)) != _pixel_sha(actual):
                errors.append(f"{question['key']} candidate outcome mismatch")
    for record in indexed:
        path = output / record["file"]
        if not path.is_file():
            errors.append(f"missing image {record['file']}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != record["fileSha256"]:
            errors.append(f"file hash mismatch {record['file']}")
    contact = analysis["auditContactSheet"]
    contact_path = output / contact["file"]
    if not contact_path.is_file():
        errors.append(f"missing contact sheet {contact['file']}")
    elif hashlib.sha256(contact_path.read_bytes()).hexdigest() != contact["fileSha256"]:
        errors.append("contact sheet hash mismatch")
    document = next(output.glob("*.html")).read_text(encoding="utf-8")
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
        "analysis": str(output / "nonrectilinear-adjustment-analysis.json"),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r76", required=True, type=Path)
    parser.add_argument("--decisions", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R77-NONRECTILINEAR-ADJUSTMENTS")
    args = parser.parse_args()
    print(json.dumps(render(args.r76, args.decisions, args.output,
                            args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
