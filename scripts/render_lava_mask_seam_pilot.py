#!/usr/bin/env python3
"""Render a four-question Lava mask pilot from already approved mask sets.

No EI-HD artwork is copied.  The EI-HD seam-first method is used only as the
construction/validation contract: each route starts from an approved half mask,
and every corner/diagonal/inside boundary must be an exact rotation, inverse,
solid edge, or A/B profile from that half.  The resulting masks remain review
artifacts until the reviewer selects a route.
"""

from __future__ import annotations

import argparse
import hashlib
import html
import json
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

from generate_terrain_atlas_presets import editor_tiles, res_index
from render_authored_mask_repair_pilot import (
    CORNER_NAMES,
    expected_owner_corners,
    mask_metrics,
)
from render_blend_mask_questionnaire import interaction_script


TILE_SIZE = 256
TOPOLOGIES = (
    ("outer-1of4-v1", "Outer 1 of 4"),
    ("edge-2of4-v1", "Edge 2 of 4"),
    ("diagonal-2of4", "Diagonal 2 of 4"),
    ("inside-3of4-v1", "Inside 3 of 4"),
)
ROUTES = {
    "approved-liquid-flow": {
        "label": "Approved liquid-flow route",
        "description": (
            "The natural, continuous-alpha Water masks approved through R75. "
            "They are texture-neutral ownership mattes, not Water artwork."
        ),
        "masks": {
            "outer-1of4-v1": "water-outer-1of4-v1-r75-authored.png",
            "edge-2of4-v1": "water-edge-2of4-v1-r75-authored.png",
            "diagonal-2of4": "water-diagonal-2of4-r75-authored.png",
            "inside-3of4-v1": "water-inside-3of4-v1-r75-authored.png",
        },
    },
    "approved-fracture-flow": {
        "label": "Approved fracture-flow route",
        "description": (
            "The sharper authored-fracture masks approved in R76. They retain "
            "natural tongues and fissures while using the same exact A/B seam contract."
        ),
        "masks": {
            "outer-1of4-v1": "ice-outer-1of4-v1-r76-adjusted.png",
            "edge-2of4-v1": "ice-edge-2of4-v1-r76-adjusted.png",
            "diagonal-2of4": "ice-diagonal-2of4-r76-adjusted.png",
            "inside-3of4-v1": "ice-inside-3of4-v1-r76-adjusted.png",
        },
    },
}


def _pixel_sha(image: Image.Image) -> str:
    return hashlib.sha256(image.tobytes()).hexdigest().upper()


def _file_sha(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(8 * 1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest().upper()


def _save(image: Image.Image, path: Path) -> dict:
    path.parent.mkdir(parents=True, exist_ok=True)
    image.save(path, "PNG", optimize=True)
    return {
        "file": f"images/{path.name}",
        "fileSha256": _file_sha(path),
        "pixelSha256": _pixel_sha(image),
    }


def _checker(size: tuple[int, int]) -> Image.Image:
    width, height = size
    yy, xx = np.mgrid[0:height, 0:width]
    cells = ((xx // 16 + yy // 16) & 1).astype(np.uint8)
    values = np.where(cells[..., None] == 0,
                      np.array([40, 44, 48], dtype=np.uint8),
                      np.array([72, 77, 82], dtype=np.uint8))
    return Image.fromarray(values, "RGB")


def _masked_preview(texture: Image.Image, mask: Image.Image) -> Image.Image:
    background = _checker(texture.size)
    return Image.composite(texture.convert("RGB"), background, mask.convert("L"))


def _mixed_preview(owner: Image.Image, support: Image.Image,
                   mask: Image.Image) -> Image.Image:
    return Image.composite(owner.convert("RGB"), support.convert("RGB"),
                           mask.convert("L"))


def _heatmap(mask: Image.Image) -> Image.Image:
    values = np.asarray(mask.convert("L"), dtype=np.float32) / 255.0
    rgb = np.zeros((*values.shape, 3), dtype=np.uint8)
    owner = values >= 0.8
    support = values <= 0.2
    feather = ~(owner | support)
    rgb[owner] = (33, 170, 82)
    rgb[support] = (25, 58, 115)
    rgb[feather] = (225, 137, 36)
    return Image.fromarray(rgb, "RGB")


def _edge_profiles(half: Image.Image) -> list[np.ndarray]:
    values = np.asarray(half.convert("L"), dtype=np.int16)
    side = values[:, 0]
    profiles = [
        np.zeros(TILE_SIZE, dtype=np.int16),
        np.full(TILE_SIZE, 255, dtype=np.int16),
        side, side[::-1], 255 - side, (255 - side)[::-1],
        values[0], values[0][::-1], values[-1], values[-1][::-1],
    ]
    return profiles


def seam_contract(half: Image.Image, mask: Image.Image,
                  topology: str) -> dict:
    if half.size != (TILE_SIZE, TILE_SIZE) or mask.size != half.size:
        raise ValueError("Lava pilot masks must be 256x256")
    values = np.asarray(mask.convert("L"), dtype=np.int16)
    profiles = _edge_profiles(half)
    errors = []
    for edge in (values[0], values[:, -1], values[-1], values[:, 0]):
        errors.append(min(int(np.abs(edge - profile).max())
                          for profile in profiles))
    expected = expected_owner_corners(topology)
    corners = (int(values[0, 0]), int(values[0, -1]),
               int(values[-1, -1]), int(values[-1, 0]))
    if any(value != (255 if owned else 0)
           for value, owned in zip(corners, expected)):
        raise ValueError(f"{topology}: mask corners do not match owner assignment")
    if max(errors) != 0:
        raise ValueError(f"{topology}: boundary is not derived from the half-mask profiles")
    return {
        "edgeMaxAbsoluteErrors": dict(zip(
            ("north", "east", "south", "west"), errors)),
        "allBoundaryProfilesExact": True,
        "cornerValues": dict(zip(CORNER_NAMES, corners)),
        "ownerCorners": [name for name, owned in zip(CORNER_NAMES, expected)
                         if owned],
    }


def _seam_island(mask: Image.Image, texture: Image.Image) -> Image.Image:
    """Four rotated 1/4 masks meeting at one vertex expose every shared seam."""
    base = mask.convert("L")
    rotations = (
        base.transpose(Image.Transpose.ROTATE_180),
        base.transpose(Image.Transpose.ROTATE_90),
        base.transpose(Image.Transpose.ROTATE_270),
        base,
    )
    canvas = _checker((TILE_SIZE * 2, TILE_SIZE * 2))
    for index, rotated in enumerate(rotations):
        tile = _masked_preview(texture, rotated)
        canvas.paste(tile, ((index % 2) * TILE_SIZE,
                            (index // 2) * TILE_SIZE))
    draw = ImageDraw.Draw(canvas)
    draw.line((TILE_SIZE, 0, TILE_SIZE, TILE_SIZE * 2), fill=(255, 0, 255))
    draw.line((0, TILE_SIZE, TILE_SIZE * 2, TILE_SIZE), fill=(255, 0, 255))
    return canvas


def load_lava_uniforms(hd_res: Path) -> tuple[Image.Image, Image.Image, dict]:
    index = res_index(hd_res)
    red = Image.fromarray(editor_tiles(hd_res, index, "bz14h", 4)[4], "RGBA")
    orange = Image.fromarray(editor_tiles(hd_res, index, "Zone10", 8)[5], "RGBA")
    if red.size != (TILE_SIZE, TILE_SIZE) or orange.size != red.size:
        raise ValueError("approved JMV Lava uniforms are not 256x256")
    return red, orange, {
        "archive": str(hd_res),
        "archiveSha256": _file_sha(hd_res),
        "red": {"namespace": "bz14h", "slot": 4,
                "pixelSha256": _pixel_sha(red)},
        "orange": {"namespace": "Zone10", "slot": 5,
                   "pixelSha256": _pixel_sha(orange)},
    }


def render(mask_root: Path, output: Path, red: Image.Image,
           orange: Image.Image, source: dict,
           checkpoint: str = "R123-LAVA-SEAM-PILOT") -> dict:
    if output.exists() and any(output.iterdir()):
        raise ValueError(f"pilot output is not empty: {output}")
    images = output / "images"
    images.mkdir(parents=True, exist_ok=True)
    red_record = _save(red.convert("RGB"), images / "lava-red-uniform.png")
    orange_record = _save(orange.convert("RGB"), images / "lava-orange-uniform.png")

    route_masks: dict[str, dict[str, Image.Image]] = {}
    route_analysis: dict[str, dict] = {}
    checker_records = {}
    for route_key, route in ROUTES.items():
        route_masks[route_key] = {}
        topology_records = {}
        for topology, _label in TOPOLOGIES:
            path = mask_root / route["masks"][topology]
            with Image.open(path) as opened:
                mask = opened.convert("L")
                mask.load()
            route_masks[route_key][topology] = mask
        half = route_masks[route_key]["edge-2of4-v1"]
        for topology, _label in TOPOLOGIES:
            mask = route_masks[route_key][topology]
            topology_records[topology] = {
                "resource": route["masks"][topology],
                "resourceFileSha256": _file_sha(
                    mask_root / route["masks"][topology]
                ),
                "pixelSha256": _pixel_sha(mask),
                "seamContract": seam_contract(half, mask, topology),
                "quarterContract": mask_metrics(mask, mask, topology),
            }
        checker_records[route_key] = _save(
            _seam_island(route_masks[route_key]["outer-1of4-v1"], red),
            images / f"{route_key}-four-tile-seam-check.png")
        route_analysis[route_key] = {
            "label": route["label"],
            "description": route["description"],
            "halfMask": route["masks"]["edge-2of4-v1"],
            "topologies": topology_records,
            "fourTileSeamCheck": checker_records[route_key],
        }

    questions = []
    cards = []
    options = (
        '<option value="">Choose after reviewing both routes</option>'
        '<option>Prefer approved liquid-flow route</option>'
        '<option>Prefer approved fracture-flow route</option>'
        '<option>Neither; make a distinct Lava route</option>'
        '<option>Still uncertain</option>'
    )
    for number, (topology, label) in enumerate(TOPOLOGIES, 1):
        key = f"lava-mask-{topology}"
        owner_corners = [name for name, owned in zip(
            CORNER_NAMES, expected_owner_corners(topology)) if owned]
        route_html = []
        route_rows = {}
        for route_key, route in ROUTES.items():
            mask = route_masks[route_key][topology]
            prefix = f"{number:02d}-{route_key}-{topology}"
            records = {
                "mask": _save(mask, images / f"{prefix}-mask.png"),
                "heatmap": _save(_heatmap(mask), images / f"{prefix}-heatmap.png"),
                "redEmpty": _save(_masked_preview(red, mask),
                                  images / f"{prefix}-red-on-empty.png"),
                "orangeEmpty": _save(_masked_preview(orange, mask),
                                     images / f"{prefix}-orange-on-empty.png"),
                "redOrange": _save(_mixed_preview(red, orange, mask),
                                   images / f"{prefix}-red-on-orange.png"),
            }
            route_rows[route_key] = records
            figures = "".join(
                f'<figure><img src="{html.escape(record["file"])}" alt="{html.escape(caption)}">'
                f'<figcaption>{html.escape(caption)}</figcaption></figure>'
                for caption, record in (
                    ("Ownership mask (white = Lava)", records["mask"]),
                    ("80/20 contract heatmap", records["heatmap"]),
                    ("Lava / Red on empty overlay", records["redEmpty"]),
                    ("Lava / Orange on empty overlay", records["orangeEmpty"]),
                    ("Lava / Red over Lava / Orange", records["redOrange"]),
                ))
            route_html.append(
                f'<section class="route"><h3>{html.escape(route["label"])}</h3>'
                f'<p>{html.escape(route["description"])}</p><div class="figures">'
                f'{figures}</div></section>')
        cards.append(f'''<article data-key="{html.escape(key)}">
<h2>{number}. Lava · {html.escape(label)}</h2>
<p><strong>Exact owner corners:</strong> {html.escape(", ".join(owner_corners))}. No rotation search is performed: both candidates use this same canonical corner assignment.</p>
<div class="routes">{"".join(route_html)}</div>
<div class="answer-row"><label>Decision<select class="answer">{options}</select></label><label>Note<input class="note" type="text" placeholder="Optional detail"></label></div>
</article>''')
        questions.append({
            "key": key,
            "topology": topology,
            "ownerCorners": owner_corners,
            "routes": route_rows,
        })

    checker_html = "".join(
        f'<figure><img src="{html.escape(checker_records[key]["file"])}" '
        f'alt="{html.escape(ROUTES[key]["label"])} four-tile seam checker">'
        f'<figcaption>{html.escape(ROUTES[key]["label"])} · four rotated corner masks meet at the magenta cross</figcaption></figure>'
        for key in ROUTES)
    script = interaction_script(checkpoint)
    document = f'''<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>{html.escape(checkpoint)} Lava masks</title>
<style>:root{{color-scheme:dark}}*{{box-sizing:border-box}}body{{max-width:1780px;margin:auto;padding:20px;background:#181818;color:#eee;font:14px/1.45 system-ui,sans-serif}}h1{{margin-top:0}}h2{{margin:0}}article{{border-top:2px solid #666;padding:20px 0;margin:24px 0}}button,select,input,textarea{{font:inherit}}button,select,input{{padding:8px;background:#242424;color:#fff;border:1px solid #777}}.toolbar,.answer-row{{display:flex;gap:10px;align-items:end;flex-wrap:wrap;margin:12px 0}}label{{display:grid;gap:4px;min-width:330px}}.routes,.checkers{{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:16px}}.route{{padding:14px;background:#242424;border:1px solid #555}}.figures{{display:grid;grid-template-columns:repeat(3,minmax(128px,256px));gap:10px}}figure{{margin:0}}figure img{{display:block;width:256px;height:256px;max-width:100%;object-fit:contain;background:#303030}}.checkers img{{width:512px;height:512px}}figcaption{{margin-top:5px;color:#c7d3df}}.notice{{border-left:4px solid #e09a33;padding:10px 14px;background:#28231c}}#answer-export-text{{width:100%;min-height:130px;background:#242424;color:#fff;border:1px solid #666}}@media(max-width:950px){{.routes,.checkers{{grid-template-columns:1fr}}}}@media(max-width:650px){{.figures{{grid-template-columns:1fr}}label{{min-width:100%}}}}</style></head><body>
<h1>Blend-mask {html.escape(checkpoint)} focused review</h1>
<p class="notice"><strong>Why this is a pilot:</strong> the reviewed catalog contains genuine uniform Lava artwork but no authored Lava transition family. The current runtime therefore reaches the rejected geometric fallback. These two candidates reuse only already-approved natural ownership mattes. The EI-HD article is used as a seam contract, not as an asset source: a half mask is authoritative, other topologies keep exact rotated/inverted A/B edge profiles, and the four-tile checks below expose the joins. Zero EI-HD pixels are copied.</p>
<p>Both routes pass every exact boundary-profile and corner-polarity check. The orange heatmap is feather, green is ≥80% owner, navy is ≤20% owner. A failed historical percentage gate is disclosed in the JSON but does not silently reject a user-approved authored mask.</p>
<div class="toolbar"><button id="submit-answers">Submit answers to Codex</button><button id="copy-answers">Copy answers</button><button id="download-answers">Download answers.txt</button><button id="show-answers">Select review text</button><span id="status"></span></div>
<textarea id="answer-export-text" readonly aria-label="Saved review text"></textarea>
<h2>EI-HD-style four-tile seam checks</h2><div class="checkers">{checker_html}</div>
<h2>Exact uniforms used in every outcome</h2><div class="figures"><figure><img src="{red_record["file"]}" alt="Lava Red uniform"><figcaption>Lava / Red · bz14h slot 4</figcaption></figure><figure><img src="{orange_record["file"]}" alt="Lava Orange uniform"><figcaption>Lava / Orange · Zone10 slot 5</figcaption></figure></div>
<main>{"".join(cards)}</main><script>{script}</script></body></html>'''
    html_path = output / f"blend-mask-{checkpoint.lower()}-questionnaire.html"
    html_path.write_text(document, encoding="utf-8")
    analysis = {
        "schema": "ei-lava-mask-seam-pilot-v1",
        "checkpoint": checkpoint,
        "status": "review-only-not-installed",
        "semanticPresetSha256": "1B8493A9CC562603D57B4BDE97F069534B94AFE9A85D1F901C0AF7941B1E6D7A",
        "eiHdAssetUse": "method-only; zero EI-HD pixels copied",
        "uniformSource": source,
        "uniformImages": {"red": red_record, "orange": orange_record},
        "routes": route_analysis,
        "questions": questions,
    }
    analysis_path = output / "lava-mask-seam-analysis.json"
    analysis_path.write_text(json.dumps(
        analysis, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    return {"html": html_path, "analysis": analysis_path,
            "questionCount": len(questions)}


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--hd-res", type=Path, required=True)
    parser.add_argument("--mask-root", type=Path,
                        default=Path("assets/resources/terrain_blend_masks"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--checkpoint", default="R123-LAVA-SEAM-PILOT")
    args = parser.parse_args()
    red, orange, source = load_lava_uniforms(args.hd_res)
    result = render(args.mask_root, args.output, red, orange, source,
                    args.checkpoint)
    print(result["html"])
    print(result["analysis"])
    print(f"questions={result['questionCount']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
