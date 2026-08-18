#!/usr/bin/env python3
"""Render a durable four-corner questionnaire from paint-issue reports."""

from __future__ import annotations

import argparse
from collections import defaultdict
import html
import json
from pathlib import Path

from PIL import Image

from audit_terrain_atlas_presets import CORNER_NAMES, signatures
from generate_terrain_atlas_presets import editor_tiles, res_index
from render_pending_family_questionnaire import questionnaire_interaction_script
from render_semantic_atlas_review import common_style


def save_tile(images: Path, namespace: str, slot: int, tile) -> str:
    name = f"{namespace.lower()}-slot-{slot}.jpg"
    path = images / name
    if not path.exists():
        Image.fromarray(tile[:, :, :3], "RGB").resize(
            (640, 640), Image.Resampling.NEAREST).save(
                path, quality=95, subsampling=0, optimize=True)
    return f"images/{name}"


def marked_slots(report_dir: Path) -> tuple[dict, dict]:
    result: dict[tuple[str, int], list[dict]] = defaultdict(list)
    report_names: dict[str, str] = {}
    for path in sorted(report_dir.glob("*-paint-issues.json")):
        report = json.loads(path.read_text(encoding="utf-8"))
        namespace = report["mprNamespace"]
        report_names[namespace.lower()] = namespace
        for mark in report.get("marks", []):
            for cell in mark.get("tileSnapshot", []):
                if not cell.get("insideMark"):
                    continue
                slot = int(cell["packed"]["atlasSlot"])
                result[(namespace.lower(), slot)].append({
                    "category": mark.get("category", "unknown"),
                    "markId": int(mark.get("id", -1)),
                    "x": int(cell["x"]), "y": int(cell["y"]),
                    "rotation": int(cell["packed"].get("rotation", 0)),
                })
    return result, report_names


def family_prefix(name: str) -> str:
    return name.split(" / ", 1)[0]


def render(presets_path: Path, hd_archive: Path, report_dir: Path,
           output: Path, checkpoint: str) -> dict:
    catalog = json.loads(presets_path.read_text(encoding="utf-8"))
    presets = {p["namespace"].lower(): p for p in catalog["presets"]}
    marked, report_names = marked_slots(report_dir)
    missing = sorted({namespace for namespace, _ in marked if namespace not in presets})
    if missing:
        raise ValueError(f"reports have no preset: {missing}")

    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)
    archive_index = res_index(hd_archive)
    tile_cache = {}
    signature_cache = {}

    def tiles(namespace: str):
        if namespace not in tile_cache:
            preset = presets[namespace]
            tile_cache[namespace] = editor_tiles(
                hd_archive, archive_index, preset["namespace"],
                int(preset["slotCount"]) // 64)
        return tile_cache[namespace]

    def table(namespace: str):
        if namespace not in signature_cache:
            signature_cache[namespace] = signatures(presets[namespace])
        return signature_cache[namespace]

    cards = []
    analysis = []
    for number, ((namespace, slot), occurrences) in enumerate(sorted(marked.items()), 1):
        preset = presets[namespace]
        families = {int(f["id"]): f for f in preset["families"]}
        signature = table(namespace)[slot]
        family_names = [families[value]["name"] for value in signature]
        prefixes = {family_prefix(name) for name in family_names}
        signature_layers = {
            layer for value in signature
            for layer in families[value].get("allowedLayers", [])}
        sibling_ids = [
            family_id for family_id, family in families.items()
            if signature_layers.intersection(family.get("allowedLayers", []))]
        # Put the visually likely sibling class first, but retain every
        # same-layer family. Cross-category mistakes such as a stone anchor in
        # a ground brush are otherwise almost impossible to recognize.
        sibling_ids.sort(key=lambda value: (
            family_prefix(families[value]["name"]) not in prefixes,
            families[value]["name"].lower()))
        uniform_by_family: dict[int, list[int]] = defaultdict(list)
        for candidate_slot, candidate_signature in enumerate(table(namespace)):
            if len(set(candidate_signature)) == 1 and candidate_signature[0] in sibling_ids:
                uniform_by_family[candidate_signature[0]].append(candidate_slot)

        target_image = save_tile(images, namespace, slot, tiles(namespace)[slot])
        reference_html = []
        references = []
        for family_id in sibling_ids:
            family = families[family_id]
            slots = uniform_by_family.get(family_id, [])[:2]
            kind = "uniform"
            if not slots:
                if family_id not in signature:
                    continue
                slots = [int(family["thumbnailSlot"])]
                kind = "thumbnail/stamp (no uniform tile)"
            for reference_slot in slots:
                reference_image = save_tile(
                    images, namespace, reference_slot, tiles(namespace)[reference_slot])
                reference_html.append(
                    f'<div class="reference"><img src="{reference_image}" '
                    f'alt="{html.escape(family["name"])}"><strong>{html.escape(family["name"])}</strong>'
                    f'<span>{html.escape(preset["namespace"])} slot {reference_slot}; {kind}</span></div>')
                references.append({"family": family["name"], "slot": reference_slot,
                                   "kind": kind})

        category_options = [families[value]["name"] for value in sibling_ids]
        all_options = sorted({f["name"] for f in families.values()}, key=str.lower)
        corner_controls = []
        for corner, current_name in zip(CORNER_NAMES, family_names):
            ordered = [current_name] + [name for name in category_options if name != current_name]
            ordered += [name for name in all_options if name not in ordered]
            options = [f'<option value="Keep current: {html.escape(current_name)}">Keep current: {html.escape(current_name)}</option>']
            options += [f'<option value="{html.escape(name)}">{html.escape(name)}</option>'
                        for name in ordered if name != current_name]
            options += ['<option value="Different family (note)">Different family (note)</option>',
                        '<option value="Still uncertain">Still uncertain</option>']
            key = f'{preset["namespace"].lower()}-slot{slot}-{corner.lower()}'
            corner_controls.append(
                f'<div class="corner-answer" data-key="{key}"><label><strong>{corner}</strong> '
                f'<span class="current">current: {html.escape(current_name)}</span>'
                f'<select class="answer">{"".join(options)}</select></label>'
                '<label>Note <input class="note" type="text" placeholder="Optional detail"></label></div>')

        categories = sorted({item["category"] for item in occurrences})
        cells = sorted({(item["x"], item["y"], item["rotation"]) for item in occurrences})
        comparison_key = f'{preset["namespace"].lower()}-slot{slot}-comparison-comment'
        cards.append(f'''<section class="card marked-card">
<h2>{number}. {html.escape(preset["namespace"])} slot {slot}</h2>
<p class="muted">Marked as {html.escape(", ".join(categories))}; {len(cells)} distinct map occurrence(s). Current signature: {html.escape(" | ".join(f"{c}: {n}" for c, n in zip(CORNER_NAMES, family_names)))}</p>
<div class="review-layout"><div class="target"><div class="tile target-tile"><img src="{target_image}" alt="{html.escape(preset["namespace"])} slot {slot}"><span class="cross-v"></span><span class="cross-h"></span><b class="label nw">NW</b><b class="label ne">NE</b><b class="label se">SE</b><b class="label sw">SW</b></div>{"".join(corner_controls)}</div>
<div><h3>Same-atlas family comparisons</h3><p class="muted">Likely sibling class first, followed by every other same-layer uniform family.</p><div class="reference-grid">{"".join(reference_html)}</div></div></div>
<div class="comparison-comment" data-key="{comparison_key}"><select class="answer" aria-hidden="true" tabindex="-1"><option value=""></option></select><label><strong>Comparison-tile comments</strong><textarea class="note" rows="3" placeholder="Optional: note a wrong, suspicious, duplicate, or misleading comparison tile/family"></textarea></label></div>
</section>''')
        analysis.append({"namespace": preset["namespace"], "slot": slot,
                         "currentSignature": dict(zip(CORNER_NAMES, family_names)),
                         "categories": categories, "occurrences": occurrences,
                         "references": references})

    script = questionnaire_interaction_script(checkpoint)
    style = common_style() + '''
.marked-card{margin:22px 0}.review-layout{display:grid;grid-template-columns:minmax(360px,680px) 1fr;gap:22px}.target-tile{width:min(640px,100%)}
.cross-v,.cross-h{position:absolute;background:#ff4545;pointer-events:none}.cross-v{left:50%;top:0;width:3px;height:100%}.cross-h{top:50%;left:0;height:3px;width:100%}.label{position:absolute;background:#b00020;color:#fff;padding:4px 7px}.nw{left:4px;top:4px}.ne{right:4px;top:4px}.se{right:4px;bottom:4px}.sw{left:4px;bottom:4px}
.reference-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}.reference{background:#202124;border:1px solid #5f6368;padding:7px}.reference img{width:100%;display:block}.reference strong,.reference span{display:block}.reference span{font-size:12px;color:#bdc1c6}.corner-answer{display:grid;grid-template-columns:1fr 1fr;gap:8px}.corner-answer label{display:grid;gap:4px}.current{color:#fbbc04;margin-left:8px}.comparison-comment{margin-top:16px;padding:12px;background:#252629;border:1px solid #5f6368;border-radius:6px}.comparison-comment .answer{display:none}.comparison-comment label,.comparison-comment textarea{display:block;width:100%;box-sizing:border-box}.comparison-comment textarea{margin-top:7px;resize:vertical}.export-panel textarea{width:100%;min-height:160px}@media(max-width:1000px){.review-layout{grid-template-columns:1fr}}
'''
    document = f'''<!doctype html><html><head><meta charset="utf-8"><title>Semantic atlas {checkpoint} marked-tile review</title><style>{style}</style></head><body>
<h1>Semantic atlas {checkpoint}: marked authored tiles</h1>
<p class="muted">{len(cards)} distinct marked tiles from {len(report_names)} atlases. Review all four corners independently. Each card shows every same-layer uniform family anchor in the same atlas (up to two tiles per family), with likely siblings first; a labelled thumbnail/stamp is used only when a currently assigned family has no uniform authored tile. Answers save locally as you make them.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button> <span id="status" class="muted"></span></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section>
<main>{"".join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"semantic-atlas-{checkpoint.lower()}-marked-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    (output / "marked-tile-analysis.json").write_text(
        json.dumps({"checkpoint": checkpoint, "tiles": analysis}, indent=2,
                   ensure_ascii=False) + "\n", encoding="utf-8")
    required = ("submit-answers", "copy-answers", "download-answers",
                "answer-export-text", "localStorage.setItem", "sendFollowUpMessage")
    absent = [value for value in required if value not in document]
    if absent:
        raise ValueError(f"questionnaire controls missing: {absent}")
    if document.count('class="card marked-card"') != len(marked):
        raise ValueError("not every marked tile produced one review card")
    if document.count('class="corner-answer"') != len(marked) * 4:
        raise ValueError("not every marked tile produced four corner controls")
    if document.count('class="comparison-comment"') != len(marked):
        raise ValueError("not every marked tile produced one comparison comment")
    return {"markedTiles": len(marked), "cornerQuestions": len(marked) * 4,
            "atlases": len(report_names), "images": len(list(images.glob("*"))),
            "html": str(html_path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--hd", required=True, type=Path)
    parser.add_argument("--reports", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R69")
    args = parser.parse_args()
    print(json.dumps(render(args.presets, args.hd, args.reports,
                            args.output, args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
