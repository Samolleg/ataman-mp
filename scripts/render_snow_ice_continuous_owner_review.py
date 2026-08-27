#!/usr/bin/env python3
"""Render Ice owner-opacity reach without tile-quadrant clipping."""

from __future__ import annotations

import argparse
import base64
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
from render_snow_ice_feathered_boundary_review import (
    _chamfer_euclidean_distance,
)
from render_snow_ice_gradient_repair_review import (
    _connected_component_from_corners,
)
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _summary


def _smoothstep(values: np.ndarray) -> np.ndarray:
    values = np.clip(values, 0.0, 1.0)
    return values * values * (3.0 - 2.0 * values)


def apply_continuous_owner_opacity_reach(
        authored_mask: Image.Image, baseline_mask: Image.Image,
        topology: str, core_owner_alpha: int = 128,
        owner_gate_full_alpha: int = 192,
        exact_edge_radius: float = 4.0,
        opacity_scale_pixels: float = 10.0,
        opacity_exponent: float = 1.10,
        corner_hold_pixels: int = 12) -> tuple[Image.Image, dict]:
    """Raise owner alpha continuously; never clip at tile midlines."""
    if not 0 < core_owner_alpha < owner_gate_full_alpha < 255:
        raise ValueError("continuous owner alpha gates are invalid")
    if exact_edge_radius < 0.0 or opacity_scale_pixels <= 0.0:
        raise ValueError("continuous owner distances are invalid")
    if opacity_exponent <= 0.0:
        raise ValueError("continuous owner exponent must be positive")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if (owner != (True, True, True, False) or
            not topology.startswith("inside-3of4")):
        raise ValueError("continuous owner reach requires NW/NE/SE ownership")

    authored_u8 = np.asarray(
        authored_mask.convert("L"), dtype=np.uint8).copy()
    baseline_u8 = np.asarray(
        baseline_mask.convert("L"), dtype=np.uint8).copy()
    if authored_u8.shape != baseline_u8.shape:
        raise ValueError("authored and baseline masks must have the same size")
    height, width = authored_u8.shape
    if (corner_hold_pixels < 1 or corner_hold_pixels >= height // 2 or
            corner_hold_pixels >= width // 2):
        raise ValueError("corner hold is invalid for the mask size")

    support_core = authored_u8 <= core_owner_alpha
    connected_core = _connected_component_from_corners(
        support_core, support)
    distance = _chamfer_euclidean_distance(connected_core)
    exact_edge = distance <= exact_edge_radius
    outward = np.maximum(distance - exact_edge_radius, 0.0)
    owner_pressure = 1.0 - np.exp(-np.power(
        outward / float(opacity_scale_pixels), opacity_exponent))
    owner_pressure = np.minimum(owner_pressure, 254.0 / 255.0)

    authored_f32 = authored_u8.astype(np.float32)
    owner_gate = _smoothstep(
        (authored_f32 - float(core_owner_alpha)) /
        float(owner_gate_full_alpha - core_owner_alpha))
    baseline = baseline_u8.astype(np.float32) / 255.0
    raised = np.maximum(baseline, owner_pressure)
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)
    corner_distance = np.minimum.reduce((
        np.hypot(xx, yy),
        np.hypot(float(width - 1) - xx, yy),
        np.hypot(float(width - 1) - xx, float(height - 1) - yy),
        np.hypot(xx, float(height - 1) - yy),
    ))
    corner_hold = _smoothstep(
        1.0 - corner_distance / float(corner_hold_pixels))
    repair_weight = owner_gate * (1.0 - corner_hold)
    repaired = baseline + repair_weight * (raised - baseline)
    repaired[exact_edge] = baseline[exact_edge]

    semantic_corners = np.zeros_like(connected_core, dtype=np.bool_)
    semantic_corners[0, 0] = True
    semantic_corners[0, -1] = True
    semantic_corners[-1, -1] = True
    semantic_corners[-1, 0] = True
    repaired[semantic_corners] = baseline[semantic_corners]

    result_u8 = np.uint8(np.clip(np.rint(repaired * 255.0), 0, 255))
    changed = result_u8 != baseline_u8
    more_opaque = result_u8 > baseline_u8
    more_transparent = result_u8 < baseline_u8
    newly_full = np.logical_and(result_u8 == 255, baseline_u8 < 255)
    mid_x = width // 2
    mid_y = height // 2
    baseline_vertical = np.abs(
        baseline_u8[:, mid_x].astype(np.int16) -
        baseline_u8[:, mid_x - 1].astype(np.int16))
    result_vertical = np.abs(
        result_u8[:, mid_x].astype(np.int16) -
        result_u8[:, mid_x - 1].astype(np.int16))
    baseline_horizontal = np.abs(
        baseline_u8[mid_y].astype(np.int16) -
        baseline_u8[mid_y - 1].astype(np.int16))
    result_horizontal = np.abs(
        result_u8[mid_y].astype(np.int16) -
        result_u8[mid_y - 1].astype(np.int16))
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "continuous-authored-alpha-owner-opacity-reach",
        "coreOwnerAlpha": core_owner_alpha,
        "ownerGateFullAlpha": owner_gate_full_alpha,
        "exactEdgeRadiusPixels": exact_edge_radius,
        "opacityScalePixels": opacity_scale_pixels,
        "opacityExponent": opacity_exponent,
        "cornerHoldPixels": corner_hold_pixels,
        "connectedCorePixels": int(connected_core.sum()),
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "pixelsMadeMoreOpaque": int(more_opaque.sum()),
        "pixelsMadeMoreTransparent": int(more_transparent.sum()),
        "changedInsideExactEdge": int((changed & exact_edge).sum()),
        "changedSemanticCornerPixels": int(
            (changed & semantic_corners).sum()),
        "newFullOpacityPixels": int(newly_full.sum()),
        "maxAddedVerticalMidlineJump": int(np.maximum(
            result_vertical - baseline_vertical, 0).max()),
        "maxAddedHorizontalMidlineJump": int(np.maximum(
            result_horizontal - baseline_horizontal, 0).max()),
        "usesHardQuadrantClipping": False,
        "usesHardCornerPatchClipping": False,
        "usesRadialCornerHold": True,
        "usesContinuousAuthoredOwnerWeight": True,
        "usesCorrectOwnerPolarity": True,
        "usesRoundedCoreDistance": True,
        "usesHardFullOpacityCutoff": False,
        "usesGlobalBlur": False,
        "usesBinaryOutput": False,
        "usesNewNoise": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, r59: Path, r62: Path, output: Path,
           checkpoint: str = "R63-ICE-CONTINUOUS-OWNER-REACH") -> dict:
    """Render rejected quadrant-clipped R62 beside continuous R63."""
    r57 = r57.resolve()
    r59 = r59.resolve()
    r62 = r62.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"continuous-owner output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()

    r57_bytes = (r57 / "gradient-repair-analysis.json").read_bytes()
    r59_bytes = (r59 / "feathered-boundary-analysis.json").read_bytes()
    r62_bytes = (r62 / "opaque-owner-analysis.json").read_bytes()
    r57_analysis = json.loads(r57_bytes.decode("utf-8"))
    r59_analysis = json.loads(r59_bytes.decode("utf-8"))
    r62_analysis = json.loads(r62_bytes.decode("utf-8"))
    r57_by_key = {item["key"]: item for item in r57_analysis["questions"]}
    r59_by_key = {item["key"]: item for item in r59_analysis["questions"]}
    r62_by_key = {item["key"]: item for item in r62_analysis["questions"]}
    if (any(key not in r57_by_key for key in REVIEW_KEYS) or
            any(key not in r59_by_key for key in REVIEW_KEYS) or
            any(key not in r62_by_key for key in REVIEW_KEYS)):
        raise ValueError("R57, R59, or R62 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        source_question = r57_by_key[key]
        baseline_question = r59_by_key[key]
        rejected = r62_by_key[key]
        topology = rejected["topology"]
        prefix = f"{number:02d}-{rejected['family']}-{topology}"
        aligned = _load(
            r62, rejected["source"]["files"]["alignedSource"], "RGB")
        authored = _load(
            r57, source_question["r55"]["files"]["mask"], "L")
        r59_mask = _load(
            r59, baseline_question["feathered"]["files"]["mask"], "L")
        r62_mask = _load(
            r62, rejected["opaqueOwner"]["files"]["mask"], "L")
        continuous, process = apply_continuous_owner_opacity_reach(
            authored, r59_mask, topology)
        continuous_metrics = mask_metrics(
            authored, continuous, topology)

        source_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "authoredMask": _save(
                authored, images / f"{prefix}-authored-mask.png"),
        }
        r62_files = {
            "mask": _save(
                r62_mask, images / f"{prefix}-r62-quadrant-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r62_mask),
                images / f"{prefix}-r62-quadrant-heatmap.png"),
        }
        continuous_files = {
            "mask": _save(
                continuous,
                images / f"{prefix}-r63-continuous-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(continuous),
                images / f"{prefix}-r63-continuous-heatmap.png"),
            "difference": _save(
                _difference_image(r62_mask, continuous),
                images / f"{prefix}-r62-to-r63-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                rejected["outcomes"], 1):
            owner = _load(r62, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r62, prior_outcome["files"]["support"], "RGB")
            r62_result = Image.composite(owner, support, r62_mask)
            continuous_result = Image.composite(
                owner, support, continuous)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r62Result": _save(
                    r62_result, images / f"{outcome_prefix}-r62-result.png"),
                "continuousResult": _save(
                    continuous_result,
                    images / f"{outcome_prefix}-r63-result.png"),
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
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform</figcaption></figure>
<figure><img src="{files['r62Result']['file']}" alt="R62 result"><figcaption><strong>R62 rejected square-cut result</strong></figcaption></figure>
<figure><img src="{files['continuousResult']['file']}" alt="R63 result"><figcaption><strong>R63 continuous result</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Approve continuous owner reach",
            "Direction correct; make terminal feather wider",
            "Direction correct; make terminal feather narrower",
            "Keep R59 without opaque-owner repair",
            "Reject both corrected candidates",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture &middot; {html.escape(topology)}</h2>
<p class="notice">R63 removes every tile-quadrant and rectangular corner gate. Owner opacity is weighted continuously from the authored alpha and rounded distance to the connected dark edge. Both tile midlines are ordinary pixels, not geometry boundaries.</p>
<p class="muted">Only the four semantic corner pixels are exact; their surrounding hold fades radially. The authored dark edge and its first {process['exactEdgeRadiusPixels']:.0f} px remain exact. R63 never lowers alpha or creates a new 255 contour.</p>
<p class="metric">R62 rejected square mask: {html.escape(_summary(rejected['opaqueOwner']['metrics']))}<br>R63 continuous: {html.escape(_summary(continuous_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) &middot; more opaque: {process['pixelsMadeMoreOpaque']} &middot; more transparent: {process['pixelsMadeMoreTransparent']} &middot; added vertical-midline jump: {process['maxAddedVerticalMidlineJump']} &middot; added horizontal-midline jump: {process['maxAddedHorizontalMidlineJump']} &middot; exact edge changed: {process['changedInsideExactEdge']} &middot; semantic corners changed: {process['changedSemanticCornerPixels']}</p>
<div class="evidence-grid">
<figure><img src="{source_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{source_files['authoredMask']['file']}" alt="Authored mask"><figcaption>Authored edge and alpha authority</figcaption></figure>
<figure><img src="{r62_files['mask']['file']}" alt="R62 mask"><figcaption><strong>R62 rejected quadrant-clipped mask</strong></figcaption></figure>
<figure><img src="{r62_files['heatmap']['file']}" alt="R62 heatmap"><figcaption>R62 square-cut heatmap</figcaption></figure>
<figure><img src="{continuous_files['mask']['file']}" alt="R63 mask"><figcaption><strong>R63 continuous owner mask</strong></figcaption></figure>
<figure><img src="{continuous_files['heatmap']['file']}" alt="R63 heatmap"><figcaption>R63 continuous heatmap</figcaption></figure>
<figure><img src="{continuous_files['difference']['file']}" alt="R62 to R63 difference"><figcaption>R62 &rarr; R63 removal of rectangular gates</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional continuity or feather adjustment"></label></div></article>''')
        questions.append({
            "key": key,
            "family": rejected["family"],
            "topology": topology,
            "sourceProvenance": rejected["sourceProvenance"],
            "sourceAlignment": rejected["sourceAlignment"],
            "allowedDecisions": decisions,
            "source": {"files": source_files},
            "r62": {"process": rejected["opaqueOwner"]["process"],
                    "metrics": rejected["opaqueOwner"]["metrics"],
                    "files": r62_files},
            "continuous": {"process": process,
                           "metrics": continuous_metrics,
                           "files": continuous_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} &middot; continuous Ice owner reach</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R62 is rejected for the visible square cut in question 1. R63 removes both the quadrant clipping that caused it and the rectangular corner holds found during visual QA.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-continuous-owner-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoints": [r57_analysis["checkpoint"],
                              r59_analysis["checkpoint"],
                              r62_analysis["checkpoint"]],
        "r57AnalysisSha256": hashlib.sha256(r57_bytes).hexdigest(),
        "r59AnalysisSha256": hashlib.sha256(r59_bytes).hexdigest(),
        "r62AnalysisSha256": hashlib.sha256(r62_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "continuous-owner-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "continuous-owner-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("continuous owner must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("continuous-owner keys or order differ from R62")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["source"]["files"].values())
        indexed.extend(question["r62"]["files"].values())
        indexed.extend(question["continuous"]["files"].values())
        process = question["continuous"]["process"]
        if (process["method"] !=
                "continuous-authored-alpha-owner-opacity-reach" or
                process["usesHardQuadrantClipping"] or
                process["usesHardCornerPatchClipping"] or
                not process["usesRadialCornerHold"] or
                not process["usesContinuousAuthoredOwnerWeight"] or
                not process["usesCorrectOwnerPolarity"] or
                not process["usesRoundedCoreDistance"] or
                process["usesHardFullOpacityCutoff"] or
                process["usesGlobalBlur"] or
                process["usesBinaryOutput"] or
                process["usesNewNoise"] or
                process["pixelsMadeMoreTransparent"] or
                process["pixelsMadeMoreOpaque"] < 15000 or
                process["changedInsideExactEdge"] or
                process["changedSemanticCornerPixels"] or
                process["newFullOpacityPixels"] or
                process["maxAddedVerticalMidlineJump"] > 8 or
                process["maxAddedHorizontalMidlineJump"] > 8 or
                not 0.25 < process["changedFraction"] < 0.45):
            errors.append(
                f"continuous owner reach escaped contract: {question['key']}")
        if process["distinctAlphaValues"] < 200:
            errors.append(
                f"continuous owner reach lost alpha detail: {question['key']}")
        r62_mask = Image.open(
            output / question["r62"]["files"]["mask"]["file"])
        continuous = Image.open(
            output / question["continuous"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r62_mask)) !=
                    files["r62Result"]["pixelSha256"]):
                errors.append(f"R62 outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, continuous)) !=
                    files["continuousResult"]["pixelSha256"]):
                errors.append(f"R63 outcome mismatch: {question['key']}")
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


def validate_review_decisions(output: Path, decisions_path: Path,
                              preset_path: Path) -> dict:
    """Verify approved masks, semantic sources, uniforms, and outcomes."""
    rendered = validate_output(output)
    analysis = json.loads((output / "continuous-owner-analysis.json")
                          .read_text(encoding="utf-8"))
    review = json.loads(decisions_path.read_text(encoding="utf-8"))
    preset_bytes = preset_path.read_bytes()
    preset_sha = hashlib.sha256(preset_bytes).hexdigest()
    reviewed_preset_sha = review.get("reviewedPresetSha256")
    compatible_preset_shas = review.get("compatiblePresetSha256", [])
    if preset_sha != reviewed_preset_sha and preset_sha not in compatible_preset_shas:
        raise ValueError("R63 decisions do not match a validated compatible preset")
    catalog = json.loads(preset_bytes.decode("utf-8"))
    presets = {item["namespace"]: item for item in catalog["presets"]}

    def slot_signature(namespace: str, slot: int) -> tuple[str, ...]:
        preset = presets.get(namespace)
        if not preset or slot < 0 or slot >= preset["slotCount"]:
            raise ValueError(f"missing semantic slot {namespace}:{slot}")
        names = {item["id"]: item["name"] for item in preset["families"]}
        payload = base64.b64decode(preset["signaturesBase64"])
        ids = payload[slot * 4:slot * 4 + 4]
        if len(ids) != 4 or any(item not in names for item in ids):
            raise ValueError(f"invalid semantic signature {namespace}:{slot}")
        return tuple(names[item] for item in ids)

    questions = {item["key"]: item for item in analysis["questions"]}
    decisions = review.get("decisions", {})
    if set(decisions) != set(questions):
        raise ValueError("R63 decision keys do not exactly match the review")
    expected_masks = review.get("reviewedMaskPixelSha256", {})
    verified_uniforms = 0
    verified_outcomes = 0
    for key, decision in decisions.items():
        if decision.get("answer") != "Approve continuous owner reach":
            raise ValueError(f"R63 route is not explicitly approved: {key}")
        question = questions[key]
        alignment = question["sourceAlignment"]
        if slot_signature(alignment["namespace"], alignment["slot"]) != tuple(
                alignment["sourceCornerMaterials"]):
            raise ValueError(f"authored source corners changed: {key}")
        if (question["continuous"]["files"]["mask"]["pixelSha256"] !=
                expected_masks.get(key)):
            raise ValueError(f"approved mask identity changed: {key}")
        for outcome in question["outcomes"]:
            for source_name, material_name in (
                    (outcome["ownerSource"], outcome["ownerMaterial"]),
                    (outcome["supportSource"], outcome["supportMaterial"])):
                match = re.match(r"^([^:]+):(\d+)\b", source_name)
                if not match:
                    raise ValueError(f"uniform provenance is invalid: {key}")
                signature = slot_signature(match.group(1), int(match.group(2)))
                if signature != (material_name,) * 4:
                    raise ValueError(f"source is not four-corner uniform: {key}")
                verified_uniforms += 1
            files = outcome["files"]
            if not files.get("r62Result") or not files.get("continuousResult"):
                raise ValueError(f"both rendered outcomes are required: {key}")
            # validate_output already recomposed and hash-checked both images.
            verified_outcomes += 2
    if review.get("approvedRoutes") != list(questions) or review.get(
            "pendingRoutes"):
        raise ValueError("R63 approved/pending route lists are inconsistent")
    return {
        **rendered,
        "approved": len(decisions),
        "pending": 0,
        "approvedUniformReferencesVerified": verified_uniforms,
        "approvedRenderedOutcomesVerified": verified_outcomes,
        "reviewedPresetSha256": reviewed_preset_sha,
        "validatedPresetSha256": preset_sha,
        "presetCompatibilityReverified": preset_sha != reviewed_preset_sha,
        "generationAuthorized": True,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r57", required=True, type=Path)
    parser.add_argument("--r59", required=True, type=Path)
    parser.add_argument("--r62", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--checkpoint", default="R63-ICE-CONTINUOUS-OWNER-REACH")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.r59, args.r62, args.output,
                            args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
