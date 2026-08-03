#!/usr/bin/env python3
"""Render the six unresolved R53 Snow/Ice mask decisions.

Four questions expose the already-reviewed detailed result for an explicit
visual override.  The two Ice inside masks additionally receive a narrowly
scoped cleanup that removes only alpha disconnected from every authored owner
corner inside the protected quadrant.
"""

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
    CORNER_NAMES,
    _difference_image,
    _image_hash,
    _save,
    expected_owner_corners,
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script
from render_soft_authored_mask_pilot import _alpha_heatmap


FOLLOW_UP_KEYS = (
    "route-compare-snow-clumps-diagonal-2of4",
    "route-compare-snow-clumps-inside-3of4-v1",
    "route-compare-snow-clumps-inside-3of4-v2",
    "route-compare-ice-fracture-diagonal-2of4",
    "route-compare-ice-fracture-inside-3of4-v1",
    "route-compare-ice-fracture-inside-3of4-v2",
)
CLEANUP_KEYS = frozenset((
    "route-compare-ice-fracture-inside-3of4-v1",
    "route-compare-ice-fracture-inside-3of4-v2",
))


def _quadrant_slice(index: int, height: int, width: int):
    half_y, half_x = height // 2, width // 2
    return (
        (slice(0, half_y), slice(0, half_x)),
        (slice(0, half_y), slice(half_x, width)),
        (slice(half_y, height), slice(half_x, width)),
        (slice(half_y, height), slice(0, half_x)),
    )[index]


def _owned_corner_seeds(foreground: np.ndarray,
                        expected: tuple[bool, bool, bool, bool]):
    height, width = foreground.shape
    patch = max(4, min(height, width) // 16)
    areas = (
        (range(0, patch), range(0, patch)),
        (range(0, patch), range(width - patch, width)),
        (range(height - patch, height), range(width - patch, width)),
        (range(height - patch, height), range(0, patch)),
    )
    for owned, (ys, xs) in zip(expected, areas):
        if not owned:
            continue
        for y in ys:
            for x in xs:
                if foreground[y, x]:
                    yield y, x


def remove_disconnected_protected_alpha(
        mask: Image.Image, topology: str,
        connectivity_floor: float = 0.20) -> tuple[Image.Image, dict]:
    """Clear only protected-quadrant alpha not joined to an owner corner.

    Connectivity is measured at the same 20% alpha boundary used by the
    protected-quarter contract.  Pixels in authored owner quadrants and the
    connected transition body are byte-exact invariants.
    """
    expected = expected_owner_corners(topology)
    protected = [index for index, owned in enumerate(expected) if not owned]
    if len(protected) != 1 or not topology.startswith("inside-3of4"):
        raise ValueError("protected-corner cleanup is limited to inside-3of4")
    values = np.asarray(mask.convert("L"), dtype=np.uint8).copy()
    original = values.copy()
    foreground = values >= int(round(connectivity_floor * 255.0))
    reachable = np.zeros_like(foreground, dtype=np.bool_)
    queue = deque()
    for point in _owned_corner_seeds(foreground, expected):
        if not reachable[point]:
            reachable[point] = True
            queue.append(point)
    height, width = values.shape
    while queue:
        y, x = queue.popleft()
        for dy in (-1, 0, 1):
            for dx in (-1, 0, 1):
                if not (dx or dy):
                    continue
                ny, nx = y + dy, x + dx
                if (0 <= ny < height and 0 <= nx < width and
                        foreground[ny, nx] and not reachable[ny, nx]):
                    reachable[ny, nx] = True
                    queue.append((ny, nx))
    protected_mask = np.zeros_like(foreground, dtype=np.bool_)
    area = _quadrant_slice(protected[0], height, width)
    protected_mask[area] = True
    removed = protected_mask & ~reachable & (values > 0)
    values[removed] = 0
    changed = values != original
    outside = changed & ~protected_mask
    cleaned = Image.fromarray(values, "L")
    return cleaned, {
        "method": "protected-corner disconnected-alpha cleanup",
        "connectivityFloor": connectivity_floor,
        "protectedCorners": [CORNER_NAMES[index] for index in protected],
        "removedPixels": int(removed.sum()),
        "changedPixels": int(changed.sum()),
        "changedFraction": round(float(changed.mean()), 6),
        "changedOutsideProtected": int(outside.sum()),
        "ownerQuadrantsPixelExact": int(outside.sum()) == 0,
        "connectedPixelsChanged": int((changed & reachable).sum()),
    }


def _load(root: Path, record: dict, mode: str) -> Image.Image:
    with Image.open(root / record["file"]) as source:
        image = source.convert(mode)
        image.load()
    if _image_hash(image) != record["pixelSha256"]:
        raise ValueError(f"R53 pixel hash mismatch: {record['file']}")
    return image


def _summary(metrics: dict) -> str:
    affected = min(metrics["affectedQuarterStrongCoverage"].values())
    protected = max(metrics["protectedQuarterOwnerPresence"].values(),
                    default=0.0)
    expected = set(metrics["expectedOwnerCorners"])
    corner_pass = all(
        value >= 0.80 if corner in expected else value <= 0.20
        for corner, value in metrics["cornerPatchMeans"].items())
    return (f"affected minimum {affected:.1%} · protected maximum "
            f"{protected:.1%} · corner patches "
            f"{'PASS' if corner_pass else 'FAIL'}")


def _options(cleanup: bool) -> list[str]:
    if cleanup:
        return [
            "Still uncertain",
            "Prefer protected-corner-cleaned detailed route",
            "Approve current detailed route without cleanup",
            "Reject detailed route",
            "Needs another authored source",
        ]
    return [
        "Still uncertain",
        "Approve current detailed route despite measured gate",
        "Reject detailed route",
        "Needs another authored source",
    ]


def render(r53: Path, output: Path,
           checkpoint: str = "R54-SNOW-ICE-PENDING") -> dict:
    r53 = r53.resolve()
    output = output.resolve()
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"pending-review output is not empty: {output}")
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir()
    source_bytes = (r53 / "route-comparison-analysis.json").read_bytes()
    source = json.loads(source_bytes.decode("utf-8"))
    by_key = {item["key"]: item for item in source["questions"]}
    if any(key not in by_key for key in FOLLOW_UP_KEYS):
        raise ValueError("R53 is missing an unresolved review key")

    questions = []
    cards = []
    for number, key in enumerate(FOLLOW_UP_KEYS, 1):
        prior = by_key[key]
        topology = prior["topology"]
        prefix = f"{number:02d}-{prior['family']}-{topology}"
        route = prior["routes"]["detailed"]
        current = _load(r53, route["files"]["mask"], "L")
        aligned = _load(r53, route["files"]["alignedSource"], "RGB")
        current_files = {
            "alignedSource": _save(
                aligned, images / f"{prefix}-aligned-source.png"),
            "mask": _save(current, images / f"{prefix}-current-mask.png"),
            "heatmap": _save(
                _alpha_heatmap(current),
                images / f"{prefix}-current-heatmap.png"),
        }
        cleanup = key in CLEANUP_KEYS
        cleaned = None
        cleanup_audit = None
        cleaned_metrics = None
        cleaned_files = None
        if cleanup:
            cleaned, cleanup_audit = remove_disconnected_protected_alpha(
                current, topology)
            cleaned_metrics = mask_metrics(cleaned, cleaned, topology)
            cleaned_files = {
                "mask": _save(
                    cleaned, images / f"{prefix}-cleaned-mask.png"),
                "heatmap": _save(
                    _alpha_heatmap(cleaned),
                    images / f"{prefix}-cleaned-heatmap.png"),
                "difference": _save(
                    _difference_image(current, cleaned),
                    images / f"{prefix}-cleanup-difference.png"),
            }

        outcome_entries = []
        outcome_cards = []
        for outcome_number, prior_outcome in enumerate(
                prior["outcomes"], 1):
            owner = _load(r53, prior_outcome["files"]["owner"], "RGB")
            support = _load(r53, prior_outcome["files"]["support"], "RGB")
            current_result = Image.composite(owner, support, current)
            outcome_prefix = f"{prefix}-example-{outcome_number}"
            files = {
                "owner": _save(
                    owner, images / f"{outcome_prefix}-owner.png"),
                "currentResult": _save(
                    current_result,
                    images / f"{outcome_prefix}-current-result.png"),
                "support": _save(
                    support, images / f"{outcome_prefix}-support.png"),
            }
            cleaned_result = None
            if cleaned is not None:
                cleaned_result = Image.composite(owner, support, cleaned)
                files["cleanedResult"] = _save(
                    cleaned_result,
                    images / f"{outcome_prefix}-cleaned-result.png")
            entry = {
                key_name: prior_outcome[key_name]
                for key_name in (
                    "ownerMaterial", "supportMaterial", "scenario",
                    "ownerSource", "supportSource")
            }
            entry["files"] = files
            outcome_entries.append(entry)
            cleaned_figure = ("" if cleaned_result is None else
                f'<figure><img src="{files["cleanedResult"]["file"]}" '
                'alt="Protected-corner-cleaned outcome"><figcaption><strong>'
                'Cleaned detailed result</strong></figcaption></figure>')
            outcome_cards.append(f'''<section class="example"><h3>{html.escape(entry['ownerMaterial'])} + {html.escape(entry['supportMaterial'])}</h3>
<p class="muted">Requested: {html.escape(entry['ownerSource'])} · support: {html.escape(entry['supportSource'])}</p><div class="example-grid">
<figure><img src="{files['owner']['file']}" alt="Requested uniform"><figcaption>Requested uniform</figcaption></figure>
<figure><img src="{files['currentResult']['file']}" alt="Current detailed outcome"><figcaption><strong>Current detailed result</strong></figcaption></figure>
{cleaned_figure}<figure><img src="{files['support']['file']}" alt="Support uniform"><figcaption>Support uniform</figcaption></figure>
</div></section>''')

        choices = _options(cleanup)
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in choices)
        current_metrics = route["contractMetrics"]
        cleaned_evidence = ""
        cleanup_note = (
            "This is the unchanged R53 detailed mask. The failed numeric gate "
            "is visible but no longer disables an explicit visual approval.")
        if cleanup:
            cleaned_evidence = f'''
<figure><img src="{cleaned_files['mask']['file']}" alt="Cleaned detailed mask"><figcaption><strong>Protected-corner-cleaned candidate</strong></figcaption></figure>
<figure><img src="{cleaned_files['heatmap']['file']}" alt="Cleaned mask heatmap"><figcaption>Cleaned alpha heatmap</figcaption></figure>
<figure><img src="{cleaned_files['difference']['file']}" alt="Cleanup difference"><figcaption>Cleanup difference · only disconnected SW alpha removed</figcaption></figure>'''
            cleanup_note = (
                f"The candidate removes {cleanup_audit['removedPixels']} "
                "pixels only from the protected SW quadrant. All pixels in "
                "the three authored owner quadrants and the connected blend "
                "body are byte-exact.")
        source_alignment = route["sourceAlignment"]
        cards.append(f'''<article class="pilot-question" data-key="{html.escape(key)}">
<h2>{number}. {html.escape(prior['family'])} · {html.escape(topology)}</h2>
<p class="notice">{html.escape(cleanup_note)}</p>
<p class="muted">Source {html.escape(route['provenance'])} · semantic-corner transform: {source_alignment['sourceRotationClockwise']}° clockwise, {'inverted owner polarity' if source_alignment['sourceOwnerInverted'] else 'original owner polarity'}, then fixed physical→logical vertical mirror.</p>
<p class="metric">Current: {html.escape(_summary(current_metrics))}{'' if cleaned_metrics is None else '<br>Cleaned: ' + html.escape(_summary(cleaned_metrics))}</p>
<div class="evidence-grid">
<figure><img src="{current_files['alignedSource']['file']}" alt="Aligned authored source"><figcaption>Aligned authored transition source</figcaption></figure>
<figure><img src="{current_files['mask']['file']}" alt="Current detailed mask"><figcaption><strong>Current detailed candidate</strong></figcaption></figure>
<figure><img src="{current_files['heatmap']['file']}" alt="Current detailed heatmap"><figcaption>Current alpha heatmap</figcaption></figure>{cleaned_evidence}</div>
<div class="examples">{''.join(outcome_cards)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{option_html}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional exact mask detail"></label></div></article>''')
        questions.append({
            "key": key,
            "family": prior["family"],
            "topology": topology,
            "sourceProvenance": route["provenance"],
            "sourceAlignment": route["sourceAlignment"],
            "allowedDecisions": choices,
            "current": {
                "metrics": current_metrics,
                "files": current_files,
            },
            "cleaned": None if not cleanup else {
                "audit": cleanup_audit,
                "metrics": cleaned_metrics,
                "files": cleaned_files,
            },
            "outcomes": outcome_entries,
        })

    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368)}}*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1780px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:20px}}h3{{font-size:15px}}.muted{{color:var(--muted)}}.notice{{padding:12px;border:1px solid #5b8def;background:var(--panel)}}.metric{{font:600 13px/1.5 Consolas,monospace}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}label{{display:grid;gap:4px;min-width:380px}}.pilot-question{{margin:30px 0;padding-top:22px;border-top:2px solid var(--border)}}.example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}figure{{margin:0}}figure img{{display:block;width:100%;height:auto;background:var(--panel)}}figcaption{{padding-top:5px;color:var(--muted)}}.evidence-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(200px,256px));gap:12px;margin:12px 0 20px}}.examples{{display:grid;grid-template-columns:repeat(auto-fit,minmax(520px,1fr));gap:14px}}.example-grid{{display:grid;grid-template-columns:repeat(auto-fit,minmax(120px,256px));gap:8px}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}@media(max-width:650px){{body{{padding:10px}}.evidence-grid,.example-grid,.examples{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} · unresolved Snow/Ice masks</h1>
<p class="notice"><strong>Review-only; runtime is unchanged.</strong> This contains only the six R53 rows left unresolved. Four unchanged detailed candidates can now be explicitly approved despite the numeric gate. The two Ice inside candidates add a narrow cleanup option: only alpha islands disconnected from all three authored owner corners are removed inside the protected SW quadrant. No rotation, polarity, authored outline, blur, or owner-quadrant pixel is changed.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section><main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-snow-ice-pending-review-v1",
        "checkpoint": checkpoint,
        "sourceCheckpoint": source["checkpoint"],
        "sourceAnalysisSha256": hashlib.sha256(source_bytes).hexdigest(),
        "runtimeChanged": False,
        "questionCount": len(questions),
        "questions": questions,
    }
    analysis_path = output / "pending-review-analysis.json"
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
    analysis = json.loads((output / "pending-review-analysis.json")
                          .read_text(encoding="utf-8"))
    html_files = list(output.glob("*.html"))
    if len(html_files) != 1:
        raise ValueError("pending review must contain exactly one HTML file")
    document = html_files[0].read_text(encoding="utf-8")
    questions = analysis.get("questions", [])
    errors = []
    if [item.get("key") for item in questions] != list(FOLLOW_UP_KEYS):
        errors.append("pending review keys or order differ from R53")
    references = set(re.findall(r'<img src="([^"]+)"', document))
    indexed = []
    outcomes = 0
    cleaned = 0
    for question in questions:
        indexed.extend(question["current"]["files"].values())
        current_mask = Image.open(
            output / question["current"]["files"]["mask"]["file"])
        cleaned_record = question.get("cleaned")
        cleaned_mask = None
        if cleaned_record:
            cleaned += 1
            indexed.extend(cleaned_record["files"].values())
            audit = cleaned_record["audit"]
            if (not audit["ownerQuadrantsPixelExact"] or
                    audit["changedOutsideProtected"] or
                    audit["connectedPixelsChanged"]):
                errors.append(
                    f"cleanup escaped protected disconnected alpha: "
                    f"{question['key']}")
            cleaned_mask = Image.open(
                output / cleaned_record["files"]["mask"]["file"])
        for outcome in question["outcomes"]:
            outcomes += 1
            indexed.extend(outcome["files"].values())
            files = outcome["files"]
            owner = Image.open(output / files["owner"]["file"]).convert("RGB")
            support = Image.open(
                output / files["support"]["file"]).convert("RGB")
            if (_image_hash(Image.composite(owner, support, current_mask)) !=
                    files["currentResult"]["pixelSha256"]):
                errors.append(f"current outcome mismatch: {question['key']}")
            if cleaned_mask is not None:
                if (_image_hash(Image.composite(
                        owner, support, cleaned_mask)) !=
                        files["cleanedResult"]["pixelSha256"]):
                    errors.append(
                        f"cleaned outcome mismatch: {question['key']}")
    for entry in indexed:
        path = output / entry["file"]
        if not path.is_file():
            errors.append(f"missing indexed image: {entry['file']}")
            continue
        if hashlib.sha256(path.read_bytes()).hexdigest() != entry["sha256"]:
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
    if document.count('class="pilot-question"') != len(FOLLOW_UP_KEYS):
        errors.append("HTML question count differs from analysis")
    if errors:
        raise ValueError("\n".join(errors[:100]))
    return {
        "status": "PASS",
        "questions": len(questions),
        "outcomes": outcomes,
        "cleanedCandidates": cleaned,
        "verifiedImages": len(indexed),
        "htmlImageReferences": len(references),
        "runtimeChanged": False,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--r53", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R54-SNOW-ICE-PENDING")
    args = parser.parse_args()
    print(json.dumps(render(args.r53, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
