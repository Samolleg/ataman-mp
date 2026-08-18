#!/usr/bin/env python3
"""Render a high-resolution family catalog and a reviewed-anchor questionnaire."""

from __future__ import annotations

import argparse
import html
import json
from collections import defaultdict
from pathlib import Path

import numpy as np
from PIL import Image

from audit_terrain_atlas_presets import (
    CORNER_NAMES, corner_patch, feature, ranked_anchor_candidates, signatures,
)
from generate_terrain_atlas_presets import editor_tiles, res_index


def review_document_errors(catalog_document: str,
                           questionnaire_document: str,
                           expected_families: int) -> list[str]:
    """Validate that every requested review item has its interactive evidence."""
    errors = []
    comment_fields = catalog_document.count('class="family-comment"')
    if comment_fields != expected_families:
        errors.append(
            f"family comments {comment_fields} != families {expected_families}")
    for marker, message in (
            ('id="submit-comments"', "family comment submit is missing"),
            ('id="show-comments"', "family comment export is missing"),
            ('id="comment-export-text"', "family comment export panel is missing"),
            ('sendFollowUpMessage', "family comment direct submit is missing"),
            ('localStorage.setItem', "family comment persistence is missing")):
        if marker not in catalog_document:
            errors.append(message)
    for marker, message in (
            ('id="submit-answers"', "questionnaire submit is missing"),
            ('id="show-answers"', "questionnaire export is missing"),
            ('id="answer-export-text"', "questionnaire export panel is missing"),
            ('sendFollowUpMessage', "questionnaire direct submit is missing"),
            ('localStorage.setItem', "questionnaire persistence is missing")):
        if questionnaire_document and marker not in questionnaire_document:
            errors.append(message)
    family_questions = questionnaire_document.count(
        'class="corner-answer family-question"')
    family_visuals = questionnaire_document.count('class="compare-grid"')
    if family_questions != family_visuals:
        errors.append(
            f"family-question visuals {family_visuals} != questions {family_questions}")
    focused_questions = questionnaire_document.count(
        'data-follow-up="common-gravel"')
    if focused_questions:
        hd_variants = questionnaire_document.count('data-variant="hd"')
        original_variants = questionnaire_document.count(
            'data-variant="original"')
        if hd_variants != focused_questions:
            errors.append(
                f"HD comparison variants {hd_variants} != focused questions {focused_questions}")
        if original_variants != focused_questions:
            errors.append(
                f"original comparison variants {original_variants} != focused questions {focused_questions}")
    return errors


def save_tile(images: Path, namespace: str, slot: int, tile: np.ndarray) -> str:
    name = f"{namespace.lower()}-slot-{slot}.jpg"
    path = images / name
    if not path.exists():
        Image.fromarray(tile[:, :, :3], "RGB").resize(
            (512, 512), Image.Resampling.NEAREST).save(
                path, quality=94, subsampling=0, optimize=True)
    return f"images/{name}"


def save_corner_crop(images: Path, source: str, namespace: str, slot: int,
                     corner: int, tile: np.ndarray) -> str:
    """Save an enlarged visual quadrant for human corner comparison."""
    name = f"{source}-{namespace.lower()}-slot-{slot}-{CORNER_NAMES[corner].lower()}.png"
    path = images / name
    height, width = tile.shape[:2]
    half_h, half_w = height // 2, width // 2
    top = 0 if corner in (0, 1) else height - half_h
    left = 0 if corner in (0, 3) else width - half_w
    crop = tile[top:top + half_h, left:left + half_w, :3]
    Image.fromarray(crop, "RGB").resize(
        (512, 512), Image.Resampling.NEAREST).save(path, optimize=True)
    return f"images/{name}"


def pending_review_namespaces(namespaces: list[str],
                              reviewed_through: str | None) -> list[str]:
    """Return the catalog suffix after an explicitly completed namespace."""
    if not reviewed_through:
        return list(namespaces)
    try:
        completed_index = namespaces.index(reviewed_through)
    except ValueError as error:
        raise ValueError(
            f"reviewed-through namespace {reviewed_through!r} is not in catalog") \
            from error
    return namespaces[completed_index + 1:]


def corner_class(corner: str) -> str:
    return "corner-" + corner.lower()


def common_style() -> str:
    return """
body{font-family:Segoe UI,Arial,sans-serif;background:#202124;color:#f1f3f4;margin:0;padding:20px}
h1,h2,h3{font-weight:500}.muted{color:#bdc1c6}.toolbar{position:sticky;top:0;background:#202124;padding:10px 0;z-index:4}
input,select,textarea,button{font:inherit;background:#303134;color:#f1f3f4;border:1px solid #5f6368;border-radius:4px;padding:8px}
button{cursor:pointer}.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:18px}
.card{border:1px solid #5f6368;border-radius:8px;padding:14px;background:#292a2d}.tile{position:relative;display:inline-block;max-width:100%}
.tile img{display:block;width:100%;height:auto;image-rendering:auto}.marker{position:absolute;border:5px solid #ff4545;box-sizing:border-box;pointer-events:none}
.corner-nw{left:0;top:0;width:50%;height:50%}.corner-ne{left:50%;top:0;width:50%;height:50%}.corner-se{left:50%;top:50%;width:50%;height:50%}.corner-sw{left:0;top:50%;width:50%;height:50%}
.candidate{display:grid;grid-template-columns:112px 1fr;gap:10px;align-items:start;margin:10px 0}.candidate .tile{width:112px}
.candidate label{display:block;margin:3px 0}.question{margin:24px 0;padding-top:16px;border-top:1px solid #5f6368}.corner-answer{margin:14px 0;padding:12px;background:#303134;border-radius:6px}
.catalog-card img{width:100%;max-width:320px}.sig{font-family:Consolas,monospace;font-size:12px;word-break:break-word}.tag{display:inline-block;padding:2px 6px;border:1px solid #5f6368;border-radius:10px;margin-right:4px;font-size:12px}
.family-comment{display:block;width:100%;min-height:74px;box-sizing:border-box;resize:vertical}.comment-label{display:block;margin-top:12px}.compare-grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(190px,260px));gap:12px;margin:12px 0}.compare-grid img{display:block;width:100%;height:auto}.compare-label{display:block;margin-top:5px}
.zoom-comparison{display:grid;grid-template-columns:repeat(3,minmax(180px,1fr));gap:14px;margin:12px 0}.zoom-comparison img{display:block;width:100%;height:auto;image-rendering:pixelated}.variant-title{margin:16px 0 4px}.distance{font-family:Consolas,monospace}
.export-panel{margin:12px 0;padding:12px;border:1px solid #5f6368;border-radius:6px;background:#292a2d}.export-panel textarea{display:block;width:100%;min-height:180px;box-sizing:border-box;margin:8px 0;resize:vertical}.primary{border-color:#8ab4f8;background:#174ea6}.toolbar button{margin-right:6px}
@media(max-width:700px){body{padding:10px}.grid{grid-template-columns:1fr}.candidate{grid-template-columns:92px 1fr}.zoom-comparison{grid-template-columns:1fr}}
"""


def render_catalog(catalog: dict, archive: Path, output: Path,
                   reviewed_through: str | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)
    index = res_index(archive)
    owners: dict[tuple[int, str], str] = {}
    aliases: dict[str, list[str]] = defaultdict(list)
    for preset in catalog["presets"]:
        key = (preset["slotCount"], preset["signaturesBase64"])
        owner = owners.setdefault(key, preset["namespace"])
        aliases[owner].append(preset["namespace"])
    pending_namespaces = pending_review_namespaces(
        list(owners.values()), reviewed_through)
    pending_set = set(pending_namespaces)
    cards = []
    total = 0
    for preset in catalog["presets"]:
        key = (preset["slotCount"], preset["signaturesBase64"])
        if owners[key] != preset["namespace"]:
            continue
        namespace = preset["namespace"]
        if namespace not in pending_set:
            continue
        table = signatures(preset)
        tiles = editor_tiles(archive, index, namespace, preset["slotCount"] // 64)
        family_counts = defaultdict(int)
        uniform = defaultdict(list)
        for slot, signature in enumerate(table):
            if len(set(signature)) == 1:
                uniform[signature[0]].append(slot)
            for family_id in signature:
                family_counts[family_id] += 1
        family_html = []
        for family in sorted(preset["families"], key=lambda item: item["id"]):
            total += 1
            fid = int(family["id"])
            slot = int(family.get("thumbnailSlot", 0))
            image = save_tile(images, namespace, slot, tiles[slot])
            signature = table[slot]
            thumb_kind = "clean uniform" if signature == (fid, fid, fid, fid) else "transition/stamp"
            family_html.append(f"""
<article class="card catalog-card" data-search="{html.escape((namespace+' '+family['name']).lower())}" data-family="{html.escape(namespace)} family {fid}: {html.escape(family['name'])}">
 <a href="{image}"><img src="{image}" alt="{html.escape(family['name'])}, {namespace}, slot {slot}"></a>
 <h3>{html.escape(family['name'])}</h3>
 <div><span class="tag">{html.escape(namespace)}</span><span class="tag">family {fid}</span><span class="tag">{thumb_kind}</span></div>
 <p class="muted">Aliases: {html.escape(', '.join(aliases[namespace]))}<br>Thumbnail slot {slot}; {family_counts[fid]} assigned corners; {len(uniform[fid])} nominal four-corner slots.</p>
 <p class="sig">Thumbnail signature: {' / '.join(map(str, signature))}</p>
 <label class="comment-label" for="comment-{html.escape(namespace.lower())}-{fid}">Comment</label>
 <textarea class="family-comment" id="comment-{html.escape(namespace.lower())}-{fid}" data-comment-key="{html.escape(namespace)}:{fid}" placeholder="Family identity, duplicate, naming, thumbnail, or corner-assignment notes"></textarea>
</article>""")
        cards.append(f"<section><h2>{html.escape(namespace)} <span class='muted'>({len(family_html)} families)</span></h2><div class='grid'>{''.join(family_html)}</div></section>")
    document = f"""<!doctype html><meta charset="utf-8"><title>Semantic atlas family catalog</title><style>{common_style()}</style>
<h1>Remaining semantic-atlas families</h1><p class="muted">{total} families across {len(pending_namespaces)} unique signature tables remain for review{f' after {html.escape(reviewed_through)}' if reviewed_through else ''}. Images use the current shipped family thumbnail at 512 px. A transition/stamp label means the thumbnail is not a four-corner member of its own family.</p>
<div class="toolbar"><input id="filter" type="search" placeholder="Filter namespace or family" aria-label="Filter families"> <button id="submit-comments" class="primary">Submit comments to Codex</button><button id="show-comments">Show review text</button> <span id="comment-status" class="muted"></span></div>
<section id="comment-export" class="export-panel" hidden><strong>Family review text</strong><p class="muted">This remains available even when browser clipboard access is blocked. Select it and copy normally.</p><textarea id="comment-export-text" readonly></textarea><button id="select-comment-text">Select all text</button></section>
<main>{''.join(cards)}</main><script>
const storagePrefix='ei-semantic-family-comment-v1:';
const fields=[...document.querySelectorAll('.family-comment')];
const status=document.getElementById('comment-status');
const updateStatus=()=>{{const count=fields.filter(x=>x.value.trim()).length;status.textContent=count+' commented famil'+(count===1?'y':'ies');}};
fields.forEach(x=>{{try{{x.value=localStorage.getItem(storagePrefix+x.dataset.commentKey)||'';}}catch(e){{}}x.addEventListener('input',()=>{{try{{localStorage.setItem(storagePrefix+x.dataset.commentKey,x.value);}}catch(e){{}}updateStatus();}});}});
updateStatus();
const f=document.getElementById('filter');f.addEventListener('input',()=>{{const q=f.value.toLowerCase();document.querySelectorAll('.catalog-card').forEach(c=>c.hidden=!(c.dataset.search+' '+c.querySelector('.family-comment').value.toLowerCase()).includes(q));}});
const commentRows=()=>fields.filter(x=>x.value.trim()).map(x=>x.closest('.catalog-card').dataset.family+' — '+x.value.trim());
const commentText=()=>{{const rows=commentRows();return 'Semantic-atlas family review comments:\\n'+rows.join('\\n')+'\\nApply comments as review evidence; verify identities and connected corners before changing presets.';}};
const showComments=(message)=>{{const panel=document.getElementById('comment-export');const text=document.getElementById('comment-export-text');panel.hidden=false;text.value=commentText();text.focus();text.select();status.textContent=message||commentRows().length+' comments shown below';}};
document.getElementById('show-comments').addEventListener('click',()=>showComments());
document.getElementById('select-comment-text').addEventListener('click',()=>{{const text=document.getElementById('comment-export-text');text.focus();text.select();status.textContent='Review text selected';}});
document.getElementById('submit-comments').addEventListener('click',async()=>{{const rows=commentRows();if(!rows.length){{status.textContent='No family comments to submit';return;}}try{{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){{await window.openai.sendFollowUpMessage({{prompt:commentText(),title:'Submit semantic-atlas family review'}});status.textContent=rows.length+' family comments submitted to Codex';return;}}}}catch(e){{}}showComments('Direct submission is unavailable; review text is shown below');}});
</script>"""
    errors = review_document_errors(document, "", total)
    errors = [value for value in errors
              if not value.startswith("family-question visuals")]
    if errors:
        raise ValueError("; ".join(errors))
    (output / "semantic-atlas-family-catalog.html").write_text(document, encoding="utf-8")
    return {"uniqueTables": len(pending_namespaces), "families": total,
            "reviewedThrough": reviewed_through}


def render_questionnaire(catalog: dict, archive: Path, anchors_path: Path,
                         output: Path,
                         original_archive: Path | None = None) -> dict:
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)
    preset = next(item for item in catalog["presets"] if item["namespace"] == "bz3g")
    table = signatures(preset)
    family_names = {int(item["id"]): item["name"] for item in preset["families"]}
    anchors_json = json.loads(anchors_path.read_text(encoding="utf-8"))
    verified = {int(fid): set(map(int, slots)) for fid, slots in anchors_json["profiles"]["bz3g"].items()}
    tiles = editor_tiles(archive, res_index(archive), "bz3g", preset["slotCount"] // 64)
    original_tiles = (editor_tiles(original_archive, res_index(original_archive),
                                   "bz3g", preset["slotCount"] // 64)
                      if original_archive else None)
    prototypes = defaultdict(lambda: defaultdict(list))
    for family_id, slots in verified.items():
        for slot in slots:
            for corner in range(4):
                prototypes[family_id][slot].append(feature(corner_patch(tiles[slot], corner)))

    targets: dict[int, set[int]] = defaultdict(set)
    completed_review = json.loads(anchors_path.with_name(
        "semantic_atlas_unresolved_review_decisions.json").read_text(
            encoding="utf-8"))
    corner_number = {"NW": 0, "NE": 1, "SE": 2, "SW": 3}
    for decision in completed_review["cornerDecisions"]:
        if decision.get("status") == "needs-follow-up":
            targets[int(decision["slot"])].add(
                corner_number[decision["corner"]])

    comparison_family_ids = {
        "Ground / Common": next(fid for fid, name in family_names.items()
                                if name == "Ground / Common"),
        "Ground / Gravel": next(fid for fid, name in family_names.items()
                                if name == "Ground / Gravel"),
    }

    def closest_anchor(source_tiles: list[np.ndarray], target_slot: int,
                       target_corner: int, family_id: int) -> tuple[int, int, float]:
        sample = feature(corner_patch(source_tiles[target_slot], target_corner))
        candidates = []
        for anchor_slot in verified[family_id]:
            if anchor_slot == target_slot:
                continue
            for anchor_corner in range(4):
                anchor = feature(corner_patch(source_tiles[anchor_slot], anchor_corner))
                score = float(np.sqrt(np.square(sample - anchor).mean()))
                candidates.append((score, anchor_slot, anchor_corner))
        score, anchor_slot, anchor_corner = min(candidates)
        return anchor_slot, anchor_corner, score

    questions = []
    item_count = 0
    for slot in sorted(targets):
        corner_blocks = []
        for corner in sorted(targets[slot]):
            item_count += 1
            corner_name = CORNER_NAMES[corner]
            variants = [("HD", "hd", tiles)]
            if original_tiles is not None:
                variants.append(("Original pixels", "original", original_tiles))
            comparisons = []
            for variant_label, source, source_tiles in variants:
                target_crop = save_corner_crop(
                    images, source, "bz3g", slot, corner, source_tiles[slot])
                columns = [f'<div><img src="{target_crop}" alt="{variant_label} target slot {slot} {corner_name}"><strong class="compare-label">Target — slot {slot} {corner_name}</strong></div>']
                for family_label, family_id in comparison_family_ids.items():
                    anchor_slot, anchor_corner, score = closest_anchor(
                        source_tiles, slot, corner, family_id)
                    anchor_crop = save_corner_crop(
                        images, source, "bz3g", anchor_slot, anchor_corner,
                        source_tiles[anchor_slot])
                    columns.append(f'<div><img src="{anchor_crop}" alt="{variant_label} verified {html.escape(family_label)} anchor"><strong class="compare-label">{html.escape(family_label)} — slot {anchor_slot} {CORNER_NAMES[anchor_corner]}</strong><span class="muted distance">distance {score:.6f}</span></div>')
                comparisons.append(
                    f'<h3 class="variant-title">{variant_label}</h3><div class="zoom-comparison" data-variant="{source}">{"".join(columns)}</div>')
            options = ('<option value="keep">Keep current</option>'
                       '<option value="common">Ground / Common</option>'
                       '<option value="gravel">Ground / Gravel</option>'
                       '<option value="different">Different family / explain</option>')
            corner_blocks.append(f"""<div class="corner-answer" data-follow-up="common-gravel" data-key="bz3g slot {slot} {corner_name}"><h2>bz3g slot {slot} {corner_name}</h2><p class="muted">Compare the same target corner against independently reviewed whole-tile anchors. The enlarged square is the complete quadrant; the distance is measured only at the vertex.</p>{''.join(comparisons)}<label>Decision <select class="answer">{options}</select></label><label> Note <input class="note" type="text" placeholder="Optional"></label></div>""")
        questions.append(f"""<section class="question">{''.join(corner_blocks)}</section>""")
    document = f"""<!doctype html><meta charset="utf-8"><title>Semantic atlas unresolved corners</title><style>{common_style()}</style>
<h1>Final Ground / Common versus Ground / Gravel follow-up</h1><p class="muted">{item_count} close-hue bz3g corners remain. Each target is shown at high resolution beside separately reviewed Common and Gravel anchors, once from the HD atlas and once from the original game pixels. Stone / Gray is resolved and no longer appears here.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers to Codex</button><button id="show-answers">Show review text</button> <span id="status" class="muted"></span></div>
<section id="answer-export" class="export-panel" hidden><strong>Questionnaire review text</strong><p class="muted">This remains available even when browser clipboard access is blocked. Select it and copy normally.</p><textarea id="answer-export-text" readonly></textarea><button id="select-answer-text">Select all text</button></section><main>{''.join(questions)}</main>
<script>
const answerPrefix='ei-semantic-questionnaire-v1:';
const blocks=[...document.querySelectorAll('[data-key]')];
const status=document.getElementById('status');
const saveBlock=(block)=>{{const answer=block.querySelector('.answer');const note=block.querySelector('.note');try{{localStorage.setItem(answerPrefix+block.dataset.key,JSON.stringify({{answer:answer?answer.value:'',note:note?note.value:''}}));}}catch(e){{}}updateStatus();}};
blocks.forEach(block=>{{const answer=block.querySelector('.answer');const note=block.querySelector('.note');try{{const saved=JSON.parse(localStorage.getItem(answerPrefix+block.dataset.key)||'null');if(saved){{if(answer&&saved.answer)answer.value=saved.answer;if(note)note.value=saved.note||'';}}}}catch(e){{}}if(answer)answer.addEventListener('change',()=>saveBlock(block));if(note)note.addEventListener('input',()=>saveBlock(block));}});
const decisionRows=()=>{{const rows=[];blocks.forEach(block=>{{const answer=block.querySelector('.answer');const note=block.querySelector('.note');const noteText=note?note.value.trim():'';if(answer&&answer.value)rows.push(block.dataset.key+': '+answer.options[answer.selectedIndex].text+(noteText?' — '+noteText:''));else if(noteText)rows.push(block.dataset.key+': Comment — '+noteText);}});return rows;}};
function updateStatus(){{const count=decisionRows().length;status.textContent=count+' saved response'+(count===1?'':'s');}}
const answerText=()=>{{const rows=decisionRows();return 'Semantic-atlas unresolved review decisions:\\n'+rows.join('\\n')+'\\nApply only explicit decisions; verify connected corners before propagation.';}};
const showAnswers=(message)=>{{const panel=document.getElementById('answer-export');const text=document.getElementById('answer-export-text');panel.hidden=false;text.value=answerText();text.focus();text.select();status.textContent=message||decisionRows().length+' responses shown below';}};
document.getElementById('show-answers').addEventListener('click',()=>showAnswers());
document.getElementById('select-answer-text').addEventListener('click',()=>{{const text=document.getElementById('answer-export-text');text.focus();text.select();status.textContent='Review text selected';}});
document.getElementById('submit-answers').addEventListener('click',async()=>{{const rows=decisionRows();if(!rows.length){{status.textContent='No answers or notes to submit';return;}}try{{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){{await window.openai.sendFollowUpMessage({{prompt:answerText(),title:'Submit semantic-atlas questionnaire'}});status.textContent=rows.length+' responses submitted to Codex';return;}}}}catch(e){{}}showAnswers('Direct submission is unavailable; review text is shown below');}});
updateStatus();
</script>"""
    visual_errors = review_document_errors(
        '<textarea class="family-comment"></textarea><button id="submit-comments"></button><button id="show-comments"></button><textarea id="comment-export-text"></textarea><script>localStorage.setItem("x","y");window.openai.sendFollowUpMessage({});</script>',
        document, 1)
    if visual_errors:
        raise ValueError("; ".join(visual_errors))
    (output / "semantic-atlas-unresolved-questionnaire.html").write_text(document, encoding="utf-8")
    (output / "manifest.json").write_text(json.dumps({
        "schema": "ei-semantic-atlas-unresolved-questionnaire-v1",
        "namespace": "bz3g", "cornerQuestions": item_count,
        "comparisonVariants": 2 if original_tiles is not None else 1,
        "tileQuestions": len(targets), "verifiedAnchorSlots": {
            str(fid): sorted(slots) for fid, slots in verified.items()},
    }, indent=2) + "\n", encoding="utf-8")
    return {"cornerQuestions": item_count, "tileQuestions": len(targets)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", type=Path, required=True)
    parser.add_argument("--archive", type=Path, required=True)
    parser.add_argument("--original-archive", type=Path)
    parser.add_argument("--verified-anchors", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--catalog-reviewed-through")
    args = parser.parse_args()
    catalog = json.loads(args.presets.read_text(encoding="utf-8"))
    result = {
        "catalog": render_catalog(
            catalog, args.archive, args.output / "family-catalog",
            reviewed_through=args.catalog_reviewed_through),
        "questionnaire": render_questionnaire(
            catalog, args.archive, args.verified_anchors,
            args.output / "questionnaire", args.original_archive),
    }
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
