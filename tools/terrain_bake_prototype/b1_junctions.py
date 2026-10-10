"""B1 evidence from the C++ renderer and existing private stage 0/A snapshots.

No stock assets are embedded. Writes only the ignored build-tests-B1-junctions.
Use the existing Python environment; no network or dependency installation.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw


def read(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def prepare(repo, out):
    old = repo / "build-tests-132-bakecore"
    receipt = read(old / "zone-input-receipt.json")
    for name, expected in receipt["snapshots"].items():
        assert sha(Path(name)) == expected, name
    atlas = read(repo / "build-tests-terrain-bake-stage-1b/input/atlas.json")
    fingerprint = hashlib.sha256()
    fingerprint.update(f"{atlas['catalog']['textureCount'] * 64}\n".encode())
    for slot in range(atlas["catalog"]["textureCount"] * 64):
        im = Image.open(old / f"zone-input/input/atlas/{slot}.png").convert("RGBA")
        fingerprint.update(f"{im.width}x{im.height}\n".encode())
        fingerprint.update(im.tobytes())
    assert fingerprint.hexdigest() == receipt["atlasPixelSha256"] == atlas["pixelSha256"]
    fixture = read(old / "zone-fixture.json")
    private = out / "zone-input"
    private.mkdir(exist_ok=True)
    # Copy only actually consumed uniform tiles and banks to owned evidence.
    for m in fixture["materials"]:
        paths = []
        for name in m["tiles"]:
            src = Path(name)
            dst = private / src.name
            dst.write_bytes(src.read_bytes())
            paths.append(str(dst))
        m["tiles"] = paths
    for b in fixture["brushes"]:
        src = Path(b["file"])
        dst = private / src.name
        dst.write_bytes(src.read_bytes())
        b["file"] = str(dst)
    fixture["legacy"] = False
    fixture["output"] = str(out / "zone6-b1.png")
    write(out / "zone-fixture.json", fixture)
    fixture["legacy"] = True
    fixture["output"] = str(out / "zone6-a.png")
    write(out / "zone-fixture-a.json", fixture)
    receipt["consumed"] = {str(p): sha(p) for p in sorted(private.iterdir())}
    write(out / "input-receipt.json", receipt)
    print("Zone6 inputs verified and copied; 1216 RGBA atlas slots fingerprint matched", flush=True)


def groups(field, types, width, height):
    result = collections.Counter()
    for y in range(height):
        for x in range(width):
            s = (int(field[y, x]), int(field[y, x+1]), int(field[y+1, x+1]), int(field[y+1, x]))
            result[(s, types[y*width+x])] += 1
    return result


def count(group, junction_n=4):
    return sum(min(junction_n if len(set(s)) > 2 else 4, n) for (s, _), n in group.items())


def corpus(repo, out):
    root = repo / "build-tests-terrain-bake-research-0"
    stats = read(root / "statistics.json")
    rows = []
    snapshots = {}
    for row in stats["rows"]:
        if row["profile"] != "deja-vu":
            continue
        path = root / row["export"]
        m = read(path)
        assert m["semanticExact"]
        assert sha(Path(m["path"])) == m["sha256"], m["file"]
        snapshots[str(path)] = sha(path)
        w, h = m["width"], m["height"]
        field = np.asarray(m["field"]).reshape(h+1, w+1)
        types = [m["runtimeTypes"][p & 0x3fff] for p in m["land"]]
        g = groups(field, types, w, h)
        overlay = collections.Counter()
        for p, s in zip(m["overlay"], m["overlaySignatures"]):
            if p >= 0 and s and min(s) > 0:
                overlay[(tuple(s), m["runtimeTypes"][p & 0x3fff])] += 1
        # Same finite grammar assumption as stage 0; not physical pinned slots.
        overlay_estimate = count(overlay)
        binary = sum(min(4, n) for (s, _), n in g.items() if len(set(s)) <= 2)
        rows.append({"map": m["file"], "stage0N4": row["n4"], "binaryLandN4": binary,
                     "junctionLandN4": count(g)-binary, "landN4": count(g),
                     "overlayEstimateN4": overlay_estimate,
                     "totalN4": count(g)+overlay_estimate,
                     "junctionN2Total": count(g, 2)+overlay_estimate,
                     "junctionN1Total": count(g, 1)+overlay_estimate})
    summary = {key: {"fit2048": sum(r[key] <= 2048 for r in rows),
                     "fit4096": sum(r[key] <= 4096 for r in rows),
                     "maximum": max(r[key] for r in rows)}
               for key in ("stage0N4", "totalN4", "junctionN2Total", "junctionN1Total")}
    write(out / "corpus-budget.json", {"rows": rows, "summary": summary, "snapshots": snapshots,
          "basis": "finite ordered signatures + reviewed predecessor class; shared field land, stage0 overlay estimate; no pixel bake, animations/pinned reserve not measured"})
    print(json.dumps(summary), flush=True)


def inspect(repo, out):
    fixture = read(out / "zone-fixture.json")
    field = np.asarray(fixture["field"]).reshape(33, 33)
    g = groups(field, fixture["preferredTypes"], 32, 32)
    nodes = {3: [], 4: []}
    counts = collections.Counter()
    for y in range(32):
        for x in range(32):
            n = len(set(field[y:y+2, x:x+2].ravel()))
            counts[n] += 1
            if n > 2:
                nodes[n].append((x, y))
    current = Image.open(repo / "build-tests-terrain-bake-stage-1b/current.png").convert("RGB")
    a = Image.open(out / "zone6-a.png").convert("RGB")
    b = Image.open(out / "zone6-b1.png").convert("RGB")
    assert current.size == a.size == b.size == (4096, 4096)
    old_a = Image.open(repo / "build-tests-132-bakecore/zone6-cpp-mixed-edge.png").convert("RGB")
    assert a.tobytes() == old_a.tobytes(), "two-material + legacy A bytes changed"
    typed = set()
    node_typed = set()
    unchanged = collections.Counter()
    aa, bb = np.asarray(a), np.asarray(b)
    for y in range(32):
        for x in range(32):
            ys = slice((31-y)*128, (32-y)*128)
            xs = slice(x*128, (x+1)*128)
            n = len(set(field[y:y+2, x:x+2].ravel()))
            h = hashlib.sha256(bb[ys, xs].tobytes()).hexdigest()
            key = (h, fixture["preferredTypes"][y*32+x])
            typed.add(key)
            if n > 2:
                node_typed.add(key)
            unchanged[n] += int(np.array_equal(aa[ys, xs], bb[ys, xs]))
    errors = []
    for t in range(1, 32):
        errors += [int(np.max(np.abs(bb[:, t*128-1].astype(int)-bb[:, t*128].astype(int)))),
                   int(np.max(np.abs(bb[t*128-1].astype(int)-bb[t*128].astype(int))))]
    assert max(errors) == 0 and len(typed) == count(g) == 439
    assert unchanged[1] == counts[1] and unchanged[2] == counts[2]
    marked = a.copy()
    draw = ImageDraw.Draw(marked)
    for n, cells in nodes.items():
        for x, y in cells:
            draw.rectangle((x*128+2, (31-y)*128+2, (x+1)*128-3, (32-y)*128-3),
                           outline=(255, 60, 170) if n == 3 else (0, 240, 255), width=5)
    current.save(out / "zone6-current.png")
    marked.save(out / "zone6-a-unsupported.png")
    panels = [("Current", current), ("A: unsupported pink=3 cyan=4", marked), ("B1: supported", b)]
    preview = Image.new("RGB", (2040, 720), (24, 24, 24))
    d = ImageDraw.Draw(preview)
    for i, (label, im) in enumerate(panels):
        d.text((680*i+8, 8), label, fill="white")
        preview.paste(im.resize((680, 680), Image.Resampling.LANCZOS), (680*i, 32))
    preview.save(out / "preview.png")
    # Four distant parts of the crop include the lime and grass junctions too.
    selected = []
    for tx, ty in ((3, 3), (27, 27), (5, 24), (26, 5)):
        candidates = [(x, y) for x, y in nodes[3] if 2 <= x <= 29 and 2 <= y <= 29
                      and all(abs(x-a)+abs(y-b) >= 10 for a, b in selected)]
        selected.append(min(candidates, key=lambda p: ((p[0]-tx)**2+(p[1]-ty)**2, p)))
    assert len(selected) == 4 and len(nodes[4]) == 1
    selected += nodes[4]
    crops = []
    for index, (x, y) in enumerate(selected):
        left, bottom = max(0, min(27, x-2)), max(0, min(27, y-2))
        box = (left*128, (32-bottom-5)*128, (left+5)*128, (32-bottom)*128)
        sheet = Image.new("RGB", (1920, 684), (24, 24, 24))
        d = ImageDraw.Draw(sheet)
        for i, (label, im) in enumerate(panels):
            d.text((i*640+8, 8), f"{label} / node world ({88+x},{136+y})", fill="white")
            sheet.paste(im.crop(box), (i*640, 32))
            cx, cy = (x-left)*128+64, (4-(y-bottom))*128+64
            # Margin tick marks locate the central cell without painting its pixels.
            d.line((i*640+cx, 24, i*640+cx, 31), fill="white", width=2)
        name = f"closeup-{index+1}-{'triple' if index < 4 else 'quad'}.png"
        sheet.save(out / name)
        crops.append({"file": name, "nodeWorld": [88+x, 136+y], "cropOrigin": [88+left, 136+bottom]})
    result = {"unsupportedBefore": counts[3]+counts[4], "unsupportedAfter": 0,
              "cellsByMaterialCount": dict(counts), "typedUnique": len(typed),
              "junctionTypedUnique": len(node_typed), "recipeSlots": count(g),
              "binaryRecipeSlots": sum(min(4, n) for (s, _), n in g.items() if len(set(s)) <= 2),
              "unchangedCellsByMaterialCount": dict(unchanged), "legacyAByteIdentical": True,
              "edges": 1984, "rgbChannelComparisons": 1984*128*3, "maxSeamDifference": max(errors),
              "closeups": crops, "pngs": {p.name: {"sha256": sha(p), "size": Image.open(p).size}
                         for p in sorted(out.glob("*.png"))}}
    write(out / "verification.json", result)
    for name, expected in read(out / "input-receipt.json")["consumed"].items():
        assert sha(Path(name)) == expected
    print(json.dumps({k: v for k, v in result.items() if k not in ("pngs", "closeups")}), flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("step", choices=("prepare", "corpus", "inspect"))
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[2])
    args = parser.parse_args()
    repo = args.repo.resolve()
    out = repo / "build-tests-B1-junctions"
    out.mkdir(exist_ok=True)
    {"prepare": prepare, "corpus": corpus, "inspect": inspect}[args.step](repo, out)
