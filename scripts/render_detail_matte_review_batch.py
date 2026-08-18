#!/usr/bin/env python3
"""Review detailed opacity reconstruction for the nine R44-approved masks.

The R44 repaired masks remain the approved baseline. This renderer reuses their
exact authored sources and topology alignments, reconstructs a continuous alpha
field from the authored transition itself, and presents detail-compatible
uniform outcomes for review. It does not change runtime generation.
"""

from __future__ import annotations

import argparse
import csv
from dataclasses import dataclass
import hashlib
import html
import json
import re
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

from render_authored_mask_repair_pilot import (
    _cell,
    _difference_image,
    _image_hash,
    _save,
    _sha256_bytes,
    _slug,
    align_authored_mask,
    expected_owner_corners,
    repair_authored_mask,
)
from render_blend_mask_questionnaire import interaction_script
from render_soft_authored_mask_pilot import (
    _alpha_heatmap,
    reconstruct_soft_ownership,
)


@dataclass(frozen=True)
class ApprovedCase:
    family: str
    topology: str
    provenance: str
    decision_key: str


APPROVED_CASES = (
    ApprovedCase("grass-strands", "outer-1of4-v1", "BaseGipat:450",
                 "authored-repair-grass-strands-outer-1of4-v1"),
    ApprovedCase("grass-strands", "edge-2of4-v1", "BaseGipat:349",
                 "authored-repair-grass-strands-edge-2of4-v1"),
    ApprovedCase("grass-strands", "diagonal-2of4", "BaseGipat:56",
                 "authored-repair-grass-strands-diagonal-2of4"),
    ApprovedCase("soil-mottle", "outer-1of4-v2", "Zone10:235",
                 "authored-repair-soil-mottle-outer-1of4-v2"),
    ApprovedCase("soil-mottle", "edge-2of4-v2", "Zone1:479",
                 "authored-repair-soil-mottle-edge-2of4-v2"),
    ApprovedCase("soil-mottle", "inside-3of4-v1", "BaseGipat:450",
                 "authored-repair-soil-mottle-inside-3of4-v1"),
    ApprovedCase("stone-fracture", "outer-1of4-v1", "BaseGipat:308",
                 "authored-repair-stone-fracture-outer-1of4-v1"),
    ApprovedCase("stone-fracture", "edge-2of4-v1", "BaseGipat:256",
                 "authored-repair-stone-fracture-edge-2of4-v1"),
    ApprovedCase("stone-fracture", "diagonal-2of4", "Bz7g:218",
                 "authored-repair-stone-fracture-diagonal-2of4"),
)

MAX_EFFECTIVE_DETAIL_RATIO = 2.0


def effective_detail(image: Image.Image) -> float:
    """Mean local RGB change, used only to avoid review-image mismatch."""
    values = np.asarray(image.convert("RGB"), dtype=np.int16)
    horizontal = np.abs(values[:, 1:] - values[:, :-1]).sum(axis=2).mean()
    vertical = np.abs(values[1:] - values[:-1]).sum(axis=2).mean()
    return float((horizontal + vertical) * 0.5)


def compatible_representatives(review: Path, rows: list[dict[str, str]],
                               limit: int = 3) -> list[dict[str, object]]:
    """Choose scenario coverage among uniforms with comparable effective detail."""
    cache: dict[str, float] = {}

    def score(relative: str) -> float:
        if relative not in cache:
            with Image.open(review / relative) as image:
                cache[relative] = effective_detail(image)
        return cache[relative]

    candidates = []
    seen = set()
    for row in rows:
        identity = (row["owner_material_id"], row["support_material_id"])
        if identity in seen:
            continue
        seen.add(identity)
        owner_detail = score(row["owner_uniform"])
        support_detail = score(row["support_uniform"])
        ratio = (max(owner_detail, support_detail) /
                 max(1e-6, min(owner_detail, support_detail)))
        if ratio > MAX_EFFECTIVE_DETAIL_RATIO:
            continue
        enriched = dict(row)
        enriched.update({
            "owner_effective_detail": round(owner_detail, 4),
            "support_effective_detail": round(support_detail, 4),
            "effective_detail_ratio": round(ratio, 4),
        })
        candidates.append(enriched)

    candidates.sort(key=lambda row: (
        row["effective_detail_ratio"], row["owner_material"].casefold(),
        row["support_material"].casefold(), row["provenance"]))
    selected = []
    selected_ids = set()
    for scenario in ("cross-style", "same-style"):
        for row in candidates:
            identity = (row["owner_material_id"], row["support_material_id"])
            if row["support_scenario"] == scenario and identity not in selected_ids:
                selected.append(row)
                selected_ids.add(identity)
                break
    for row in candidates:
        identity = (row["owner_material_id"], row["support_material_id"])
        if identity not in selected_ids:
            selected.append(row)
            selected_ids.add(identity)
        if len(selected) >= limit:
            break
    return selected[:limit]


def _source_manifest(review: Path) -> dict[int, dict]:
    path = review.parent / "manifest.json"
    if not path.is_file():
        return {}
    payload = json.loads(path.read_text(encoding="utf-8"))
    return {int(item["material_id"]): item
            for item in payload.get("reviewed_pure_sources", [])}


def _find_source(rows: list[dict[str, str]], case: ApprovedCase) -> dict[str, str]:
    for row in rows:
        if (row["owner_style"] == case.family and
                row["topology"] == case.topology and
                row["provenance"] == case.provenance):
            return row
    raise ValueError(
        f"missing approved authored source {case.family} {case.topology} "
        f"{case.provenance}")


def _source_label(material_id: str, sources: dict[int, dict]) -> str:
    item = sources.get(int(material_id))
    if not item:
        return "source provenance unavailable"
    return (f"{item['source_namespace']}:{item['reviewed_pure_anchor_slot']} · "
            f"{item['source_tile_width']} px · no resample"
            if not item.get("resampled") else
            f"{item['source_namespace']}:{item['reviewed_pure_anchor_slot']} · "
            f"resampled {item['source_tile_width']}→{item['target_tile_width']} px")


def _outcomes(review: Path, rows: list[dict[str, str]],
              approved: Image.Image, detailed: Image.Image, images: Path,
              prefix: str, sources: dict[int, dict]) -> tuple[list, str]:
    entries = []
    sections = []
    representatives = compatible_representatives(review, rows)
    if len(representatives) < 3:
        raise ValueError(
            f"{prefix} has only {len(representatives)} detail-compatible outcomes")
    for number, row in enumerate(representatives, 1):
        with Image.open(review / row["owner_uniform"]) as source:
            owner = source.convert("RGB")
        with Image.open(review / row["support_uniform"]) as source:
            support = source.convert("RGB")
        approved_result = Image.composite(owner, support, approved)
        detailed_result = Image.composite(owner, support, detailed)
        item_prefix = f"{prefix}-example-{number}"
        files = {
            "owner": _save(owner, images / f"{item_prefix}-owner.png"),
            "approvedResult": _save(
                approved_result,
                images / f"{item_prefix}-approved-result.png"),
            "detailedResult": _save(
                detailed_result,
                images / f"{item_prefix}-detailed-result.png"),
            "support": _save(support, images / f"{item_prefix}-support.png"),
        }
        entry = {
            "ownerMaterial": row["owner_material"],
            "supportMaterial": row["support_material"],
            "scenario": row["support_scenario"],
            "ownerSource": _source_label(row["owner_material_id"], sources),
            "supportSource": _source_label(row["support_material_id"], sources),
            "ownerEffectiveDetail": row["owner_effective_detail"],
            "supportEffectiveDetail": row["support_effective_detail"],
            "effectiveDetailRatio": row["effective_detail_ratio"],
            "files": files,
        }
        entries.append(entry)
        sections.append(f'''<section class="example"><h3>{html.escape(row['owner_material'])} + {html.escape(row['support_material'])}</h3>
<p class="muted">{html.escape(row['support_scenario'])} · effective-detail ratio {row['effective_detail_ratio']:.2f}</p>
<div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Requested uniform"><figcaption>Requested · {html.escape(entry['ownerSource'])} · detail {row['owner_effective_detail']:.1f}</figcaption></figure>
<figure><img src="{files['approvedResult']['file']}" alt="Approved R44 result"><figcaption>Approved R44 result</figcaption></figure>
<figure><img src="{files['detailedResult']['file']}" alt="Detailed opacity result"><figcaption><strong>Detailed opacity result</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support · {html.escape(entry['supportSource'])} · detail {row['support_effective_detail']:.1f}</figcaption></figure>
</div></section>''')
    return entries, "".join(sections)


def _verify_r44_decisions() -> None:
    path = Path(__file__).with_name("authored_mask_repair_r44_decisions.json")
    payload = json.loads(path.read_text(encoding="utf-8"))
    decisions = payload.get("decisions", {})
    for case in APPROVED_CASES:
        answer = decisions.get(case.decision_key, {}).get("answer")
        if answer != "Approve repaired authored mask":
            raise ValueError(f"R44 authority does not approve {case.decision_key}")


def render(review: Path, output: Path,
           checkpoint: str = "R48-DETAIL-MATTE-BATCH") -> dict:
    _verify_r44_decisions()
    review = review.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"review output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["owner_style"], row["topology"])].append(row)
    sources = _source_manifest(review)

    questions = []
    cards = []
    options = "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
        for value in (
            "Keep approved R44 mask",
            "Approve detailed opacity matte",
            "Too soft or detail lost",
            "Wrong ownership shape",
            "Authored texture confused with opacity",
            "Reject this reconstruction"))

    for number, case in enumerate(APPROVED_CASES, 1):
        selected = _find_source(rows, case)
        raw = _cell(review, selected["sheet"], int(selected["row"]),
                    int(selected["column"]), "L")
        if _image_hash(raw).lower() != selected["mask_sha256"].lower():
            raise ValueError(f"source mask hash mismatch for {case.provenance}")
        aligned, alignment = align_authored_mask(raw, case.topology)
        authored = _cell(
            review, selected["source_example_sheet"], int(selected["row"]),
            int(selected["column"]), "RGB")
        authored_aligned = authored.rotate(
            alignment["rotationCounterClockwise"])
        seed = int(_sha256_bytes(
            f"{case.family}|{case.topology}|{case.provenance}".encode())[:8], 16)
        approved, approved_metrics = repair_authored_mask(
            aligned, case.topology, seed)
        detailed, detail_metrics = reconstruct_soft_ownership(
            authored_aligned, expected_owner_corners(case.topology),
            remove_bounded_holes=False)
        prefix = f"{number:02d}-{_slug(case.family)}-{_slug(case.topology)}"
        files = {
            "source": _save(authored, images / f"{prefix}-source.png"),
            "alignedSource": _save(
                authored_aligned, images / f"{prefix}-aligned-source.png"),
            "approved": _save(
                approved, images / f"{prefix}-approved-r44.png"),
            "detailed": _save(
                detailed, images / f"{prefix}-detailed.png"),
            "heatmap": _save(
                _alpha_heatmap(detailed), images / f"{prefix}-heatmap.png"),
            "difference": _save(
                _difference_image(approved, detailed),
                images / f"{prefix}-difference.png"),
        }
        outcomes, outcomes_html = _outcomes(
            review, grouped[(case.family, case.topology)], approved, detailed,
            images, prefix, sources)
        key = f"detail-matte-{case.family}-{case.topology}"
        questions.append({
            "key": key,
            "r44DecisionKey": case.decision_key,
            "family": case.family,
            "topology": case.topology,
            "source": case.provenance,
            "alignment": alignment,
            "approvedMetrics": approved_metrics,
            "detailMetrics": detail_metrics,
            "files": files,
            "outcomes": outcomes,
        })
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. {html.escape(case.family)} · {html.escape(case.topology)}</h2>
<p class="metric">Authored source {html.escape(case.provenance)} · {detail_metrics['softPixelFraction']:.1%} intermediate-alpha pixels · {detail_metrics['distinctAlphaValues']} alpha values · no broad blur · no topology remake</p>
<div class="evidence-grid">
<figure><img src="{files['source']['file']}" alt="Authored transition"><figcaption>Authored transition</figcaption></figure>
<figure><img src="{files['alignedSource']['file']}" alt="Aligned authored transition"><figcaption>Aligned transition used for opacity reconstruction</figcaption></figure>
<figure><img src="{files['approved']['file']}" alt="Approved R44 mask"><figcaption>Approved R44 topology baseline</figcaption></figure>
<figure><img src="{files['detailed']['file']}" alt="Detailed opacity matte"><figcaption><strong>Detailed opacity matte</strong></figcaption></figure>
<figure><img src="{files['heatmap']['file']}" alt="Continuous alpha heatmap"><figcaption>Continuous-alpha heatmap</figcaption></figure>
<figure><img src="{files['difference']['file']}" alt="R44 to detailed difference"><figcaption>Approved R44 → detailed difference</figcaption></figure>
</div><div class="examples">{outcomes_html}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional reconstruction detail"></label></div></article>''')

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} detailed opacity review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:320px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.metric{{font:600 13px/1.4 Consolas,monospace}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(100px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · approved-family detail test</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> These are the nine masks explicitly approved in R44. The R44 shape remains the baseline. The candidate reconstructs continuous opacity from the same authored transition without broad Gaussian softening or geometric remaking. Outcome examples are ranked for comparable effective detail; the earlier Sand/Dark + Grass/Common mismatch is excluded.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-approved-family-detail-matte-review-v1",
        "checkpoint": checkpoint,
        "sourceReview": "modern-atlas-r41-mask-review/blend-mask-review",
        "runtimeChanged": False,
        "questionCount": len(questions),
        "maximumEffectiveDetailRatio": MAX_EFFECTIVE_DETAIL_RATIO,
        "questions": questions,
    }
    analysis_path = output / "detail-matte-review-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {
        "html": str(html_path),
        "analysis": str(analysis_path),
        **validate_output(output),
    }


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "detail-matte-review-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("review batch must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    errors = []
    questions = analysis.get("questions", [])
    if (len(questions) != len(APPROVED_CASES) or
            analysis.get("questionCount") != len(APPROVED_CASES)):
        errors.append("review batch does not contain the nine approved questions")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    for reference in references:
        if (reference.startswith(("/", "\\")) or ":" in reference or
                not (output / reference).is_file()):
            errors.append(f"missing or non-relocatable image: {reference}")
    verified_images = 0
    for question, case in zip(questions, APPROVED_CASES):
        if question.get("r44DecisionKey") != case.decision_key:
            errors.append(f"decision authority mismatch for {case.decision_key}")
        entries = list(question.get("files", {}).values())
        for outcome in question.get("outcomes", []):
            entries.extend(outcome["files"].values())
            if outcome["effectiveDetailRatio"] > MAX_EFFECTIVE_DETAIL_RATIO:
                errors.append(f"mismatched-detail outcome in {question['key']}")
        if len(question.get("outcomes", [])) != 3:
            errors.append(f"{question['key']} does not have three outcomes")
        for entry in entries:
            path = output / entry["file"]
            if not path.is_file():
                errors.append(f"missing indexed image: {entry['file']}")
                continue
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                errors.append(f"file hash mismatch: {entry['file']}")
            verified_images += 1
        metrics = question["detailMetrics"]
        if (metrics["usesExternalUniformPixels"] or
                metrics["usesGlobalBinaryCleanup"] or
                metrics["usesBroadGaussianSoftening"]):
            errors.append(f"{question['key']} used a rejected reconstruction step")
        mask = Image.open(output / question["files"]["detailed"]["file"])
        values = np.asarray(mask.convert("L"), dtype=np.uint8)
        points = ((0, 0), (0, -1), (-1, -1), (-1, 0))
        expected = [255 if owned else 0 for owned in
                    expected_owner_corners(question["topology"])]
        if [int(values[point]) for point in points] != expected:
            errors.append(f"corner contract failed for {question['key']}")
        approved = Image.open(
            output / question["files"]["approved"]["file"]).convert("L")
        detailed = mask.convert("L")
        for outcome in question["outcomes"]:
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, approved)) !=
                    files["approvedResult"]["pixelSha256"]):
                errors.append(f"approved outcome mismatch in {question['key']}")
            if (_image_hash(Image.composite(owner, support, detailed)) !=
                    files["detailedResult"]["pixelSha256"]):
                errors.append(f"detailed outcome mismatch in {question['key']}")
    for marker in ('class="pilot-question"', 'class="answer form-select"',
                   'class="note form-control"', 'id="submit-answers"',
                   "window.openai.sendFollowUpMessage", "localStorage.setItem"):
        if marker not in document:
            errors.append(f"HTML missing interaction marker {marker}")
    if document.count('class="pilot-question"') != len(APPROVED_CASES):
        errors.append("HTML question count differs from analysis")
    if errors:
        raise ValueError("\n".join(errors[:80]))
    return {
        "status": "PASS",
        "questions": len(questions),
        "outcomes": sum(len(item["outcomes"]) for item in questions),
        "verifiedImages": verified_images,
        "htmlImageReferences": len(references),
        "maximumEffectiveDetailRatio": max(
            outcome["effectiveDetailRatio"]
            for question in questions for outcome in question["outcomes"]),
        "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R48-DETAIL-MATTE-BATCH")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
