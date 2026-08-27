#!/usr/bin/env python3
"""Render the consolidated R100 authored-tile questionnaire.

This renderer intentionally reuses the locked R92 comparison primitives.  It
shows one four-corner card per provenance-qualified authored source and never
uses any questioned source as comparison evidence elsewhere in the page.
"""

from __future__ import annotations

import argparse
import base64
import hashlib
import html
import json
from pathlib import Path

from render_r92_complete_questionnaire import (
    CORNER_ORDER, DISPLAY_ORDER, Renderer, esc,
)


def sha256(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            value.update(block)
    return value.hexdigest().upper()


class ConsolidatedRenderer(Renderer):
    def __init__(self, queue: Path, *args, checkpoint: str = "r100",
                 **kwargs) -> None:
        super().__init__(*args, **kwargs)
        self.queue_path = queue
        self.queue = json.loads(queue.read_text(encoding="utf-8"))
        self.checkpoint = checkpoint.lower()

    def support_reference_specs(self, family_name: str, option: str,
                                preferred_namespace: str) -> list[dict]:
        """Find a visible mapped-corner fallback when no uniform exists."""
        presets = list(self.catalog["presets"])
        presets.sort(key=lambda row: (
            row["namespace"].casefold() != preferred_namespace.casefold(),
            row["namespace"].casefold()))
        for preset in presets:
            family = next((row for row in preset["families"]
                           if row["name"] == family_name), None)
            if family is None:
                continue
            family_id = int(family["id"])
            signatures = base64.b64decode(preset["signaturesBase64"])
            for slot in range(int(preset["slotCount"])):
                source = f"{preset['namespace'].casefold()}:{slot}:hd"
                if source in self.questioned_sources:
                    continue
                signature = list(signatures[slot * 4:slot * 4 + 4])
                if family_id not in signature:
                    continue
                try:
                    self.atlas_tiles(preset["namespace"], "hd")[slot]
                    self.atlas_tiles(preset["namespace"], "original")[slot]
                except (KeyError, IndexError):
                    continue
                return [{"namespace": preset["namespace"], "slot": slot,
                         "rotation": 0, "title": family_name,
                         "option": option}]
        return []

    def family_specs(self, family_name: str, namespace: str) -> list[dict]:
        specs = self.family_reference_specs(
            family_name, family_name, limit=2,
            preferred_namespace=namespace)
        if specs:
            return specs
        return self.support_reference_specs(family_name, family_name, namespace)

    def candidate_families(self, namespace: str, slot: int) -> tuple[list[str], dict[str, list[dict]]]:
        preset = self.by_namespace[namespace.casefold()]
        signatures = base64.b64decode(preset["signaturesBase64"])
        family_by_id = {int(row["id"]): row for row in preset["families"]}
        signature = list(signatures[slot * 4:slot * 4 + 4])
        current_names = [family_by_id[value]["name"] for value in signature]
        layers = {layer for value in signature
                  for layer in family_by_id[value].get("allowedLayers", [])}
        names = list(dict.fromkeys(current_names + sorted(
            (row["name"] for row in preset["families"]
             if layers.intersection(row.get("allowedLayers", []))),
            key=str.casefold)))
        refs: dict[str, list[dict]] = {}
        for name in names:
            specs = self.family_specs(name, namespace)
            if specs:
                refs[name] = specs
        # A concrete option without visible HD and original evidence is not
        # offered.  The escape options remain available for such identities.
        options = [name for name in names if name in refs]
        for current in current_names:
            if current not in options:
                # Keep-current is an explicit non-concrete choice when every
                # available anchor for the current label is questioned.  The
                # current family name remains visible on the target signature,
                # but is not laundered into trusted evidence or a concrete
                # option without an HD+original reference row.
                continue
        return options, refs

    def add_source_card(self, record: dict) -> None:
        namespace = record["namespace"]
        slot = int(record["slot"])
        current = self.corner_families(namespace, slot)
        options, refs_by_family = self.candidate_families(namespace, slot)
        flat_refs = []
        seen = set()
        for name in options:
            for spec in refs_by_family[name]:
                key = (spec["namespace"].casefold(), int(spec["slot"]), name)
                if key in seen:
                    continue
                seen.add(key)
                flat_refs.append(spec)

        body = self.comparison_rows(
            namespace, slot, flat_refs,
            f"Target {namespace} slot {slot}",
            prefix=f"{self.checkpoint}-{namespace.lower()}-{slot}")
        controls = []
        for corner in DISPLAY_ORDER:
            default = (current[corner] if current[corner] in options
                       else "Keep current assignment")
            values = options + [
                "Keep current assignment",
                "Different existing family (describe in comment)",
                "New distinct family (name in comment)",
                "Still uncertain",
            ]
            controls.append(
                f'<label>{corner} assignment<select class="answer" '
                f'data-field="{corner}" data-current-option="{esc(default)}">'
                f'{self.options(values, default)}</select></label>')
        uniform_options = "".join(
            f'<option value="{esc(name)}">{esc(name)}</option>' for name in options)
        controls.insert(0,
            '<div class="set-uniform"><label>Set all four corners '
            f'<select class="uniform-choice">{uniform_options}</select></label>'
            '<button type="button" class="set-all">Apply to NW/NE/SW/SE</button></div>')
        corroboration = record.get("corroboration", "")
        summary = record["reason"]
        if corroboration:
            summary += f" Corroboration: {corroboration}."
        self.add_card(
            f"{self.checkpoint}-{namespace.lower()}-slot{slot}",
            f"{namespace} slot {slot} - all four corners",
            summary, body, "".join(controls), current)

    def render(self) -> dict:
        items = self.queue["authoredItems"]
        self.questioned_sources = {
            f"{row['namespace'].casefold()}:{int(row['slot'])}:{variant}"
            for row in items for variant in ("hd", "original")
        }
        for record in items:
            self.add_source_card(record)

        style = """
:root{color-scheme:dark}*{box-sizing:border-box}body{margin:0;padding:18px;background:#202124;color:#f1f3f4;font:16px Segoe UI,Arial,sans-serif}h1,h2,h3{font-weight:500}.lead{max-width:1600px;border-left:5px solid #fbbc04;background:#292a2d;padding:12px}.toolbar{position:sticky;top:0;z-index:20;display:flex;gap:8px;align-items:center;flex-wrap:wrap;background:#17181b;border:1px solid #666;padding:10px}.toolbar button,.set-all{font:inherit;color:#fff;background:#303134;border:1px solid #777;padding:8px;border-radius:4px}.toolbar .primary{background:#1769aa}#export{width:min(1500px,97vw);height:150px;background:#111;color:#eee}.question{max-width:1800px;margin:22px 0;padding:14px;background:#292a2d;border:1px solid #666;border-radius:8px}.summary{color:#fbbc04}.comparison-row{display:flex;gap:12px;overflow-x:auto;align-items:flex-start;padding-bottom:8px}.tile{flex:0 0 280px;margin:0;padding:8px;background:#17181b;border:1px solid #5f6368}.tile.target{border:3px solid #e57373}.tile-image{position:relative;width:256px;height:256px}.tile img{display:block;width:256px;height:256px;object-fit:contain;background:#000}.tile figcaption span{display:block;margin-top:3px}.controls{display:grid;grid-template-columns:1fr 1fr;gap:10px;max-width:1500px}.controls label{display:grid;gap:6px}.answer,.note,.uniform-choice{font:inherit;color:#fff;background:#303134;border:1px solid #777;padding:8px}.comment{display:grid;gap:6px;max-width:1500px;margin-top:12px}.note{min-height:70px}.set-uniform{grid-column:1/-1;display:flex;gap:8px;align-items:end;padding:10px;background:#222326;border:1px solid #666}.set-uniform label{flex:1}.set-all{white-space:nowrap}@media(max-width:700px){.tile{flex-basis:216px}.tile-image,.tile img{width:192px;height:192px}.controls{grid-template-columns:1fr}.set-uniform{grid-column:1}}
"""
        script = r"""
const prefix='semantic-CHECKPOINT-consolidated:';const qs=[...document.querySelectorAll('.question')],out=document.getElementById('export'),status=document.getElementById('status');
function controls(q){return[...q.querySelectorAll('.answer')]}function defaults(q){return JSON.parse(q.dataset.defaults)}function state(q){const values={};for(const el of controls(q))values[el.dataset.field]=el.value;return{values,note:q.querySelector('.note').value.trim()}}function changed(q,s){const d=defaults(q);return Object.keys(s.values).some(k=>s.values[k]!==d[k])||!!s.note}function line(q,s){const order=['NW','NE','SE','SW'];const fields=order.filter(k=>k in s.values).map(k=>k+'='+s.values[k]);return q.dataset.key+': '+fields.join(', ')+(s.note?' - '+s.note:'')}function text(){return['Semantic-atlas CHECKPOINT consolidated review decisions:',...qs.map(q=>[q,state(q)]).filter(([q,s])=>changed(q,s)).map(([q,s])=>line(q,s)),'Apply only explicit changed decisions; untouched defaults remain context, not confirmations.'].join('\n')}function update(m){out.value=text();status.textContent=m||qs.filter(q=>changed(q,state(q))).length+' changed response(s)'}function save(q){try{localStorage.setItem(prefix+q.dataset.key,JSON.stringify(state(q)))}catch(e){}update('Saved locally')}for(const q of qs){try{const s=JSON.parse(localStorage.getItem(prefix+q.dataset.key)||'null');if(s){for(const el of controls(q))if(s.values&&s.values[el.dataset.field])el.value=s.values[el.dataset.field];q.querySelector('.note').value=s.note||''}}catch(e){}for(const el of q.querySelectorAll('select.answer,textarea'))el.addEventListener(el.tagName==='TEXTAREA'?'input':'change',()=>save(q));q.querySelector('.set-all').onclick=()=>{const value=q.querySelector('.uniform-choice').value;for(const el of controls(q))el.value=value;save(q)}}function selectText(m){out.focus();out.select();update(m)}document.getElementById('select').onclick=()=>selectText('Answer text selected');document.getElementById('copy').onclick=async()=>{try{await navigator.clipboard.writeText(text());update('Answers copied')}catch(e){selectText('Clipboard unavailable; answer text selected')}};document.getElementById('download').onclick=()=>{const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([text()],{type:'text/plain;charset=utf-8'}));a.download='semantic-atlas-CHECKPOINT-answers.txt';a.click();setTimeout(()=>URL.revokeObjectURL(a.href),0);update('Answers downloaded')};document.getElementById('submit').onclick=async()=>{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){await window.openai.sendFollowUpMessage({prompt:text(),title:'Submit semantic-atlas CHECKPOINT review'});update('Responses submitted')}else selectText('Direct submission unavailable; answer text selected')};document.getElementById('reset').onclick=()=>{for(const q of qs){localStorage.removeItem(prefix+q.dataset.key);const d=defaults(q);for(const el of controls(q))el.value=d[el.dataset.field];q.querySelector('.note').value=''}update('Local answers reset')};update();window.__SEMANTIC_READY__={checkpoint:'CHECKPOINT',questions:qs.length,allImages256:true,displayOrder:['NW','NE','SW','SE'],serializationOrder:['NW','NE','SE','SW']};
"""
        script = script.replace("CHECKPOINT", self.checkpoint.upper())
        label = self.checkpoint.upper()
        document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Semantic atlas {label} consolidated review</title><style>{style}</style></head><body data-questionnaire-contract="semantic-atlas-questionnaire-v1"><h1>Semantic atlas {label} - residual authored-tile review</h1><p class="lead">All {len(items)} pending provenance-qualified authored sources. Every card reviews all four corners now. Every concrete option has visible HD and original evidence at identical displayed scale. Questioned tiles are excluded from every reference pool. Untouched defaults are not exported.</p><div class="toolbar"><button class="primary" id="copy">Copy changed answers</button><button id="download">Download answers.txt</button><button id="select">Select answer text</button><button id="submit">Submit answers</button><button id="reset">Reset local answers</button><span id="status"></span></div><textarea id="export" readonly></textarea><main>{''.join(self.cards)}</main><script>{script}</script></body></html>'''
        self.output.parent.mkdir(parents=True, exist_ok=True)
        self.output.write_text(document, encoding="utf-8")
        manifest = {
            "schema": f"ei-semantic-atlas-{self.checkpoint}-consolidated-questionnaire-v1",
            "questionnaireContract": "semantic-atlas-questionnaire-v1",
            "questions": len(self.cards),
            "questionKeys": self.question_keys,
            "markedItemKeys": self.question_keys,
            "resolvedMarkedItemKeys": [],
            "displayOrder": list(DISPLAY_ORDER),
            "serializationOrder": list(CORNER_ORDER),
            "browserValidation": {"status": "not-run", "requiredForHandoff": True},
            "questionedSources": sorted(self.questioned_sources),
            "evidenceGaps": self.queue.get("evidenceGaps", []),
            "htmlSha256": sha256(self.output),
            "queueSha256": sha256(self.queue_path),
            "copiedOrExtractedImages": len(list(self.images.glob("*.png"))),
        }
        self.output.with_suffix(".manifest.json").write_text(
            json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
        return manifest


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--queue", required=True, type=Path)
    parser.add_argument("--r90-audit", required=True, type=Path)
    parser.add_argument("--r72-audit", required=True, type=Path)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--hd-archive", required=True, type=Path)
    parser.add_argument("--original-archive", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="r100")
    args = parser.parse_args()
    renderer = ConsolidatedRenderer(
        args.queue, args.r90_audit, args.r72_audit, args.presets,
        args.hd_archive, args.original_archive, args.output,
        checkpoint=args.checkpoint)
    print(json.dumps(renderer.render(), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
