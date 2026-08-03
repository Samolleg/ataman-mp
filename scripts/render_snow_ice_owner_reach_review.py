#!/usr/bin/env python3
"""Render correct-polarity Ice owner-opacity reach review."""

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
from render_snow_ice_feathered_boundary_review import (
    _chamfer_euclidean_distance,
)
from render_snow_ice_gradient_repair_review import (
    _connected_component_from_corners,
)
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _quadrant_slice, _summary


def raise_owner_opacity_toward_reviewed_reach(
        authored_mask: Image.Image, baseline_mask: Image.Image,
        topology: str, core_owner_alpha: int = 128,
        exact_edge_radius: float = 4.0,
        opacity_scale_pixels: float = 10.0,
        opacity_exponent: float = 1.10) -> tuple[Image.Image, dict]:
    """Raise owner alpha in NW and SE; preserve NE, SW, and authored edge."""
    if not 0 < core_owner_alpha < 255:
        raise ValueError("owner reach core alpha is invalid")
    if exact_edge_radius < 0.0 or opacity_scale_pixels <= 0.0:
        raise ValueError("owner reach distances are invalid")
    if opacity_exponent <= 0.0:
        raise ValueError("owner reach exponent must be positive")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if (owner != (True, True, True, False) or
            not topology.startswith("inside-3of4")):
        raise ValueError("owner reach requires NW/NE/SE ownership")

    authored_u8 = np.asarray(
        authored_mask.convert("L"), dtype=np.uint8).copy()
    baseline_u8 = np.asarray(
        baseline_mask.convert("L"), dtype=np.uint8).copy()
    if authored_u8.shape != baseline_u8.shape:
        raise ValueError("authored and baseline masks must have the same size")
    height, width = authored_u8.shape

    support_core = authored_u8 <= core_owner_alpha
    connected_core = _connected_component_from_corners(
        support_core, support)
    distance = _chamfer_euclidean_distance(connected_core)
    exact_edge = distance <= exact_edge_radius
    outward = np.maximum(distance - exact_edge_radius, 0.0)
    owner_pressure = 1.0 - np.exp(-np.power(
        outward / float(opacity_scale_pixels), opacity_exponent))
    # Never create a new hard 255 contour. Existing authored 255 stays exact.
    owner_pressure = np.minimum(owner_pressure, 254.0 / 255.0)

    nw_region = np.zeros_like(connected_core, dtype=np.bool_)
    ne_region = np.zeros_like(connected_core, dtype=np.bool_)
    se_region = np.zeros_like(connected_core, dtype=np.bool_)
    sw_region = np.zeros_like(connected_core, dtype=np.bool_)
    nw_region[_quadrant_slice(0, height, width)] = True
    ne_region[_quadrant_slice(1, height, width)] = True
    se_region[_quadrant_slice(2, height, width)] = True
    sw_region[_quadrant_slice(3, height, width)] = True
    requested_regions = np.logical_or(nw_region, se_region)
    apply_region = np.logical_and(
        requested_regions, np.logical_not(exact_edge))

    baseline = baseline_u8.astype(np.float32) / 255.0
    repaired = baseline.copy()
    repaired[apply_region] = np.maximum(
        baseline[apply_region], owner_pressure[apply_region])
    result_u8 = np.uint8(np.clip(np.rint(repaired * 255.0), 0, 255))
    changed = result_u8 != baseline_u8
    more_opaque = result_u8 > baseline_u8
    more_transparent = result_u8 < baseline_u8
    newly_full = np.logical_and(result_u8 == 255, baseline_u8 < 255)
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "connected-core-correct-polarity-owner-opacity-reach",
        "coreOwnerAlpha": core_owner_alpha,
        "exactEdgeRadiusPixels": exact_edge_radius,
        "opacityScalePixels": opacity_scale_pixels,
        "opacityExponent": opacity_exponent,
        "connectedCorePixels": int(connected_core.sum()),
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "pixelsMadeMoreOpaque": int(more_opaque.sum()),
        "pixelsMadeMoreTransparent": int(more_transparent.sum()),
        "changedNWPixels": int((changed & nw_region).sum()),
        "changedSEPixels": int((changed & se_region).sum()),
        "changedNEPixels": int((changed & ne_region).sum()),
        "changedSWPixels": int((changed & sw_region).sum()),
        "changedInsideExactEdge": int((changed & exact_edge).sum()),
        "newFullOpacityPixels": int(newly_full.sum()),
        "usesCorrectOwnerPolarity": True,
        "usesRoundedCoreDistance": True,
        "usesHardFullOpacityCutoff": False,
        "usesGlobalBlur": False,
        "usesBinaryOutput": False,
        "usesNewNoise": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, r59: Path, output: Path,
           checkpoint: str = "R62-ICE-OPAQUE-OWNER-REACH") -> dict:
    """Render R59 beside the correct-polarity opaque-owner candidate."""
    r57 = r57.resolve()
    r59 = r59.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"opaque-owner output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()

    r57_bytes = (r57 / "gradient-repair-analysis.json").read_bytes()
    r59_bytes = (r59 / "feathered-boundary-analysis.json").read_bytes()
    r57_analysis = json.loads(r57_bytes.decode("utf-8"))
    r59_analysis = json.loads(r59_bytes.decode("utf-8"))
    r57_by_key = {item["key"]: item for item in r57_analysis["questions"]}
    r59_by_key = {item["key"]: item for item in r59_analysis["questions"]}
    if (any(key not in r57_by_key for key in REVIEW_KEYS) or
            any(key not in r59_by_key for key in REVIEW_KEYS)):
        raise ValueError("R57 or R59 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        source_question = r57_by_key[key]
        prior = r59_by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        aligned = _load(
            r59, prior["r58"]["files"]["alignedSource"], "RGB")
        authored = _load(
            r57, source_question["r55"]["files"]["mask"], "L")
        r59_mask = _load(
            r59, prior["feathered"]["files"]["mask"], "L")
        opaque_owner, process = (
            raise_owner_opacity_toward_reviewed_reach(
                authored, r59_mask, topology))
        opaque_metrics = mask_metrics(
            authored, opaque_owner, topology)

        source_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "authoredMask": _save(
                authored, images / f"{prefix}-authored-mask.png"),
        }
        r59_files = {
            "mask": _save(
                r59_mask, images / f"{prefix}-r59-rounded-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r59_mask),
                images / f"{prefix}-r59-rounded-heatmap.png"),
        }
        opaque_files = {
            "mask": _save(
                opaque_owner,
                images / f"{prefix}-r62-opaque-owner-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(opaque_owner),
                images / f"{prefix}-r62-opaque-owner-heatmap.png"),
            "difference": _save(
                _difference_image(r59_mask, opaque_owner),
                images / f"{prefix}-r59-to-r62-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r59, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r59, prior_outcome["files"]["support"], "RGB")
            r59_result = Image.composite(owner, support, r59_mask)
            opaque_result = Image.composite(
                owner, support, opaque_owner)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r59Result": _save(
                    r59_result, images / f"{outcome_prefix}-r59-result.png"),
                "opaqueResult": _save(
                    opaque_result,
                    images / f"{outcome_prefix}-r62-result.png"),
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
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform &middot; white mask material</figcaption></figure>
<figure><img src="{files['r59Result']['file']}" alt="R59 result"><figcaption><strong>R59 insufficient owner reach</strong></figcaption></figure>
<figure><img src="{files['opaqueResult']['file']}" alt="R62 result"><figcaption><strong>R62 opaque owner reach + narrow feather</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform &middot; dark mask material</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Approve opaque-owner reach",
            "Direction correct; make terminal feather wider",
            "Direction correct; make terminal feather narrower",
            "Direction correct; increase owner reach",
            "Keep R59 rounded feather",
            "Reject both candidates",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture &middot; {html.escape(topology)}</h2>
<p class="notice">R62 uses the corrected polarity: white means the Ice owner mask is opaque. It raises owner alpha only in the requested NW and SE fields, bringing substantial white paint toward the blue reach. Only the narrow terminal approach to the authored dark edge remains translucent.</p>
<p class="muted">NE and SW are copied pixel-for-pixel from R59. The connected authored dark edge and its first {process['exactEdgeRadiusPixels']:.0f} px remain exact. R62 never lowers alpha, never creates a new 255 contour, and uses no blur, thresholded output, noise, rotation, inversion, or source substitution.</p>
<p class="metric">R59: {html.escape(_summary(prior['feathered']['metrics']))}<br>R62: {html.escape(_summary(opaque_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) &middot; more opaque: {process['pixelsMadeMoreOpaque']} &middot; more transparent: {process['pixelsMadeMoreTransparent']} &middot; NW changed: {process['changedNWPixels']} &middot; SE changed: {process['changedSEPixels']} &middot; NE changed: {process['changedNEPixels']} &middot; SW changed: {process['changedSWPixels']} &middot; exact edge changed: {process['changedInsideExactEdge']}</p>
<div class="evidence-grid">
<figure><img src="{source_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{source_files['authoredMask']['file']}" alt="Authored mask"><figcaption>Authored dark edge authority</figcaption></figure>
<figure><img src="{r59_files['mask']['file']}" alt="R59 mask"><figcaption><strong>R59 rounded mask</strong></figcaption></figure>
<figure><img src="{r59_files['heatmap']['file']}" alt="R59 heatmap"><figcaption>R59 alpha heatmap</figcaption></figure>
<figure><img src="{opaque_files['mask']['file']}" alt="R62 mask"><figcaption><strong>R62 opaque-owner reach mask</strong></figcaption></figure>
<figure><img src="{opaque_files['heatmap']['file']}" alt="R62 heatmap"><figcaption>R62 alpha heatmap</figcaption></figure>
<figure><img src="{opaque_files['difference']['file']}" alt="R59 to R62 difference"><figcaption>R59 &rarr; R62 added owner opacity</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional reach or terminal-feather adjustment"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": prior["sourceAlignment"],
            "allowedDecisions": decisions,
            "source": {"files": source_files},
            "r59": {"process": prior["feathered"]["process"],
                    "metrics": prior["feathered"]["metrics"],
                    "files": r59_files},
            "opaqueOwner": {"process": process,
                            "metrics": opaque_metrics,
                            "files": opaque_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} &middot; correct-polarity Ice owner reach</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R61 is rejected because it made the owner mask more transparent near the blend edge. R62 performs only the opposite operation: substantial white owner opacity reaches toward the reviewed contour, followed by a narrow terminal feather.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-opaque-owner-reach-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoints": [r57_analysis["checkpoint"],
                              r59_analysis["checkpoint"]],
        "r57AnalysisSha256": hashlib.sha256(r57_bytes).hexdigest(),
        "r59AnalysisSha256": hashlib.sha256(r59_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "opaque-owner-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "opaque-owner-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("opaque owner must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("opaque-owner keys or order differ from R59")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["source"]["files"].values())
        indexed.extend(question["r59"]["files"].values())
        indexed.extend(question["opaqueOwner"]["files"].values())
        process = question["opaqueOwner"]["process"]
        if (process["method"] !=
                "connected-core-correct-polarity-owner-opacity-reach" or
                not process["usesCorrectOwnerPolarity"] or
                not process["usesRoundedCoreDistance"] or
                process["usesHardFullOpacityCutoff"] or
                process["usesGlobalBlur"] or
                process["usesBinaryOutput"] or
                process["usesNewNoise"] or
                process["pixelsMadeMoreTransparent"] or
                process["pixelsMadeMoreOpaque"] < 10000 or
                not process["changedNWPixels"] or
                not process["changedSEPixels"] or
                process["changedNEPixels"] or
                process["changedSWPixels"] or
                process["changedInsideExactEdge"] or
                process["newFullOpacityPixels"] or
                not 0.15 < process["changedFraction"] < 0.35):
            errors.append(
                f"opaque owner reach escaped contract: {question['key']}")
        if process["distinctAlphaValues"] < 200:
            errors.append(
                f"opaque owner reach lost alpha detail: {question['key']}")
        r59_mask = Image.open(
            output / question["r59"]["files"]["mask"]["file"])
        opaque_owner = Image.open(
            output / question["opaqueOwner"]["files"]["mask"]["file"])
        baseline_u8 = np.asarray(r59_mask.convert("L"), dtype=np.uint8)
        opaque_u8 = np.asarray(opaque_owner.convert("L"), dtype=np.uint8)
        if np.any(opaque_u8 < baseline_u8):
            errors.append(f"R62 lowered owner opacity: {question['key']}")
        height, width = baseline_u8.shape
        if not np.array_equal(
                baseline_u8[:height // 2, width // 2:],
                opaque_u8[:height // 2, width // 2:]):
            errors.append(f"R62 changed accepted NE: {question['key']}")
        if not np.array_equal(
                baseline_u8[height // 2:, :width // 2],
                opaque_u8[height // 2:, :width // 2]):
            errors.append(f"R62 changed support SW: {question['key']}")
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r59_mask)) !=
                    files["r59Result"]["pixelSha256"]):
                errors.append(f"R59 outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, opaque_owner)) !=
                    files["opaqueResult"]["pixelSha256"]):
                errors.append(f"R62 outcome mismatch: {question['key']}")
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


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r57", required=True, type=Path)
    parser.add_argument("--r59", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument(
        "--checkpoint", default="R62-ICE-OPAQUE-OWNER-REACH")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.r59, args.output,
                            args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
