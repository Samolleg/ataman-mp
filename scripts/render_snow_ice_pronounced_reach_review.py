#!/usr/bin/env python3
"""Render Ice review with a pronounced connected support reach."""

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
from render_snow_ice_gradient_repair_review import (
    _connected_component_from_corners,
)
from render_snow_ice_feathered_boundary_review import (
    _chamfer_euclidean_distance,
)
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _summary


def strengthen_connected_support_reach(
        authored_mask: Image.Image, baseline_mask: Image.Image,
        topology: str, core_owner_alpha: int = 128,
        reach_radius_pixels: float = 72.0,
        boundary_support_strength: float = 0.70,
        reach_exponent: float = 0.65,
        corner_hold_pixels: int = 12) -> tuple[Image.Image, dict]:
    """Extend a coherent support band from the connected authored core."""
    if not 0 < core_owner_alpha < 255:
        raise ValueError("support core alpha is invalid")
    if reach_radius_pixels <= 0.0:
        raise ValueError("support reach radius must be positive")
    if not 0.0 < boundary_support_strength < 1.0:
        raise ValueError("boundary support strength must be in (0, 1)")
    if reach_exponent <= 0.0:
        raise ValueError("support reach exponent must be positive")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if (owner != (True, True, True, False) or
            not topology.startswith("inside-3of4")):
        raise ValueError("pronounced support reach requires NW/NE/SE ownership")

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
    reach_band = np.logical_and(
        np.logical_not(connected_core), distance < reach_radius_pixels)
    normalized_reach = np.clip(
        1.0 - distance / float(reach_radius_pixels), 0.0, 1.0)
    support_pressure = (
        boundary_support_strength *
        np.power(normalized_reach, reach_exponent))

    result_f32 = baseline_u8.astype(np.float32).copy()
    # Multiplication retains the baseline's continuous local variations while
    # adding one coherent, contour-relative support stroke.
    result_f32[reach_band] *= 1.0 - support_pressure[reach_band]
    owner_corner_patches = np.zeros_like(reach_band, dtype=np.bool_)
    owner_corner_patches[:corner_hold_pixels, :corner_hold_pixels] = True
    owner_corner_patches[:corner_hold_pixels, -corner_hold_pixels:] = True
    owner_corner_patches[-corner_hold_pixels:, -corner_hold_pixels:] = True
    result_f32[owner_corner_patches] = baseline_u8[owner_corner_patches]
    result_u8 = np.uint8(np.clip(np.rint(result_f32), 0, 255))

    changed = result_u8 != baseline_u8
    support_dominant_before = baseline_u8 <= core_owner_alpha
    support_dominant_after = result_u8 < 128
    new_support_dominant = np.logical_and(
        support_dominant_after, np.logical_not(support_dominant_before))
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "connected-core-rounded-support-reach",
        "coreOwnerAlpha": core_owner_alpha,
        "reachRadiusPixels": reach_radius_pixels,
        "boundarySupportStrength": boundary_support_strength,
        "reachExponent": reach_exponent,
        "cornerHoldPixels": corner_hold_pixels,
        "connectedCorePixels": int(connected_core.sum()),
        "reachBandPixels": int(reach_band.sum()),
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedConnectedReachPixels": int(
            (changed & reach_band).sum()),
        "changedOutsideConnectedReachPixels": int(
            (changed & np.logical_not(reach_band)).sum()),
        "changedDistantOwnerPixels": int(
            (changed & np.logical_not(reach_band)).sum()),
        "changedOwnerCornerPatchPixels": int(
            (changed & owner_corner_patches).sum()),
        "supportDominantPixelsBefore": int(support_dominant_before.sum()),
        "supportDominantPixelsAfter": int(support_dominant_after.sum()),
        "newSupportDominantPixels": int(new_support_dominant.sum()),
        "preservesContinuousAuthoredDetail": True,
        "usesConnectedSupportCore": True,
        "usesRoundedDistanceReach": True,
        "usesGlobalBlur": False,
        "usesBinaryOutput": False,
        "usesNewNoise": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, r59: Path, output: Path,
           checkpoint: str = "R61-ICE-PRONOUNCED-SUPPORT-REACH") -> dict:
    """Render R59 beside a coherent support band reaching the drawn guide."""
    r57 = r57.resolve()
    r59 = r59.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"pronounced-reach output is not empty: {output}")
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
        pronounced, process = strengthen_connected_support_reach(
            authored, r59_mask, topology)
        pronounced_metrics = mask_metrics(
            authored, pronounced, topology)

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
        pronounced_files = {
            "mask": _save(
                pronounced,
                images / f"{prefix}-r61-pronounced-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(pronounced),
                images / f"{prefix}-r61-pronounced-heatmap.png"),
            "difference": _save(
                _difference_image(r59_mask, pronounced),
                images / f"{prefix}-r59-to-r61-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r59, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r59, prior_outcome["files"]["support"], "RGB")
            r59_result = Image.composite(owner, support, r59_mask)
            pronounced_result = Image.composite(
                owner, support, pronounced)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r59Result": _save(
                    r59_result, images / f"{outcome_prefix}-r59-result.png"),
                "pronouncedResult": _save(
                    pronounced_result,
                    images / f"{outcome_prefix}-r61-result.png"),
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
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform &middot; distant field</figcaption></figure>
<figure><img src="{files['r59Result']['file']}" alt="R59 result"><figcaption><strong>R59 narrow/weak reach</strong></figcaption></figure>
<figure><img src="{files['pronouncedResult']['file']}" alt="R61 result"><figcaption><strong>R61 pronounced outward support reach</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform &middot; invading body</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Approve pronounced outward support reach",
            "Direction correct; reduce reach or strength",
            "Direction correct; increase reach or strength",
            "Keep R59 rounded feather",
            "Reject both candidates",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture &middot; {html.escape(topology)}</h2>
<p class="notice">R61 treats the dark connected authored body as the shape authority, then carries a rounded continuous-alpha support stroke up to {process['reachRadiusPixels']:.0f} px outward. This moves a visibly pronounced transition toward the blue guide instead of merely changing a few alpha levels inside R59's old footprint.</p>
<p class="muted">The connected core remains exact. Existing local alpha detail is retained multiplicatively through the new band. Detached extracted speckles do not define its shape; there is no global blur, binary output, added noise, rotation, inversion, or texture substitution.</p>
<p class="metric">R59: {html.escape(_summary(prior['feathered']['metrics']))}<br>R61: {html.escape(_summary(pronounced_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) &middot; reach band: {process['reachBandPixels']} px &middot; newly support-dominant: {process['newSupportDominantPixels']} px &middot; changed outside band: {process['changedOutsideConnectedReachPixels']} &middot; distant owner changed: {process['changedDistantOwnerPixels']} &middot; owner corner patches changed: {process['changedOwnerCornerPatchPixels']}</p>
<div class="evidence-grid">
<figure><img src="{source_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{source_files['authoredMask']['file']}" alt="Authored mask"><figcaption>Authored mask supplying the irregular connected core</figcaption></figure>
<figure><img src="{r59_files['mask']['file']}" alt="R59 mask"><figcaption><strong>R59 rounded mask</strong></figcaption></figure>
<figure><img src="{r59_files['heatmap']['file']}" alt="R59 heatmap"><figcaption>R59 alpha heatmap</figcaption></figure>
<figure><img src="{pronounced_files['mask']['file']}" alt="R61 mask"><figcaption><strong>R61 pronounced reach mask</strong></figcaption></figure>
<figure><img src="{pronounced_files['heatmap']['file']}" alt="R61 heatmap"><figcaption>R61 alpha heatmap</figcaption></figure>
<figure><img src="{pronounced_files['difference']['file']}" alt="R59 to R61 difference"><figcaption>R59 &rarr; R61 added support strength</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional reach distance or strength adjustment"></label></div></article>''')
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
            "pronounced": {"process": process,
                           "metrics": pronounced_metrics,
                           "files": pronounced_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} &middot; pronounced Ice support reach</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R60 is rejected because it was less pronounced than R59 and did not move the visible blend body toward the drawn blue contour. R61 compares R59 directly against a coherent outward support stroke.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-pronounced-support-reach-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoints": [r57_analysis["checkpoint"],
                              r59_analysis["checkpoint"]],
        "r57AnalysisSha256": hashlib.sha256(r57_bytes).hexdigest(),
        "r59AnalysisSha256": hashlib.sha256(r59_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "pronounced-reach-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "pronounced-reach-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("pronounced reach must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("pronounced-reach keys or order differ from R59")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["source"]["files"].values())
        indexed.extend(question["r59"]["files"].values())
        indexed.extend(question["pronounced"]["files"].values())
        process = question["pronounced"]["process"]
        if (process["method"] != "connected-core-rounded-support-reach" or
                not process["usesConnectedSupportCore"] or
                not process["usesRoundedDistanceReach"] or
                not process["preservesContinuousAuthoredDetail"] or
                process["usesGlobalBlur"] or
                process["usesBinaryOutput"] or
                process["usesNewNoise"] or
                process["changedOutsideConnectedReachPixels"] or
                process["changedDistantOwnerPixels"] or
                process["changedOwnerCornerPatchPixels"] or
                process["newSupportDominantPixels"] < 5000 or
                not 0.25 < process["changedFraction"] < 0.45):
            errors.append(
                f"pronounced support reach escaped contract: {question['key']}")
        if process["distinctAlphaValues"] < 200:
            errors.append(
                f"pronounced reach lost alpha detail: {question['key']}")
        r59_mask = Image.open(
            output / question["r59"]["files"]["mask"]["file"])
        pronounced = Image.open(
            output / question["pronounced"]["files"]["mask"]["file"])
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
            if (_image_hash(Image.composite(owner, support, pronounced)) !=
                    files["pronouncedResult"]["pixelSha256"]):
                errors.append(f"R61 outcome mismatch: {question['key']}")
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
        "--checkpoint", default="R61-ICE-PRONOUNCED-SUPPORT-REACH")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.r59, args.output,
                            args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
