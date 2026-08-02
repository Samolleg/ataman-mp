#!/usr/bin/env python3
"""Render a family/topology-grouped questionnaire from a blend-mask review."""

from __future__ import annotations

import argparse
import csv
import hashlib
import html
import json
import os
import re
from collections import defaultdict
from functools import lru_cache
from pathlib import Path

from PIL import Image, ImageOps


TILE_SIZE = 256


@lru_cache(maxsize=8)
def _loaded_sheet(path_text: str) -> Image.Image:
    with Image.open(path_text) as source:
        source.load()
        return source.copy()


def interaction_script(checkpoint: str) -> str:
    header = json.dumps(
        f"Blend-mask {checkpoint} focused review decisions:\n",
        ensure_ascii=False)
    footer = json.dumps(
        "\nApply only explicit mask decisions; verify the source/target uniforms "
        "and both rendered outcomes before changing generation.",
        ensure_ascii=False)
    download = json.dumps(f"blend-mask-{checkpoint.lower()}-answers.txt")
    title = json.dumps(f"Submit blend-mask {checkpoint} review")
    return f"""
const prefix='ei-semantic-{checkpoint.lower()}-review:';
const blocks=[...document.querySelectorAll('[data-key]')];
const status=document.getElementById('status');
function save(block){{const answer=block.querySelector('.answer').value;const note=block.querySelector('.note').value;try{{localStorage.setItem(prefix+block.dataset.key,JSON.stringify({{answer,note}}));}}catch(e){{}}update();}}
blocks.forEach(block=>{{const answer=block.querySelector('.answer');const note=block.querySelector('.note');try{{const saved=JSON.parse(localStorage.getItem(prefix+block.dataset.key)||'null');if(saved){{if(saved.answer)answer.value=saved.answer;note.value=saved.note||'';}}}}catch(e){{}}answer.addEventListener('change',()=>save(block));note.addEventListener('input',()=>save(block));}});
function rows(){{return blocks.map(block=>{{const answer=block.querySelector('.answer').value;const note=block.querySelector('.note').value.trim();return answer||note?block.dataset.key+': '+(answer||'Comment')+(note?' \\u2014 '+note:''):'';}}).filter(Boolean);}}
function text(){{return {header}+rows().join('\\n')+{footer};}}
function update(message){{document.getElementById('answer-export-text').value=text();status.textContent=message||rows().length+' saved response'+(rows().length===1?'':'s');}}
document.getElementById('show-answers').addEventListener('click',()=>{{const area=document.getElementById('answer-export-text');area.focus();area.select();update('Review text selected');}});
document.getElementById('copy-answers').addEventListener('click',async()=>{{const area=document.getElementById('answer-export-text');area.focus();area.select();try{{await navigator.clipboard.writeText(text());update('Answers copied');}}catch(e){{update('Clipboard unavailable; review text selected');}}}});
document.getElementById('download-answers').addEventListener('click',()=>{{const blob=new Blob([text()],{{type:'text/plain;charset=utf-8'}});const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download={download};document.body.appendChild(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(link.href),0);update('Answers downloaded');}});
document.getElementById('submit-answers').addEventListener('click',async()=>{{if(!rows().length){{update('No answers to submit');return;}}try{{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){{await window.openai.sendFollowUpMessage({{prompt:text(),title:{title}}});update(rows().length+' responses submitted');return;}}}}catch(e){{}}const area=document.getElementById('answer-export-text');area.focus();area.select();update('Direct submission unavailable; review text selected');}});
update();
""".strip()


def _relative(path: Path, output: Path) -> str:
    return Path(os.path.relpath(path, output)).as_posix()


def _sheet_cell(review: Path, file_name: str, row: int,
                column: int, mode: str | None = None) -> Image.Image:
    path = review / file_name
    source = _loaded_sheet(str(path.resolve()))
    width, height = source.size
    if (width % TILE_SIZE or height % TILE_SIZE or
            (column + 1) * TILE_SIZE > width or
            (row + 1) * TILE_SIZE > height):
        raise ValueError(
            f"invalid 256px sprite cell {file_name}:{row},{column} "
            f"in {width}x{height}")
    result = source.crop((column * TILE_SIZE, row * TILE_SIZE,
                          (column + 1) * TILE_SIZE,
                          (row + 1) * TILE_SIZE))
    return result.convert(mode) if mode else result.copy()


def _sprite(review: Path, output: Path, file_name: str,
            row: int, column: int) -> str:
    path = review / file_name
    width, height = _loaded_sheet(str(path.resolve())).size
    if (width % TILE_SIZE or height % TILE_SIZE or
            (column + 1) * TILE_SIZE > width or
            (row + 1) * TILE_SIZE > height):
        raise ValueError(
            f"invalid 256px sprite cell {file_name}:{row},{column} "
            f"in {width}x{height}")
    return (f"background-image:url('{html.escape(_relative(path, output))}');"
            f"background-size:{width}px {height}px;"
            f"background-position:{-column * TILE_SIZE}px "
            f"{-row * TILE_SIZE}px")


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def _topology_label(value: str) -> str:
    match = re.fullmatch(r"([a-z]+)-(\d)of4(?:-v(\d))?", value)
    if not match:
        return value.replace("-", " ")
    shape, corners, variation = match.groups()
    label = f"{shape} {corners} of 4"
    return label + (f" · variation {variation}" if variation else "")


def _raw_mask_hash(image: Image.Image) -> str:
    return hashlib.sha256(image.convert("L").tobytes()).hexdigest()


def _canonical_mask_hash(image: Image.Image, topology: str) -> str:
    """Hash one boundary modulo legal quarter turns and 2/4 polarity."""
    mask = image.convert("L")
    candidates: list[bytes] = []
    for turns in range(4):
        rotated = mask.rotate(90 * turns)
        candidates.append(rotated.tobytes())
        if "-2of4" in topology:
            candidates.append(ImageOps.invert(rotated).tobytes())
    return min(hashlib.sha256(value).hexdigest() for value in candidates)


def _logical_group(row: dict[str, str]) -> tuple[str, str]:
    return row["mask_family"], row["topology"]


def _application_key(row: dict[str, str]) -> str:
    return (f'mask-{row["owner_material_id"]}-{row["support_material_id"]}-'
            f'{row["support_scenario"]}-{row["topology"]}')


def _representatives(rows: list[dict[str, str]], limit: int = 4) -> list[dict[str, str]]:
    ordered = sorted(rows, key=lambda row: (
        row["support_scenario"], row["mask_sha256"],
        row["owner_material"].casefold(), row["support_material"].casefold(),
        int(row["owner_material_id"]), int(row["support_material_id"])))
    selected: list[dict[str, str]] = []
    selected_keys: set[str] = set()

    def add(row: dict[str, str]) -> None:
        key = _application_key(row)
        if len(selected) < limit and key not in selected_keys:
            selected.append(row)
            selected_keys.add(key)

    # Show every legal polarity/orientation form first, then both support
    # scenarios, before filling the remaining slots with material diversity.
    for raw_hash in sorted({row["mask_sha256"] for row in ordered}):
        add(next(row for row in ordered if row["mask_sha256"] == raw_hash))
    for scenario in sorted({row["support_scenario"] for row in ordered}):
        add(next(row for row in ordered if row["support_scenario"] == scenario))
    for row in ordered:
        add(row)
    return selected


def _read_previous_rejections(
        path: Path | None) -> tuple[list[tuple[str, str, str]], int]:
    if path is None:
        return [], 0
    decisions: list[tuple[str, str, str]] = []
    ignored_keeps = 0
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^(mask-[^:]+):\s*(.*)$", line)
        if not match:
            continue
        key, payload = match.groups()
        parts = re.split(r"\s+(?:—|вЂ”)\s+", payload, maxsplit=1)
        answer = parts[0].strip()
        note = parts[1].strip() if len(parts) == 2 else ""
        if answer.startswith("Keep current:"):
            ignored_keeps += 1
            continue
        decisions.append((key, answer, note))
    return decisions, ignored_keeps


def _application(row: dict[str, str], canonical_hash: str) -> dict:
    return {
        "oldKey": (f'mask-{row["owner_material_id"]}-'
                   f'{row["support_scenario"]}-{row["topology"]}'),
        "key": _application_key(row),
        "ownerMaterialId": int(row["owner_material_id"]),
        "ownerMaterial": row["owner_material"],
        "ownerStyle": row["owner_style"],
        "supportMaterialId": int(row["support_material_id"]),
        "supportMaterial": row["support_material"],
        "scenario": row["support_scenario"],
        "row": int(row["row"]),
        "column": int(row["column"]),
        "maskSheet": row["sheet"],
        "heatmapSheet": row["heatmap_sheet"],
        "heatmapSha256": row["heatmap_sha256"],
        "resultSheet": row["result_example_sheet"],
        "resultSha256": row["result_example_sha256"],
        "linearResultSheet": row["linear_result_sheet"],
        "linearResultSha256": row["linear_result_sha256"],
        "ownerUniform": row["owner_uniform"],
        "ownerUniformSha256": row["owner_uniform_sha256"],
        "supportUniform": row["support_uniform"],
        "supportUniformSha256": row["support_uniform_sha256"],
        "rawMaskSha256": row["mask_sha256"],
        "canonicalMaskSha256": canonical_hash,
    }


def render(review: Path, output: Path, checkpoint: str = "R42-MASK",
           previous_answers: Path | None = None) -> dict:
    review = review.resolve()
    _loaded_sheet.cache_clear()
    output.mkdir(parents=True, exist_ok=True)
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    required = {
        "sheet", "row", "column", "owner_material_id", "owner_material",
        "owner_style", "mask_family", "support_scenario",
        "support_material_id", "support_material", "topology", "variation",
        "provenance", "mask_sha256", "missing", "heatmap_sheet",
        "heatmap_sha256", "heatmap_missing", "result_example_sheet",
        "result_example_sha256", "result_example_missing",
        "linear_result_sheet", "linear_result_sha256",
        "linear_result_missing", "owner_uniform",
        "owner_uniform_sha256", "support_uniform", "support_uniform_sha256",
    }
    if not rows or required.difference(rows[0]):
        raise ValueError(
            "blend-mask review index is missing questionnaire fields: "
            f"{sorted(required.difference(rows[0] if rows else {}))}")

    groups: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    canonical_hashes: dict[str, str] = {}
    old_to_group: dict[str, tuple[str, str]] = {}
    for row in rows:
        image = _sheet_cell(review, row["sheet"], int(row["row"]),
                            int(row["column"]), "L")
        raw_hash = _raw_mask_hash(image)
        if raw_hash.lower() != row["mask_sha256"].lower():
            raise ValueError(
                f'mask hash mismatch at {row["sheet"]}:{row["row"]},'
                f'{row["column"]}')
        canonical_hash = _canonical_mask_hash(image, row["topology"])
        canonical_hashes[_application_key(row)] = canonical_hash
        logical = _logical_group(row)
        groups[logical].append(row)
        old_to_group[(f'mask-{row["owner_material_id"]}-'
                      f'{row["support_scenario"]}-{row["topology"]}')] = logical

    group_slugs = {(_slug(family), topology): (family, topology)
                   for family, topology in groups}
    legacy_owner_groups: dict[tuple[str, str, str], set[tuple[str, str]]] = (
        defaultdict(set))
    for row in rows:
        legacy_owner_groups[
            (_slug(row["owner_style"]), row["support_scenario"],
             row["topology"])].add(_logical_group(row))
    prior_notes: dict[tuple[str, str], list[str]] = defaultdict(list)
    prior_decisions: dict[tuple[str, str], list[dict[str, str]]] = defaultdict(list)
    unmapped_prior_rejections: list[str] = []
    previous_rejections, ignored_previous_keeps = _read_previous_rejections(
        previous_answers)
    for old_key, answer, note in previous_rejections:
        logical = old_to_group.get(old_key)
        if logical is None:
            match = re.fullmatch(
                r"mask-family-(.+?)-(cross-style|same-style)-"
                r"((?:outer|inside|edge|diagonal)-\dof4(?:-v\d)?)"
                r"(?:-[0-9a-f]+)?", old_key)
            if match:
                logical = group_slugs.get((match.group(1), match.group(3)))
                if logical is None:
                    candidates = legacy_owner_groups.get(
                        (match.group(1), match.group(2), match.group(3)), set())
                    if len(candidates) == 1:
                        logical = next(iter(candidates))
        if logical is not None:
            text = answer + (f" — {note}" if note else "")
            if text not in prior_notes[logical]:
                prior_notes[logical].append(text)
            prior_decisions[logical].append({
                "oldKey": old_key, "decision": answer, "note": note})
        else:
            unmapped_prior_rejections.append(old_key)

    cards: list[str] = []
    analysis: list[dict] = []
    keys: set[str] = set()
    ordered_groups = sorted(groups.items(), key=lambda item: (
        item[0][0].casefold(), item[0][1]))
    for number, (logical, group_rows) in enumerate(ordered_groups, 1):
        mask_family, topology = logical
        orbit_hashes = {
            canonical_hashes[_application_key(row)] for row in group_rows}
        if len(orbit_hashes) != 1:
            raise ValueError(
                f"{mask_family}/{topology} contains "
                f"{len(orbit_hashes)} unrelated candidate masks")
        canonical_hash = next(iter(orbit_hashes))
        raw_hashes = sorted({row["mask_sha256"] for row in group_rows})
        provenance_values = sorted({row["provenance"] for row in group_rows})
        key = f"mask-family-{_slug(mask_family)}-{_slug(topology)}"
        if key in keys:
            raise ValueError(f"duplicate questionnaire key: {key}")
        keys.add(key)

        options = [
            f"Keep current: canonical {mask_family} {_topology_label(topology)}",
            "Reject: coverage or corner anchors",
            "Reject: boundary shape or feather",
            "Reject: topology or orientation",
            "Different mask design (note)",
            "Still uncertain",
        ]
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in options)

        representatives = _representatives(group_rows)
        first = representatives[0]
        image_row, column = int(first["row"]), int(first["column"])
        mask_style = _sprite(review, output, first["sheet"], image_row, column)
        heatmap_style = _sprite(
            review, output, first["heatmap_sheet"], image_row, column)
        examples_html: list[str] = []
        representative_keys: set[str] = set()
        for example in representatives:
            application_key = _application_key(example)
            representative_keys.add(application_key)
            owner_uniform = _relative(review / example["owner_uniform"], output)
            support_uniform = _relative(review / example["support_uniform"], output)
            result_style = _sprite(
                review, output, example["result_example_sheet"],
                int(example["row"]), int(example["column"]))
            linear_style = _sprite(
                review, output, example["linear_result_sheet"],
                int(example["row"]), int(example["column"]))
            examples_html.append(f'''<section class="application-example" data-application-key="{html.escape(application_key)}">
<h3>{html.escape(example["owner_material"])} + {html.escape(example["support_material"])}</h3>
<p class="muted">{html.escape(example["support_scenario"])} · raw mask {html.escape(example["mask_sha256"][:12])}</p>
<div class="example-grid">
 <figure class="visual requested-uniform"><img src="{html.escape(owner_uniform)}" alt="Requested uniform {html.escape(example["owner_material"])}"><figcaption>Requested uniform<br><strong>{html.escape(example["owner_material"])}</strong></figcaption></figure>
 <figure class="visual resulting-blend encoded-result"><span class="sprite" role="img" aria-label="Current encoded-RGB blend outcome" style="{result_style}"></span><figcaption>A · current runtime<br><strong>Encoded-RGB blend</strong></figcaption></figure>
 <figure class="visual resulting-blend linear-result"><span class="sprite" role="img" aria-label="Review-only linear-light blend outcome" style="{linear_style}"></span><figcaption>B · review only<br><strong>Linear-light blend</strong></figcaption></figure>
 <figure class="visual support-uniform"><img src="{html.escape(support_uniform)}" alt="Support uniform {html.escape(example["support_material"])}"><figcaption>Support uniform<br><strong>{html.escape(example["support_material"])}</strong></figcaption></figure>
</div></section>''')

        notes = prior_notes.get(logical, [])
        prior_html = ""
        if notes:
            items = "".join(f"<li>{html.escape(note)}</li>" for note in notes)
            prior_html = ("<aside class=\"prior-review\"><strong>R41 predecessor "
                          "rejections (context only; not an answer for this new "
                          f"mask):</strong><ul>{items}</ul></aside>")
        material_ids = sorted({int(row["owner_material_id"]) for row in group_rows}
                              | {int(row["support_material_id"])
                                 for row in group_rows})
        shown = len(representatives)
        cards.append(f'''<article class="mask-question" data-key="{html.escape(key)}" data-style="{html.escape(mask_family)}" data-materials="{' '.join(map(str, material_ids))}">
<h2>{number}. {html.escape(mask_family)} · {html.escape(_topology_label(topology))}</h2>
<p class="muted">One canonical boundary shared by {len(group_rows)} material-pair applications · {shown} representative outcomes shown · {len(raw_hashes)} legal rotation/complement form{'s' if len(raw_hashes) != 1 else ''}</p>
{prior_html}<div class="shared-evidence">
 <figure class="visual ownership-mask"><span class="sprite" role="img" aria-label="Canonical ownership mask" style="{mask_style}"></span><figcaption>Ownership mask<br><strong>White = requested example material</strong></figcaption></figure>
 <figure class="visual threshold-heatmap"><span class="sprite" role="img" aria-label="Coverage threshold heatmap" style="{heatmap_style}"></span><figcaption>Contract heatmap<br><strong>green ≥80% · navy ≤20% · orange feather</strong></figcaption></figure>
</div><div class="examples">{''.join(examples_html)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{option_html}</select></label><label>Note<input class="note form-control" type="text" value="" placeholder="Optional detail"></label></div>
</article>''')

        applications: list[dict] = []
        for row in group_rows:
            app = _application(row, canonical_hashes[_application_key(row)])
            app["representative"] = app["key"] in representative_keys
            applications.append(app)
        analysis.append({
            "key": key,
            "maskFamily": mask_family,
            "topology": topology,
            "variation": int(first["variation"]),
            "provenance": provenance_values,
            "canonicalMaskSha256": canonical_hash,
            "rawMaskSha256s": raw_hashes,
            "applicationCount": len(group_rows),
            "applications": applications,
            "examples": [app for app in applications if app["representative"]],
            "priorR41Notes": notes,
            "priorR41Decisions": prior_decisions.get(logical, []),
        })

    mask_families = sorted({row["mask_family"] for row in rows})
    materials = {
        int(row["owner_material_id"]): row["owner_material"] for row in rows}
    materials.update({
        int(row["support_material_id"]): row["support_material"] for row in rows})
    style_options = "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
        for value in mask_families)
    material_options = "".join(
        f'<option value="{value}">{html.escape(name)}</option>'
        for value, name in sorted(
            materials.items(), key=lambda item: item[1].casefold()))
    gamma_key = "blend-compositor-gamma"
    gamma_options = "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
        for value in (
            "Keep runtime encoded-RGB (current)",
            "Switch runtime to linear-light",
            "Still uncertain",
        ))
    gamma_card = f'''<article class="compositor-question" data-key="{gamma_key}">
<h2>Global compositor choice</h2>
<p>The mask shape is identical in A and B. A is the current runtime calculation; B converts RGB to linear light for mixing and converts back. This choice is deliberately review-only until you select it here.</p>
<div class="answer-row"><label>Decision<select class="answer form-select">{gamma_options}</select></label><label>Note<input class="note form-control" type="text" value="" placeholder="Optional global compositor note"></label></div>
</article>'''
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>
:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368);--warn:light-dark(#7a4600,#ffd28a)}}
*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1740px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:18px;margin:0}}h3{{font-size:15px;margin:0 0 4px}}button,select,input,textarea{{font:inherit}}button{{padding:8px 12px}}.muted{{color:var(--muted)}}.toolbar,.filters,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}label{{display:grid;gap:4px;min-width:300px}}select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}.mask-question,.compositor-question{{margin:24px 0;padding:18px 0;border-top:2px solid var(--border)}}.compositor-question{{padding:18px;background:var(--panel);border:1px solid var(--border)}}.shared-evidence{{display:grid;grid-template-columns:repeat(2,minmax(150px,256px));gap:14px;margin:12px 0 18px}}.examples{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}}.application-example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}.example-grid{{display:grid;grid-template-columns:repeat(4,minmax(100px,256px));gap:8px}}figure{{margin:0}}figure img,.sprite{{display:block;width:256px;height:256px;max-width:100%;object-fit:contain;background-color:var(--panel);background-repeat:no-repeat;image-rendering:auto}}figcaption{{padding-top:5px;color:var(--muted)}}figcaption strong{{color:var(--fg)}}.prior-review{{padding:10px 12px;border-left:4px solid var(--warn);background:var(--panel);color:var(--warn)}}.prior-review ul{{margin:6px 0 0;padding-left:20px}}.export-panel{{margin:14px 0}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}[hidden]{{display:none!important}}@media(max-width:1200px){{.example-grid{{grid-template-columns:repeat(2,minmax(100px,256px))}}}}@media(max-width:900px){{.examples{{grid-template-columns:1fr}}}}@media(max-width:650px){{body{{padding:10px}}.example-grid{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}
</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused review</h1>
<p class="muted">{len(groups)} shared mask-family/topology questions cover {len(rows)} material-pair applications. There are no per-material or per-stock-tile questions. Every mask is shown with a threshold heatmap and up to four exact results using full requested/support uniform tiles. A/B compares the current encoded-RGB runtime with a review-only linear-light compositor; it does not duplicate the mask decision.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<div class="filters"><label>Mask family<select id="style-filter"><option value="">All mask families</option>{style_options}</select></label><label>Material example<select id="material-filter"><option value="">All materials</option>{material_options}</select></label></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea></section>
{gamma_card}<main>{''.join(cards)}</main><script>{script}
const styleFilter=document.getElementById('style-filter');const materialFilter=document.getElementById('material-filter');function filterCards(){{document.querySelectorAll('.mask-question').forEach(card=>{{const materials=(card.dataset.materials||'').split(' ');card.hidden=Boolean((styleFilter.value&&card.dataset.style!==styleFilter.value)||(materialFilter.value&&!materials.includes(materialFilter.value)));}});}}styleFilter.addEventListener('change',filterCards);materialFilter.addEventListener('change',filterCards);filterCards();
</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis_path = output / "blend-mask-analysis.json"
    analysis_path.write_text(json.dumps({
        "schema": "ei-blend-mask-questionnaire-v4",
        "checkpoint": checkpoint,
        "applicationCount": len(rows),
        "maskQuestionCount": len(groups),
        "globalQuestions": [{"key": gamma_key, "kind": "gamma-compositor"}],
        "priorReview": {
            "explicitRejections": sum(
                len(values) for values in prior_decisions.values()),
            "affectedGroups": sum(bool(values)
                                  for values in prior_notes.values()),
            "ignoredDefaultKeeps": ignored_previous_keeps,
            "unmappedExplicitRejectionKeys": unmapped_prior_rejections,
        },
        "questions": analysis,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {
        "questions": len(groups),
        "globalQuestions": 1,
        "applications": len(rows),
        "materials": len(materials),
        "maskFamilies": len(mask_families),
        "priorRejectionGroups": sum(bool(values) for values in prior_notes.values()),
        "priorRejections": sum(len(values) for values in prior_decisions.values()),
        "ignoredPriorKeeps": ignored_previous_keeps,
        "unmappedPriorRejections": unmapped_prior_rejections,
        "html": str(html_path),
        "analysis": str(analysis_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R42-MASK")
    parser.add_argument("--answers", type=Path,
                        help="Earlier questionnaire export; only explicit "
                             "rejections are retained as context")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint,
                            args.answers), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
