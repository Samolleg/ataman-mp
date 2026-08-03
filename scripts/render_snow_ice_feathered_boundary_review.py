#!/usr/bin/env python3
"""Render rounded, non-clamping Ice boundary feather review."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import math
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
from render_snow_ice_gradient_repair_review import (
    _connected_component_from_corners,
)
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _quadrant_slice, _summary


def _chamfer_euclidean_distance(selected: np.ndarray) -> np.ndarray:
    """Approximate Euclidean distance with axial and diagonal weights."""
    height, width = selected.shape
    distance = np.full((height, width), float(height + width),
                       dtype=np.float32)
    distance[selected] = 0.0
    diagonal = math.sqrt(2.0)
    for y in range(height):
        for x in range(width):
            value = distance[y, x]
            if y:
                value = min(value, distance[y - 1, x] + 1.0)
                if x:
                    value = min(value, distance[y - 1, x - 1] + diagonal)
                if x + 1 < width:
                    value = min(value, distance[y - 1, x + 1] + diagonal)
            if x:
                value = min(value, distance[y, x - 1] + 1.0)
            distance[y, x] = value
    for y in range(height - 1, -1, -1):
        for x in range(width - 1, -1, -1):
            value = distance[y, x]
            if y + 1 < height:
                value = min(value, distance[y + 1, x] + 1.0)
                if x:
                    value = min(value, distance[y + 1, x - 1] + diagonal)
                if x + 1 < width:
                    value = min(value, distance[y + 1, x + 1] + diagonal)
            if x + 1 < width:
                value = min(value, distance[y, x + 1] + 1.0)
            distance[y, x] = value
    return distance


def repair_owner_artifacts_with_feathered_boundary(
        mask: Image.Image, topology: str,
        exact_edge_radius: int = 4,
        feather_scale: float = 32.0,
        feather_exponent: float = 1.35,
        support_floor: float = 0.20) -> tuple[Image.Image, dict]:
    """Lift owner opacity with rounded distance and no saturation cutoff."""
    if (exact_edge_radius < 0 or feather_scale <= 0.0 or
            feather_exponent <= 0.0):
        raise ValueError("rounded feather parameters are invalid")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if sum(support) != 1 or not topology.startswith("inside-3of4"):
        raise ValueError("rounded feather is limited to inside-3of4")
    original_u8 = np.asarray(mask.convert("L"), dtype=np.uint8).copy()
    values = original_u8.astype(np.float32) / 255.0
    strong_support = values <= 1.0 - support_floor
    connected_support = _connected_component_from_corners(
        strong_support, support)
    distance = _chamfer_euclidean_distance(connected_support)
    outward = np.maximum(distance - float(exact_edge_radius), 0.0)
    pressure = 1.0 - np.exp(-np.power(
        outward / float(feather_scale), feather_exponent))
    # Never create a full-opacity isocontour. Existing authored 255 values stay.
    pressure = np.minimum(pressure, 254.0 / 255.0)
    owner_region = np.zeros_like(connected_support, dtype=np.bool_)
    support_region = np.zeros_like(connected_support, dtype=np.bool_)
    for index, owned in enumerate(owner):
        area = _quadrant_slice(
            index, original_u8.shape[0], original_u8.shape[1])
        (owner_region if owned else support_region)[area] = True
    repaired = values.copy()
    repaired[owner_region] = np.maximum(
        values[owner_region], pressure[owner_region])
    result_u8 = np.uint8(np.clip(np.rint(repaired * 255.0), 0, 255))
    changed = result_u8 != original_u8
    exact_zone = distance <= exact_edge_radius
    newly_full = np.logical_and(result_u8 == 255, original_u8 < 255)
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "rounded-authored-boundary-asymptotic-feather",
        "distanceMetric": "chamfer-euclidean",
        "supportFloor": support_floor,
        "exactEdgeRadiusPixels": exact_edge_radius,
        "featherScalePixels": feather_scale,
        "featherExponent": feather_exponent,
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedConnectedSupportPixels": int(
            (changed & connected_support).sum()),
        "changedInsideExactEdgeRadius": int((changed & exact_zone).sum()),
        "changedSupportQuadrantPixels": int(
            (changed & support_region).sum()),
        "newFullOpacityPixels": int(newly_full.sum()),
        "usesHardSaturation": False,
        "usesBoundaryDistanceGradient": True,
        "usesTilePositionGradient": False,
        "usesSolidOwnerPlateau": False,
        "usesGlobalBinaryCleanup": False,
        "usesBroadGaussianSoftening": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, r58: Path, output: Path,
           checkpoint: str = "R59-ICE-FEATHERED-BOUNDARY") -> dict:
    """Render the rejected square R58 field beside the rounded R59 field."""
    r57 = r57.resolve()
    r58 = r58.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"feathered-boundary output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()

    r57_bytes = (r57 / "gradient-repair-analysis.json").read_bytes()
    r58_bytes = (r58 / "boundary-gradient-analysis.json").read_bytes()
    r57_analysis = json.loads(r57_bytes.decode("utf-8"))
    r58_analysis = json.loads(r58_bytes.decode("utf-8"))
    r57_by_key = {item["key"]: item for item in r57_analysis["questions"]}
    r58_by_key = {item["key"]: item for item in r58_analysis["questions"]}
    if (any(key not in r57_by_key for key in REVIEW_KEYS) or
            any(key not in r58_by_key for key in REVIEW_KEYS)):
        raise ValueError("R57 or R58 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        pristine = r57_by_key[key]
        rejected = r58_by_key[key]
        topology = pristine["topology"]
        prefix = f"{number:02d}-{pristine['family']}-{topology}"
        aligned = _load(
            r57, pristine["r55"]["files"]["alignedSource"], "RGB")
        r55_mask = _load(r57, pristine["r55"]["files"]["mask"], "L")
        r58_mask = _load(r58, rejected["boundary"]["files"]["mask"], "L")
        feathered, process = repair_owner_artifacts_with_feathered_boundary(
            r55_mask, topology)
        feathered_metrics = mask_metrics(r55_mask, feathered, topology)

        r58_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(
                r58_mask, images / f"{prefix}-r58-square-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r58_mask),
                images / f"{prefix}-r58-square-heatmap.png"),
        }
        feathered_files = {
            "mask": _save(
                feathered, images / f"{prefix}-r59-feathered-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(feathered),
                images / f"{prefix}-r59-feathered-heatmap.png"),
            "difference": _save(
                _difference_image(r58_mask, feathered),
                images / f"{prefix}-r58-to-r59-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                rejected["outcomes"], 1):
            owner = _load(r58, prior_outcome["files"]["owner"], "RGB")
            support = _load(r58, prior_outcome["files"]["support"], "RGB")
            r58_result = Image.composite(owner, support, r58_mask)
            feathered_result = Image.composite(owner, support, feathered)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r58Result": _save(
                    r58_result, images / f"{outcome_prefix}-r58-result.png"),
                "featheredResult": _save(
                    feathered_result,
                    images / f"{outcome_prefix}-r59-result.png"),
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
<p class="muted">Owner: {html.escape(entry['ownerSource'])} &middot; support: {html.escape(entry['supportSource'])}</p><div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform &middot; NW/NE/SE</figcaption></figure>
<figure><img src="{files['r58Result']['file']}" alt="R58 square feather result"><figcaption><strong>R58 rejected square/clamped feather</strong></figcaption></figure>
<figure><img src="{files['featheredResult']['file']}" alt="R59 rounded feather result"><figcaption><strong>R59 rounded, non-clamping feather</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform &middot; SW</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer rounded asymptotic feather",
            "Keep R58 square/clamped feather",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture &middot; {html.escape(topology)}</h2>
<p class="notice">R59 keeps the exact authored blend contour and its first {process['exactEdgeRadiusPixels']} px untouched. Beyond it, owner opacity follows rounded axial/diagonal distance and approaches full ownership gradually; there is no square metric and no hard full-opacity radius.</p>
<p class="muted">Same authored source, exact corners, transform, owner uniform, and support uniform as R58. R59 starts from the pristine R55-derived mask, not from the rejected R58 repair.</p>
<p class="metric">R58: {html.escape(_summary(rejected['boundary']['metrics']))}<br>R59: {html.escape(_summary(feathered_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) &middot; connected blend changed: {process['changedConnectedSupportPixels']} &middot; exact edge zone changed: {process['changedInsideExactEdgeRadius']} &middot; SW changed: {process['changedSupportQuadrantPixels']} &middot; newly full pixels: {process['newFullOpacityPixels']} &middot; alpha levels: {process['distinctAlphaValues']}</p>
<div class="evidence-grid">
<figure><img src="{r58_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{r58_files['mask']['file']}" alt="R58 square mask"><figcaption><strong>R58 rejected square/clamped mask</strong></figcaption></figure>
<figure><img src="{r58_files['heatmap']['file']}" alt="R58 square heatmap"><figcaption>R58 square/clamped alpha heatmap</figcaption></figure>
<figure><img src="{feathered_files['mask']['file']}" alt="R59 feathered mask"><figcaption><strong>R59 rounded asymptotic mask</strong></figcaption></figure>
<figure><img src="{feathered_files['heatmap']['file']}" alt="R59 feathered heatmap"><figcaption>R59 rounded alpha heatmap</figcaption></figure>
<figure><img src="{feathered_files['difference']['file']}" alt="R58 to R59 difference"><figcaption>R58 &rarr; R59 difference</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional contour or feather adjustment"></label></div></article>''')
        questions.append({
            "key": key,
            "family": pristine["family"],
            "topology": topology,
            "sourceProvenance": pristine["sourceProvenance"],
            "sourceAlignment": pristine["sourceAlignment"],
            "allowedDecisions": decisions,
            "r58": {"process": rejected["boundary"]["process"],
                    "metrics": rejected["boundary"]["metrics"],
                    "files": r58_files},
            "feathered": {"process": process,
                          "metrics": feathered_metrics,
                          "files": feathered_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} &middot; rounded Ice boundary feather</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R58 is rejected for straight square cutoff contours along the top and east. R59 preserves the authored edge but replaces equal-cost square distance and hard saturation with rounded distance and a non-clamping feather.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-feathered-boundary-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoints": [r57_analysis["checkpoint"],
                              r58_analysis["checkpoint"]],
        "r57AnalysisSha256": hashlib.sha256(r57_bytes).hexdigest(),
        "r58AnalysisSha256": hashlib.sha256(r58_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "feathered-boundary-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "feathered-boundary-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("feathered boundary must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("feathered-boundary keys or order differ from R58")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["r58"]["files"].values())
        indexed.extend(question["feathered"]["files"].values())
        process = question["feathered"]["process"]
        if (process["method"] !=
                "rounded-authored-boundary-asymptotic-feather" or
                process["distanceMetric"] != "chamfer-euclidean" or
                process["usesHardSaturation"] or
                not process["usesBoundaryDistanceGradient"] or
                process["usesTilePositionGradient"] or
                process["usesSolidOwnerPlateau"] or
                process["usesGlobalBinaryCleanup"] or
                process["usesBroadGaussianSoftening"] or
                process["changedConnectedSupportPixels"] or
                process["changedInsideExactEdgeRadius"] or
                process["changedSupportQuadrantPixels"] or
                process["newFullOpacityPixels"]):
            errors.append(f"feathered repair escaped its contract: {question['key']}")
        if process["distinctAlphaValues"] < 64:
            errors.append(f"feathered repair lost alpha detail: {question['key']}")
        r58_mask = Image.open(
            output / question["r58"]["files"]["mask"]["file"])
        feathered = Image.open(
            output / question["feathered"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r58_mask)) !=
                    files["r58Result"]["pixelSha256"]):
                errors.append(f"R58 outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, feathered)) !=
                    files["featheredResult"]["pixelSha256"]):
                errors.append(f"R59 outcome mismatch: {question['key']}")
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
    """Verify R59 remains pending the two directional shoulder changes."""
    rendered = validate_output(output)
    analysis = json.loads((output / "feathered-boundary-analysis.json")
                          .read_text(encoding="utf-8"))
    review = json.loads(decisions_path.read_text(encoding="utf-8"))
    questions = {item["key"]: item for item in analysis["questions"]}
    decisions = review.get("decisions", {})
    if set(decisions) != set(questions):
        raise ValueError("R59 decision keys do not exactly match the review")
    for key, decision in decisions.items():
        if (decision.get("answer") !=
                "Needs stronger directional edge feather"):
            raise ValueError(f"R59 must remain pending edge feather: {key}")
        note = decision.get("note", "").casefold()
        if ("ne is correct" not in note or
                "nw" not in note or "north/top" not in note or
                "se" not in note or "east/right" not in note):
            raise ValueError(f"R59 directional feedback is missing: {key}")
    if review.get("approvedRoutes"):
        raise ValueError("R59 must not authorize a generation route")
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
    parser.add_argument("--r58", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R59-ICE-FEATHERED-BOUNDARY")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.r58, args.output,
                            args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
