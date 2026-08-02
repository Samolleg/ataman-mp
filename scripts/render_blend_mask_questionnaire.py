#!/usr/bin/env python3
"""Render an interactive questionnaire from a generated blend-mask review."""

from __future__ import annotations

import argparse
import csv
import html
import json
import os
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


def render(review: Path, output: Path, checkpoint: str = "R37-MASK") -> dict:
    review = review.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with (review / "index.csv").open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle))
    required = {
        "sheet", "row", "column", "owner_material_id", "owner_material",
        "owner_style", "support_scenario", "support_material_id",
        "support_material", "topology", "variation", "provenance",
        "source_example_sheet", "result_example_sheet", "owner_uniform",
        "support_uniform",
    }
    if not rows or required.difference(rows[0]):
        raise ValueError(
            f"blend-mask review index is missing questionnaire fields: {sorted(required.difference(rows[0] if rows else {}))}")

    cards = []
    analysis = []
    keys = set()
    for number, row in enumerate(rows, 1):
        owner_id = int(row["owner_material_id"])
        scenario = row["support_scenario"]
        topology = row["topology"]
        key = f"mask-{owner_id}-{scenario}-{topology}"
        if key in keys:
            raise ValueError(f"duplicate questionnaire key: {key}")
        keys.add(key)
        provenance = row["provenance"] or "unidentified stock source"
        options = [
            f"Keep current: {provenance}",
            "Reject: bad straight cut",
            "Reject: bad shape/coverage",
            "Reject: wrong stock source",
            "Reject: wrong orientation",
            "Different mask/source (note)",
            "Still uncertain",
        ]
        option_html = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in options)
        image_row = int(row["row"])
        column = int(row["column"])
        owner_uniform = _relative(review / row["owner_uniform"], output)
        support_uniform = _relative(review / row["support_uniform"], output)
        mask_style = _sprite(review, output, row["sheet"], image_row, column)
        source_style = _sprite(
            review, output, row["source_example_sheet"], image_row, column)
        result_style = _sprite(
            review, output, row["result_example_sheet"], image_row, column)
        cards.append(f'''<article class="mask-question" data-key="{html.escape(key)}" data-style="{html.escape(row["owner_style"])}" data-material="{owner_id}" data-sheet-row="{image_row}" data-sheet-column="{column}">
<h2>{number}. {html.escape(row["owner_material"])} · {html.escape(topology)}</h2>
<p class="muted">{html.escape(scenario)} support: {html.escape(row["support_material"])} · stock source {html.escape(provenance)}</p>
<div class="visual-grid">
 <figure class="visual requested-uniform"><img src="{html.escape(owner_uniform)}" alt="Requested uniform {html.escape(row["owner_material"])}"><figcaption>Requested uniform<br><strong>{html.escape(row["owner_material"])}</strong></figcaption></figure>
 <figure class="visual ownership-mask"><span class="sprite" role="img" aria-label="Selected ownership mask" style="{mask_style}"></span><figcaption>Selected ownership mask<br><strong>{html.escape(provenance)}</strong></figcaption></figure>
 <figure class="visual resulting-blend"><span class="sprite" role="img" aria-label="Generated blend outcome" style="{result_style}"></span><figcaption>Generated outcome<br><strong>{html.escape(topology)}</strong></figcaption></figure>
 <figure class="visual support-uniform"><img src="{html.escape(support_uniform)}" alt="Support uniform {html.escape(row["support_material"])}"><figcaption>Support uniform<br><strong>{html.escape(row["support_material"])}</strong></figcaption></figure>
 <figure class="visual authored-source"><span class="sprite" role="img" aria-label="Original authored stock transition" style="{source_style}"></span><figcaption>Authored stock transition<br><strong>{html.escape(provenance)}</strong></figcaption></figure>
</div>
<div class="answer-row"><label>Decision<select class="answer form-select">{option_html}</select></label><label>Note<input class="note form-control" type="text" placeholder="Optional detail"></label></div>
</article>''')
        analysis.append({
            "key": key, "ownerMaterialId": owner_id,
            "ownerMaterial": row["owner_material"],
            "ownerStyle": row["owner_style"],
            "supportMaterialId": int(row["support_material_id"]),
            "supportMaterial": row["support_material"],
            "scenario": scenario, "topology": topology,
            "variation": int(row["variation"]), "provenance": provenance,
            "maskSha256": row.get("mask_sha256", ""),
            "row": image_row, "column": column,
            "maskSheet": row["sheet"],
            "sourceSheet": row["source_example_sheet"],
            "resultSheet": row["result_example_sheet"],
            "ownerUniform": row["owner_uniform"],
            "supportUniform": row["support_uniform"],
        })

    styles = sorted({row["owner_style"] for row in rows})
    materials = {}
    for row in rows:
        materials[int(row["owner_material_id"])] = row["owner_material"]
    style_options = "".join(
        f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
        for value in styles)
    material_options = "".join(
        f'<option value="{value}">{html.escape(name)}</option>'
        for value, name in sorted(materials.items(), key=lambda item: item[1].lower()))
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title>
<style>
:root{{color-scheme:light dark;--bg:light-dark(#fff,#181818);--fg:light-dark(#1a1c1f,#fff);--muted:light-dark(#5f6368,#bdc1c6);--panel:light-dark(#f4f5f6,#252629);--border:light-dark(#d7d9dc,#5f6368);--accent:light-dark(#1769aa,#83c3ff)}}
*{{box-sizing:border-box}}body{{margin:0 auto;max-width:1500px;padding:18px;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{font-size:17px;margin:0}}button,select,input,textarea{{font:inherit}}button{{padding:8px 12px}}.muted{{color:var(--muted)}}.toolbar,.filters,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}label{{display:grid;gap:4px;min-width:220px}}select,input{{padding:8px;background:var(--bg);color:var(--fg);border:1px solid var(--border)}}.mask-question{{margin:18px 0;padding:14px 0;border-top:1px solid var(--border)}}.visual-grid{{display:grid;grid-template-columns:repeat(5,minmax(150px,256px));gap:12px;align-items:start}}figure{{margin:0}}figure img,.sprite{{display:block;width:256px;height:256px;max-width:100%;object-fit:contain;background-color:var(--panel);background-repeat:no-repeat;image-rendering:auto}}figcaption{{padding-top:5px;color:var(--muted)}}figcaption strong{{color:var(--fg)}}.export-panel{{margin:14px 0}}#answer-export-text{{width:100%;min-height:150px;background:var(--panel);color:var(--fg);border:1px solid var(--border)}}[hidden]{{display:none!important}}@media(max-width:900px){{.visual-grid{{grid-template-columns:repeat(2,minmax(140px,256px))}}}}@media(max-width:520px){{body{{padding:10px}}.visual-grid{{grid-template-columns:1fr}}.answer-row label{{min-width:100%}}}}
</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused review</h1><p class="muted">{len(rows)} selected mask applications. Every decision shows full requested/support uniform tiles, the transferred ownership mask, its original authored stock transition, and the generated result. Defaults keep the current reviewed source; answers save immediately.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<div class="filters"><label>Style<select id="style-filter"><option value="">All styles</option>{style_options}</select></label><label>Material<select id="material-filter"><option value="">All materials</option>{material_options}</select></label></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea></section>
<main>{''.join(cards)}</main><script>{script}
const styleFilter=document.getElementById('style-filter');const materialFilter=document.getElementById('material-filter');function filterCards(){{document.querySelectorAll('.mask-question').forEach(card=>{{card.hidden=Boolean((styleFilter.value&&card.dataset.style!==styleFilter.value)||(materialFilter.value&&card.dataset.material!==materialFilter.value));}});}}styleFilter.addEventListener('change',filterCards);materialFilter.addEventListener('change',filterCards);filterCards();
</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis_path = output / "blend-mask-analysis.json"
    analysis_path.write_text(json.dumps({
        "schema": "ei-blend-mask-questionnaire-v2",
        "checkpoint": checkpoint, "questions": analysis,
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"questions": len(rows), "materials": len(materials),
            "styles": len(styles), "html": str(html_path),
            "analysis": str(analysis_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--review", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R37-MASK")
    args = parser.parse_args()
    print(json.dumps(render(args.review, args.output, args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
