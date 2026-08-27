#!/usr/bin/env python3
"""Compare conservative and detailed authored routes for unresolved soft masks."""

from __future__ import annotations

import argparse
import base64
import csv
import hashlib
import html
import json
from collections import defaultdict
from pathlib import Path

from PIL import Image, ImageOps

from render_authored_mask_repair_pilot import (
    _cell,
    _representatives,
    _save,
    _source_quality,
    align_runtime_review_mask_exact,
    expected_owner_corners,
    mask_metrics,
    repair_authored_mask,
)
from render_blend_mask_questionnaire import interaction_script
from render_semantic_atlas_review import common_style
from render_soft_authored_mask_pilot import (
    _alpha_heatmap,
    _difference_image,
    reconstruct_soft_ownership,
)


CASES = (
    ("sand-grain", "diagonal-2of4"),
    ("water-ripples", "outer-1of4-v1"),
    ("water-ripples", "edge-2of4-v1"),
    ("water-ripples", "diagonal-2of4"),
    ("water-ripples", "inside-3of4-v1"),
)

# R70 explicitly rejected the selected bz16h:332 Sand diagonal source and
# requested another genuine authored source.  Zone7:423 is the other distinct
# provenance-qualified Sand diagonal already present in the R41 evidence.
SOURCE_OVERRIDES = {
    ("sand-grain", "diagonal-2of4"): "Zone7:423",
}


def _rotate_pattern_clockwise(values: tuple) -> tuple:
    return values[3], values[0], values[1], values[2]


def _material_style(name: str) -> str:
    normalized = name.strip().casefold()
    if normalized.startswith("grass"):
        return "grass-strands"
    if "snow" in normalized:
        return "snow-clumps"
    if normalized.startswith("sand"):
        return "sand-grain"
    if normalized.startswith("rubble") or "gravel" in normalized:
        return "gravel-chunks"
    if normalized.startswith("water") or "/ water" in normalized:
        return "water-ripples"
    if normalized.startswith("ice"):
        return "ice-fracture"
    if normalized.startswith("paving") or normalized.startswith("slabs"):
        return "paving-courses"
    if normalized.startswith("stone") or normalized.startswith("rock"):
        return "stone-fracture"
    if (normalized.startswith("ground") or "soil" in normalized
            or "dirt" in normalized or "mud" in normalized):
        return "soil-mottle"
    return "organic-fallback"


def _source_alignment(presets: dict[str, dict], provenance: str,
                      topology: str, requested_family: str) -> dict:
    namespace, separator, slot_text = provenance.rpartition(":")
    if not separator or namespace not in presets or not slot_text.isdigit():
        raise ValueError(f"unresolvable authored provenance: {provenance}")
    preset = presets[namespace]
    slot = int(slot_text)
    packed = base64.b64decode(preset["signaturesBase64"])
    offset = slot * 4
    if offset + 4 > len(packed):
        raise ValueError(f"source slot outside preset: {provenance}")
    signature = tuple(packed[offset:offset + 4])
    unique = sorted(set(signature))
    if len(unique) != 2:
        raise ValueError(f"source is not a two-family blend: {provenance}")

    # Preset signatures describe the physical atlas preview.  This is also the
    # coordinate system of the source image.  Replaying an authored source must
    # therefore start from these corners directly, apply only a legal packed
    # clockwise quarter-turn, and perform the single preview-to-grid Y mirror
    # afterward.  The previous code mirrored the signature before this search,
    # which double-mirrored the source and could map the support field as owner.
    preview = signature
    expected = expected_owner_corners(topology)
    requested_preview = (expected[3], expected[2], expected[1], expected[0])
    names = {family["id"]: family["name"]
             for family in preset.get("families", [])}
    candidates = []
    for turns in range(4):
        for material in unique:
            if tuple(value == material for value in preview) == requested_preview:
                candidates.append((
                    _material_style(names.get(material, str(material)))
                        != requested_family,
                    turns, material))
        preview = _rotate_pattern_clockwise(preview)
    if not candidates:
        raise ValueError(
            f"source corners cannot supply {topology}: {provenance}")
    # The corner pattern is mandatory.  When both complementary fields can
    # satisfy it (especially diagonals), the requested semantic mask family is
    # mandatory too.  Only a source with no matching family uses the stable
    # earliest-turn fallback needed for reviewed cross-style transfers.
    style_fallback, turns, mapped = min(candidates)
    return {
        "fixedPreviewToGridMirrorY": True,
        "stockRotationClockwise": turns * 90,
        "mappedSourceMaterialId": mapped,
        "mappedSourceMaterial": names.get(mapped, str(mapped)),
        "mappedSourceStyle": _material_style(
            names.get(mapped, str(mapped))),
        "requestedMaskFamily": requested_family,
        "exactSourceStyleMatch": not style_fallback,
        "sourceOwnerInverted": mapped != unique[0],
        "sourceSignature": list(signature),
    }


def _align_source_image(source: Image.Image, alignment: dict) -> Image.Image:
    """Replay the exact stock turn, then the same fixed grid reflection."""
    rotated = source.rotate(-alignment["stockRotationClockwise"])
    return ImageOps.flip(rotated)


def _slug(value: str) -> str:
    return "-".join(filter(None, ("".join(
        character if character.isalnum() else " "
        for character in value.lower())).split()))


def _select_source(review: Path, rows: list[dict[str, str]],
                   family: str, topology: str):
    candidates = {}
    required_provenance = SOURCE_OVERRIDES.get((family, topology))
    for row in rows:
        if row.get("missing") == "1":
            continue
        if (required_provenance is not None
                and row["provenance"] != required_provenance):
            continue
        raw = _cell(review, row["sheet"], int(row["row"]),
                    int(row["column"]), "L")
        aligned, transform = align_runtime_review_mask_exact(raw, topology)
        identity = hashlib.sha256(aligned.tobytes()).hexdigest()
        candidate = (
            round(_source_quality(aligned, topology), 6),
            row["provenance"], row, raw, aligned, transform)
        previous = candidates.get(identity)
        if previous is None or candidate[:2] < previous[:2]:
            candidates[identity] = candidate
    if not candidates:
        raise ValueError(f"no authored source for {topology}")
    return min(candidates.values(), key=lambda value: value[:2])


def _metric_summary(metrics: dict) -> str:
    affected = min(metrics["affectedQuarterStrongCoverage"].values())
    protected = max(metrics["protectedQuarterOwnerPresence"].values())
    return (f"owner min {affected:.1%} · protected max {protected:.1%} · "
            f"corner/topology gate {'PASS' if metrics['structuralTargetMet'] else 'review'}")


def render(review: Path, output: Path,
           checkpoint: str = "R65-SOFT-MASK-ROUTES") -> dict:
    review, output = review.resolve(), output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    preset_payload = json.loads((Path(__file__).resolve().parents[1]
        / "assets" / "resources" / "terrain_atlas_presets.json")
        .read_text(encoding="utf-8"))
    presets = {preset["namespace"]: preset
               for preset in preset_payload["presets"]}
    grouped = defaultdict(list)
    for row in rows:
        grouped[(row["owner_style"], row["topology"])].append(row)

    questions, cards = [], []
    for number, (family, topology) in enumerate(CASES, 1):
        group = grouped.get((family, topology), [])
        if not group:
            raise ValueError(f"missing authored evidence for {family}/{topology}")
        (_, provenance, selected, raw, aligned,
         transform) = _select_source(review, group, family, topology)
        seed = int(hashlib.sha256(
            f"{family}|{topology}|{provenance}".encode()).hexdigest()[:8], 16)
        conservative, conservative_metrics = repair_authored_mask(
            aligned, topology, seed)
        source = _cell(
            review, selected["source_example_sheet"], int(selected["row"]),
            int(selected["column"]), "RGB")
        source_alignment = _source_alignment(
            presets, provenance, topology, family)
        # Reproduce the exact stock-selector turn for this provenance, then
        # cross the physical-preview/logical-grid boundary once.  The mask
        # uses the identical fixed mirror; no pixel score, arbitrary diagonal
        # half-turn, or extra inversion participates.
        aligned_source = _align_source_image(source, source_alignment)
        detailed, detailed_process = reconstruct_soft_ownership(
            aligned_source, expected_owner_corners(topology),
            remove_bounded_holes=family == "sand-grain"
            and topology.startswith("edge-2of4"))
        detailed_metrics = mask_metrics(aligned, detailed, topology)
        affected = min(
            detailed_metrics["affectedQuarterStrongCoverage"].values())
        protected = max(
            detailed_metrics["protectedQuarterOwnerPresence"].values())
        # RGB palette recovery can occasionally reverse the continuous field
        # even though the exact topology has already fixed which corners belong
        # to the owner. Correct only that unmistakable polarity inversion; this
        # does not search rotations or choose a best-looking orientation.
        if affected < 0.5 and protected > 0.5:
            detailed = ImageOps.invert(detailed)
            corner_points = ((0, 0), (detailed.width - 1, 0),
                             (detailed.width - 1, detailed.height - 1),
                             (0, detailed.height - 1))
            for point, owned in zip(
                    corner_points, expected_owner_corners(topology)):
                detailed.putpixel(point, 255 if owned else 0)
            detailed_process["polarityCorrectedFromExactCorners"] = True
            detailed_metrics = mask_metrics(aligned, detailed, topology)
        else:
            detailed_process["polarityCorrectedFromExactCorners"] = False
        prefix = f"{number:02d}-{_slug(family)}-{_slug(topology)}"
        files = {
            "source": _save(source, images / f"{prefix}-source.png"),
            "alignedSource": _save(
                aligned_source, images / f"{prefix}-aligned-source.png"),
            "alignedMask": _save(
                aligned, images / f"{prefix}-aligned-mask.png"),
            "conservativeMask": _save(
                conservative, images / f"{prefix}-conservative-mask.png"),
            "detailedMask": _save(
                detailed, images / f"{prefix}-detailed-mask.png"),
            "detailedHeatmap": _save(
                _alpha_heatmap(detailed),
                images / f"{prefix}-detailed-heatmap.png"),
            "routeDifference": _save(
                _difference_image(conservative, detailed),
                images / f"{prefix}-route-difference.png"),
        }
        outcomes, outcome_html = [], []
        for example_number, row in enumerate(_representatives(group, 3), 1):
            owner = Image.open(review / row["owner_uniform"]).convert("RGB")
            support = Image.open(review / row["support_uniform"]).convert("RGB")
            conservative_result = Image.composite(owner, support, conservative)
            detailed_result = Image.composite(owner, support, detailed)
            example_prefix = f"{prefix}-example-{example_number}"
            example_files = {
                "owner": _save(owner, images / f"{example_prefix}-owner.png"),
                "support": _save(
                    support, images / f"{example_prefix}-support.png"),
                "conservativeResult": _save(
                    conservative_result,
                    images / f"{example_prefix}-conservative-result.png"),
                "detailedResult": _save(
                    detailed_result,
                    images / f"{example_prefix}-detailed-result.png"),
            }
            outcomes.append({
                "ownerMaterial": row["owner_material"],
                "supportMaterial": row["support_material"],
                "scenario": row["support_scenario"],
                "files": example_files,
            })
            outcome_html.append(f"""
<section class="outcome"><h3>{html.escape(row['owner_material'])} into {html.escape(row['support_material'])} <span>{html.escape(row['support_scenario'])}</span></h3>
<div class="compare-grid four"><figure><img src="{example_files['owner']['file']}" alt="Owner uniform"><figcaption>Owner uniform</figcaption></figure>
<figure><img src="{example_files['support']['file']}" alt="Support uniform"><figcaption>Support uniform</figcaption></figure>
<figure><img src="{example_files['conservativeResult']['file']}" alt="Conservative outcome"><figcaption>Conservative authored repair</figcaption></figure>
<figure><img src="{example_files['detailedResult']['file']}" alt="Detailed outcome"><figcaption><strong>Detailed opacity outcome</strong></figcaption></figure></div></section>""")
        key = f"route-compare-{_slug(family)}-{_slug(topology)}"
        choices = (
            "Still uncertain", "Prefer conservative authored repair",
            "Prefer detailed opacity route", "Prefer raw aligned authored mask",
            "Neither candidate is acceptable", "Needs another authored source")
        cards.append(f"""
<article class="question" data-key="{key}"><h2>{number}. {html.escape(family)} · {html.escape(topology)}</h2>
<p class="muted">Source {html.escape(provenance)} · exact corner assignment; no orientation score chooses the rotation.</p>
<p><strong>Conservative:</strong> {html.escape(_metric_summary(conservative_metrics))}<br><strong>Detailed:</strong> {html.escape(_metric_summary(detailed_metrics))}</p>
<div class="compare-grid routes"><figure><img src="{files['source']['file']}" alt="Authored source"><figcaption>Authored transition</figcaption></figure>
<figure><img src="{files['alignedSource']['file']}" alt="Aligned source"><figcaption>Corner-aligned transition</figcaption></figure>
<figure><img src="{files['alignedMask']['file']}" alt="Raw aligned mask"><figcaption>Raw aligned authored mask</figcaption></figure>
<figure><img src="{files['conservativeMask']['file']}" alt="Conservative mask"><figcaption>Conservative authored repair</figcaption></figure>
<figure><img src="{files['detailedMask']['file']}" alt="Detailed mask"><figcaption><strong>Detailed opacity mask</strong></figcaption></figure>
<figure><img src="{files['detailedHeatmap']['file']}" alt="Detailed heatmap"><figcaption>Detailed alpha heatmap</figcaption></figure></div>
{''.join(outcome_html)}
<div class="answer-row"><label>Decision <select class="answer">{''.join(f'<option>{html.escape(choice)}</option>' for choice in choices)}</select></label><label>Note <input class="note" type="text" placeholder="Optional"></label></div></article>""")
        questions.append({
            "key": key, "family": family, "topology": topology,
            "sourceProvenance": provenance, "alignment": transform,
            "sourceAlignment": source_alignment,
            "files": files,
            "conservative": {"metrics": conservative_metrics},
            "detailed": {"metrics": detailed_metrics,
                         "process": detailed_process},
            "outcomes": outcomes,
        })

    script = interaction_script(checkpoint)
    document = f"""<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(checkpoint)} mask review</title><style>{common_style()}
body{{max-width:1700px;margin:auto}}.question{{padding:18px 0;border-top:2px solid #5f6368}}.compare-grid{{grid-template-columns:repeat(auto-fit,minmax(180px,256px))}}.four{{grid-template-columns:repeat(4,minmax(150px,256px))}}figure{{margin:0}}figure img{{width:256px;max-width:100%;height:auto;display:block}}figcaption{{color:#bdc1c6;padding-top:5px}}.outcome{{padding:10px;margin:12px 0;background:#292a2d}}.outcome h3 span{{color:#bdc1c6;font-weight:400}}.answer-row{{display:flex;gap:12px;flex-wrap:wrap}}.answer-row label{{display:grid;gap:4px;min-width:320px}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused review</h1><p>This is five questions, one per unresolved soft family/topology baseline. Sand uses its approved detailed methodology; Water is shown as an explicit conservative-vs-detailed A/B because its route has not been reviewed. Every card uses a genuine authored transition and three genuine uniform-pair outcomes.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers</button><button id="show-answers">Show/select answers</button><span id="status"></span></div>
<section class="export-panel"><textarea id="answer-export-text" readonly></textarea></section>{''.join(cards)}<script>{script}</script></body></html>"""
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-soft-family-route-comparison-v1",
        "checkpoint": checkpoint,
        "sourceReview": str(review),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "soft-family-route-analysis.json"
    analysis_path.write_text(json.dumps(
        analysis, indent=2, ensure_ascii=False), encoding="utf-8")
    return validate(output)


def validate(output: Path) -> dict:
    analysis = json.loads((output / "soft-family-route-analysis.json")
                          .read_text(encoding="utf-8"))
    html_text = next(output.glob("*.html")).read_text(encoding="utf-8")
    errors = []
    questions = analysis.get("questions", [])
    if len(questions) != len(CASES) or len(questions) != 5:
        errors.append("soft route review must contain exactly five questions")
    if html_text.count('class="question" data-key=') != len(questions):
        errors.append("HTML question count differs from analysis")
    for marker in ("submit-answers", "copy-answers", "download-answers",
                   "show-answers", "answer-export-text", "sendFollowUpMessage"):
        if marker not in html_text:
            errors.append(f"missing interaction marker: {marker}")
    verified = 0
    for question in questions:
        records = list(question["files"].values())
        for outcome in question["outcomes"]:
            records.extend(outcome["files"].values())
        for record in records:
            path = output / record["file"]
            if not path.is_file() or path.stat().st_size == 0:
                errors.append(f"missing image: {record['file']}")
                continue
            with Image.open(path) as image:
                image.load()
                pixel_hash = hashlib.sha256(image.tobytes()).hexdigest()
            if pixel_hash != record["pixelSha256"]:
                errors.append(f"pixel hash mismatch: {record['file']}")
            verified += 1
        conservative = Image.open(
            output / question["files"]["conservativeMask"]["file"]).convert("L")
        detailed = Image.open(
            output / question["files"]["detailedMask"]["file"]).convert("L")
        for outcome in question["outcomes"]:
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(output / files["support"]["file"]).convert("RGB")
            expected = (("conservativeResult", conservative),
                        ("detailedResult", detailed))
            for name, mask in expected:
                actual = Image.open(output / files[name]["file"]).convert("RGB")
                if actual.tobytes() != Image.composite(
                        owner, support, mask).tobytes():
                    errors.append(f"outcome mismatch: {question['key']} {name}")
    if errors:
        raise ValueError("\n".join(errors[:50]))
    return {
        "html": str(next(output.glob("*.html"))),
        "analysis": str(output / "soft-family-route-analysis.json"),
        "status": "PASS", "questions": len(questions),
        "examples": sum(len(question["outcomes"]) for question in questions),
        "verifiedImages": verified, "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R65-SOFT-MASK-ROUTES")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
