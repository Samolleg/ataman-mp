#!/usr/bin/env python3
"""Render Ice owner artifact repair with an edge-to-center opacity ramp."""

from __future__ import annotations

import argparse
from collections import deque
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
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import (
    _corner_seeds,
    _load,
    _quadrant_slice,
    _summary,
)


def _connected_component_from_corners(
        selected: np.ndarray,
        corners: tuple[bool, bool, bool, bool]) -> np.ndarray:
    reachable = np.zeros_like(selected, dtype=np.bool_)
    queue = deque()
    for point in _corner_seeds(selected, corners):
        if not reachable[point]:
            reachable[point] = True
            queue.append(point)
    height, width = selected.shape
    while queue:
        y, x = queue.popleft()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not (dx or dy):
                    continue
                ny, nx = y + dy, x + dx
                if (0 <= ny < height and 0 <= nx < width and
                        selected[ny, nx] and not reachable[ny, nx]):
                    reachable[ny, nx] = True
                    queue.append((ny, nx))
    return reachable


def _distance_from(selected: np.ndarray) -> np.ndarray:
    """Return an eight-neighbour pixel distance from a selected body."""
    height, width = selected.shape
    distance = np.full((height, width), height + width, dtype=np.int32)
    queue = deque()
    for y, x in np.argwhere(selected):
        distance[y, x] = 0
        queue.append((int(y), int(x)))
    while queue:
        y, x = queue.popleft()
        next_distance = distance[y, x] + 1
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not (dx or dy):
                    continue
                ny, nx = y + dy, x + dx
                if (0 <= ny < height and 0 <= nx < width and
                        next_distance < distance[ny, nx]):
                    distance[ny, nx] = next_distance
                    queue.append((ny, nx))
    return distance


def _edge_to_center_gradient(
        height: int, width: int,
        owner_corners: tuple[bool, bool, bool, bool]) -> np.ndarray:
    """Return one at owner-facing outer edges and zero at tile centre."""
    half_y, half_x = height // 2, width // 2
    result = np.zeros((height, width), dtype=np.float32)
    yy, xx = np.mgrid[0:height, 0:width]
    inward = (
        (xx / max(1, half_x - 1), yy / max(1, half_y - 1)),
        ((width - 1 - xx) / max(1, width - half_x - 1),
         yy / max(1, half_y - 1)),
        ((width - 1 - xx) / max(1, width - half_x - 1),
         (height - 1 - yy) / max(1, height - half_y - 1)),
        (xx / max(1, half_x - 1),
         (height - 1 - yy) / max(1, height - half_y - 1)),
    )
    for index, owned in enumerate(owner_corners):
        if not owned:
            continue
        area = _quadrant_slice(index, height, width)
        local = 1.0 - np.minimum(
            np.clip(inward[index][0], 0.0, 1.0),
            np.clip(inward[index][1], 0.0, 1.0))
        local = local * local * (3.0 - 2.0 * local)
        result[area] = local[area]
    return result


def repair_owner_artifacts_with_edge_gradient(
        mask: Image.Image, topology: str,
        exact_edge_radius: int = 6,
        feather_radius: int = 18,
        support_floor: float = 0.20) -> tuple[Image.Image, dict]:
    """Lift owner-field dips by a ramp without replacing blend geometry."""
    if exact_edge_radius < 0 or feather_radius <= exact_edge_radius:
        raise ValueError("gradient protection radii are invalid")
    owner = expected_owner_corners(topology)
    support = tuple(not value for value in owner)
    if sum(support) != 1 or not topology.startswith("inside-3of4"):
        raise ValueError("gradient repair is limited to inside-3of4")
    original_u8 = np.asarray(mask.convert("L"), dtype=np.uint8).copy()
    values = original_u8.astype(np.float32) / 255.0
    strong_support = values <= 1.0 - support_floor
    connected_support = _connected_component_from_corners(
        strong_support, support)
    distance = _distance_from(connected_support)
    blend = np.clip(
        (distance.astype(np.float32) - exact_edge_radius) /
        float(feather_radius - exact_edge_radius), 0.0, 1.0)
    blend = blend * blend * (3.0 - 2.0 * blend)
    gradient = _edge_to_center_gradient(
        original_u8.shape[0], original_u8.shape[1], owner)
    owner_region = np.zeros_like(connected_support, dtype=np.bool_)
    support_region = np.zeros_like(connected_support, dtype=np.bool_)
    for index, owned in enumerate(owner):
        area = _quadrant_slice(
            index, original_u8.shape[0], original_u8.shape[1])
        (owner_region if owned else support_region)[area] = True
    target = np.maximum(values, gradient)
    repaired = values.copy()
    repaired[owner_region] = (
        values[owner_region] +
        (target[owner_region] - values[owner_region]) * blend[owner_region])
    result_u8 = np.uint8(np.clip(np.rint(repaired * 255.0), 0, 255))
    changed = result_u8 != original_u8
    exact_zone = distance <= exact_edge_radius
    result = Image.fromarray(result_u8, "L")
    return result, {
        "method": "owner-edge-to-center-gradient-lift",
        "supportFloor": support_floor,
        "exactEdgeRadiusPixels": exact_edge_radius,
        "featherRadiusPixels": feather_radius,
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedConnectedSupportPixels": int(
            (changed & connected_support).sum()),
        "changedInsideExactEdgeRadius": int((changed & exact_zone).sum()),
        "changedSupportQuadrantPixels": int(
            (changed & support_region).sum()),
        "ownerOuterEdgeTarget": 1.0,
        "ownerCenterTarget": 0.0,
        "usesSolidOwnerPlateau": False,
        "usesGlobalBinaryCleanup": False,
        "usesBroadGaussianSoftening": False,
        "distinctAlphaValues": int(len(np.unique(result_u8))),
    }


def render(r56: Path, output: Path,
           checkpoint: str = "R57-ICE-GRADIENT-REPAIR") -> dict:
    r56 = r56.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"gradient-repair output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    source_bytes = (r56 / "owner-plateau-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    by_key = {item["key"]: item for item in source["questions"]}
    if any(key not in by_key for key in REVIEW_KEYS):
        raise ValueError("R56 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        aligned = _load(
            r56, prior["r55"]["files"]["alignedSource"], "RGB")
        r55_mask = _load(r56, prior["r55"]["files"]["mask"], "L")
        gradient, process = repair_owner_artifacts_with_edge_gradient(
            r55_mask, topology)
        gradient_metrics = mask_metrics(r55_mask, gradient, topology)
        r55_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(
                r55_mask, images / f"{prefix}-r55-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r55_mask),
                images / f"{prefix}-r55-heatmap.png"),
        }
        gradient_files = {
            "mask": _save(
                gradient, images / f"{prefix}-gradient-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(gradient),
                images / f"{prefix}-gradient-heatmap.png"),
            "difference": _save(
                _difference_image(r55_mask, gradient),
                images / f"{prefix}-gradient-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r56, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r56, prior_outcome["files"]["support"], "RGB")
            r55_result = Image.composite(owner, support, r55_mask)
            gradient_result = Image.composite(owner, support, gradient)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r55Result": _save(
                    r55_result, images / f"{outcome_prefix}-r55-result.png"),
                "gradientResult": _save(
                    gradient_result,
                    images / f"{outcome_prefix}-gradient-result.png"),
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
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform · NW/NE/SE</figcaption></figure>
<figure><img src="{files['r55Result']['file']}" alt="R55 result"><figcaption><strong>R55 partial cleanup</strong></figcaption></figure>
<figure><img src="{files['gradientResult']['file']}" alt="Gradient repaired result"><figcaption><strong>Edge→center gradient repair</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform · SW</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer edge-to-center gradient repair",
            "Keep R55 partial cleanup",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture · {html.escape(topology)}</h2>
<div class="corner-contract"><div>NW<br><strong>OWNER · 100% at outer edges</strong></div><div>NE<br><strong>OWNER · 100% at outer edges</strong></div><div>SW<br><strong>SUPPORT · unchanged</strong></div><div>SE<br><strong>OWNER · 100% at outer edges</strong></div></div>
<p class="notice">R56's hard owner plateau is rejected. This candidate only raises support-like opacity dips to a smooth edge→center minimum: 100% on owner-facing outer tile edges, decreasing continuously to 0% repair pressure at the center. The connected authored blend and its inner {process['exactEdgeRadiusPixels']} px remain byte-exact; repair influence feathers in through {process['featherRadiusPixels']} px.</p>
<p class="muted">Same source {html.escape(prior['sourceProvenance'])} and exact semantic transform. Existing authored opacity above the ramp is retained.</p>
<p class="metric">R55: {html.escape(_summary(prior['r55']['metrics']))}<br>Gradient: {html.escape(_summary(gradient_metrics))}<br>Changed {process['changedPixels']} px ({process['changedFraction']:.2%}) · connected edge changed: {process['changedConnectedSupportPixels']} · exact edge zone changed: {process['changedInsideExactEdgeRadius']} · SW changed: {process['changedSupportQuadrantPixels']} · alpha levels: {process['distinctAlphaValues']}</p>
<div class="evidence-grid">
<figure><img src="{r55_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{r55_files['mask']['file']}" alt="R55 mask"><figcaption><strong>R55 partial cleanup mask</strong></figcaption></figure>
<figure><img src="{r55_files['heatmap']['file']}" alt="R55 heatmap"><figcaption>R55 alpha heatmap</figcaption></figure>
<figure><img src="{gradient_files['mask']['file']}" alt="Gradient repaired mask"><figcaption><strong>Edge→center gradient-repaired mask</strong></figcaption></figure>
<figure><img src="{gradient_files['heatmap']['file']}" alt="Gradient heatmap"><figcaption>Gradient-repaired alpha heatmap</figcaption></figure>
<figure><img src="{gradient_files['difference']['file']}" alt="Gradient difference"><figcaption>R55 → gradient lift; green is added owner opacity</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional gradient or edge detail"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": prior["sourceAlignment"],
            "cornerOwnership": {
                "owner": ["NW", "NE", "SE"], "support": ["SW"]},
            "allowedDecisions": decisions,
            "r55": {"metrics": prior["r55"]["metrics"],
                    "files": r55_files},
            "gradient": {"process": process, "metrics": gradient_metrics,
                         "files": gradient_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.corner-contract{{display:grid;grid-template-columns:repeat(2,minmax(180px,300px));width:max-content;max-width:100%;border:2px solid var(--border);margin:10px 0}}.corner-contract div{{padding:14px;border:1px solid var(--border);background:var(--panel)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples,.corner-contract{{grid-template-columns:1fr;width:100%}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · Ice gradient repair</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R56's hard 100% owner plateau is rejected. These two candidates repair owner-field artifacts with a smooth outer-edge-to-center opacity floor while retaining the original connected blend edge and its feather.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-gradient-repair-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoint": source["checkpoint"],
        "sourceAnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "gradient-repair-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "gradient-repair-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("gradient repair must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("gradient-repair keys or order differ from R56")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["r55"]["files"].values())
        indexed.extend(question["gradient"]["files"].values())
        process = question["gradient"]["process"]
        if (process["usesSolidOwnerPlateau"] or
                process["usesGlobalBinaryCleanup"] or
                process["usesBroadGaussianSoftening"] or
                process["changedConnectedSupportPixels"] or
                process["changedInsideExactEdgeRadius"] or
                process["changedSupportQuadrantPixels"]):
            errors.append(f"gradient repair escaped its contract: {question['key']}")
        if process["distinctAlphaValues"] < 64:
            errors.append(f"gradient repair lost alpha detail: {question['key']}")
        r55_mask = Image.open(
            output / question["r55"]["files"]["mask"]["file"])
        gradient = Image.open(
            output / question["gradient"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r55_mask)) !=
                    files["r55Result"]["pixelSha256"]):
                errors.append(f"R55 outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, gradient)) !=
                    files["gradientResult"]["pixelSha256"]):
                errors.append(f"gradient outcome mismatch: {question['key']}")
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
    parser.add_argument("--r56", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R57-ICE-GRADIENT-REPAIR")
    args = parser.parse_args()
    print(json.dumps(render(args.r56, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
