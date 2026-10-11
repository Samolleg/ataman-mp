"""EI-TERRAIN-BAKE-STAGE-C: offline, private snapshot comparison.

No game/editor launch, clipboard, network, live reads/writes or program changes.
Existing numpy/Pillow only. Never emits game pixels outside build-tests-stagec.
Reads stage-0 JSON and the private, conserved HD republish-138 witness.
Usage: python -B tools/terrain_bake_prototype/stage_c.py [--output build-tests-stagec]
"""
import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import shutil
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

SEED = 138
PIXELS = 256
SIZE = 16
PERIOD = 12.0
AMPLITUDE = .06
TINT = np.array([.012, .004, -.012], np.float32)
MODES = ('current136', 'macro4', 'macro8', 'mix2-world', 'hex-world')


def read(p):
    return json.loads(Path(p).read_text(encoding='utf-8-sig'))


def write(p, v):
    Path(p).write_text(json.dumps(v, ensure_ascii=False, indent=2), encoding='utf-8')


def sha(p):
    h = hashlib.sha256()
    with Path(p).open('rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''):
            h.update(b)
    return h.hexdigest()


def members(p):
    """Ordinary Ataman RES layout. Read-only; not an MPR loader."""
    b = Path(p).read_bytes()
    magic, count, off, names = struct.unpack_from('<4I', b)
    assert magic == 0x019ce23c and off + 22*count + names <= len(b)
    base = off + 22*count
    out = {}
    for i in range(count):
        _, size, data, _, nlen, noff = struct.unpack_from('<4IHI', b, off+22*i)
        if not nlen:
            continue
        assert noff+nlen <= names and data+size <= len(b)
        name = b[base+noff:base+noff+nlen].decode('cp1251').lower()
        assert name not in out
        out[name] = b[data:data+size]
    return out


def bc1(payload):
    magic, w, h, levels, fmt = struct.unpack_from('<5I', payload)
    assert magic == 0x00504d4d and fmt == 0x31545844 and w == h
    assert w % 4 == 0 and levels == int(math.log2(w))+1
    blocks = np.frombuffer(payload[76:76+(w//4)*(h//4)*8], np.uint8).reshape(h//4, w//4, 8)
    def endpoint(lo, hi):
        v = lo.astype(np.uint16) | (hi.astype(np.uint16) << 8)
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        # Match Ataman decodeRgb565 integer scaling, not bit replication.
        return np.stack((r*255//31, g*255//63, b*255//31), -1).astype(np.uint16), v
    a, av = endpoint(blocks[..., 0], blocks[..., 1])
    b, bv = endpoint(blocks[..., 2], blocks[..., 3])
    colors = np.zeros((*av.shape, 4, 4), np.uint16)
    colors[..., 0, :3], colors[..., 1, :3] = a, b
    colors[..., 2, :3] = np.where((av > bv)[..., None], (2*a+b)//3, (a+b)//2)
    colors[..., 3, :3] = np.where((av > bv)[..., None], (a+2*b)//3, 0)
    colors[..., :3, 3] = 255
    colors[..., 3, 3] = np.where(av > bv, 255, 0)
    ids = np.zeros(av.shape, np.uint32)
    for i in range(4):
        ids |= blocks[..., i+4].astype(np.uint32) << (8*i)
    out = np.empty((h, w, 4), np.uint8)
    for y in range(4):
        for x in range(4):
            ix = ((ids >> (2*(4*y+x))) & 3).astype(int)
            out[y::4, x::4] = np.take_along_axis(colors, ix[..., None, None], axis=2)[..., 0, :]
    return out


def signature(s):
    return tuple(s.get('corners') or [s.get('signature', {}).get(k, 0)
                 for k in ('northWest', 'northEast', 'southEast', 'southWest')])


def pools(m, a):
    out = defaultdict(list)
    for s in a['catalog']['slots']:
        sig = signature(s)
        slot = s['slot']
        if (s.get('assigned') and not s.get('ambiguous') and len(set(sig)) == 1
                and sig[0] > 0 and slot < len(m['runtimeTypes'])):
            cl = m['runtimeTypes'][slot]
            if 0 <= cl < 16 and not s.get('mixedEdge'):
                out[(sig[0], cl)].append(slot)
    return {k: sorted(v) for k, v in out.items()}


def hash136(x, y, mat, seed=SEED):
    """Exact uint32 FNV+avalanche selector from terrain_edit_model.cpp."""
    h = 2166136261
    for v in (seed, x, y, mat, mat, mat, mat):
        for i in range(4):
            h = ((h ^ ((v >> (8*i)) & 255))*16777619) & 0xffffffff
    h ^= h >> 16
    h = (h*0x7feb352d) & 0xffffffff
    h ^= h >> 15
    h = (h*0x846ca68b) & 0xffffffff
    return h ^ (h >> 16)


def pick136(x, y, mat, candidates, weights=None):
    h = hash136(x, y, mat)
    weights = weights or [1]*len(candidates)
    sel = h % sum(weights)
    for slot, weight in zip(candidates, weights):
        if sel < weight:
            break
        sel -= weight
    rh = h ^ 0x9e3779b9
    rh ^= rh >> 16
    rh = (rh*0x85ebca6b) & 0xffffffff
    rh ^= rh >> 13
    return slot, rh % 4


def noise(x, y, salt):
    """Continuous world value noise; defined for negative coordinates too."""
    def lattice(a, b):
        with np.errstate(over='ignore'):
            z = a.astype(np.uint64)*np.uint64(0x9e3779b1) ^ b.astype(np.uint64)*np.uint64(0x85ebca77) ^ np.uint64(salt)
            z = (z ^ (z >> np.uint64(16)))*np.uint64(0x7feb352d)
            z = (z ^ (z >> np.uint64(15)))*np.uint64(0x846ca68b)
            z ^= z >> np.uint64(16)
        return (z & np.uint64(0xffffff)).astype(np.float64)/0xffffff*2-1
    ix, iy = np.floor(x).astype(np.int64), np.floor(y).astype(np.int64)
    fx, fy = x-ix, y-iy
    fx, fy = fx*fx*(3-2*fx), fy*fy*(3-2*fy)
    return ((1-fx)*lattice(ix, iy)+fx*lattice(ix+1, iy))*(1-fy) + ((1-fx)*lattice(ix, iy+1)+fx*lattice(ix+1, iy+1))*fy


def macro(x, y, mat):
    # Independent world seed from source variants; one channel controls both
    # brightness and a restrained warm/cool tint to avoid a 2D tone cartesian product.
    return noise(np.asarray(x)/PERIOD, np.asarray(y)/PERIOD, (mat & 0xffff)+901)


def tone_index(x, y, mat, n):
    return int(np.clip(np.floor((float(macro(x+.5, y+.5, mat))+1)*n/2), 0, n-1))


def tone(rgb, i, n):
    level = -1+2*i/(n-1)
    return np.clip(np.rint(rgb*(1+level*(AMPLITUDE+TINT))), 0, 255).astype(np.uint8)


def natural(name):
    return name.startswith(('Grass /', 'Ground /')) and 'underwater' not in name.lower()


def integral(a):
    return np.pad(a.astype(np.int64).cumsum(0).cumsum(1), ((1, 0), (1, 0)))


def sums(a, size):
    t = integral(a)
    return t[size:, size:]-t[:-size, size:]-t[size:, :-size]+t[:-size, :-size]


def choose(m, a, ps):
    w, h = m['width'], m['height']
    names = {f['id']: f['name'] for f in a['catalog']['families']}
    field = np.asarray(m['field']).reshape(h+1, w+1)
    eligible = np.zeros((h, w), bool)
    matfield = np.zeros((h, w), np.int64)
    for k, s in enumerate(m['landSignatures']):
        if not s or len(set(s)) != 1:
            continue
        y, x = divmod(k, w)
        p = m['land'][k] & 0x3fff
        cl = m['runtimeTypes'][p]
        fs = [field[y, x], field[y, x+1], field[y+1, x+1], field[y+1, x]]
        if natural(names.get(s[0], '')) and (s[0], cl) in ps and list(s) == fs:
            eligible[y, x] = True
            matfield[y, x] = s[0]
    nover = sums(np.asarray(m['overlay']).reshape(h, w) >= 0, SIZE) == 0
    best = None
    for mat in np.unique(matfield):
        if mat <= 0:
            continue
        score = sums(matfield == mat, SIZE)
        score[~nover] = -1
        y, x = np.unravel_index(np.argmax(score), score.shape)
        item = (int(score[y, x]), int(mat), int(x), int(y))
        if best is None or item > best:
            best = item
    count, mat, x, y = best
    assert count >= SIZE*SIZE*.6, 'No sufficiently large real grass/ground field'
    return {'map': m['file'], 'x': x, 'y': y, 'size': SIZE, 'dominantMaterial': mat,
            'dominantName': names[mat], 'dominantUniformCells': count,
            'eligibleNaturalCells': int(eligible[y:y+SIZE, x:x+SIZE].sum()),
            'selection': f'maximum {SIZE}x{SIZE} single grass/ground material, exact unchanged field, no overlay'}


class Textures:
    def __init__(self, folder):
        self.folder = folder
        self.cache = {}

    def image(self, slot, rot=0):
        key = slot, rot
        if key not in self.cache:
            im = Image.open(self.folder/f'{slot}.png').convert('RGB')
            im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
            p = im.width//16
            im = im.crop((p, p, im.width-p, im.height-p)).resize((PIXELS, PIXELS), Image.Resampling.BILINEAR)
            # Resample each physical recipe once; quarter turns are SEC state.
            # Separate rotate-then-resize paths have 1-LSB Pillow roundoff and
            # must not be charged as additional physical slots.
            self.cache[key] = np.asarray(im.rotate(rot*90, expand=False))
        return self.cache[key]

    def sample(self, slot, x, y, rot=0):
        # Bilinear periodic sampling of UV-inset artwork, same density as base.
        tex = self.image(slot, rot)
        u, v = np.mod(x, 1)*PIXELS-.5, np.mod(y, 1)*PIXELS-.5
        ix, iy = np.floor(u).astype(int), np.floor(v).astype(int)
        fx, fy = (u-ix)[..., None], (v-iy)[..., None]
        return ((tex[iy % PIXELS, ix % PIXELS]*(1-fx)+tex[iy % PIXELS, (ix+1) % PIXELS]*fx)*(1-fy)
                + (tex[(iy+1) % PIXELS, ix % PIXELS]*(1-fx)+tex[(iy+1) % PIXELS, (ix+1) % PIXELS]*fx)*fy)


def phase(mat, i, j):
    b = hashlib.sha256(f'{mat},{i},{j},stagec'.encode('ascii')).digest()
    return int.from_bytes(b[:4], 'little'), int.from_bytes(b[4:8], 'little')/2**32, int.from_bytes(b[8:12], 'little')/2**32


def hex_sample(tx, candidates, mat, x, y):
    """Actual triangular-lattice/hex-neighborhood blend, three randomized sources.

    Barycentric partition of unity on equilateral triangles. Each lattice vertex
    owns one source, quarter rotation and periodic translation shared by all
    neighboring triangles. No claim of implementing histogram-preserving papers.
    """
    u, v = x-y/math.sqrt(3), 2*y/math.sqrt(3)
    i, j = np.floor(u).astype(int), np.floor(v).astype(int)
    a, b = u-i, v-j
    lower = a+b <= 1
    vi = [np.where(lower, i, i+1), i+1, i]
    vj = [np.where(lower, j, j+1), j, j+1]
    weights = [np.where(lower, 1-a-b, a+b-1), np.where(lower, a, 1-b), np.where(lower, b, 1-a)]
    weights = [np.maximum(w, 0)**2 for w in weights]
    denom = sum(weights)
    out = np.zeros((*x.shape, 3), np.float64)
    for ii, jj, wt in zip(vi, vj, weights):
        for pair in np.unique(np.stack((ii.ravel(), jj.ravel()), 1), axis=0):
            pi, pj = map(int, pair)
            mask = (ii == pi) & (jj == pj)
            h, ox, oy = phase(mat, pi, pj)
            sample = tx.sample(candidates[h % len(candidates)], x[mask]+ox, y[mask]+oy, (h >> 8) % 4)
            out[mask] += sample*(wt[mask]/denom[mask])[..., None]
    return out


def mix_sample(tx, candidates, mat, x, y):
    if len(candidates) < 2:
        return None
    weight = (noise(x/2.7, y/2.7, (mat & 0xffff)+301)+1)/2
    # Independent translations and orientation. Global weight, fixed pair.
    a = tx.sample(candidates[0], x, y, 0)
    b = tx.sample(candidates[1], x+.371, y+.613, 1)
    return a*(1-weight[..., None])+b*weight[..., None]


def prepare(checkout, root):
    src = checkout/'build-tests-terrain-bake-research-0'
    hd = checkout/'build-tests-138-hdrepublish'
    inventory = read(src/'inventory.json')
    inv = {'maps': [m for m in inventory['maps'] if m['profile'] == 'deja-vu' and m['semanticExact']],
           'atlases': [a for a in inventory['atlases'] if a['profile'] == 'deja-vu' and a['semanticExact']]}
    assert len(inv['maps']) == 41
    dest = root/'input'
    (dest/'maps').mkdir(parents=True, exist_ok=True)
    witnesses = []
    def copy(p, q):
        before = sha(p)
        shutil.copyfile(p, q)
        assert sha(q) == before == sha(p)
        witnesses.append({'source': str(p), 'copy': str(q.relative_to(root)), 'sha256': before})
    copy(src/'inventory.json', dest/'inventory-original.json')
    write(dest/'inventory.json', inv)
    for m in inv['maps']:
        copy(src/m['export'], dest/m['export'])
    z = next(m for m in inv['maps'] if m['file'] == 'zone6.mpr')
    m = read(dest/z['export'])
    atlas = next(a for a in inv['atlases'] if a['directory'] == m['atlasDirectory'])
    final = read(hd/'published-copies-results-final.json')
    rec = next(r for r in final['maps'] if Path(r['map']).name == 'zone6.mpr')
    assert rec['SECIdentical'] and rec['classesAndAnimationsIdentical']
    old = hd/'maps'/'zone6.mpr'
    assert sha(old) == m['sha256']
    copy(old, dest/'zone6-original.mpr')  # copy/hash only; never opened in editor
    private = Path(rec['output'])
    copy(private/'HDPack_Terrain.res', dest/'zone6-hd138.res')
    copy(private/'receipt.json', dest/'zone6-hd138-receipt.json')
    receipt = read(dest/'zone6-hd138-receipt.json')
    assert receipt['publishedNamespace'] == rec['publishedNamespace']
    res = members(dest/'zone6-hd138.res')
    profs = [read_bytes for name, read_bytes in res.items() if name.startswith('ei_atlas_profile_')]
    assert len(profs) == 1
    profile = json.loads(profs[0].decode('utf-8-sig'))
    assert profile['atlasSlotCount'] == m['atlasSlots'] == 1216
    slots = {s['slot']: s for s in atlas['catalog']['slots']}
    assert all(signature(s) == signature(slots[s['slot']]) for s in profile['slots'])
    for s in profile['slots']:
        s0 = slots[s['slot']]
        assert {k:v for k,v in s.items() if k not in ('sourceNamespace', 'sourceSlots')} == {k:v for k,v in s0.items() if k not in ('sourceNamespace', 'sourceSlots')}
    atlas['catalog'] = profile
    atlas['pixelSha256'] = profile['atlasPixelSha256']
    atlas['namespace'] = profile['mprNamespace']
    folder = dest/'hd-atlas'
    folder.mkdir(exist_ok=True)
    fp = hashlib.sha256(f"{m['atlasSlots']}\n".encode('ascii'))
    for page in range(19):
        name = f"{rec['publishedNamespace']}{page:03}.mmp".lower()
        decoded = bc1(res[name])
        assert decoded.shape == (2048, 2048, 4)
        for local in range(64):
            y, x = divmod(local, 8)
            rawrow = 7-y
            tile = decoded[rawrow*256:(rawrow+1)*256, x*256:(x+1)*256].copy()
            fp.update(b'256x256\n')
            fp.update(tile.tobytes())
            Image.fromarray(tile).save(folder/f'{page*64+local}.png')
    assert fp.hexdigest() == profile['atlasPixelSha256'], (fp.hexdigest(), profile['atlasPixelSha256'])
    write(dest/'zone6.json', m)
    write(dest/'zone6-atlas.json', atlas)
    write(root/'input-witnesses.json', witnesses)
    return inv, m, atlas, rec


def render(root, m, atlas, rec):
    ps = pools(m, atlas)
    crop = choose(m, atlas, ps)
    names = {f['id']: f['name'] for f in atlas['catalog']['families']}
    properties = {s['slot']: s for s in atlas['catalog']['slots']}
    tx = Textures(root/'input'/'hd-atlas')
    images = {mode: Image.new('RGB', (SIZE*PIXELS, SIZE*PIXELS)) for mode in MODES}
    keys = {mode: set() for mode in MODES}
    hashes = {mode: set() for mode in MODES}
    cells = []
    status = {s['slot']: s['result'] for s in rec['slots']}
    gridx, gridy = np.meshgrid((np.arange(PIXELS)+.5)/PIXELS, (np.arange(PIXELS)+.5)/PIXELS)
    for dy in range(SIZE):
        for dx in range(SIZE):
            x, y = crop['x']+dx, crop['y']+dy
            k = y*m['width']+x
            packed = m['land'][k]
            oldslot, oldrot = packed & 0x3fff, packed >> 14
            cl = m['runtimeTypes'][oldslot]
            sig = m['landSignatures'][k]
            mat = sig[0] if len(set(sig)) == 1 else 0
            candidates = ps.get((mat, cl), [])
            eligible = bool(mat and natural(names.get(mat, '')) and candidates)
            slot, rot = oldslot, oldrot
            if eligible:
                assert all(properties[s].get('distributionWeight', 1) > 0 for s in candidates)
                assert all(properties[s].get('randomRotationAllowed', True) for s in candidates)
                assert all(not properties[s].get('canonicalGeneratedRoute', False) and not properties[s].get('routeOwnerMaterial', 0) for s in candidates)
                slot, rot = pick136(x, y, mat, candidates, [properties[s].get('distributionWeight', 1) for s in candidates])
            base = tx.image(slot, rot)
            outputs = {mode: base for mode in MODES}
            cellkeys = {mode: ('existing', slot) for mode in MODES}
            ti = {}
            if eligible:
                for n in (4, 8):
                    ti[n] = tone_index(x, y, mat, n)
                    # Tone is orientation-independent, so SEC rotates the same physical recipe.
                    outputs[f'macro{n}'] = tone(base, ti[n], n)
                    cellkeys[f'macro{n}'] = ('tone', mat, cl, slot, ti[n])
                wx, wy = x+gridx, y+gridy
                blend = mix_sample(tx, candidates, mat, wx, wy)
                if blend is not None:
                    outputs['mix2-world'] = np.uint8(np.clip(np.rint(blend), 0, 255))
                    cellkeys['mix2-world'] = ('world', mat, cl, x, y)
                outputs['hex-world'] = np.uint8(np.clip(np.rint(hex_sample(tx, candidates, mat, wx, wy)), 0, 255))
                cellkeys['hex-world'] = ('world', mat, cl, x, y)
            for mode, rgb in outputs.items():
                images[mode].paste(Image.fromarray(rgb), (dx*PIXELS, dy*PIXELS))
                keys[mode].add(cellkeys[mode])
                if cellkeys[mode][0] != 'existing':
                    canonical = np.rot90(rgb, -rot) if mode.startswith('macro') else rgb
                    hashes[mode].add((mat, cl, hashlib.sha256(canonical.tobytes()).hexdigest()))
            cells.append({'x': x, 'y': y, 'material': mat, 'class': cl, 'eligible': eligible,
                          'oldPacked': packed, 'slot': slot, 'rotation': rot, 'tones': ti,
                          'candidateSlots': candidates, 'sourceDetail': status[slot]})
        print(f'rendered {dy+1}/{SIZE} rows', flush=True)
    counts = {}
    for mode, im in images.items():
        # North-up: grid Y increases upwards. All panels use the same flip.
        im = im.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
        im.save(root/f'{mode}.png')
        images[mode] = im
        newkeys = len([k for k in keys[mode] if k[0] != 'existing'])
        newpixels = len(hashes[mode])
        # Different source slots can carry identical artwork. Typed pixel
        # dedup may save places; it must never manufacture more than keys.
        assert newpixels <= newkeys, (mode, newkeys, newpixels)
        counts[mode] = {'newPhysicalSlots': newpixels, 'keyCount': len(keys[mode]),
                        'sourceSlotsOnCrop': len(set(c['slot'] for c in cells)),
                        'atlasAllocatedWithAppend': m['atlasSlots']+newpixels,
                        'roundedTo64': math.ceil((m['atlasSlots']+newpixels)/64)*64}
    contact(root, images, counts)
    # Same 6x6 close-up, selected by homogeneous dominant-material count.
    mask = np.array([c['material'] == crop['dominantMaterial'] for c in cells]).reshape(SIZE, SIZE)
    cy, cx = np.unravel_index(np.argmax(sums(mask, 6)), (SIZE-5, SIZE-5))
    box = (int(cx)*PIXELS, (SIZE-int(cy)-6)*PIXELS, (int(cx)+6)*PIXELS, (SIZE-int(cy))*PIXELS)
    contact(root, images, counts, 'closeup.png', box)
    # Exposes macro quantization, separate from the artwork.
    sheet = Image.new('RGB', (SIZE*24*2, SIZE*24+40))
    d = ImageDraw.Draw(sheet)
    for ni, n in enumerate((4, 8)):
        for c in cells:
            if c['eligible']:
                gray = int(40+175*c['tones'][n]/(n-1))
                fill = (gray, gray, gray)
            else:
                fill = (40, 80, 110)
            xx, yy = c['x']-crop['x'], SIZE-1-(c['y']-crop['y'])
            d.rectangle((ni*SIZE*24+xx*24, 40+yy*24, ni*SIZE*24+(xx+1)*24-1, 40+(yy+1)*24-1), fill=fill)
        d.text((ni*SIZE*24+8, 8), f'N={n} world tones (blue = unchanged)', fill='white')
    sheet.save(root/'tone-map.png')
    crop['closeupGrid'] = {'x': crop['x']+int(cx), 'y': crop['y']+int(cy), 'size': 6}
    write(root/'crop.json', crop)
    write(root/'cells.json', cells)
    write(root/'counts.json', counts)
    return crop, counts


def contact(root, images, counts, file='preview.png', box=None):
    # 3 columns; <=2048 px wide, same extent/scale. Empty sixth panel is legend.
    pw = 640
    ph = pw+56
    sheet = Image.new('RGB', (pw*3, ph*2), (24, 24, 24))
    d = ImageDraw.Draw(sheet)
    for i, (mode, im) in enumerate(images.items()):
        x, y = (i % 3)*pw, (i // 3)*ph
        d.text((x+8, y+8), f'{mode} | NEW slots {counts[mode]["newPhysicalSlots"]}', fill='white')
        if box:
            im = im.crop(box)
        sheet.paste(im.resize((pw, pw), Image.Resampling.LANCZOS), (x, y+48))
    x, y = pw*2+16, ph+24
    for i, text in enumerate(('STAGE C / Zone6 private HD138 witness',
                              'Same scale, 256 output pixels/cell',
                              'North-up, unlit RGB, Ground only',
                              'No mesh, lights, DXT encode, mip or game',
                              'Baseline: 136 selector, explicit seed 138',
                              'Stock transitions unchanged in all panels')):
        d.text((x, y+i*28), text, fill='white')
    sheet.save(root/file)


def budgets(root, inv):
    rows = []
    for item in inv['maps']:
        m = read(root/'input'/item['export'])
        a = next(a for a in inv['atlases'] if a['directory'] == m['atlasDirectory'])
        ps = pools(m, a)
        names = {f['id']: f['name'] for f in a['catalog']['families']}
        combos = {4: set(), 8: set()}
        uniform, eligible, mixeligible = 0, 0, 0
        groups = Counter()
        homogeneous = Counter()
        used = set()
        for layer in ('land', 'overlay'):
            for k, (packed, sig) in enumerate(zip(m[layer], m[layer+'Signatures'])):
                if packed < 0:
                    continue
                slot = packed & 0x3fff
                used.add(slot)
                if not sig or min(sig) <= 0:
                    raise AssertionError('Unknown semantic cell in exact corpus')
                cl = m['runtimeTypes'][slot] if slot < len(m['runtimeTypes']) else -1
                if len(set(sig)) != 1:
                    groups[(layer, tuple(sig), cl)] += 1
                    continue
                homogeneous[(layer, sig[0], cl)] += 1
                if layer != 'land':
                    continue
                uniform += 1
                candidates = ps.get((sig[0], cl), [])
                if not natural(names.get(sig[0], '')) or not candidates:
                    continue
                eligible += 1
                mixeligible += int(len(candidates) >= 2)
                y, x = divmod(k, m['width'])
                # Corpus forecast: equal physical-slot weights, natural rotation.
                # Exact modeled policy, not a full HD raster bake of these 41 maps.
                selected, _ = pick136(x, y, sig[0], candidates)
                for n in (4, 8):
                    combos[n].add((sig[0], cl, selected, tone_index(x, y, sig[0], n)))
        h = sum(min(4, n) for n in homogeneous.values())
        b4 = h+sum(min(4, n) for n in groups.values())
        additions = {'current136': 0, 'macro4': len(combos[4]), 'macro8': len(combos[8]),
                     'mix2-world': mixeligible, 'hex-world': eligible}
        capacity = item['atlasSlots']
        # Physical occupied places vs total allocated capacity remain distinct.
        row = {'map': item['file'], 'cells': m['width']*m['height'], 'allocated': capacity,
               'usedPhysical': len(used), 'landUniform': uniform, 'eligible': eligible,
               'mixEligible': mixeligible, 'n4GrammarBase': b4, 'newSlots': additions,
               'appendCapacity': {mode: math.ceil((capacity+v)/64)*64 for mode, v in additions.items()},
               'n4PlusNewConservative': {mode: math.ceil((b4+v)/64)*64 for mode, v in additions.items()}}
        rows.append(row)
    summary = {}
    for mode in MODES:
        summary[mode] = {'newMedian': float(np.median([r['newSlots'][mode] for r in rows])),
                         'newMax': max(r['newSlots'][mode] for r in rows)}
        for model in ('appendCapacity', 'n4PlusNewConservative'):
            v = [r[model][mode] for r in rows]
            summary[mode][model] = {'max': max(v), 'median': float(np.median(v)),
                                   'within2048': sum(x <= 2048 for x in v),
                                   'within4096': sum(x <= 4096 for x in v)}
    write(root/'budgets.json', {'maps': rows, 'summary': summary,
          'scope': '41 exact-bound DejaVu snapshots from stage0; Base never read',
          'assumptions': ['append preserves all existing physical slots incl padding/animations',
                          'macro keys exact for modeled seed138/equal-weight policy; no corpus pixel bake',
                          'world mix/hex one new slot per eligible cell, estimated upper bound before pixel dedup',
                          'N4 comparison conservative adds new keys without subtracting replaced uniform recipes; pinned reserve absent',
                          'natural grass/ground only, underwater/overlay/directional materials unchanged']})
    print(json.dumps(summary, indent=2), flush=True)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output', type=Path, default=Path('build-tests-stagec'))
    args = p.parse_args()
    checkout = Path(__file__).resolve().parents[2]
    root = args.output.resolve()
    assert root == checkout/'build-tests-stagec', 'Writes restricted to owned private root'
    root.mkdir(exist_ok=True)
    git = lambda *args: subprocess.check_output(['git', *args], cwd=checkout, text=True).strip()
    assert git('branch', '--show-current') == 'main'
    source_sha = git('rev-parse', 'HEAD')
    inv, m, a, rec = prepare(checkout, root)
    crop, counts = render(root, m, a, rec)
    budgets(root, inv)
    write(root/'result.json', {'task': 'EI-TERRAIN-BAKE-STAGE-C', 'head': source_sha,
          'agentsSha256': sha(checkout/'AGENTS.md'), 'version': (checkout/'VERSION').read_text().strip(),
          'crop': crop, 'counts': counts, 'parameters': {'seed': SEED, 'pixels': PIXELS, 'tones': [4, 8],
          'macroPeriodCells': PERIOD, 'brightnessAmplitude': AMPLITUDE, 'relativeRgbTint': TINT.tolist(),
          'mixPeriodCells': 2.7, 'hexLatticeSpacingCells': 1, 'hexWeightExponent': 2},
          'hdAtlasPixelSha256': a['pixelSha256'], 'sourceMprSha256': m['sha256'],
          'scope': 'offline RGB render; no live or UI actions',
          'limitations': ['baseline uses explicit seed138; it is not a screenshot of owners current seed',
                          'macro is constant within a cell: finite tones introduce contour/edge steps',
                          'mix/hex continuous within same material only; unchanged transition interface can mismatch',
                          'hex uses ordinary RGB blending, not histogram preserving stochastic texturing',
                          'no DXT1 encoding, gutters, mip/mesh/light/runtime or owner acceptance']})
    print('COMPLETE: render and budgets; run stage_c_verify.py for readback', flush=True)


if __name__ == '__main__':
    main()
