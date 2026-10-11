"""Independent PNG/input readback and diagnostic measurements for stage C.

Usage: python -B tools/terrain_bake_prototype/stage_c_verify.py
Only build-tests-stagec is written. Zero seam is not required for stock/tones:
the measurements deliberately retain visible grid and quantization failures.
"""
import ast
from collections import Counter
import hashlib
import json
import math
from pathlib import Path
import sys

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageFilter
import stage_c as c


def gray(a):
    return np.sum(a.astype(np.float64)*[.2126, .7152, .0722], axis=-1)


def corr(a, b):
    a, b = a.ravel(), b.ravel()
    a, b = a-a.mean(), b-b.mean()
    d = float(np.sqrt(np.dot(a, a)*np.dot(b, b)))
    return float(np.dot(a, b)/d) if d else None


def run():
    checkout = Path(__file__).resolve().parents[2]
    root = checkout/'build-tests-stagec'
    result = c.read(root/'result.json')
    crop, counts = result['crop'], result['counts']
    cells = c.read(root/'cells.json')
    m = c.read(root/'input'/'zone6.json')
    atlas = c.read(root/'input'/'zone6-atlas.json')
    size, p = crop['size'], result['parameters']['pixels']
    modes = tuple(counts)
    checks = Counter()
    def check(v, label, count=1):
        if not v:
            raise AssertionError(label)
        checks[label] += count
    witnesses = c.read(root/'input-witnesses.json')
    for w in witnesses:
        # Existing private snapshot inputs only; no live MPR/RES reads.
        check(c.sha(w['source']) == w['sha256'] == c.sha(root/w['copy']), 'input hashes')
    fingerprint = hashlib.sha256(f"{m['atlasSlots']}\n".encode('ascii'))
    for slot in range(m['atlasSlots']):
        with Image.open(root/'input'/'hd-atlas'/f'{slot}.png') as im:
            check(im.size == (256, 256), 'source dimensions')
            fingerprint.update(b'256x256\n')
            fingerprint.update(im.convert('RGBA').tobytes())
    check(fingerprint.hexdigest() == atlas['pixelSha256'], 'HD exact fingerprint')
    images = {}
    for mode in modes:
        with Image.open(root/f'{mode}.png') as im:
            check(im.size == (size*p, size*p), 'full PNG dimensions')
            # Restore grid-space cell/raster ordering for independent readback.
            images[mode] = np.asarray(im.convert('RGB'))[::-1].copy()
    ps = c.pools(m, atlas)
    # Independent scalar translation of the 136 selector, using struct bytes.
    def independent_pick(x, y, mat, slots):
        import struct
        h = 2166136261
        for byte in struct.pack('<7I', c.SEED, x, y, mat, mat, mat, mat):
            h = ((h ^ byte)*16777619) % 2**32
        h = ((h ^ (h >> 16))*0x7feb352d) % 2**32
        h = ((h ^ (h >> 15))*0x846ca68b) % 2**32
        h ^= h >> 16
        r = h ^ 0x9e3779b9
        r = ((r ^ (r >> 16))*0x85ebca6b) % 2**32
        r ^= r >> 13
        return slots[h % len(slots)], r % 4
    new_hashes = {mode: set() for mode in modes}
    bounded_records = {(r['x'], r['y']): r for r in c.read(root/'hex-bank8-cells.json')} if 'hex-bank8' in modes else {}
    lowmask = np.zeros((size, size), bool)
    source_results = Counter()
    eligible_count, modified_current = 0, 0
    for record in cells:
        x, y, mat, cl = [record[k] for k in ('x', 'y', 'material', 'class')]
        dx, dy = x-crop['x'], y-crop['y']
        box = slice(dy*p, (dy+1)*p), slice(dx*p, (dx+1)*p)
        base = images['current136'][box]
        with Image.open(root/'input'/'hd-atlas'/f"{record['slot']}.png") as im:
            im = im.convert('RGB').transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            q = im.width//16
            im = im.crop((q, q, im.width-q, im.height-q)).resize((p, p), Image.Resampling.BILINEAR)
            expected = np.asarray(im.rotate(record['rotation']*90, expand=False))
        check(np.array_equal(expected, base), 'baseline PNG source readback')
        modified_current += int(record['oldPacked'] != record['slot']+(record['rotation'] << 14))
        if record['eligible']:
            eligible_count += 1
            check(independent_pick(x, y, mat, ps[(mat, cl)]) == (record['slot'], record['rotation']), '136 scalar selector')
            source_results[record['sourceDetail']] += 1
        for mode in modes[1:]:
            tile = images[mode][box]
            changed = record['eligible'] and (mode != 'mix2-world' or len(record['candidateSlots']) >= 2)
            if not changed:
                check(np.array_equal(base, tile), 'unaffected cell bytes')
                continue
            if mode.startswith('macro'):
                n = int(mode[-1])
                ti = record['tones'][str(n)]
                level = -1+2*ti/(n-1)
                expected = np.uint8(np.clip(np.rint(base*(1+level*(c.AMPLITUDE+c.TINT))), 0, 255))
                check(np.array_equal(tile, expected), 'tone PNG readback')
                tile = np.rot90(tile, -record['rotation'])
            elif mode == 'hex-bank8':
                r = bounded_records[(x, y)]
                label = (c.hash136(x, y, mat, c.SEED+1) >> 8) % 8
                check(label == r['label'], 'finite hex label')
                with Image.open(root/'hex-bank8'/f"{mat}-{cl}-{label}.png") as im:
                    expected = np.asarray(im.convert('RGB'))
                tile = np.rot90(tile, -record['rotation'])
                check(np.array_equal(tile, expected), 'finite hex bank readback')
            new_hashes[mode].add((mat, cl, hashlib.sha256(tile.tobytes()).hexdigest()))
        lowmask[dy, dx] = mat == crop['dominantMaterial']
    for mode in modes:
        check(len(new_hashes[mode]) == counts[mode]['newPhysicalSlots'], 'typed pixel slot count')
    # Diagnostic image metrics only within adjoining dominant-material cells.
    metrics = {}
    mask = np.repeat(np.repeat(lowmask, p, 0), p, 1)
    for mode, a in images.items():
        g = gray(a)
        blurred = np.asarray(Image.fromarray(np.uint8(np.clip(g, 0, 255))).filter(ImageFilter.GaussianBlur(3)), float)
        highpass = g-blurred
        pair_x = mask[:, :-p] & mask[:, p:]
        pair_y = mask[:-p] & mask[p:]
        repeat = {'x': corr(highpass[:, :-p][pair_x], highpass[:, p:][pair_x]),
                  'y': corr(highpass[:-p][pair_y], highpass[p:][pair_y])}
        seam, inside, boundary_pairs = [], [], 0
        for dy in range(size):
            for dx in range(size):
                if not lowmask[dy, dx]:
                    continue
                tile = g[dy*p:(dy+1)*p, dx*p:(dx+1)*p]
                inside.append(np.abs(np.diff(tile[8:-8, 8:-8], axis=0)).ravel())
                inside.append(np.abs(np.diff(tile[8:-8, 8:-8], axis=1)).ravel())
                if dx+1 < size and lowmask[dy, dx+1]:
                    xx = (dx+1)*p
                    seam.extend(np.abs(g[dy*p:(dy+1)*p, xx-1]-g[dy*p:(dy+1)*p, xx]))
                    boundary_pairs += 1
                if dy+1 < size and lowmask[dy+1, dx]:
                    yy = (dy+1)*p
                    seam.extend(np.abs(g[yy-1, dx*p:(dx+1)*p]-g[yy, dx*p:(dx+1)*p]))
                    boundary_pairs += 1
        seam, inside = np.asarray(seam), np.concatenate(inside)
        means = np.array([g[y*p:(y+1)*p, x*p:(x+1)*p].mean() for y in range(size) for x in range(size) if lowmask[y, x]])
        metrics[mode] = {'highpassLagOneCellCorrelation': repeat,
                         'highpassStd': float(highpass[mask].std()),
                         'cellMeanStd': float(means.std()), 'dominantCellEdges': boundary_pairs,
                         'seamLumaMean': float(seam.mean()), 'seamLumaP95': float(np.percentile(seam, 95)),
                         'interiorPixelDeltaMean': float(inside.mean()),
                         'gridToInteriorRatio': float(seam.mean()/inside.mean())}
    # Check C0 of free-world same-material functions by evaluating the shared
    # coordinates from adjacent cell origins. This is not a test of transition seams.
    tx = c.Textures(root/'input'/'hd-atlas')
    traces = {}
    t = np.linspace(0, 1, 129)
    for mode, func in (('mix2-world', c.mix_sample), ('hex-world', c.hex_sample)):
        maxerr, edges, samples = 0., 0, 0
        for dy in range(size):
            for dx in range(size):
                r = cells[dy*size+dx]
                if not r['eligible'] or (mode == 'mix2-world' and len(r['candidateSlots']) < 2):
                    continue
                for ox, oy in ((1, 0), (0, 1)):
                    if dx+ox >= size or dy+oy >= size:
                        continue
                    q = cells[(dy+oy)*size+dx+ox]
                    if (q['material'], q['class']) != (r['material'], r['class']) or not q['eligible']:
                        continue
                    x, y = r['x'], r['y']
                    xa, ya = (np.full_like(t, x+1), y+t) if ox else (x+t, np.full_like(t, y+1))
                    xb, yb = (np.full_like(t, q['x']), q['y']+t) if ox else (q['x']+t, np.full_like(t, q['y']))
                    lhs = func(tx, r['candidateSlots'], r['material'], xa, ya)
                    rhs = func(tx, q['candidateSlots'], q['material'], xb, yb)
                    maxerr = max(maxerr, float(np.abs(lhs-rhs).max()))
                    edges += 1
                    samples += len(t)
        check(maxerr < 1e-9, 'same-material analytic C0', edges)
        traces[mode] = {'edges': edges, 'samples': samples, 'maxRgbDifference': maxerr}
    # Adjacent triangular-simplex sides must approach the same result. A change
    # to vertex/weight indexing would fail this independent coordinate probe.
    candidates = next(r['candidateSlots'] for r in cells if r['material'] == crop['dominantMaterial'])
    eps = 1e-8
    u = np.linspace(.02, .98, 65)+crop['x']
    v = np.floor(crop['y']*2/math.sqrt(3)) + 1-(u-np.floor(u))
    x, y = u+v/2, v*math.sqrt(3)/2
    left = c.hex_sample(tx, candidates, crop['dominantMaterial'], x-eps, y)
    right = c.hex_sample(tx, candidates, crop['dominantMaterial'], x+eps, y)
    triangle_error = float(np.abs(left-right).max())
    check(triangle_error < .01, 'hex simplex continuity', len(u))
    tones = {}
    for n in (4, 8):
        edges, different, max_step = 0, 0, 0.
        for dy in range(size):
            for dx in range(size):
                r = cells[dy*size+dx]
                if not r['eligible']:
                    continue
                for ox, oy in ((1, 0), (0, 1)):
                    if dx+ox >= size or dy+oy >= size:
                        continue
                    q = cells[(dy+oy)*size+dx+ox]
                    if (r['material'], r['class']) != (q['material'], q['class']) or not q['eligible']:
                        continue
                    delta = abs(r['tones'][str(n)]-q['tones'][str(n)])
                    edges += 1
                    different += int(delta > 0)
                    max_step = max(max_step, delta*2*c.AMPLITUDE/(n-1))
        tones[f'macro{n}'] = {'sameMaterialEdges': edges, 'changedToneEdges': different,
                              'maxRelativeBrightnessStep': max_step,
                              'toneLevelsUsed': dict(Counter(r['tones'][str(n)] for r in cells if r['eligible']))}
    b = c.read(root/'budgets.json')
    check(len(b['maps']) == 41, 'corpus coverage')
    for row in b['maps']:
        for mode in modes:
            expected = math.ceil((row['allocated']+row['newSlots'][mode])/64)*64
            check(row['appendCapacity'][mode] == expected, 'page rounding')
        check(row['newSlots']['mix2-world'] <= row['newSlots']['hex-world'] <= row['landUniform'], 'eligible budget bounds')
    for f in ('preview.png', 'closeup.png', 'tone-map.png'):
        with Image.open(root/f) as im:
            check(im.width <= 2048, 'contact PNG dimensions')
            im.load()
    for f in ('stage_c.py', 'stage_c_bounded.py', 'stage_c_verify.py'):
        ast.parse((checkout/'tools'/'terrain_bake_prototype'/f).read_text(encoding='utf-8'))
        checks['Python AST'] += 1
    out = {'PASS': sum(checks.values()), 'FAIL': 0, 'SKIP': 0, 'checks': dict(checks),
           'sourceDetailEligible': dict(source_results), 'eligibleCells': eligible_count,
           'baselineChangedRefs': modified_current, 'metrics': metrics, 'toneBoundaries': tones,
           'analyticSameMaterialTraces': traces, 'hexSimplexMaxDifference': triangle_error,
           'artifacts': {p.name: c.sha(p) for p in root.glob('*.png')},
           'sourceFiles': {f: c.sha(checkout/'tools'/'terrain_bake_prototype'/f) for f in ('stage_c.py', 'stage_c_bounded.py', 'stage_c_verify.py')},
           'runtime': 'NOT RUN', 'ownerVisualAcceptance': 'NOT RUN',
           'note': 'diagnostic correlations/contrast/grid ratios are not art PASS or seam guarantees for stock/tones/transitions'}
    c.write(root/'verification.json', out)
    print(json.dumps(out, indent=2))


if __name__ == '__main__':
    run()
