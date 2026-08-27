#!/usr/bin/env python3
"""Render a compact review of reported three-/four-material junction masks.

The input manifest is emitted by modern_atlas_real_map_builder's
--issue-mask-review mode. Questions are grouped by mask style and topology,
never by physical atlas slot or semantic material family. Each question shows
up to four genuine reported applications, the exact current ownership input,
all full uniform sources, and the resulting current generated blend.
"""

from __future__ import annotations

import argparse
import html
import json
import os
from pathlib import Path

from render_blend_mask_questionnaire import interaction_script
from render_semantic_atlas_review import common_style


CHOICES = (
    "Keep current junction mask",
    "Replace via approved authored / EI-HD seam route",
    "Logic error: this should not be a junction mask",
    "Still uncertain",
)


def _relative(path: Path, output: Path) -> str:
    return Path(os.path.relpath(path, output)).as_posix()


def _corner_table(signature: dict) -> str:
    def material(corner: str) -> str:
        return html.escape(signature[corner]["material"])

    return f"""
<div class="corner-map" aria-label="Semantic corner assignment">
 <div><strong>NW</strong><span>{material('northWest')}</span></div>
 <div><strong>NE</strong><span>{material('northEast')}</span></div>
 <div><strong>SW</strong><span>{material('southWest')}</span></div>
 <div><strong>SE</strong><span>{material('southEast')}</span></div>
</div>"""


def _select_examples(examples: list[dict], limit: int = 4) -> list[dict]:
    """Prefer different owner materials, then different exact signatures."""
    ordered = sorted(
        examples,
        key=lambda row: (row["mark_id"], row["cell_y"], row["cell_x"],
                         row["material"].casefold()))
    selected: list[dict] = []
    used_materials: set[str] = set()
    used_signatures: set[tuple[int, ...]] = set()

    def signature_key(row: dict) -> tuple[int, ...]:
        signature = row["signature"]
        return tuple(signature[key]["material_id"] for key in (
            "northWest", "northEast", "southEast", "southWest"))

    for require_new_material, require_new_signature in (
            (True, True), (False, True), (False, False)):
        for row in ordered:
            if row in selected:
                continue
            sig = signature_key(row)
            if require_new_material and row["material"] in used_materials:
                continue
            if require_new_signature and sig in used_signatures:
                continue
            selected.append(row)
            used_materials.add(row["material"])
            used_signatures.add(sig)
            if len(selected) >= limit:
                return selected
    return selected


def render(manifest_path: Path, output: Path,
           checkpoint: str = "R99-JUNCTION") -> dict:
    manifest_path = manifest_path.resolve()
    root = manifest_path.parent
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    if manifest.get("schema") != "ei-junction-mask-issue-review-v1":
        raise ValueError("not an ei-junction-mask-issue-review-v1 manifest")
    output.mkdir(parents=True, exist_ok=True)
    full_examples = {row["id"]: row for row in manifest["examples"]}
    options = "".join(
        f'<option value="{html.escape(choice)}">{html.escape(choice)}</option>'
        for choice in CHOICES)
    cards: list[str] = []
    analysis: list[dict] = []
    missing: list[str] = []

    for index, group in enumerate(sorted(
            manifest["mask_groups"], key=lambda item: item["key"]), 1):
        shown = _select_examples(group["examples"])
        example_html: list[str] = []
        for row in shown:
            complete = full_examples[row["example_id"]]
            paths = [root / row["mask_file"], root / row["generated_file"]]
            paths.extend(root / component["uniform_file"]
                         for component in complete["components"])
            for path in paths:
                if not path.is_file():
                    missing.append(str(path))
            uniforms = "".join(f"""
<figure><a href="{html.escape(_relative(root / component['uniform_file'], output))}"><img src="{html.escape(_relative(root / component['uniform_file'], output))}" alt="Full uniform source for {html.escape(component['material'])}"></a><figcaption>Uniform: <strong>{html.escape(component['material'])}</strong></figcaption></figure>"""
                for component in complete["components"])
            example_html.append(f"""
<article class="application-example">
 <h3>Report {row['mark_id']} · cell {row['cell_x']},{row['cell_y']} · owner {html.escape(row['material'])}</h3>
 {_corner_table(row['signature'])}
 <div class="image-grid">
  <figure><a href="{html.escape(_relative(root / row['mask_file'], output))}"><img src="{html.escape(_relative(root / row['mask_file'], output))}" alt="Current {html.escape(group['style'])} {html.escape(group['topology'])} ownership mask"></a><figcaption><strong>Current ownership input</strong><br>{html.escape(row['provenance'])}</figcaption></figure>
  <figure><a href="{html.escape(_relative(root / row['generated_file'], output))}"><img src="{html.escape(_relative(root / row['generated_file'], output))}" alt="Current generated three or four material blend"></a><figcaption><strong>Resulting current blend</strong></figcaption></figure>
  {uniforms}
 </div>
</article>""")
        key = f"junction-mask-{group['key']}"
        cards.append(f"""
<section class="question" data-key="{html.escape(key)}" data-style="{html.escape(group['style'])}">
 <h2>{index}. {html.escape(group['style'])} · {html.escape(group['topology'])}</h2>
 <p>This is one shared current mathematical mask identity. It appears in {len(group['examples'])} provenance-qualified reported component application(s); {len(shown)} diverse examples are shown. White belongs to the named owner. The full generated result combines this with the other component masks.</p>
 <div class="examples">{''.join(example_html)}</div>
 <div class="answer-row"><label>Decision<select class="answer">{options}</select></label><label>Note<input class="note" type="text" value="" placeholder="What edge/ownership behavior is wrong?"></label></div>
</section>""")
        analysis.append({
            "key": key,
            "style": group["style"],
            "topology": group["topology"],
            "reportedComponentApplications": len(group["examples"]),
            "shownExamples": [row["example_id"] for row in shown],
            "default": CHOICES[0],
        })

    if missing:
        raise FileNotFoundError("missing questionnaire images:\n" +
                                "\n".join(sorted(set(missing))))
    script = interaction_script(checkpoint)
    style = common_style() + """
body{max-width:1800px;margin:0 auto}.question{margin:28px 0;padding:18px;border:1px solid #5f6368;border-radius:8px;background:#292a2d}.examples{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}.application-example{padding:12px;border:1px solid #5f6368;border-radius:6px;background:#202124}.image-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(170px,256px));gap:10px}.image-grid figure{margin:0}.image-grid img{display:block;width:256px;height:256px;max-width:100%;object-fit:contain;background:#161719}.image-grid figcaption{padding-top:5px;color:#bdc1c6}.corner-map{display:grid;grid-template-columns:repeat(2,minmax(120px,1fr));gap:4px;margin:8px 0 12px;font-size:12px}.corner-map div{display:grid;grid-template-columns:28px 1fr;gap:4px;padding:5px;background:#303134}.corner-map strong{color:#8ab4f8}.answer-row{display:flex;gap:12px;flex-wrap:wrap;margin-top:14px}.answer-row label{display:grid;gap:4px;min-width:360px}.export-panel{margin:12px 0;padding:12px;border:1px solid #5f6368;border-radius:6px}.export-panel textarea{display:block;width:100%;min-height:170px;margin-top:8px;box-sizing:border-box}.warning{border-left:4px solid #f9ab00;padding:10px 14px;background:#303134}@media(max-width:1050px){.examples{grid-template-columns:1fr}}@media(max-width:650px){.answer-row label{min-width:100%}.image-grid{grid-template-columns:1fr}}
"""
    document = f"""<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Blend-mask {html.escape(checkpoint)} review</title><style>{style}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused junction review</h1>
<p>{len(analysis)} shared style/topology questions cover all {manifest['selected_three_four_material_cells']} reported three-/four-material cells. They are grouped by mask family and corner topology, not atlas slot or semantic material name. Each question has at most four genuine reported applications.</p>
<p class="warning"><strong>Confirmed current behavior:</strong> these junctions bypass the R44–R77 approved authored masks and use the legacy mathematical fallback. Selecting the replacement route means preserving the authored interior, repairing shared A/B edge profiles with the EI-HD seam workflow, and validating multi-tile connections before runtime installation. It does not approve an unseen replacement.</p>
<p class="muted">Every displayed image crosses the atlas/engine Y boundary exactly once. Labels are visual NW NE / SW SE; serialized engine order remains NW,NE,SE,SW. Uniform images are full 256×256 source tiles. Semantic preset SHA-256: {html.escape(manifest['semantic_preset_sha256'])}.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status" class="muted"></span></div>
<section class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea></section>
<main>{''.join(cards)}</main><script>{script}</script></body></html>"""
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis_path = output / "junction-mask-analysis.json"
    analysis_path.write_text(json.dumps({
        "schema": "ei-junction-mask-questionnaire-v1",
        "checkpoint": checkpoint,
        "issueReportSha256": manifest["issue_report_sha256"],
        "semanticPresetSha256": manifest["semantic_preset_sha256"],
        "reportedThreeFourMaterialCells":
            manifest["selected_three_four_material_cells"],
        "maskQuestionCount": len(analysis),
        "questions": analysis,
        "unreviewedSeparateSets": {
            "uniformSignatureCells":
                manifest.get("selected_uniform_signature_cells", 0),
            "twoMaterialCells":
                manifest.get("selected_two_material_cells", 0),
            "meaning": "Kept out of this junction-mask questionnaire; they require logic or two-material review and are not evidence for a 3/4-material mask.",
        },
    }, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {
        "questions": len(analysis),
        "reportedThreeFourMaterialCells":
            manifest["selected_three_four_material_cells"],
        "html": str(html_path),
        "analysis": str(analysis_path),
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--checkpoint", default="R99-JUNCTION")
    args = parser.parse_args()
    print(json.dumps(render(args.manifest, args.output, args.checkpoint),
                     indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
