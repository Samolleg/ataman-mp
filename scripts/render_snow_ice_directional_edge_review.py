#!/usr/bin/env python3
"""Render Ice review with authored-detail feather on two outer shoulders."""

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
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _quadrant_slice, _summary


def _smoothstep(values: np.ndarray) -> np.ndarray:
    values = np.clip(values, 0.0, 1.0)
    return values * values * (3.0 - 2.0 * values)


def preserve_authored_detail_on_support_adjacent_edges(
        authored_mask: Image.Image, rounded_mask: Image.Image,
        topology: str, shoulder_strength: float = 1.0,
        corner_hold_pixels: int = 12,
        shoulder_profile_exponent: float = 0.50
        ) -> tuple[Image.Image, dict]:
    """Ease only R59-added opacity on NW-north and SE-east shoulders."""
    if not 0.0 <= shoulder_strength <= 1.0:
        raise ValueError("shoulder strength must be in [0, 1]")
    if not 0.0 < shoulder_profile_exponent <= 1.0:
        raise ValueError("shoulder profile exponent must be in (0, 1]")
    owner = expected_owner_corners(topology)
    if (owner != (True, True, True, False) or
            not topology.startswith("inside-3of4")):
        raise ValueError("directional edge feather requires NW/NE/SE ownership")

    authored_u8 = np.asarray(
        authored_mask.convert("L"), dtype=np.uint8).copy()
    rounded_u8 = np.asarray(
        rounded_mask.convert("L"), dtype=np.uint8).copy()
    if authored_u8.shape != rounded_u8.shape:
        raise ValueError("authored and rounded masks must have the same size")
    height, width = authored_u8.shape
    if height < 4 or width < 4:
        raise ValueError("directional edge feather mask is too small")

    mid_y = height // 2
    mid_x = width // 2
    if (corner_hold_pixels < 1 or corner_hold_pixels >= mid_y or
            corner_hold_pixels >= mid_x):
        raise ValueError("corner hold is invalid for the mask size")
    yy, xx = np.mgrid[0:height, 0:width].astype(np.float32)

    # The sine lobes are zero at each semantic corner and at NE's two
    # boundaries. They peak midway along only the requested outer shoulders.
    north_depth = np.power(
        _smoothstep(1.0 - yy / float(max(mid_y, 1))),
        shoulder_profile_exponent)
    nw_along = np.power(np.clip(np.sin(
        math.pi * np.clip(xx / float(max(mid_x, 1)), 0.0, 1.0)),
        0.0, 1.0), shoulder_profile_exponent)
    nw_shoulder = north_depth * nw_along
    nw_shoulder[np.logical_or(xx >= mid_x, yy >= mid_y)] = 0.0

    east_depth = np.power(_smoothstep(
        (xx - float(mid_x)) / float(max(width - mid_x - 1, 1))),
        shoulder_profile_exponent)
    se_along = np.power(np.clip(np.sin(math.pi * np.clip(
        (yy - float(mid_y)) / float(max(height - mid_y - 1, 1)),
        0.0, 1.0)), 0.0, 1.0), shoulder_profile_exponent)
    se_shoulder = east_depth * se_along
    se_shoulder[np.logical_or(xx < mid_x, yy < mid_y)] = 0.0

    shoulder = np.maximum(nw_shoulder, se_shoulder)
    retention = 1.0 - float(shoulder_strength) * shoulder
    owner_corner_patches = np.zeros_like(shoulder, dtype=np.bool_)
    owner_corner_patches[:corner_hold_pixels, :corner_hold_pixels] = True
    owner_corner_patches[:corner_hold_pixels, -corner_hold_pixels:] = True
    owner_corner_patches[-corner_hold_pixels:, -corner_hold_pixels:] = True
    retention[owner_corner_patches] = 1.0
    authored = authored_u8.astype(np.float32)
    rounded = np.maximum(
        rounded_u8.astype(np.float32), authored)
    added = rounded - authored
    result_u8 = np.uint8(np.clip(np.rint(
        authored + added * retention), 0, 255))

    changed = result_u8 != rounded_u8
    below_source = result_u8 < authored_u8
    ne_region = np.zeros_like(changed, dtype=np.bool_)
    ne_region[_quadrant_slice(1, height, width)] = True
    support_region = np.zeros_like(changed, dtype=np.bool_)
    support_region[_quadrant_slice(3, height, width)] = True
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "authored-floor-directional-owner-shoulder-feather",
        "shoulderStrength": shoulder_strength,
        "shoulderProfileExponent": shoulder_profile_exponent,
        "cornerHoldPixels": corner_hold_pixels,
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedNEPixels": int((changed & ne_region).sum()),
        "changedSupportQuadrantPixels": int(
            (changed & support_region).sum()),
        "changedOwnerCornerPatchPixels": int(
            (changed & owner_corner_patches).sum()),
        "changedNWNorthShoulderPixels": int(
            (changed & (nw_shoulder > 0.0)).sum()),
        "changedSEEastShoulderPixels": int(
            (changed & (se_shoulder > 0.0)).sum()),
        "pixelsBelowAuthoredSource": int(below_source.sum()),
        "preservesAuthoredContinuousAlpha": True,
        "usesDirectionalShoulders": True,
        "usesGlobalBlur": False,
        "usesBinaryThreshold": False,
        "usesNewNoise": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r57: Path, r59: Path, output: Path,
           checkpoint: str = "R60-ICE-DIRECTIONAL-EDGE-FEATHER") -> dict:
    """Render R59 beside an authored-floor directional shoulder candidate."""
    r57 = r57.resolve()
    r59 = r59.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"directional-edge output is not empty: {output}")
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
        directional, process = (
            preserve_authored_detail_on_support_adjacent_edges(
                authored, r59_mask, topology))
        directional_metrics = mask_metrics(
            authored, directional, topology)

        authored_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(
                authored, images / f"{prefix}-authored-floor-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(authored),
                images / f"{prefix}-authored-floor-heatmap.png"),
        }
        r59_files = {
            "mask": _save(
                r59_mask, images / f"{prefix}-r59-rounded-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r59_mask),
                images / f"{prefix}-r59-rounded-heatmap.png"),
        }
        directional_files = {
            "mask": _save(
                directional,
                images / f"{prefix}-r60-directional-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(directional),
                images / f"{prefix}-r60-directional-heatmap.png"),
            "difference": _save(
                _difference_image(r59_mask, directional),
                images / f"{prefix}-r59-to-r60-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r59, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r59, prior_outcome["files"]["support"], "RGB")
            r59_result = Image.composite(owner, support, r59_mask)
            directional_result = Image.composite(
                owner, support, directional)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r59Result": _save(
                    r59_result, images / f"{outcome_prefix}-r59-result.png"),
                "directionalResult": _save(
                    directional_result,
                    images / f"{outcome_prefix}-r60-result.png"),
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
<figure><img src="{files['r59Result']['file']}" alt="R59 result"><figcaption><strong>R59 rounded feather</strong></figcaption></figure>
<figure><img src="{files['directionalResult']['file']}" alt="R60 result"><figcaption><strong>R60 stronger NW-top / SE-east feather</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform &middot; SW</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer stronger directional edge feather",
            "Keep R59 rounded feather",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture &middot; {html.escape(topology)}</h2>
<p class="notice">NE is copied pixel-for-pixel from R59. R60 keeps more of the authored continuous-alpha detail only along the middle of the NW north/top edge and SE east/right edge. It never lowers a pixel below the authored mask, never changes SW, and holds {process['cornerHoldPixels']} px around all three owner corners exactly.</p>
<p class="muted">No blur, threshold, synthesized noise, rotation, inversion, or source substitution. Same authored source, corner assignment, transform, and HD owner/support uniforms as R59.</p>
<p class="metric">R59: {html.escape(_summary(prior['feathered']['metrics']))}<br>R60: {html.escape(_summary(directional_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) &middot; NE changed: {process['changedNEPixels']} &middot; SW changed: {process['changedSupportQuadrantPixels']} &middot; corner patches changed: {process['changedOwnerCornerPatchPixels']} &middot; pixels below authored floor: {process['pixelsBelowAuthoredSource']} &middot; NW-top changes: {process['changedNWNorthShoulderPixels']} &middot; SE-east changes: {process['changedSEEastShoulderPixels']}</p>
<div class="evidence-grid">
<figure><img src="{authored_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{authored_files['mask']['file']}" alt="Authored floor mask"><figcaption>Authored continuous-alpha floor</figcaption></figure>
<figure><img src="{authored_files['heatmap']['file']}" alt="Authored floor heatmap"><figcaption>Authored floor heatmap</figcaption></figure>
<figure><img src="{r59_files['mask']['file']}" alt="R59 mask"><figcaption><strong>R59 rounded mask</strong></figcaption></figure>
<figure><img src="{r59_files['heatmap']['file']}" alt="R59 heatmap"><figcaption>R59 heatmap</figcaption></figure>
<figure><img src="{directional_files['mask']['file']}" alt="R60 mask"><figcaption><strong>R60 directional edge mask</strong></figcaption></figure>
<figure><img src="{directional_files['heatmap']['file']}" alt="R60 heatmap"><figcaption>R60 heatmap</figcaption></figure>
<figure><img src="{directional_files['difference']['file']}" alt="R59 to R60 difference"><figcaption>R59 &rarr; R60: gray/black restores authored softness</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional shoulder strength or extent adjustment"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": prior["sourceAlignment"],
            "allowedDecisions": decisions,
            "authored": {"files": authored_files},
            "r59": {"process": prior["feathered"]["process"],
                    "metrics": prior["feathered"]["metrics"],
                    "files": r59_files},
            "directional": {"process": process,
                            "metrics": directional_metrics,
                            "files": directional_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} &middot; support-adjacent Ice edge feather</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R59's NE field is accepted and remains exact. R60 restores more authored softness only on the NW north/top and SE east/right shoulders identified in review.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-directional-edge-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoints": [r57_analysis["checkpoint"],
                              r59_analysis["checkpoint"]],
        "r57AnalysisSha256": hashlib.sha256(r57_bytes).hexdigest(),
        "r59AnalysisSha256": hashlib.sha256(r59_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "directional-edge-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "directional-edge-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("directional edge must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("directional-edge keys or order differ from R59")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["authored"]["files"].values())
        indexed.extend(question["r59"]["files"].values())
        indexed.extend(question["directional"]["files"].values())
        process = question["directional"]["process"]
        if (process["method"] !=
                "authored-floor-directional-owner-shoulder-feather" or
                not process["preservesAuthoredContinuousAlpha"] or
                not process["usesDirectionalShoulders"] or
                process["usesGlobalBlur"] or
                process["usesBinaryThreshold"] or
                process["usesNewNoise"] or
                process["changedNEPixels"] or
                process["changedSupportQuadrantPixels"] or
                process["changedOwnerCornerPatchPixels"] or
                process["pixelsBelowAuthoredSource"] or
                not process["changedNWNorthShoulderPixels"] or
                not process["changedSEEastShoulderPixels"]):
            errors.append(
                f"directional edge repair escaped its contract: {question['key']}")
        if process["distinctAlphaValues"] < 64:
            errors.append(
                f"directional edge repair lost alpha detail: {question['key']}")
        authored = np.asarray(Image.open(
            output / question["authored"]["files"]["mask"]["file"])
            .convert("L"), dtype=np.uint8)
        r59_mask = Image.open(
            output / question["r59"]["files"]["mask"]["file"])
        directional = Image.open(
            output / question["directional"]["files"]["mask"]["file"])
        directional_u8 = np.asarray(directional.convert("L"), dtype=np.uint8)
        if np.any(directional_u8 < authored):
            errors.append(f"R60 fell below authored floor: {question['key']}")
        r59_u8 = np.asarray(r59_mask.convert("L"), dtype=np.uint8)
        height, width = r59_u8.shape
        if not np.array_equal(
                r59_u8[:height // 2, width // 2:],
                directional_u8[:height // 2, width // 2:]):
            errors.append(f"R60 changed accepted NE: {question['key']}")
        if not np.array_equal(
                r59_u8[height // 2:, :width // 2],
                directional_u8[height // 2:, :width // 2]):
            errors.append(f"R60 changed support SW: {question['key']}")
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
            if (_image_hash(Image.composite(owner, support, directional)) !=
                    files["directionalResult"]["pixelSha256"]):
                errors.append(f"R60 outcome mismatch: {question['key']}")
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
        "--checkpoint", default="R60-ICE-DIRECTIONAL-EDGE-FEATHER")
    args = parser.parse_args()
    print(json.dumps(render(args.r57, args.r59, args.output,
                            args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
