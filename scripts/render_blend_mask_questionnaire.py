#!/usr/bin/env python3
"""Render a grouped interactive questionnaire from a generated blend-mask review."""

from __future__ import annotations

import argparse
import csv
import html
import json
import os
import re
from collections import defaultdict
from pathlib import Path

from PIL import Image


def interaction_script(checkpoint: str) -> str:
    header = json.dumps(
        f"Blend-mask {checkpoint} focused review decisions:\n",
        ensure_ascii=False)
    footer = json.dumps(
        "\nApply only explicit mask decisions; verify the requested/support uniforms and rendered outcome before changing generation.",
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


def _sprite(review: Path, output: Path, file_name: str,
            row: int, column: int) -> str:
    path = review / file_name
    with Image.open(path) as image:
        width, height = image.size
    tile = 256
    if (width % tile or height % tile or
            (column + 1) * tile > width or (row + 1) * tile > height):
        raise ValueError(
            f"invalid 256px sprite cell {file_name}:{row},{column} in {width}x{height}")
    return (f"background-image:url('{html.escape(_relative(path, output))}');"
            f"background-size:{width}px {height}px;"
            f"background-position:{-column * tile}px {-row * tile}px")


def _logical_group(row: dict[str, str]) -> tuple[str, str, str, str]:
    return (row["owner_style"], row["support_scenario"], row["topology"],
            row["mask_sha256"])


def _slug(value: str) -> str:
    return re.sub(r"[^a-z0-9]+", "-", value.lower()).strip("-")


def _topology_label(value: str) -> str:
    match = re.fullmatch(r"([a-z]+)-(\d)of4(?:-v(\d))?", value)
    if not match:
        return value.replace("-", " ")
    shape, corners, variation = match.groups()
    label = f"{shape} {corners} of 4"
    return label + (f" · variation {variation}" if variation else "")


def _representatives(rows: list[dict[str, str]], limit: int = 4) -> list[dict[str, str]]:
    ordered = sorted(rows, key=lambda row: (
        row["owner_material"].casefold(), row["support_material"].casefold(),
        int(row["owner_material_id"]), int(row["support_material_id"])))
    if len(ordered) <= limit:
        return ordered
    indices = [round(index * (len(ordered) - 1) / (limit - 1))
               for index in range(limit)]
    return [ordered[index] for index in indices]


def _read_previous_answers(path: Path | None) -> dict[str, tuple[str, str]]:
    if path is None:
        return {}
    answers: dict[str, tuple[str, str]] = {}
    for line in path.read_text(encoding="utf-8-sig").splitlines():
        match = re.match(r"^(mask-[^:]+):\s*(.*)$", line)
        if not match:
            continue
        key, payload = match.groups()
        answer, separator, note = payload.partition(" — ")
        answer, note = answer.strip(), note.strip() if separator else ""
        if answer.startswith("Keep current:") and not note:
            continue
        answers[key] = (answer, note)
    return answers


def _application(row: dict[str, str]) -> dict:
    return {
        "oldKey": (f'mask-{row["owner_material_id"]}-'
                   f'{row["support_scenario"]}-{row["topology"]}'),
        "ownerMaterialId": int(row["owner_material_id"]),
        "ownerMaterial": row["owner_material"],
        "supportMaterialId": int(row["support_material_id"]),
        "supportMaterial": row["support_material"],
        "row": int(row["row"]), "column": int(row["column"]),
        "maskSheet": row["sheet"],
        "sourceSheet": row["source_example_sheet"],
        "resultSheet": row["result_example_sheet"],
        "ownerUniform": row["owner_uniform"],
        "supportUniform": row["support_uniform"],
    }


def render(review: Path, output: Path, checkpoint: str = "R37-MASK",
           previous_answers: Path | None = None) -> dict:
    review = review.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    required = {
        "sheet", "row", "column", "owner_material_id", "owner_material",
        "owner_style", "support_scenario", "support_material_id",
        "support_material", "topology", "variation", "provenance",
        "mask_sha256", "source_example_sheet", "result_example_sheet",
        "owner_uniform", "support_uniform",
    }
    if not rows or required.difference(rows[0]):
        raise ValueError(
            "blend-mask review index is missing questionnaire fields: "
            f"{sorted(required.difference(rows[0] if rows else {}))}")

    groups: dict[tuple[str, str, str, str], list[dict[str, str]]] = defaultdict(list)
    old_to_group = {}
    for row in rows:
        logical = _logical_group(row)
        groups[logical].append(row)
        old_to_group[_application(row)["oldKey"]] = logical

    migrated: dict[tuple[str, str, str, str], list[tuple[str, str]]] = defaultdict(list)
    for old_key, decision in _read_previous_answers(previous_answers).items():
        logical = old_to_group.get(old_key)
        if logical is not None:
            migrated[logical].append(decision)

    cards = []
    analysis = []
    keys = set()
    migrated_count = 0
    conflict_count = 0
    ordered_groups = sorted(groups.items(), key=lambda item: (
        item[0][0].casefold(), item[0][1], item[0][2], item[0][3]))
    for number, (logical, group_rows) in enumerate(ordered_groups, 1):
        owner_style, scenario, topology, mask_sha256 = logical
        provenance_values = sorted({row["provenance"] or "unidentified stock source"
                                    for row in group_rows})
        provenance = ", ".join(provenance_values)
        key = (f"mask-family-{_slug(owner_style)}-{_slug(scenario)}-"
               f"{_slug(topology)}-{mask_sha256[:12].lower()}")
        if key in keys:
            raise ValueError(f"duplicate questionnaire key: {key}")
        keys.add(key)

        prior = sorted(set(migrated.get(logical, [])))
        conflict = len(prior) > 1
        migrated_answer = prior[0][0] if len(prior) == 1 else ""
        migrated_note = prior[0][1] if len(prior) == 1 else ""
        migrated_count += bool(migrated_answer or migrated_note)
        conflict_count += conflict

        options = [
            f"Keep current: {provenance}",
            "Reject: bad straight cut", "Reject: bad shape/coverage",
            "Reject: wrong stock source", "Reject: wrong orientation",
            "Different mask/source (note)", "Still uncertain",
        ]
        if migrated_answer and migrated_answer not in options:
            options.append(migrated_answer)
        option_html = "".join(
            f'<option value="{html.escape(value)}"'
            f'{" selected" if value == migrated_answer else ""}>'
            f'{html.escape(value)}</option>' for value in options)

        representatives = _representatives(group_rows)
        first = representatives[0]
        image_row, column = int(first["row"]), int(first["column"])
        mask_style = _sprite(review, output, first["sheet"], image_row, column)
        source_style = _sprite(review, output, first["source_example_sheet"],
                               image_row, column)
        examples_html = []
        representative_keys = set()
        for example in representatives:
            app = _application(example)
            representative_keys.add(app["oldKey"])
            owner_uniform = _relative(review / example["owner_uniform"], output)
            support_uniform = _relative(review / example["support_uniform"], output)
            result_style = _sprite(review, output, example["result_example_sheet"],
                                   int(example["row"]), int(example["column"]))
            examples_html.append(f'''<section class="application-example" data-application-key="{html.escape(app["oldKey"])}">
<h3>{html.escape(example["owner_material"])} + {html.escape(example["support_material"])}</h3>
<div class="example-grid">
 <figure class="visual requested-uniform"><img src="{html.escape(owner_uniform)}" alt="Requested uniform {html.escape(example["owner_material"])}"><figcaption>Requested family<br><strong>{html.escape(example["owner_material"])}</strong></figcaption></figure>
 <figure class="visual resulting-blend"><span class="sprite" role="img" aria-label="Generated blend outcome" style="{result_style}"></span><figcaption>Exact generated outcome<br><strong>{html.escape(_topology_label(topology))}</strong></figcaption></figure>
 <figure class="visual support-uniform"><img src="{html.escape(support_uniform)}" alt="Support uniform {html.escape(example["support_material"])}"><figcaption>Support family<br><strong>{html.escape(example["support_material"])}</strong></figcaption></figure>
</div></section>''')

        material_ids = sorted({int(row["owner_material_id"]) for row in group_rows})
        migration_warning = ""
        if conflict:
            decisions = "; ".join(
                answer + (f" — {note}" if note else "") for answer, note in prior)
            migration_warning = (f'<p class="migration-warning"><strong>Conflicting previous per-material answers:</strong> '
                                 f'{html.escape(decisions)}. Review this shared mask once below.</p>')
        shown = len(representatives)
        cards.append(f'''<article class="mask-question" data-key="{html.escape(key)}" data-style="{html.escape(owner_style)}" data-materials="{' '.join(map(str, material_ids))}">
<h2>{number}. {html.escape(owner_style)} · {html.escape(scenario)} · {html.escape(_topology_label(topology))}</h2>
<p class="muted">Shared by {len(group_rows)} material-pair applications · {shown} representative outcomes shown · Authored source evidence: {html.escape(provenance)}</p>
{migration_warning}<div class="shared-evidence">
 <figure class="visual ownership-mask"><span class="sprite" role="img" aria-label="Selected ownership mask" style="{mask_style}"></span><figcaption>Shared ownership mask<br><strong>{html.escape(owner_style)}</strong></figcaption></figure>
 <figure class="visual authored-source"><span class="sprite" role="img" aria-label="Original authored stock transition" style="{source_style}"></span><figcaption>Authored source evidence<br><strong>{html.escape(provenance)}</strong></figcaption></figure>
</div><div class="examples">{''.join(examples_html)}</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{option_html}</select></label><label>Note<input class="note form-control" type="text" value="{html.escape(migrated_note)}" placeholder="Optional detail"></label></div>
</article>''')

        applications = []
        for row in group_rows:
            app = _application(row)
            app["representative"] = app["oldKey"] in representative_keys
            applications.append(app)
        analysis.append({
            "key": key, "ownerStyle": owner_style, "scenario": scenario,
            "topology": topology, "variation": int(first["variation"]),
            "provenance": provenance_values, "maskSha256": mask_sha256,
            "applicationCount": len(group_rows),
            "applications": applications,
            "examples": [app for app in applications if app["representative"]],
            "migratedAnswer": migrated_answer, "migratedNote": migrated_note,
            "migrationConflict": conflict,
        })

    styles = sorted({row["owner_style"] for row in rows})
    materials = {int(row["owner_material_id"]): row["owner_material"] for row in rows}
    style_options = "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
        for value in styles)
    material_options = "".join(
        f'<option value="{value}">{html.escape(name)}</option>'
        for value, name in sorted(materials.items(), key=lambda item: item[1].casefold()))
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>
:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368);--warn:light-dark(#7a4600,#ffd28a)}}
*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1500px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:18px;margin:0}}h3{{font-size:15px;margin:0 0 8px}}button,select,input,textarea{{font:inherit}}button{{padding:8px 12px}}.muted{{color:var(--muted)}}.toolbar,.filters,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}label{{display:grid;gap:4px;min-width:260px}}select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}.mask-question{{margin:24px 0;padding:18px 0;border-top:2px solid var(--border)}}.shared-evidence{{display:grid;grid-template-columns:repeat(2,minmax(150px,256px));gap:14px;margin:12px 0 18px}}.examples{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:14px}}.application-example{{padding:12px;background:var(--panel);border:1px solid var(--border)}}.example-grid{{display:grid;grid-template-columns:repeat(3,minmax(100px,256px));gap:8px}}figure{{margin:0}}figure img,.sprite{{display:block;width:256px;height:256px;max-width:100%;object-fit:contain;background-color:var(--panel);background-repeat:no-repeat;image-rendering:auto}}figcaption{{padding-top:5px;color:var(--muted)}}figcaption strong{{color:var(--fg)}}.migration-warning{{color:var(--warn)}}.export-panel{{margin:14px 0}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}[hidden]{{display:none!important}}@media(max-width:1000px){{.examples{{grid-template-columns:1fr}}}}@media(max-width:650px){{body{{padding:10px}}.example-grid{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}
</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused review</h1><p class="muted">{len(groups)} shared mask questions cover {len(rows)} material-pair applications. Each question names the target visual style, support scenario, and corner assignment; stock area/tile numbers are provenance only. Up to four exact generated outcomes show how the shared mask behaves across different material families. Defaults keep the current reviewed source; answers save immediately.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<div class="filters"><label>Target style<select id="style-filter"><option value="">All styles</option>{style_options}</select></label><label>Material example<select id="material-filter"><option value="">All materials</option>{material_options}</select></label></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea></section>
<main>{''.join(cards)}</main><script>{script}
const styleFilter=document.getElementById('style-filter');const materialFilter=document.getElementById('material-filter');function filterCards(){{document.querySelectorAll('.mask-question').forEach(card=>{{const materials=(card.dataset.materials||'').split(' ');card.hidden=Boolean((styleFilter.value&&card.dataset.style!==styleFilter.value)||(materialFilter.value&&!materials.includes(materialFilter.value)));}});}}styleFilter.addEventListener('change',filterCards);materialFilter.addEventListener('change',filterCards);filterCards();
</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis_path = output / "blend-mask-analysis.json"
    analysis_path.write_text(json.dumps({
        "schema": "ei-blend-mask-questionnaire-v3",
        "checkpoint": checkpoint, "applicationCount": len(rows),
        "questions": analysis,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"questions": len(groups), "applications": len(rows),
            "materials": len(materials), "styles": len(styles),
            "migratedDecisions": migrated_count,
            "migrationConflicts": conflict_count,
            "html": str(html_path), "analysis": str(analysis_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R37-MASK")
    parser.add_argument("--answers", type=Path,
                        help="Earlier per-material questionnaire export to migrate")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint, args.answers),
                     indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
