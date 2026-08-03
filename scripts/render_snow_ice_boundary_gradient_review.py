#!/usr/bin/env python3
"""Render Ice repair driven by distance from the authored blend boundary."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
from pathlib import Path

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import (
    _difference_image,
    _image_hash,
    _save,
    expected_owner_corners,
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_gradient_repair_review import (
    _connected_component_from_corners,
    _distance_from,
)
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import (
    _load,
    _quadrant_slice,
    _summary,
)


def repair_owner_artifacts_by_boundary_distance(
        mask: Image.Image, topology: str,
        exact_edge_radius: int = 4,
        full_owner_radius: int = 56,
        support_floor: float = 0.20) -> tuple[Image.Image, dict]:
    """Lift owner opacity as distance from the authored support body grows."""
    if exact_edge_radius < 0 or full_owner_radius <= exact_edge_radius:
        raise ValueError("boundary-gradient radii are invalid")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if sum(support) != 1 or not topology.startswith("inside-3of4"):
        raise ValueError("boundary-gradient repair is limited to inside-3of4")
    original_u8 = np.asarray(mask.convert("L"), dtype=np.uint8).copy()
    values = original_u8.astype(np.float32) / 255.0
    strong_support = values <= 1.0 - support_floor
    connected_support = _connected_component_from_corners(
        strong_support, support)
    distance = _distance_from(connected_support)
    pressure = np.clip(
        (distance.astype(np.float32) - exact_edge_radius) /
        float(full_owner_radius - exact_edge_radius), 0.0, 1.0)
    pressure = pressure * pressure * (3.0 - 2.0 * pressure)
    owner_region = np.zeros_like(connected_support, dtype=np.bool_)
    support_region = np.zeros_like(connected_support, dtype=np.bool_)
    for index, owned in enumerate(owner):
        area = _quadrant_slice(
            index, original_u8.shape[0], original_u8.shape[1])
        (owner_region if owned else support_region)[area] = True
    target = np.maximum(values, pressure)
    repaired = values.copy()
    repaired[owner_region] = target[owner_region]
    result_u8 = np.uint8(np.clip(np.rint(repaired * 255.0), 0, 255))
    changed = result_u8 != original_u8
    exact_zone = distance <= exact_edge_radius
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "authored-boundary-distance-owner-lift",
        "supportFloor": support_floor,
        "exactEdgeRadiusPixels": exact_edge_radius,
        "fullOwnerRadiusPixels": full_owner_radius,
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedConnectedSupportPixels": int(
            (changed & connected_support).sum()),
        "changedInsideExactEdgeRadius": int((changed & exact_zone).sum()),
        "changedSupportQuadrantPixels": int(
            (changed & support_region).sum()),
        "usesBoundaryDistanceGradient": True,
        "usesTilePositionGradient": False,
        "usesSolidOwnerPlateau": False,
        "usesGlobalBinaryCleanup": False,
        "usesBroadGaussianSoftening": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, output: Path,
           checkpoint: str = "R58-ICE-BOUNDARY-GRADIENT") -> dict:
    r57 = r57.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"boundary-gradient output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    source_bytes = (r57 / "gradient-repair-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    by_key = {item["key"]: item for item in source["questions"]}
    if any(key not in by_key for key in REVIEW_KEYS):
        raise ValueError("R57 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        aligned = _load(
            r57, prior["r55"]["files"]["alignedSource"], "RGB")
        r55_mask = _load(r57, prior["r55"]["files"]["mask"], "L")
        r57_mask = _load(r57, prior["gradient"]["files"]["mask"], "L")
        boundary, process = repair_owner_artifacts_by_boundary_distance(
            r55_mask, topology)
        boundary_metrics = mask_metrics(r55_mask, boundary, topology)
        r57_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(
                r57_mask, images / f"{prefix}-r57-tile-gradient-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r57_mask),
                images / f"{prefix}-r57-tile-gradient-heatmap.png"),
        }
        boundary_files = {
            "mask": _save(
                boundary, images / f"{prefix}-boundary-gradient-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(boundary),
                images / f"{prefix}-boundary-gradient-heatmap.png"),
            "difference": _save(
                _difference_image(r57_mask, boundary),
                images / f"{prefix}-boundary-gradient-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r57, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r57, prior_outcome["files"]["support"], "RGB")
            r57_result = Image.composite(owner, support, r57_mask)
            boundary_result = Image.composite(owner, support, boundary)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r57Result": _save(
                    r57_result, images / f"{outcome_prefix}-r57-result.png"),
                "boundaryResult": _save(
                    boundary_result,
                    images / f"{outcome_prefix}-boundary-result.png"),
                "support": _save(
                    support, images / f"{outcome_prefix}-support.png"),
            }
            entry = {
                name: prior_outcome[name] for name in (
                    "ownerMaterial", "supportMaterial", "scenario",
                    "ownerSource", "supportSource")
            }
            entry["files"] = files
            outcome_entries.append(entry)
            outcome_cards.append(f'''<section class="example"><h3>{html.escape(entry['ownerMaterial'])} + {html.escape(entry['supportMaterial'])}</h3>
<p class="muted">Owner: {html.escape(entry['ownerSource'])} · support: {html.escape(entry['supportSource'])}</p><div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform · distant field</figcaption></figure>
<figure><img src="{files['r57Result']['file']}" alt="R57 tile gradient result"><figcaption><strong>R57 tile-position gradient</strong></figcaption></figure>
<figure><img src="{files['boundaryResult']['file']}" alt="Boundary gradient result"><figcaption><strong>Authored-edge-distance gradient</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform · connected body</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer authored-edge-distance gradient",
            "Keep R57 tile-position gradient",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture · {html.escape(topology)}</h2>
<p class="notice">The new paint thickness follows the irregular authored support boundary, not quadrant coordinates. The connected blend and first {process['exactEdgeRadiusPixels']} px remain exact; owner opacity then increases smoothly with distance and reaches full ownership {process['fullOwnerRadiusPixels']} px from that edge. Existing authored opacity above this floor remains untouched.</p>
<p class="muted">Same source {html.escape(prior['sourceProvenance'])}, corner assignment, and semantic transform. R57's tile-position haze is shown only as the rejected comparison.</p>
<p class="metric">R57: {html.escape(_summary(prior['gradient']['metrics']))}<br>Boundary distance: {html.escape(_summary(boundary_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) · connected blend changed: {process['changedConnectedSupportPixels']} · exact edge zone changed: {process['changedInsideExactEdgeRadius']} · SW changed: {process['changedSupportQuadrantPixels']} · alpha levels: {process['distinctAlphaValues']}</p>
<div class="evidence-grid">
<figure><img src="{r57_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{r57_files['mask']['file']}" alt="R57 tile gradient mask"><figcaption><strong>R57 rejected tile-position mask</strong></figcaption></figure>
<figure><img src="{r57_files['heatmap']['file']}" alt="R57 heatmap"><figcaption>R57 alpha heatmap</figcaption></figure>
<figure><img src="{boundary_files['mask']['file']}" alt="Boundary-distance mask"><figcaption><strong>Authored-edge-distance mask</strong></figcaption></figure>
<figure><img src="{boundary_files['heatmap']['file']}" alt="Boundary-distance heatmap"><figcaption>Boundary-distance alpha heatmap</figcaption></figure>
<figure><img src="{boundary_files['difference']['file']}" alt="Boundary-gradient difference"><figcaption>R57 → boundary-distance difference</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional feather distance or contour detail"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": prior["sourceAlignment"],
            "allowedDecisions": decisions,
            "r57": {"metrics": prior["gradient"]["metrics"],
                    "files": r57_files},
            "boundary": {"process": process, "metrics": boundary_metrics,
                         "files": boundary_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · authored-edge-distance gradient</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R57's quadrant-shaped haze is rejected. These candidates follow the supplied painted reference: thin owner paint next to the exact authored blend contour, progressively thicker with distance, and fully owned in the distant field.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-boundary-gradient-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoint": source["checkpoint"],
        "sourceAnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "boundary-gradient-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "boundary-gradient-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("boundary gradient must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("boundary-gradient keys or order differ from R57")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["r57"]["files"].values())
        indexed.extend(question["boundary"]["files"].values())
        process = question["boundary"]["process"]
        if (not process["usesBoundaryDistanceGradient"] or
                process["usesTilePositionGradient"] or
                process["usesSolidOwnerPlateau"] or
                process["usesGlobalBinaryCleanup"] or
                process["usesBroadGaussianSoftening"] or
                process["changedConnectedSupportPixels"] or
                process["changedInsideExactEdgeRadius"] or
                process["changedSupportQuadrantPixels"]):
            errors.append(f"boundary gradient escaped contract: {question['key']}")
        if process["distinctAlphaValues"] < 64:
            errors.append(f"boundary gradient lost alpha detail: {question['key']}")
        r57_mask = Image.open(
            output / question["r57"]["files"]["mask"]["file"])
        boundary = Image.open(
            output / question["boundary"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r57_mask)) !=
                    files["r57Result"]["pixelSha256"]):
                errors.append(f"R57 outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, boundary)) !=
                    files["boundaryResult"]["pixelSha256"]):
                errors.append(f"boundary outcome mismatch: {question['key']}")
    for entry in indexed:
        path = output / entry["file"]
        if not path.is_file():
            errors.append(f"missing indexed image: {entry['file']}")
        elif hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
            errors.append(f"file hash mismatch: {entry['file']}")
    for reference in references:
        if (reference.startswith(("/", "\\")) or ":" in reference or
                not (output / reference).is_file()):
            errors.append(f"missing or non-relocatable image: {reference}")
    if references != {item["file"] for item in indexed}:
        errors.append("HTML image references differ from indexed images")
    for marker in ('class="pilot-question"',
                   'class="answer form-select"',
                   'class="note form-control"', 'id="submit-answers"',
                   "window.openai.sendFollowUpMessage",
                   "localStorage.setItem"):
        if marker not in document:
            errors.append(f"HTML missing interaction marker {marker}")
    if document.count('class="pilot-question"') != len(REVIEW_KEYS):
        errors.append("HTML question count differs from analysis")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS", "questions": len(questions),
        "outcomes": outcomes, "verifiedImages": len(indexed),
        "htmlImageReferences": len(references),
        "runtimeChanged": False,
    }


def validate_review_decisions(output: Path, decisions_path: Path) -> dict:
    """Verify rejection of R58's square, hard-clamped distance field."""
    rendered = validate_output(output)
    analysis = json.loads((output / "boundary-gradient-analysis.json")
                          .read_text(encoding="utf-8"))
    review = json.loads(decisions_path.read_text(encoding="utf-8"))
    questions = {item["key"]: item for item in analysis["questions"]}
    decisions = review.get("decisions", {})
    if set(decisions) != set(questions):
        raise ValueError("R58 decision keys do not exactly match the review")
    for key, decision in decisions.items():
        if decision.get("answer") != "Reject square cutoff feather":
            raise ValueError(f"R58 square cutoff must remain rejected: {key}")
        note = decision.get("note", "").casefold()
        if ("straight" not in note or
                not any(side in note for side in ("east", "right")) or
                "rounded euclidean" not in note or
                "hard full-opacity cutoff" not in note):
            raise ValueError(f"R58 rounded-feather direction is missing: {key}")
    if review.get("approvedRoutes"):
        raise ValueError("R58 must not authorize a generation route")
    return {
        **rendered,
        "approved": 0,
        "rejected": len(decisions),
        "pending": len(decisions),
        "generationChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r57", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R58-ICE-BOUNDARY-GRADIENT")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
