#!/usr/bin/env python3
"""Render the small cross-variant questionnaire left after family review."""

from __future__ import annotations

import argparse
import html
import json
from pathlib import Path

from PIL import Image

from audit_terrain_atlas_presets import CORNER_NAMES, signatures
from generate_terrain_atlas_presets import editor_tiles, read_member, res_index
from render_semantic_atlas_review import common_style


CORNER_INDEX = {name: index for index, name in enumerate(CORNER_NAMES)}


def archive_has_namespace(index: dict, preset: dict) -> bool:
    parts = int(preset["slotCount"]) // 64
    return all(
        f"{preset['namespace']}{part:03d}.mmp".lower() in index
        for part in range(parts)
    )


def questionnaire_interaction_script(checkpoint: str = "R49") -> str:
    review_header = json.dumps(
        f"Semantic-atlas {checkpoint} focused review decisions:\n",
        ensure_ascii=False)
    review_footer = json.dumps(
        "\nApply only explicit decisions; verify exact connected corners before propagation.",
        ensure_ascii=False)
    download_name = json.dumps(
        f"semantic-atlas-{checkpoint.lower()}-answers.txt")
    submit_title = json.dumps(
        f"Submit semantic-atlas {checkpoint} review")
    line_break = json.dumps("\n")
    return f"""
const prefix='ei-semantic-{checkpoint.lower()}-review:';
const blocks=[...document.querySelectorAll('[data-key]')];
const status=document.getElementById('status');
function save(block){{const answer=block.querySelector('.answer').value;const note=block.querySelector('.note').value;try{{localStorage.setItem(prefix+block.dataset.key,JSON.stringify({{answer,note}}));}}catch(e){{}}update();}}
blocks.forEach(block=>{{const answer=block.querySelector('.answer');const note=block.querySelector('.note');try{{const saved=JSON.parse(localStorage.getItem(prefix+block.dataset.key)||'null');if(saved){{if(saved.answer)answer.value=saved.answer;note.value=saved.note||'';}}}}catch(e){{}}answer.addEventListener('change',()=>save(block));note.addEventListener('input',()=>save(block));}});
function rows(){{return blocks.map(block=>{{const answer=block.querySelector('.answer').value;const note=block.querySelector('.note').value.trim();const changed=answer!==block.dataset.default;return changed||note?block.dataset.key+': '+(changed?answer:'Comment')+(note?' \\u2014 '+note:''):'';}}).filter(Boolean);}}
function text(){{return {review_header}+rows().join({line_break})+{review_footer};}}
function update(message){{document.getElementById('answer-export-text').value=text();status.textContent=message||rows().length+' saved response'+(rows().length===1?'':'s');}}
document.getElementById('show-answers').addEventListener('click',()=>{{const area=document.getElementById('answer-export-text');area.focus();area.select();update('Review text selected');}});
document.getElementById('copy-answers').addEventListener('click',async()=>{{const area=document.getElementById('answer-export-text');area.focus();area.select();try{{await navigator.clipboard.writeText(text());update('Answers copied');}}catch(e){{update('Clipboard unavailable; review text selected');}}}});
document.getElementById('download-answers').addEventListener('click',()=>{{const blob=new Blob([text()],{{type:'text/plain;charset=utf-8'}});const link=document.createElement('a');link.href=URL.createObjectURL(blob);link.download={download_name};document.body.appendChild(link);link.click();link.remove();setTimeout(()=>URL.revokeObjectURL(link.href),0);update('Answers downloaded');}});
document.getElementById('submit-answers').addEventListener('click',async()=>{{if(!rows().length){{update('No answers to submit');return;}}try{{if(window.openai&&typeof window.openai.sendFollowUpMessage==='function'){{await window.openai.sendFollowUpMessage({{prompt:text(),title:{submit_title}}});update(rows().length+' responses submitted');return;}}}}catch(e){{}}const area=document.getElementById('answer-export-text');area.focus();area.select();update('Direct submission unavailable; review text selected');}});
update();
""".strip()


def save_tile(images: Path, variant: str, namespace: str, slot: int,
              tile, rotation: int = 0) -> str:
    rotation = int(rotation) % 4
    suffix = f"-r{rotation * 90}" if rotation else ""
    name = f"{variant}-{namespace.lower()}-slot-{slot}{suffix}.jpg"
    path = images / name
    image = Image.fromarray(tile[:, :, :3], "RGB")
    if rotation:
        image = image.rotate(rotation * 90)
    image.resize(
        (512, 512), Image.Resampling.NEAREST).save(
        path, quality=94, subsampling=0, optimize=True)
    return f"images/{name}"


def save_corner(images: Path, variant: str, namespace: str, slot: int,
                corner: int, tile) -> str:
    name = (f"{variant}-{namespace.lower()}-slot-{slot}-"
            f"{CORNER_NAMES[corner].lower()}.png")
    path = images / name
    height, width = tile.shape[:2]
    half_h, half_w = height // 2, width // 2
    top = 0 if corner in (0, 1) else half_h
    left = 0 if corner in (0, 3) else half_w
    crop = tile[top:top + half_h, left:left + half_w, :3]
    Image.fromarray(crop, "RGB").resize(
        (512, 512), Image.Resampling.NEAREST).save(path, optimize=True)
    return f"images/{name}"


def save_family_reference(images: Path, variant: str, namespace: str,
                          slot: int, corner: int, kind: str,
                          tile, rotation: int = 0) -> tuple[str, str]:
    """Keep family evidence at tile scale and mark non-uniform fallbacks.

    A uniform family anchor must be shown as the complete authored tile.  When
    a family has no uniform tile, show its complete thumbnail/stamp and mark
    the owned corner instead of enlarging one quarter until it resembles a
    uniform reference.
    """
    image = save_tile(images, variant, namespace, slot, tile, rotation)
    marker = ""
    if kind != "uniform":
        marker = f'<span class="marker corner-{CORNER_NAMES[corner].lower()}"></span>'
    return image, marker


def family_reference(preset: dict, family_id: int) -> tuple[int, int, str]:
    table = signatures(preset)
    for slot, signature in enumerate(table):
        if signature == (family_id, family_id, family_id, family_id):
            return slot, 0, "uniform"
    family = next(value for value in preset["families"]
                  if int(value["id"]) == family_id)
    slot = int(family["thumbnailSlot"])
    try:
        corner = table[slot].index(family_id)
    except ValueError:
        corner = 0
    return slot, corner, "thumbnail/stamp"


def family_references(preset: dict, family_id: int,
                      preferred_slots: list[dict] | None = None,
                      limit: int = 3,
                      excluded_slots: set[int] | None = None
                      ) -> list[tuple[int, int, str, int]]:
    """Return exact requested sources first, then up to three uniform variants."""
    table = signatures(preset)
    excluded = set(excluded_slots or ())
    rows: list[tuple[int, int, str, int]] = []
    for preferred in preferred_slots or []:
        slot = int(preferred["slot"])
        if slot in excluded:
            continue
        rotation = int(preferred.get("rotation", 0)) % 4
        signature = table[slot]
        kind = ("uniform" if signature == (family_id,) * 4 else
                "thumbnail/stamp")
        try:
            corner = signature.index(family_id)
        except ValueError:
            corner = 0
        row = (slot, corner, kind, rotation)
        if row not in rows:
            rows.append(row)
    for slot, signature in enumerate(table):
        if slot in excluded:
            continue
        if signature != (family_id,) * 4:
            continue
        row = (slot, 0, "uniform", 0)
        if row not in rows:
            rows.append(row)
        if len(rows) >= limit:
            break
    if not rows:
        slot, corner, kind = family_reference(preset, family_id)
        if slot not in excluded:
            rows.append((slot, corner, kind, 0))
    return rows[:limit]


def original_dimensions_are_lower(
        hd_dimensions: set[tuple[int, int]],
        original_dimensions: set[tuple[int, int]]) -> bool:
    """Reject an HD override archive masquerading as original evidence."""
    if not hd_dimensions or not original_dimensions:
        return False
    return max(max(width, height) for width, height in original_dimensions) < max(
        max(width, height) for width, height in hd_dimensions)


def atlas_dimensions(archive: Path, index: dict[str, dict],
                     preset: dict) -> set[tuple[int, int]]:
    import struct

    dimensions: set[tuple[int, int]] = set()
    for part in range(int(preset["slotCount"]) // 64):
        name = f"{preset['namespace']}{part:03d}.mmp".lower()
        if name not in index:
            return set()
        payload = read_member(archive, index[name])
        _signature, width, height = struct.unpack_from("<3I", payload)
        dimensions.add((int(width), int(height)))
    return dimensions


def render(presets_path: Path, hd_archive: Path, original_archive: Path,
           questions_path: Path, output: Path) -> dict:
    catalog = json.loads(presets_path.read_text(encoding="utf-8"))
    question_document = json.loads(
        questions_path.read_text(encoding="utf-8"))
    questions = question_document["questions"]
    checkpoint = question_document.get("checkpoint", "R49")
    by_namespace = {
        value["namespace"].lower(): value for value in catalog["presets"]}
    output.mkdir(parents=True, exist_ok=True)
    images = output / "images"
    images.mkdir(exist_ok=True)

    archives = {
        "hd": (hd_archive, res_index(hd_archive)),
        "original": (original_archive, res_index(original_archive)),
    }
    reviewed_slots: dict[str, set[int]] = {}
    for question in questions:
        reviewed_slots.setdefault(
            question["namespace"].casefold(), set()).add(int(question["slot"]))
        for target in question.get("additionalTargets", []):
            reviewed_slots.setdefault(
                target["namespace"].casefold(), set()).add(int(target["slot"]))

    # A mod override named textures.res may contain a mixture of original and
    # HD terrain pages.  Prove that every displayed "Original" namespace is
    # genuinely lower resolution than its HD counterpart before rendering.
    required_namespaces = {
        question["namespace"].casefold() for question in questions
    }
    required_namespaces.update(
        reference["namespace"].casefold()
        for question in questions for reference in question["references"])
    required_namespaces.update(
        target["namespace"].casefold()
        for question in questions
        for target in question.get("additionalTargets", []))
    for namespace_key in sorted(required_namespaces):
        required = by_namespace[namespace_key]
        if not (archive_has_namespace(archives["hd"][1], required)
                and archive_has_namespace(archives["original"][1], required)):
            continue
        hd_dimensions = atlas_dimensions(
            hd_archive, archives["hd"][1], required)
        original_dimensions = atlas_dimensions(
            original_archive, archives["original"][1], required)
        if not original_dimensions_are_lower(
                hd_dimensions, original_dimensions):
            raise ValueError(
                f"{required['namespace']}: Original atlas source is not "
                f"lower-resolution than HD ({sorted(original_dimensions)} "
                f"vs {sorted(hd_dimensions)}); choose the genuine original "
                "textures.res rather than an HD/mixed mod override")
    tile_cache = {}

    def tiles(variant: str, namespace: str):
        key = (variant, namespace.lower())
        if key not in tile_cache:
            preset = by_namespace[key[1]]
            archive, index = archives[variant]
            tile_cache[key] = editor_tiles(
                archive, index, preset["namespace"],
                preset["slotCount"] // 64)
        return tile_cache[key]

    sections = []
    for number, question in enumerate(questions, 1):
        namespace = question["namespace"]
        preset = by_namespace[namespace.lower()]
        slot = int(question["slot"])
        corner_name = question.get("corner")
        corner = CORNER_INDEX[corner_name] if corner_name else None
        variant_groups = []
        for variant, variant_label in (("hd", "HD atlas"),
                                       ("original", "Original atlas")):
            variant_index = archives[variant][1]
            required_presets = [preset] + [
                by_namespace[reference["namespace"].lower()]
                for reference in question["references"]
            ] + [
                by_namespace[target["namespace"].lower()]
                for target in question.get("additionalTargets", [])
            ]
            if not all(archive_has_namespace(variant_index, required)
                       for required in required_presets):
                # Some JMV atlas namespaces exist only in the HD terrain
                # archive. Do not suppress their available evidence or show
                # an HD image under a false "Original" label.
                continue
            target_tiles = tiles(variant, namespace)
            target_full = save_tile(
                images, variant, namespace, slot, target_tiles[slot])
            target_marker = (f'<span class="marker corner-{corner_name.lower()}"></span>'
                             if corner_name else "")
            target_label = (f"Target {namespace} slot {slot} {corner_name}"
                            if corner_name else f"Target {namespace} slot {slot}")
            columns = [f"""
<div><div class="tile"><img src="{target_full}" alt="{html.escape(target_label)}, {variant_label}">{target_marker}</div><strong class="compare-label">{html.escape(target_label)}</strong></div>"""]
            for extra in question.get("additionalTargets", []):
                extra_namespace = extra["namespace"]
                extra_slot = int(extra["slot"])
                extra_corner_name = extra.get("corner")
                extra_tiles = tiles(variant, extra_namespace)
                extra_full = save_tile(
                    images, variant, extra_namespace, extra_slot,
                    extra_tiles[extra_slot])
                extra_marker = (
                    f'<span class="marker corner-{extra_corner_name.lower()}"></span>'
                    if extra_corner_name else "")
                extra_label = extra.get(
                    "label",
                    (f"{extra_namespace} slot {extra_slot} {extra_corner_name}"
                     if extra_corner_name else
                     f"{extra_namespace} slot {extra_slot}"))
                columns.append(f"""
<div><div class="tile"><img src="{extra_full}" alt="{html.escape(extra_label)}, {variant_label}">{extra_marker}</div><strong class="compare-label">{html.escape(extra_label)}</strong></div>""")
            for reference in question["references"]:
                ref_namespace = reference["namespace"]
                ref_preset = by_namespace[ref_namespace.lower()]
                family_id = int(reference["familyId"])
                family = next(value for value in ref_preset["families"]
                              if int(value["id"]) == family_id)
                ref_tiles = tiles(variant, ref_namespace)
                role = str(reference.get("role", "Comparison"))
                for ref_slot, ref_corner, kind, rotation in family_references(
                        ref_preset, family_id,
                        reference.get("preferredSlots"), limit=3,
                        excluded_slots=reviewed_slots.get(
                            ref_namespace.casefold(), set())):
                    ref_image, ref_marker = save_family_reference(
                        images, variant, ref_namespace, ref_slot, ref_corner,
                        kind, ref_tiles[ref_slot], rotation)
                    reference_note = (
                        "uniform authored tile" if kind == "uniform" else
                        f"no uniform authored tile; {CORNER_NAMES[ref_corner]} corner is the family reference")
                    if rotation:
                        reference_note += f"; audit comparison rotated {rotation * 90} degrees CCW"
                    columns.append(f"""
<div><div class="tile"><img src="{ref_image}" alt="{html.escape(family['name'])} reference, {variant_label}">{ref_marker}</div><strong class="compare-label">{html.escape(family['name'])}</strong><span class="reference-role">{html.escape(role)}</span><span class="muted">{html.escape(ref_namespace)} slot {ref_slot}; {html.escape(reference_note)}</span></div>""")
            variant_groups.append(
                f'<h3>{variant_label}</h3><div class="compare-grid" data-variant="{variant}">{"".join(columns)}</div>')
        if not variant_groups:
            raise ValueError(
                f"no atlas variant is available for {namespace} slot {slot}")
        option_rows = []
        for value in question["options"]:
            if isinstance(value, dict):
                option_rows.append((str(value["value"]),
                                    str(value.get("label", value["value"]))))
            else:
                option_rows.append((str(value), str(value)))
        default_option = question.get("defaultOption")
        if default_option is None:
            default_option = next(
                (value for value, _ in option_rows
                 if value.lower().startswith("keep current")),
                option_rows[0][0] if option_rows else "Keep current")
        default_rows = [row for row in option_rows if row[0] == default_option]
        if not default_rows:
            raise ValueError(
                f"{question['key']}: default option {default_option!r} is absent")
        option_rows = default_rows + [
            row for row in option_rows if row[0] != default_option]
        options = ''.join(
            f'<option value="{html.escape(value)}"'
            f'{" selected" if value == default_option else ""}>'
            f'{html.escape(label)}</option>'
            for value, label in option_rows)
        sections.append(f"""
<section class="question corner-answer" data-key="{html.escape(question['key'])}" data-default="{html.escape(str(default_option), quote=True)}">
 <h2>{number}. {html.escape(question['prompt'])}</h2>
 <p class="muted">{html.escape(question['context'])}</p>
 {''.join(variant_groups)}
 <div class="viz-controls"><label class="form-label">Decision <select class="answer form-select">{options}</select></label><label class="form-label">Note <input class="note form-control" type="text" placeholder="Optional detail"></label></div>
</section>""")

    interaction_script = questionnaire_interaction_script(checkpoint)
    document = f"""<!doctype html><meta charset="utf-8"><title>Semantic atlas {html.escape(checkpoint)} focused review</title><style>{common_style()} .reference-role{{display:block;color:#fbbc04;font-weight:700;margin:3px 0}}</style>
<h1>{html.escape(checkpoint)} remaining family decisions</h1><p class="muted">{len(questions)} items remain after the verified corrections. Every texture question shows the same target and titled full-tile references from each available HD/original atlas. Suggested families use a uniform tile from this atlas whenever one exists. A family with no uniform authored tile is shown only as a complete mixed tile with its relevant corner marked and explicitly labelled. Answers save locally as you make them.</p>
<div class="toolbar"><button id="submit-answers" class="primary">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button> <span id="status" class="muted"></span></div>
<section id="answer-export" class="export-panel"><strong>Saved review text</strong><textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea><p class="muted">This text updates with every answer. If submission or copying is unavailable, select it manually or download the text file.</p></section>
<main>{''.join(sections)}</main>
<script>
{interaction_script}
</script>"""
    required = ("submit-answers", "copy-answers", "download-answers",
                "answer-export-text", "sendFollowUpMessage",
                "localStorage.setItem", "navigator.clipboard.writeText")
    missing = [value for value in required if value not in document]
    if missing:
        raise ValueError(f"questionnaire controls missing: {missing}")
    if document.count('data-variant="hd"') != len(questions):
        raise ValueError("one HD comparison is required per question")
    if document.count('data-variant="original"') > len(questions):
        raise ValueError("at most one original comparison is allowed per question")
    path = output / f"semantic-atlas-{checkpoint.lower()}-questionnaire.html"
    path.write_text(document, encoding="utf-8")
    missing_images = [value for value in images.iterdir()
                      if not value.is_file() or value.stat().st_size == 0]
    if missing_images:
        raise ValueError(f"empty questionnaire images: {missing_images}")
    return {"questions": len(questions), "images": len(list(images.iterdir())),
            "output": str(path)}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--presets", required=True, type=Path)
    parser.add_argument("--hd", required=True, type=Path)
    parser.add_argument("--original", required=True, type=Path)
    parser.add_argument("--questions", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    print(json.dumps(render(args.presets, args.hd, args.original,
                            args.questions, args.output), sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
