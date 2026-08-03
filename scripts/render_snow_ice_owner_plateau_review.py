#!/usr/bin/env python3
"""Review Ice owner-plateau reconstruction against the residual R55 route."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
import re
from pathlib import Path

from PIL import Image

from render_authored_mask_repair_pilot import (
    _difference_image,
    _image_hash,
    _save,
    expected_owner_corners,
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script
from render_soft_authored_mask_pilot import (
    _alpha_heatmap,
    reconstruct_soft_ownership,
)
from render_snow_ice_owner_cleanup_review import REVIEW_KEYS
from render_snow_ice_pending_review import _load, _summary


def reconstruct_ice_owner_plateau(
        aligned_source: Image.Image,
        topology: str) -> tuple[Image.Image, dict]:
    """Recover Ice ownership with targeted false support-hole cleanup."""
    if not topology.startswith("inside-3of4"):
        raise ValueError("Ice owner plateau is limited to inside-3of4")
    return reconstruct_soft_ownership(
        aligned_source, expected_owner_corners(topology),
        remove_bounded_holes=True)


def render(r55: Path, output: Path,
           checkpoint: str = "R56-ICE-OWNER-PLATEAU") -> dict:
    r55 = r55.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"owner-plateau output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    source_bytes = (r55 / "owner-cleanup-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    by_key = {item["key"]: item for item in source["questions"]}
    if any(key not in by_key for key in REVIEW_KEYS):
        raise ValueError("R55 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        aligned = _load(
            r55, prior["current"]["files"]["alignedSource"], "RGB")
        r55_mask = _load(r55, prior["cleaned"]["files"]["mask"], "L")
        plateau, process = reconstruct_ice_owner_plateau(aligned, topology)
        plateau_metrics = mask_metrics(r55_mask, plateau, topology)
        r55_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(
                r55_mask, images / f"{prefix}-r55-owner-cleaned-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(r55_mask),
                images / f"{prefix}-r55-owner-cleaned-heatmap.png"),
        }
        plateau_files = {
            "mask": _save(
                plateau, images / f"{prefix}-owner-plateau-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(plateau),
                images / f"{prefix}-owner-plateau-heatmap.png"),
            "difference": _save(
                _difference_image(r55_mask, plateau),
                images / f"{prefix}-plateau-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r55, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r55, prior_outcome["files"]["support"], "RGB")
            r55_result = Image.composite(owner, support, r55_mask)
            plateau_result = Image.composite(owner, support, plateau)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "r55Result": _save(
                    r55_result, images / f"{outcome_prefix}-r55-result.png"),
                "plateauResult": _save(
                    plateau_result,
                    images / f"{outcome_prefix}-plateau-result.png"),
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
<figure><img src="{files['r55Result']['file']}" alt="R55 result"><figcaption><strong>R55 result · residual artifacts</strong></figcaption></figure>
<figure><img src="{files['plateauResult']['file']}" alt="Owner plateau result"><figcaption><strong>Targeted owner-plateau result</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform · SW</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer targeted owner-plateau route",
            "Keep R55 owner-cleaned route",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        hole = process["boundedHoleSuppression"]
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture · {html.escape(topology)}</h2>
<div class="corner-contract"><div>NW<br><strong>OWNER</strong></div><div>NE<br><strong>OWNER · artifact focus</strong></div><div>SW<br><strong>SUPPORT</strong></div><div>SE<br><strong>OWNER</strong></div></div>
<p class="notice">The new route reconstructs the same aligned authored source with transition-local false support-hole suppression enabled. It restores confidently solid owner plateaus while leaving the lower-confidence transition band continuous. It does not apply a global binary mask or broad blur.</p>
<p class="muted">Source {html.escape(prior['sourceProvenance'])} · exact semantic transform retained.</p>
<p class="metric">R55: {html.escape(_summary(prior['cleaned']['metrics']))}<br>Owner plateau: {html.escape(_summary(plateau_metrics))}<br>Solid owner plateau: {hole['solidOwnerPlateauPixels']} px · bounded false holes: {hole['components']} components / {hole['pixels']} core px · distinct alpha values: {process['distinctAlphaValues']} · soft pixels: {process['softPixelFraction']:.1%}</p>
<div class="evidence-grid">
<figure><img src="{r55_files['alignedSource']['file']}" alt="Aligned source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{r55_files['mask']['file']}" alt="R55 mask"><figcaption><strong>R55 owner-cleaned mask</strong></figcaption></figure>
<figure><img src="{r55_files['heatmap']['file']}" alt="R55 heatmap"><figcaption>R55 alpha heatmap</figcaption></figure>
<figure><img src="{plateau_files['mask']['file']}" alt="Owner plateau mask"><figcaption><strong>Targeted owner-plateau mask</strong></figcaption></figure>
<figure><img src="{plateau_files['heatmap']['file']}" alt="Owner plateau heatmap"><figcaption>Owner-plateau alpha heatmap</figcaption></figure>
<figure><img src="{plateau_files['difference']['file']}" alt="Plateau difference"><figcaption>R55 → owner-plateau difference</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional exact artifact detail"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": prior["sourceAlignment"],
            "cornerOwnership": {
                "owner": ["NW", "NE", "SE"], "support": ["SW"]},
            "allowedDecisions": decisions,
            "r55": {"metrics": prior["cleaned"]["metrics"],
                    "files": r55_files},
            "plateau": {"process": process, "metrics": plateau_metrics,
                        "files": plateau_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.corner-contract{{display:grid;grid-template-columns:repeat(2,minmax(180px,280px));width:max-content;max-width:100%;border:2px solid var(--border);margin:10px 0}}.corner-contract div{{padding:14px;border:1px solid var(--border);background:var(--panel)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples,.corner-contract{{grid-template-columns:1fr;width:100%}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · Ice owner plateau</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> Both R55 masks remain pending. This comparison targets the remaining support-texture artifacts inside Ice-owned NW/NE/SE. It reuses the exact R53 authored source and semantic transform; only the reconstruction's targeted false-hole suppression changes.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-owner-plateau-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoint": source["checkpoint"],
        "sourceAnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "owner-plateau-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "owner-plateau-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("owner plateau must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("owner-plateau keys or order differ from R55")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        indexed.extend(question["r55"]["files"].values())
        indexed.extend(question["plateau"]["files"].values())
        process = question["plateau"]["process"]
        if (not process["usesTargetedFalseHoleCleanup"] or
                process["usesGlobalBinaryCleanup"] or
                process["usesBroadGaussianSoftening"] or
                process["usesExternalUniformPixels"]):
            errors.append(f"invalid plateau process: {question['key']}")
        if process["cornerValues"] != {
                "NW": 255, "NE": 255, "SE": 255, "SW": 0}:
            errors.append(f"incorrect plateau corners: {question['key']}")
        r55_mask = Image.open(
            output / question["r55"]["files"]["mask"]["file"])
        plateau = Image.open(
            output / question["plateau"]["files"]["mask"]["file"])
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
            if (_image_hash(Image.composite(owner, support, plateau)) !=
                    files["plateauResult"]["pixelSha256"]):
                errors.append(f"plateau outcome mismatch: {question['key']}")
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
    parser.add_argument("--r55", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R56-ICE-OWNER-PLATEAU")
    args = parser.parse_args()
    print(json.dumps(render(args.r55, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
