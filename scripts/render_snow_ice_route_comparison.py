#!/usr/bin/env python3
"""Compare the R44 repair and approved sand-detail method on Snow and Ice."""

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
    CORNER_NAMES,
    _cell,
    _difference_image,
    _image_hash,
    _save,
    _sha256_bytes,
    _slug,
    _source_quality,
    align_authored_mask,
    expected_owner_corners,
    mask_metrics,
    repair_authored_mask,
)
from render_blend_mask_questionnaire import interaction_script
from render_detail_matte_review_batch import (
    MAX_EFFECTIVE_DETAIL_RATIO,
    _source_label,
    _source_manifest,
    effective_detail,
)
from render_soft_authored_mask_pilot import (
    _alpha_heatmap,
    reconstruct_soft_ownership,
)


@dataclass(frozen=True)
class ReviewCase:
    family: str
    topology: str


TOPOLOGIES = (
    "outer-1of4-v1", "outer-1of4-v2",
    "edge-2of4-v1", "edge-2of4-v2",
    "diagonal-2of4", "inside-3of4-v1", "inside-3of4-v2",
)
REVIEW_CASES = tuple(
    ReviewCase(family, topology)
    for family in ("snow-clumps", "ice-fracture")
    for topology in TOPOLOGIES)


def contract_violation(metrics: dict) -> float:
    """Return zero only when every user-authored topology rule passes."""
    violation = 0.0
    for value in metrics["affectedQuarterStrongCoverage"].values():
        violation += max(0.0, 0.70 - float(value))
    for value in metrics["protectedQuarterOwnerPresence"].values():
        violation += max(0.0, float(value) - 0.30)
    expected = set(metrics["expectedOwnerCorners"])
    for corner, value in metrics["cornerPatchMeans"].items():
        violation += (max(0.0, 0.80 - float(value)) if corner in expected
                      else max(0.0, float(value) - 0.20))
    return round(violation, 6)


def choose_route_candidate(candidates: list[dict]) -> dict:
    if not candidates:
        raise ValueError("route has no authored candidates")
    enriched = []
    for candidate in candidates:
        item = dict(candidate)
        item["contractViolation"] = contract_violation(
            item["contractMetrics"])
        enriched.append(item)
    return min(enriched, key=lambda item: (
        not item["contractMetrics"]["structuralTargetMet"],
        item["contractViolation"], item["sourceQuality"],
        item["provenance"]))


def _representatives(review: Path, rows: list[dict[str, str]],
                     limit: int = 3) -> list[dict]:
    """Prefer Snow/Ice-local pairs; retain and label unavoidable mismatches."""
    cache: dict[str, float] = {}

    def detail(relative: str) -> float:
        if relative not in cache:
            with Image.open(review / relative) as image:
                cache[relative] = effective_detail(image)
        return cache[relative]

    unique = []
    seen = set()
    for row in rows:
        identity = (row["owner_material_id"], row["support_material_id"])
        if identity in seen:
            continue
        seen.add(identity)
        owner_detail = detail(row["owner_uniform"])
        support_detail = detail(row["support_uniform"])
        ratio = (max(owner_detail, support_detail) /
                 max(1e-6, min(owner_detail, support_detail)))
        item = dict(row)
        item.update({
            "owner_effective_detail": round(owner_detail, 4),
            "support_effective_detail": round(support_detail, 4),
            "effective_detail_ratio": round(ratio, 4),
            "effective_detail_compatible": ratio <= MAX_EFFECTIVE_DETAIL_RATIO,
        })
        unique.append(item)
    unique.sort(key=lambda row: (
        row["support_scenario"] != "same-style",
        row["effective_detail_ratio"], row["owner_material"].casefold(),
        row["support_material"].casefold()))
    return unique[:limit]


def _build_candidates(review: Path, rows: list[dict[str, str]],
                      case: ReviewCase) -> tuple[list[dict], list[dict]]:
    r44 = []
    detailed = []
    seen = set()
    for row in rows:
        if (row["owner_style"] != case.family or
                row["topology"] != case.topology or
                row["provenance"] in seen):
            continue
        seen.add(row["provenance"])
        raw = _cell(review, row["sheet"], int(row["row"]),
                    int(row["column"]), "L")
        if _image_hash(raw).lower() != row["mask_sha256"].lower():
            raise ValueError(f"mask hash mismatch for {row['provenance']}")
        aligned, alignment = align_authored_mask(raw, case.topology)
        authored = _cell(
            review, row["source_example_sheet"], int(row["row"]),
            int(row["column"]), "RGB")
        authored_aligned = authored.rotate(
            alignment["rotationCounterClockwise"])
        seed = int(_sha256_bytes(
            f"{case.family}|{case.topology}|{row['provenance']}".encode())[:8],
                   16)
        r44_mask, _ = repair_authored_mask(aligned, case.topology, seed)
        r44_metrics = mask_metrics(aligned, r44_mask, case.topology)
        detail_mask, detail_process = reconstruct_soft_ownership(
            authored_aligned, expected_owner_corners(case.topology),
            remove_bounded_holes=False)
        detail_metrics = mask_metrics(
            detail_mask, detail_mask, case.topology)
        common = {
            "provenance": row["provenance"],
            "sourceQuality": round(_source_quality(
                aligned, case.topology), 6),
            "alignment": alignment,
            "_source": authored,
            "_alignedSource": authored_aligned,
        }
        r44.append({
            **common, "contractMetrics": r44_metrics,
            "processMetrics": {"method": "R44 repaired authored mask"},
            "_mask": r44_mask,
        })
        detailed.append({
            **common, "contractMetrics": detail_metrics,
            "processMetrics": detail_process,
            "_mask": detail_mask,
        })
    if not r44 or not detailed:
        raise ValueError(f"missing authored candidates for {case}")
    return r44, detailed


def _serial_candidate(candidate: dict) -> dict:
    return {key: value for key, value in candidate.items()
            if not key.startswith("_")}


def _contract_summary(metrics: dict) -> dict:
    affected = min(metrics["affectedQuarterStrongCoverage"].values())
    protected = max(
        metrics["protectedQuarterOwnerPresence"].values(), default=0.0)
    expected = set(metrics["expectedOwnerCorners"])
    corner_pass = all(
        (value >= 0.80 if corner in expected else value <= 0.20)
        for corner, value in metrics["cornerPatchMeans"].items())
    return {
        "affectedMinimum": round(float(affected), 4),
        "protectedMaximum": round(float(protected), 4),
        "cornerPatchesPass": bool(corner_pass),
        "eligible": bool(metrics["structuralTargetMet"]),
    }


def _badge(summary: dict) -> str:
    label = "ELIGIBLE" if summary["eligible"] else "INELIGIBLE"
    css = "pass" if summary["eligible"] else "fail"
    return f'<span class="badge {css}">{label}</span>'


def _route_files(candidate: dict, images: Path, prefix: str,
                 route: str) -> dict:
    return {
        "source": _save(
            candidate["_source"], images / f"{prefix}-{route}-source.png"),
        "alignedSource": _save(
            candidate["_alignedSource"],
            images / f"{prefix}-{route}-aligned-source.png"),
        "mask": _save(
            candidate["_mask"], images / f"{prefix}-{route}-mask.png"),
        "heatmap": _save(
            _alpha_heatmap(candidate["_mask"]),
            images / f"{prefix}-{route}-heatmap.png"),
    }


def _outcomes(review: Path, rows: list[dict[str, str]],
              r44_mask: Image.Image, detail_mask: Image.Image,
              images: Path, prefix: str,
              sources: dict[int, dict]) -> tuple[list, str]:
    entries = []
    sections = []
    for number, row in enumerate(_representatives(review, rows), 1):
        with Image.open(review / row["owner_uniform"]) as image:
            owner = image.convert("RGB")
        with Image.open(review / row["support_uniform"]) as image:
            support = image.convert("RGB")
        r44_result = Image.composite(owner, support, r44_mask)
        detail_result = Image.composite(owner, support, detail_mask)
        item_prefix = f"{prefix}-example-{number}"
        files = {
            "owner": _save(owner, images / f"{item_prefix}-owner.png"),
            "r44Result": _save(
                r44_result, images / f"{item_prefix}-r44-result.png"),
            "detailResult": _save(
                detail_result, images / f"{item_prefix}-detail-result.png"),
            "support": _save(
                support, images / f"{item_prefix}-support.png"),
        }
        compatible = row["effective_detail_compatible"]
        warning = ("effective detail matched" if compatible else
                   "detail mismatch; judge mask shape, not texture fidelity")
        entry = {
            "ownerMaterial": row["owner_material"],
            "supportMaterial": row["support_material"],
            "scenario": row["support_scenario"],
            "ownerSource": _source_label(row["owner_material_id"], sources),
            "supportSource": _source_label(row["support_material_id"], sources),
            "ownerEffectiveDetail": row["owner_effective_detail"],
            "supportEffectiveDetail": row["support_effective_detail"],
            "effectiveDetailRatio": row["effective_detail_ratio"],
            "effectiveDetailCompatible": compatible,
            "files": files,
        }
        entries.append(entry)
        sections.append(f'''<section class="example"><h3>{html.escape(row['owner_material'])} + {html.escape(row['support_material'])}</h3>
<p class="{'muted' if compatible else 'warning-text'}">{html.escape(row['support_scenario'])} · ratio {row['effective_detail_ratio']:.2f} · {html.escape(warning)}</p>
<div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Requested uniform"><figcaption>Requested · {html.escape(entry['ownerSource'])}</figcaption></figure>
<figure><img src="{files['r44Result']['file']}" alt="R44 result"><figcaption><strong>R44 result</strong></figcaption></figure>
<figure><img src="{files['detailResult']['file']}" alt="Detailed sand-route result"><figcaption><strong>Detailed sand-route result</strong></figcaption></figure>
<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support · {html.escape(entry['supportSource'])}</figcaption></figure>
</div></section>''')
    if not entries:
        raise ValueError(f"{prefix} has no genuine outcome examples")
    return entries, "".join(sections)


def _decision_options(r44_eligible: bool, detail_eligible: bool) -> list[str]:
    result = ["Still uncertain"]
    if r44_eligible:
        result.append("Prefer R44 route")
    if detail_eligible:
        result.append("Prefer detailed sand route")
    result.extend(("Neither candidate is acceptable",
                   "Needs another authored source"))
    return result


def render(review: Path, output: Path,
           checkpoint: str = "R49-SNOW-ICE-ROUTES") -> dict:
    review = review.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"comparison output is not empty: {output}")
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
    for number, case in enumerate(REVIEW_CASES, 1):
        r44_candidates, detail_candidates = _build_candidates(review, rows, case)
        r44 = choose_route_candidate(r44_candidates)
        detailed = choose_route_candidate(detail_candidates)
        r44_summary = _contract_summary(r44["contractMetrics"])
        detail_summary = _contract_summary(detailed["contractMetrics"])
        prefix = f"{number:02d}-{_slug(case.family)}-{_slug(case.topology)}"
        r44_files = _route_files(r44, images, prefix, "r44")
        detail_files = _route_files(detailed, images, prefix, "detail")
        difference = _save(
            _difference_image(r44["_mask"], detailed["_mask"]),
            images / f"{prefix}-difference.png")
        outcomes, outcomes_html = _outcomes(
            review, grouped[(case.family, case.topology)], r44["_mask"],
            detailed["_mask"], images, prefix, sources)
        decisions = _decision_options(
            r44_summary["eligible"], detail_summary["eligible"])
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in decisions)
        key = f"route-compare-{case.family}-{case.topology}"
        candidate_audit = []
        by_provenance = defaultdict(dict)
        for route, candidates in (("r44", r44_candidates),
                                  ("detailed", detail_candidates)):
            for candidate in candidates:
                serial = _serial_candidate(choose_route_candidate([candidate]))
                by_provenance[candidate["provenance"]][route] = serial
        for provenance in sorted(by_provenance):
            candidate_audit.append({
                "provenance": provenance, **by_provenance[provenance]})
        questions.append({
            "key": key,
            "family": case.family,
            "topology": case.topology,
            "allowedDecisions": decisions,
            "routes": {
                "r44": {
                    **_serial_candidate(r44), "summary": r44_summary,
                    "files": r44_files,
                },
                "detailed": {
                    **_serial_candidate(detailed), "summary": detail_summary,
                    "files": detail_files,
                },
            },
            "difference": difference,
            "candidateAudit": candidate_audit,
            "outcomes": outcomes,
        })
        same_source = r44["provenance"] == detailed["provenance"]
        source_note = ("Both routes selected the same authored source." if same_source
                       else "Routes selected different authored sources; each is the best candidate for its own contract result.")
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. {html.escape(case.family)} · {html.escape(case.topology)}</h2>
<p class="muted">{html.escape(source_note)}</p>
<div class="route-summary"><section><h3>R44 repaired authored route {_badge(r44_summary)}</h3><p class="metric">{html.escape(r44['provenance'])} · affected min {r44_summary['affectedMinimum']:.1%} · protected max {r44_summary['protectedMaximum']:.1%} · corner patches {'PASS' if r44_summary['cornerPatchesPass'] else 'FAIL'}</p></section>
<section><h3>Detailed sand route {_badge(detail_summary)}</h3><p class="metric">{html.escape(detailed['provenance'])} · affected min {detail_summary['affectedMinimum']:.1%} · protected max {detail_summary['protectedMaximum']:.1%} · corner patches {'PASS' if detail_summary['cornerPatchesPass'] else 'FAIL'}</p></section></div>
<div class="evidence-grid">
<figure><img src="{r44_files['alignedSource']['file']}" alt="R44 authored source"><figcaption>R44-selected authored transition · {html.escape(r44['provenance'])}</figcaption></figure>
<figure><img src="{r44_files['mask']['file']}" alt="R44 mask"><figcaption><strong>R44 candidate</strong> · {'eligible' if r44_summary['eligible'] else 'diagnostic only'}</figcaption></figure>
<figure><img src="{r44_files['heatmap']['file']}" alt="R44 heatmap"><figcaption>R44 alpha heatmap</figcaption></figure>
<figure><img src="{detail_files['alignedSource']['file']}" alt="Detailed-route authored source"><figcaption>Detailed-route source · {html.escape(detailed['provenance'])}</figcaption></figure>
<figure><img src="{detail_files['mask']['file']}" alt="Detailed mask"><figcaption><strong>Detailed sand-route candidate</strong> · {'eligible' if detail_summary['eligible'] else 'diagnostic only'}</figcaption></figure>
<figure><img src="{detail_files['heatmap']['file']}" alt="Detailed heatmap"><figcaption>Detailed-route alpha heatmap</figcaption></figure>
<figure><img src="{difference['file']}" alt="Route mask difference"><figcaption>R44 → detailed mask difference</figcaption></figure>
</div><div class="examples">{outcomes_html}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{options}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional route or topology detail"></label></div></article>''')

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} Snow/Ice route comparison</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368);--pass:#2da44e;--fail:#d1242f}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.warning-text{{color:#e3a008;font-weight:700}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:320px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.metric{{font:600 13px/1.4 Consolas,monospace}}.badge{{padding:2px 6px;border-radius:4px;color:#fff;font:700 11px/1.2 system-ui}}.badge.pass{{background:var(--pass)}}.badge.fail{{background:var(--fail)}}.route-summary{{display:grid;grid-template-columns:repeat(2,minmax(280px,1fr));gap:12px}}.route-summary section,.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(180px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(100px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.route-summary,.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · Snow/Ice route comparison</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> R44 and the R47 sand-detail method are evaluated independently against every Snow and Ice topology. Green candidates pass the complete 70/30 quarter and corner-patch gate. Red candidates are diagnostic only and cannot be selected as a preferred route. Outcome pairs without comparable effective detail are explicitly marked; judge mask shape there, not texture fidelity.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-snow-ice-route-comparison-v1",
        "checkpoint": checkpoint,
        "sourceReview": "modern-atlas-r41-mask-review/blend-mask-review",
        "runtimeChanged": False,
        "questionCount": len(questions),
        "hardGate": {
            "affectedQuarterMinimum": 0.70,
            "protectedQuarterMaximum": 0.30,
            "ownerCornerPatchMinimum": 0.80,
            "protectedCornerPatchMaximum": 0.20,
        },
        "questions": questions,
    }
    analysis_path = output / "route-comparison-analysis.json"
    analysis_path.write_text(
        json.dumps(analysis, indent=2, ensure_ascii=False) + "\n",
        encoding="utf-8")
    return {
        "html": str(html_path), "analysis": str(analysis_path),
        **validate_output(output),
    }


def validate_output(output: Path) -> dict:
    output = output.resolve()
    analysis = json.loads((output / "route-comparison-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("route comparison must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if len(questions) != len(REVIEW_CASES):
        errors.append("route comparison does not contain 14 questions")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    for reference in references:
        if (reference.startswith(("/", "\\")) or ":" in reference or
                not (output / reference).is_file()):
            errors.append(f"missing or non-relocatable image: {reference}")
    verified_images = 0
    eligible_counts = {"r44": 0, "detailed": 0}
    mismatched_outcomes = 0
    for question, case in zip(questions, REVIEW_CASES):
        if (question["family"], question["topology"]) != (
                case.family, case.topology):
            errors.append(f"case order mismatch for {question['key']}")
        entries = [question["difference"]]
        for route in ("r44", "detailed"):
            candidate = question["routes"][route]
            entries.extend(candidate["files"].values())
            summary = candidate["summary"]
            metrics = candidate["contractMetrics"]
            if summary["eligible"] != metrics["structuralTargetMet"]:
                errors.append(f"eligibility mismatch in {question['key']} {route}")
            if summary["eligible"]:
                eligible_counts[route] += 1
                expected_decision = ("Prefer R44 route" if route == "r44"
                                     else "Prefer detailed sand route")
                if expected_decision not in question["allowedDecisions"]:
                    errors.append(f"eligible {route} cannot be selected")
            else:
                forbidden = ("Prefer R44 route" if route == "r44"
                             else "Prefer detailed sand route")
                if forbidden in question["allowedDecisions"]:
                    errors.append(f"ineligible {route} is selectable")
        for outcome in question["outcomes"]:
            entries.extend(outcome["files"].values())
            mismatched_outcomes += not outcome["effectiveDetailCompatible"]
        for entry in entries:
            path = output / entry["file"]
            if not path.is_file():
                errors.append(f"missing indexed image: {entry['file']}")
                continue
            if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
                errors.append(f"file hash mismatch: {entry['file']}")
            verified_images += 1
        r44_mask = Image.open(
            output / question["routes"]["r44"]["files"]["mask"]["file"])
        detail_mask = Image.open(
            output / question["routes"]["detailed"]["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, r44_mask)) !=
                    files["r44Result"]["pixelSha256"]):
                errors.append(f"R44 outcome mismatch in {question['key']}")
            if (_image_hash(Image.composite(owner, support, detail_mask)) !=
                    files["detailResult"]["pixelSha256"]):
                errors.append(f"detail outcome mismatch in {question['key']}")
    for marker in ('class="pilot-question"', 'class="answer form-select"',
                   'class="note form-control"', 'id="submit-answers"',
                   "window.openai.sendFollowUpMessage", "localStorage.setItem"):
        if marker not in document:
            errors.append(f"HTML missing interaction marker {marker}")
    if document.count('class="pilot-question"') != len(REVIEW_CASES):
        errors.append("HTML question count differs from analysis")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS", "questions": len(questions),
        "outcomes": sum(len(item["outcomes"]) for item in questions),
        "verifiedImages": verified_images,
        "htmlImageReferences": len(references),
        "eligibleR44": eligible_counts["r44"],
        "eligibleDetailed": eligible_counts["detailed"],
        "detailMismatchedOutcomes": mismatched_outcomes,
        "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R49-SNOW-ICE-ROUTES")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
