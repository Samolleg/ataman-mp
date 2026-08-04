#!/usr/bin/env python3
"""Render the R87 uniform-or-four-corner questionnaire from inverse evidence."""

from __future__ import annotations

import argparse
import hashlib
import html
import json
from pathlib import Path


CORNERS = ("NW", "NE", "SE", "SW")
DISPLAY_CORNERS = ("NW", "NE", "SW", "SE")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest().upper()


def option_rows(values: list[str], selected: str) -> str:
    ordered = [selected] + sorted(
        {value for value in values if value and value != selected},
        key=str.casefold)
    return "".join(
        f'<option value="{html.escape(value, quote=True)}"'
        f'{" selected" if value == selected else ""}>'
        f'{html.escape(value)}</option>' for value in ordered)


def candidate_card(candidate: dict, corner: str, target_crop: str) -> str:
    rotation = int(candidate["rotationClockwise"])
    exact = "Exact decoded-pixel match" if candidate.get(
        "exactDecodedPixelMatch") else "Visual-distance candidate"
    return f'''<div class="candidate">
<div class="compare-pair"><div><img src="{html.escape(target_crop, quote=True)}" alt="Target {corner} crop"><small>Target {corner}</small></div>
<div><img class="rotated r{rotation}" src="{html.escape(candidate['sourceImage'], quote=True)}" alt="{html.escape(candidate['family'], quote=True)} source"><small>Source rotated {rotation}&deg; clockwise</small></div></div>
<strong>{html.escape(candidate['family'])}</strong>
<span>{html.escape(candidate['namespace'])} slot {candidate['slot']}</span>
<span>{exact}; raw distance {candidate['uncalibratedRawScore']:.7f} (lower is closer)</span>
<small>{html.escape(candidate['trust'])}</small></div>'''


def evidence_section(variant: str, record: dict) -> str:
    target = record["target"]
    corners = []
    for corner in DISPLAY_CORNERS:
        evidence = record["corners"][corner]
        candidates = evidence.get("topThreeFamilies", [])[:3]
        cards = "".join(candidate_card(
            candidate, corner, evidence["targetCropImage"])
            for candidate in candidates)
        margin = evidence.get("bestVsRunner", {}).get(
            "plainLanguage", "No runner-up comparison is available")
        corners.append(f'''<details><summary>{corner}: audit comparisons — {html.escape(margin)}</summary>
<div class="candidate-row">{cards}</div></details>''')
    return f'''<section class="variant"><h3>{variant.upper()} evidence</h3>
<div class="target"><img src="{html.escape(target['image'], quote=True)}" alt="{html.escape(target['namespace'], quote=True)} slot {target['slot']}"><strong>{html.escape(target['namespace'])} slot {target['slot']}</strong></div>
{''.join(corners)}</section>'''


def render(evidence_path: Path, preset_path: Path, output: Path) -> dict:
    evidence = json.loads(evidence_path.read_text(encoding="utf-8"))
    catalog = json.loads(preset_path.read_text(encoding="utf-8"))
    presets = {row["namespace"].casefold(): row
               for row in catalog["presets"]}
    alarms_by_target: dict[tuple[str, int], list[dict]] = {}
    for alarm in evidence.get("newConnectedAlarms", []):
        alarms_by_target.setdefault((
            alarm["targetNamespace"].casefold(),
            int(alarm["targetSlot"])), []).append(alarm)
    cards = []
    image_paths: set[str] = set()
    for number, target in enumerate(evidence["targets"], 1):
        preset = presets[target["namespaceKey"]]
        families = [family["name"] for family in preset["families"]]
        audit_families = []
        for record in target["variantEvidence"].values():
            image_paths.add(record["target"]["image"])
            for corner in CORNERS:
                corner_evidence = record["corners"][corner]
                image_paths.add(corner_evidence["targetCropImage"])
                for candidate in corner_evidence.get("topThreeFamilies", [])[:3]:
                    audit_families.append(candidate["family"])
                    image_paths.add(candidate["sourceImage"])
        all_families = list(dict.fromkeys(families + audit_families))
        default = target["questionnaireDefault"]
        default_mode = default["mode"]
        default_uniform = (default.get("uniformFamily") or
                           next(iter(default["corners"].values())))
        default_corners = default["corners"]
        contradictions = "".join(
            f'<li>{html.escape(value)}</li>'
            for value in target.get("humanContradictions", []))
        alarm_rows = "".join(
            '<li><strong>Exact same artwork:</strong> '
            f'{html.escape(alarm["connectedNamespace"])} slot '
            f'{alarm["connectedSlot"]}; current corners '
            f'{html.escape(" / ".join(alarm["connectedCatalogSignature"]))}. '
            'Resolve it with this card; the conflicting old signatures must '
            'not be propagated.</li>'
            for alarm in alarms_by_target.get(
                (target["namespaceKey"], int(target["slot"])), []))
        reason_rows = "".join(
            f'<li>{html.escape(value)}</li>' for value in target["reasons"])
        corner_controls = "".join(
            f'<label>{corner}<select class="corner corner-{corner.lower()}">'
            f'{option_rows(all_families, default_corners[corner])}</select></label>'
            for corner in DISPLAY_CORNERS)
        variants = "".join(
            evidence_section(variant, record)
            for variant, record in target["variantEvidence"].items())
        defaults_json = html.escape(json.dumps({
            "mode": default_mode,
            "uniformFamily": default_uniform,
            "corners": default_corners,
        }, ensure_ascii=False), quote=True)
        cards.append(f'''<article class="question" data-key="{html.escape(target['key'], quote=True)}" data-defaults="{defaults_json}">
<h2>{number}. {html.escape(target['namespace'])} slot {target['slot']}</h2>
<ul class="reasons">{reason_rows}{contradictions}{alarm_rows}</ul>
<div class="decision">
<label>Tile identity mode<select class="mode"><option value="four-corners"{' selected' if default_mode == 'four-corners' else ''}>Assign all four corners individually</option><option value="uniform"{' selected' if default_mode == 'uniform' else ''}>This tile is uniform</option></select></label>
<label class="uniform-control">Uniform family<select class="uniform-family">{option_rows(all_families, default_uniform)}</select></label>
<div class="corner-controls">{corner_controls}</div>
<label>Comment<input class="note" placeholder="Optional comparison note or family detail"></label>
</div>
{variants}</article>''')

    style = '''
:root{color-scheme:dark}body{font:16px Segoe UI,Arial,sans-serif;background:#202124;color:#f1f3f4;margin:0;padding:18px}h1,h2,h3{font-weight:500}.lead{max-width:1300px;background:#292a2d;border-left:5px solid #8ab4f8;padding:12px}.toolbar{position:sticky;top:0;z-index:20;background:#17181b;border:1px solid #5f6368;padding:10px;display:flex;gap:8px;align-items:center;flex-wrap:wrap}.toolbar button{font:inherit;color:#fff;background:#303134;border:1px solid #777;border-radius:4px;padding:8px}.toolbar .primary{background:#1769aa}#export{width:min(1350px,96vw);height:115px;background:#111;color:#eee}.question{max-width:1450px;background:#292a2d;border:1px solid #5f6368;border-radius:8px;padding:14px;margin:20px 0}.reasons{color:#fbbc04}.decision{display:grid;grid-template-columns:repeat(auto-fit,minmax(260px,1fr));gap:10px;background:#1c1d20;border:1px solid #73767b;padding:12px}.decision label{display:grid;gap:5px}.decision select,.decision input{font:inherit;background:#303134;color:#fff;border:1px solid #777;padding:8px}.corner-controls{grid-column:1/-1;display:grid;grid-template-columns:repeat(2,minmax(250px,1fr));gap:8px}.corner-controls label{border:1px solid #686b70;padding:8px}.uniform-control.disabled,.corner-controls.disabled{opacity:.38}.variant{border-top:1px solid #666;margin-top:14px;padding-top:10px}.target{display:inline-grid;gap:5px}.target img{width:min(512px,88vw);image-rendering:auto}.variant details{margin:10px 0;background:#222326;border:1px solid #555;padding:8px}.variant summary{cursor:pointer;font-weight:600}.candidate-row{display:flex;gap:10px;overflow-x:auto;padding:8px}.candidate{flex:0 0 370px;background:#17181b;border:1px solid #5f6368;padding:8px}.candidate strong,.candidate span,.candidate small{display:block;margin-top:4px}.compare-pair{display:grid;grid-template-columns:1fr 1fr;gap:5px}.compare-pair img{display:block;width:100%;aspect-ratio:1;object-fit:contain;background:#000}.rotated.r90{transform:rotate(90deg)}.rotated.r180{transform:rotate(180deg)}.rotated.r270{transform:rotate(270deg)}@media(max-width:700px){.corner-controls{grid-template-columns:1fr}.candidate{flex-basis:300px}}
'''
    script = '''
const prefix='semantic-r87-combined:';
const questions=[...document.querySelectorAll('.question')],status=document.getElementById('status'),out=document.getElementById('export');
function defaults(q){return JSON.parse(q.dataset.defaults)}
function state(q){const corners={};for(const n of ['NW','NE','SE','SW'])corners[n]=q.querySelector('.corner-'+n.toLowerCase()).value;return{mode:q.querySelector('.mode').value,uniformFamily:q.querySelector('.uniform-family').value,corners,note:q.querySelector('.note').value.trim()}}
function changed(q,s){const d=defaults(q);if(s.note)return true;if(s.mode!==d.mode)return true;if(s.mode==='uniform')return s.uniformFamily!==d.uniformFamily;return ['NW','NE','SE','SW'].some(n=>s.corners[n]!==d.corners[n])}
function line(q,s){if(!changed(q,s))return '';let answer='Comment';if(s.mode==='uniform')answer='Uniform: '+s.uniformFamily;else if(['NW','NE','SE','SW'].some(n=>s.corners[n]!==defaults(q).corners[n]))answer='NW='+s.corners.NW+', NE='+s.corners.NE+', SE='+s.corners.SE+', SW='+s.corners.SW;return q.dataset.key+': '+answer+(s.note?' — '+s.note:'')}
function text(){const lines=['Semantic-atlas R87 combined corner review decisions:'];for(const q of questions){const row=line(q,state(q));if(row)lines.push(row)}lines.push('Apply only explicit decisions; verify exact connected corners before propagation.');return lines.join('\\n')}
function toggle(q){const uniform=q.querySelector('.mode').value==='uniform';q.querySelector('.uniform-control').classList.toggle('disabled',!uniform);q.querySelector('.corner-controls').classList.toggle('disabled',uniform);q.querySelector('.uniform-family').disabled=!uniform;for(const el of q.querySelectorAll('.corner'))el.disabled=uniform}
function update(message){out.value=text();const count=questions.filter(q=>changed(q,state(q))).length;status.textContent=message||count+' changed response'+(count===1?'':'s')}
function save(q){localStorage.setItem(prefix+q.dataset.key,JSON.stringify(state(q)));toggle(q);update('Saved locally')}
for(const q of questions){try{const s=JSON.parse(localStorage.getItem(prefix+q.dataset.key)||'null');if(s){q.querySelector('.mode').value=s.mode;q.querySelector('.uniform-family').value=s.uniformFamily;for(const n of ['NW','NE','SE','SW'])q.querySelector('.corner-'+n.toLowerCase()).value=s.corners[n];q.querySelector('.note').value=s.note||''}}catch(e){}for(const el of q.querySelectorAll('select,input'))el.addEventListener(el.tagName==='INPUT'?'input':'change',()=>save(q));toggle(q)}
function selectText(message){out.focus();out.select();update(message)}
document.getElementById('select').onclick=()=>selectText('Answer text selected');
document.getElementById('copy').onclick=async()=>{try{await navigator.clipboard.writeText(text());update('Changed answers copied')}catch(e){selectText('Clipboard unavailable; answer text selected')}};
document.getElementById('download').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text()],{type:'text/plain;charset=utf-8'}));a.download='semantic-atlas-r87-answers.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),0);update('Answers downloaded')};
document.getElementById('reset').onclick=()=>{for(const q of questions){localStorage.removeItem(prefix+q.dataset.key);const d=defaults(q);q.querySelector('.mode').value=d.mode;q.querySelector('.uniform-family').value=d.uniformFamily;for(const n of ['NW','NE','SE','SW'])q.querySelector('.corner-'+n.toLowerCase()).value=d.corners[n];q.querySelector('.note').value='';toggle(q)}update('Local answers reset')};
update();window.__SEMANTIC_R87_READY__={questions:questions.length,reviewCheckbox:false};
'''
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Semantic atlas R87 combined corner review</title><style>{style}</style></head><body>
<h1>Semantic atlas R87 — uniform or four-corner review</h1><p class="lead">Each questioned tile is resolved on this card. Choose <b>This tile is uniform</b> and a family, or assign its corners in their physical positions: NW/NE above SW/SE. Every family selector includes all families from this atlas plus audit candidates. Audit images are comparisons only: raw distance is lower-is-closer, not a probability, and nothing is applied automatically. HD and original evidence are shown separately. Untouched defaults are omitted from the exported answer text.</p>
<div class="toolbar"><button class="primary" id="copy">Copy changed answers</button><button id="download">Download answers.txt</button><button id="select">Select answer text</button><button id="reset">Reset local answers</button><span id="status"></span></div><textarea id="export" readonly></textarea>
<main>{''.join(cards)}</main><script>{script}</script></body></html>'''
    output.write_text(document, encoding="utf-8")
    base = output.parent
    missing = sorted(path for path in image_paths
                     if not (base / path).is_file())
    if missing:
        raise ValueError(f"missing questionnaire images: {missing[:10]}")
    if document.count('class="question"') != len(evidence["targets"]):
        raise ValueError("question count mismatch")
    if 'type="checkbox"' in document or 'class="reviewed"' in document:
        raise ValueError("separate review checkbox is forbidden")
    manifest = {
        "schema": "ei-semantic-atlas-r87-combined-questionnaire-v1",
        "questions": len(evidence["targets"]),
        "referencedImages": len(image_paths),
        "exactConnectedAlarmsOnTargetCards": sum(
            len(rows) for rows in alarms_by_target.values()),
        "evidenceSha256": sha256(evidence_path),
        "presetSha256": sha256(preset_path),
        "htmlSha256": sha256(output),
        "reviewCheckbox": False,
        "sameCardUniformOrFourCorners": True,
        "untouchedDefaultsOmitted": True,
    }
    manifest_path = output.with_suffix(".manifest.json")
    manifest_path.write_text(
        json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    return {"output": str(output), "manifest": str(manifest_path), **manifest}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--evidence", required=True, type=Path)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(render(args.evidence, args.presets, args.output), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
