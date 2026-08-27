#!/usr/bin/env python3
"""Render the polarity-correct Ice inside owner-quadrant cleanup review."""

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
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script
from render_soft_authored_mask_pilot import _alpha_heatmap
from render_snow_ice_pending_review import (
    CLEANUP_KEYS,
    _load,
    _summary,
    remove_disconnected_owner_quadrant_support,
)


REVIEW_KEYS = tuple(sorted(CLEANUP_KEYS))


def render(r54: Path, output: Path,
           checkpoint: str = "R55-ICE-OWNER-CLEANUP") -> dict:
    r54 = r54.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"owner-cleanup output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    source_bytes = (r54 / "pending-review-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    by_key = {item["key"]: item for item in source["questions"]}
    if any(key not in by_key for key in REVIEW_KEYS):
        raise ValueError("R54 is missing an Ice inside question")

    questions = []
    cards = []
    for number, key in enumerate(REVIEW_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        current = _load(r54, prior["current"]["files"]["mask"], "L")
        aligned = _load(
            r54, prior["current"]["files"]["alignedSource"], "RGB")
        cleaned, cleanup_audit = (
            remove_disconnected_owner_quadrant_support(current, topology))
        cleaned_metrics = mask_metrics(current, cleaned, topology)
        current_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(current, images / f"{prefix}-current-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(current),
                images / f"{prefix}-current-heatmap.png"),
        }
        cleaned_files = {
            "mask": _save(
                cleaned, images / f"{prefix}-owner-cleaned-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(cleaned),
                images / f"{prefix}-owner-cleaned-heatmap.png"),
            "difference": _save(
                _difference_image(current, cleaned),
                images / f"{prefix}-owner-cleanup-difference.png"),
        }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r54, prior_outcome["files"]["owner"], "RGB")
            support = _load(
                r54, prior_outcome["files"]["support"], "RGB")
            current_result = Image.composite(owner, support, current)
            cleaned_result = Image.composite(owner, support, cleaned)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "currentResult": _save(
                    current_result,
                    images / f"{outcome_prefix}-current-result.png"),
                "cleanedResult": _save(
                    cleaned_result,
                    images / f"{outcome_prefix}-cleaned-result.png"),
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
<p class="muted">Requested owner: {html.escape(entry['ownerSource'])} · support: {html.escape(entry['supportSource'])}</p><div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform · NW/NE/SE</figcaption></figure>
<figure><img src="{files['currentResult']['file']}" alt="Current detailed outcome"><figcaption><strong>Current detailed result</strong></figcaption></figure>
<figure><img src="{files['cleanedResult']['file']}" alt="Owner-cleaned outcome"><figcaption><strong>Owner-quadrant-cleaned result</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform · SW</figcaption></figure>
</div></section>''')

        decisions = [
            "Still uncertain",
            "Prefer owner-quadrant-cleaned detailed route",
            "Approve current detailed route without owner cleanup",
            "Reject both candidates",
            "Needs another authored source",
        ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        source_alignment = prior["sourceAlignment"]
        current_metrics = prior["current"]["metrics"]
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. Ice fracture · {html.escape(topology)}</h2>
<div class="corner-contract" aria-label="Corner ownership"><div>NW<br><strong>OWNER</strong></div><div>NE<br><strong>OWNER · cleanup focus</strong></div><div>SW<br><strong>SUPPORT · preserved</strong></div><div>SE<br><strong>OWNER</strong></div></div>
<p class="notice">Corrected 3-of-4 polarity: disconnected support spots are filled only inside NW/NE/SE. The SW support quadrant, every support component connected to SW, the authored boundary, and all sub-20% painterly opacity are byte-exact.</p>
<p class="muted">Source {html.escape(prior['sourceProvenance'])} · semantic transform: {source_alignment['sourceRotationClockwise']}° clockwise, {'inverted owner polarity' if source_alignment['sourceOwnerInverted'] else 'original owner polarity'}, then fixed physical→logical vertical mirror.</p>
<p class="metric">Current: {html.escape(_summary(current_metrics))}<br>Owner-cleaned: {html.escape(_summary(cleaned_metrics))}<br>Filled disconnected support: {cleanup_audit['filledPixels']} pixels ({cleanup_audit['changedFraction']:.2%} of tile) · changed in SW: {cleanup_audit['changedInSupportQuadrants']} · changed connected support: {cleanup_audit['connectedPixelsChanged']} · changed sub-20% opacity: {cleanup_audit['subThresholdPixelsChanged']}</p>
<div class="evidence-grid">
<figure><img src="{current_files['alignedSource']['file']}" alt="Aligned authored source"><figcaption>Aligned authored transition</figcaption></figure>
<figure><img src="{current_files['mask']['file']}" alt="Current detailed mask"><figcaption><strong>Current detailed mask</strong></figcaption></figure>
<figure><img src="{current_files['heatmap']['file']}" alt="Current heatmap"><figcaption>Current alpha heatmap</figcaption></figure>
<figure><img src="{cleaned_files['mask']['file']}" alt="Owner-cleaned mask"><figcaption><strong>Owner-quadrant-cleaned mask</strong></figcaption></figure>
<figure><img src="{cleaned_files['heatmap']['file']}" alt="Owner-cleaned heatmap"><figcaption>Owner-cleaned alpha heatmap</figcaption></figure>
<figure><img src="{cleaned_files['difference']['file']}" alt="Owner cleanup difference"><figcaption>Difference · only filled owner-quadrant support islands</figcaption></figure></div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional exact corner detail"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": prior["sourceProvenance"],
            "sourceAlignment": source_alignment,
            "cornerOwnership": {
                "owner": ["NW", "NE", "SE"], "support": ["SW"]},
            "allowedDecisions": decisions,
            "current": {"metrics": current_metrics,
                        "files": current_files},
            "cleaned": {"audit": cleanup_audit,
                        "metrics": cleaned_metrics,
                        "files": cleaned_files},
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:400px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.corner-contract{{display:grid;grid-template-columns:repeat(2,minmax(180px,280px));width:max-content;max-width:100%;border:2px solid var(--border);margin:10px 0}}.corner-contract div{{padding:14px;border:1px solid var(--border);background:var(--panel)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples,.corner-contract{{grid-template-columns:1fr;width:100%}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · corrected Ice owner cleanup</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> These are the two R54 Ice inside rows only. They are explicitly 3-of-4 masks: NW, NE, and SE belong to the Ice owner material; SW belongs to support. The former SW-cleanup candidate is superseded and is not offered.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-ice-owner-cleanup-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoint": source["checkpoint"],
        "sourceAnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "owner-cleanup-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {"html": str(html_path), "analysis": str(analysis_path),
            **validate_output(output)}


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "owner-cleanup-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("owner cleanup must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(REVIEW_KEYS):
        errors.append("owner-cleanup keys or order differ from R54")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    for question in questions:
        if question["cornerOwnership"] != {
                "owner": ["NW", "NE", "SE"], "support": ["SW"]}:
            errors.append(f"incorrect 3-of-4 polarity: {question['key']}")
        indexed.extend(question["current"]["files"].values())
        indexed.extend(question["cleaned"]["files"].values())
        audit = question["cleaned"]["audit"]
        if (audit["changedInSupportQuadrants"] or
                audit["connectedPixelsChanged"] or
                audit["subThresholdPixelsChanged"]):
            errors.append(f"owner cleanup escaped its scope: {question['key']}")
        current = Image.open(
            output / question["current"]["files"]["mask"]["file"])
        cleaned = Image.open(
            output / question["cleaned"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, current)) !=
                    files["currentResult"]["pixelSha256"]):
                errors.append(f"current outcome mismatch: {question['key']}")
            if (_image_hash(Image.composite(owner, support, cleaned)) !=
                    files["cleanedResult"]["pixelSha256"]):
                errors.append(f"cleaned outcome mismatch: {question['key']}")
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
    """Verify that both R55 candidates remain explicitly pending."""
    rendered = validate_output(output)
    analysis = json.loads((output / "owner-cleanup-analysis.json")
                          .read_text(encoding="utf-8"))
    review = json.loads(decisions_path.read_text(encoding="utf-8"))
    questions = {item["key"]: item for item in analysis["questions"]}
    decisions = review.get("decisions", {})
    if set(decisions) != set(questions):
        raise ValueError("R55 decision keys do not exactly match the review")
    for key, decision in decisions.items():
        if decision.get("answer") != "Still uncertain":
            raise ValueError(f"R55 route was not approved: {key}")
        note = decision.get("note", "").casefold()
        if "support-texture artifact" not in note:
            raise ValueError(f"R55 residual artifact note is missing: {key}")
        for outcome in questions[key]["outcomes"]:
            if (not outcome.get("ownerSource") or
                    not outcome.get("supportSource") or
                    not outcome["files"].get("cleanedResult")):
                raise ValueError(f"R55 evidence is incomplete: {key}")
    return {
        **rendered,
        "approved": 0,
        "pending": len(decisions),
        "generationChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r54", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R55-ICE-OWNER-CLEANUP")
    args = parser.parse_args()
    print(json.dumps(render(args.r54, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
