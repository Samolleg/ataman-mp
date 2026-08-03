#!/usr/bin/env python3
"""Render a durable review of nominally uniform paint variants for one family."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

from PIL import Image

from audit_terrain_atlas_presets import signatures
from generate_terrain_atlas_presets import editor_tiles, res_index
from render_pending_family_questionnaire import questionnaire_interaction_script
from render_semantic_atlas_review import common_style


def render(presets_path: Path, archive: Path, namespace: str,
           family_name: str, output: Path, checkpoint: str) -> dict:
    catalog = json.loads(presets_path.read_text(encoding="utf-8"))
    preset = next(
        item for item in catalog["presets"]
        if item["namespace"].casefold() == namespace.casefold())
    family = next(
        item for item in preset["families"]
        if item["name"].casefold() == family_name.casefold())
    family_id = int(family["id"])
    table = signatures(preset)
    slots = [
        slot for slot, signature in enumerate(table)
        if signature == (family_id, family_id, family_id, family_id)
    ]
    if not slots:
        raise ValueError(f"{preset['namespace']} {family['name']} has no nominal uniform slots")

    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)
    tiles = editor_tiles(
        archive, res_index(archive), preset["namespace"],
        int(preset["slotCount"]) // 64)
    current = min(slots)
    cards = []
    for slot in slots:
        name = f"{preset['namespace'].lower()}-slot-{slot}.jpg"
        Image.fromarray(tiles[slot][:, :, :3], "RGB").resize(
            (640, 640), Image.Resampling.NEAREST).save(
                images / name, quality=95, subsampling=0, optimize=True)
        if slot == current:
            values = [
                "Keep included: current representative",
                "Exclude: decorative, mixed, or unsuitable",
                "Different family (note)",
            ]
        else:
            values = [
                "Keep excluded pending review",
                "Include as safe uniform paint variant",
                "Exclude: decorative, mixed, or unsuitable",
                "Different family (note)",
            ]
        options = "".join(
            f'<option value="{html.escape(value)}">{html.escape(value)}</option>'
            for value in values)
        key = f"{preset['namespace'].lower()}-{family_id}-uniform-slot-{slot}"
        cards.append(f'''<section class="variant-card" data-key="{key}">
<h2>{html.escape(preset["namespace"])} slot {slot}</h2>
<img src="images/{name}" alt="{html.escape(family["name"])} nominal uniform slot {slot}">
<label>Paint authority <select class="answer">{options}</select></label>
<label>Comment <textarea class="note" rows="3" placeholder="Optional: describe visual variation, mixed material, or duplicate"></textarea></label>
</section>''')

    script = questionnaire_interaction_script(checkpoint)
    style = common_style() + '''
.variant-grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:18px}
.variant-card{background:#202124;border:1px solid #5f6368;border-radius:7px;padding:12px}
.variant-card img{display:block;width:100%;image-rendering:auto;margin-bottom:10px}
.variant-card label,.variant-card select,.variant-card textarea{display:block;width:100%;box-sizing:border-box}
.variant-card label{margin-top:8px}.variant-card select,.variant-card textarea{margin-top:5px}
.export-panel textarea{width:100%;min-height:160px}
'''
    document = f'''<!doctype html><html><head><meta charset="utf-8"><title>{html.escape(checkpoint)} uniform variant review</title><style>{style}</style></head><body>
<h1>{html.escape(checkpoint)}: {html.escape(preset["namespace"])} {html.escape(family["name"])} paint variants</h1>
<p class="muted">These {len(slots)} tiles currently have four {html.escape(family["name"])} corner labels. That does not prove that every tile is a safe automatic interior variant. Slot {current} is the current lowest-slot representative; every other candidate remains excluded until explicitly approved. Review complete tiles, not isolated corners.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button> <span id="status" class="muted"></span></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly></textarea></section>
<main class="variant-grid">{"".join(cards)}</main><script>{script}</script></body></html>'''
    path = output / f"semantic-atlas-{checkpoint.lower()}-uniform-variants.html"
    path.write_text(document, encoding="utf-8")
    required = ("submit-answers", "copy-answers", "download-answers",
                "answer-export-text", "localStorage.setItem",
                "sendFollowUpMessage")
    missing = [value for value in required if value not in document]
    if missing:
        raise ValueError(f"questionnaire controls missing: {missing}")
    if document.count('class="variant-card"') != len(slots):
        raise ValueError("not every nominal uniform slot produced one card")
    return {"namespace": preset["namespace"], "family": family["name"],
            "nominalUniformSlots": slots, "currentRepresentative": current,
            "questions": len(slots), "html": str(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--archive", required=True, type=Path)
    parser.add_argument("--namespace", required=True)
    parser.add_argument("--family", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R79")
    args = parser.parse_args()
    print(json.dumps(render(
        args.presets, args.archive, args.namespace, args.family,
        args.output, args.checkpoint), indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
