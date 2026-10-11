"""Extra capacity experiment: finite K=8 stochastic recipes per material/class.

Run after stage_c.py, before stage_c_verify.py. Reads/writes private stagec only.
This deliberately resets stochastic phase at SEC-cell boundaries, exposing the
quality loss of reuse. It is NOT the free-world continuous hex candidate.
"""
from collections import defaultdict
import hashlib
import math
from pathlib import Path
import sys
sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw
import stage_c as c

K = 8
MODE = 'hex-bank8'


def run():
    checkout = Path(__file__).resolve().parents[2]
    root = checkout/'build-tests-stagec'
    result = c.read(root/'result.json')
    crop, counts = result['crop'], result['counts']
    cells = c.read(root/'cells.json')
    m = c.read(root/'input'/'zone6.json')
    a = c.read(root/'input'/'zone6-atlas.json')
    tx = c.Textures(root/'input'/'hd-atlas')
    p, size = c.PIXELS, crop['size']
    x, y = np.meshgrid((np.arange(p)+.5)/p, (np.arange(p)+.5)/p)
    bank = {}
    base = Image.open(root/'current136.png').convert('RGB').transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    out = base.copy()
    records, typed = [], set()
    folder = root/'hex-bank8'
    folder.mkdir(exist_ok=True)
    for r in cells:
        if not r['eligible']:
            continue
        mat, cl = r['material'], r['class']
        label = (c.hash136(r['x'], r['y'], mat, c.SEED+1) >> 8) % K
        key = mat, cl, label
        if key not in bank:
            # One coherent stochastic tile per label, then reusable rotations.
            # Large offsets decorrelate banks, with identical texel density.
            ox, oy = 31.17+label*13.371, 17.61+label*19.613
            rgb = np.uint8(np.clip(np.rint(c.hex_sample(tx, r['candidateSlots'], mat, x+ox, y+oy)), 0, 255))
            bank[key] = rgb
            Image.fromarray(rgb).save(folder/f'{mat}-{cl}-{label}.png')
        rgb = np.rot90(bank[key], r['rotation'])
        out.paste(Image.fromarray(rgb), ((r['x']-crop['x'])*p, (r['y']-crop['y'])*p))
        typed.add((mat, cl, hashlib.sha256(bank[key].tobytes()).hexdigest()))
        records.append({'x': r['x'], 'y': r['y'], 'material': mat, 'class': cl, 'label': label, 'rotation': r['rotation']})
    out = out.transpose(Image.Transpose.FLIP_TOP_BOTTOM)
    out.save(root/f'{MODE}.png')
    new = len(typed)
    counts[MODE] = {'newPhysicalSlots': new, 'keyCount': len(bank),
                    'atlasAllocatedWithAppend': m['atlasSlots']+new,
                    'roundedTo64': math.ceil((m['atlasSlots']+new)/64)*64}
    images = {mode: Image.open(root/f'{mode}.png').convert('RGB') for mode in (*c.MODES, MODE)}
    for file, box in (('preview.png', None), ('closeup.png', (
            (crop['closeupGrid']['x']-crop['x'])*p,
            (size-(crop['closeupGrid']['y']-crop['y'])-6)*p,
            (crop['closeupGrid']['x']-crop['x']+6)*p,
            (size-(crop['closeupGrid']['y']-crop['y']))*p))):
        sheet = Image.new('RGB', (1920, 1440), (24, 24, 24))
        d = ImageDraw.Draw(sheet)
        for i, (mode, im) in enumerate(images.items()):
            xx, yy = (i % 3)*640, (i // 3)*696
            d.text((xx+8, yy+8), f'{mode} | NEW slots {counts[mode]["newPhysicalSlots"]}', fill='white')
            if box:
                im = im.crop(box)
            sheet.paste(im.resize((640, 640), Image.Resampling.LANCZOS), (xx, yy+48))
        d.text((8, 1404), 'Same scale / Ground RGB north-up / private HD138 / 136 explicit seed138 / no lights, mesh, DXT encode or runtime', fill='white')
        sheet.save(root/file)
    b = c.read(root/'budgets.json')
    inv = c.read(root/'input'/'inventory.json')
    for row in b['maps']:
        item = next(m for m in inv['maps'] if m['file'] == row['map'])
        cm = c.read(root/'input'/item['export'])
        ca = next(a for a in inv['atlases'] if a['directory'] == cm['atlasDirectory'])
        names = {f['id']: f['name'] for f in ca['catalog']['families']}
        ps = c.pools(cm, ca)
        keys = set()
        for i, (packed, sig) in enumerate(zip(cm['land'], cm['landSignatures'])):
            if not sig or len(set(sig)) != 1 or not c.natural(names.get(sig[0], '')):
                continue
            cl = cm['runtimeTypes'][packed & 0x3fff]
            if (sig[0], cl) not in ps:
                continue
            yy, xx = divmod(i, cm['width'])
            label = (c.hash136(xx, yy, sig[0], c.SEED+1) >> 8) % K
            keys.add((sig[0], cl, label))
        row['newSlots'][MODE] = len(keys)
        row['appendCapacity'][MODE] = math.ceil((row['allocated']+len(keys))/64)*64
        row['n4PlusNewConservative'][MODE] = math.ceil((row['n4GrammarBase']+len(keys))/64)*64
    summary = {'newMedian': float(np.median([r['newSlots'][MODE] for r in b['maps']])),
               'newMax': max(r['newSlots'][MODE] for r in b['maps'])}
    for model in ('appendCapacity', 'n4PlusNewConservative'):
        values = [r[model][MODE] for r in b['maps']]
        summary[model] = {'median': float(np.median(values)), 'max': max(values),
                          'within2048': sum(v <= 2048 for v in values), 'within4096': sum(v <= 4096 for v in values)}
    b['summary'][MODE] = summary
    b['assumptions'].append('hex-bank8 resets stochastic phase at each cell; K8 per material/class, no shared-edge guarantee')
    result['counts'] = counts
    result['parameters']['hexFiniteRecipes'] = K
    result['limitations'].append('hex-bank8 reusable recipes break free-world continuity, expose square seams and rotate artwork')
    c.write(root/'budgets.json', b)
    c.write(root/'counts.json', counts)
    c.write(root/'result.json', result)
    c.write(root/'hex-bank8-cells.json', records)
    print({'cropNewSlots': new, 'corpus': summary})


if __name__ == '__main__':
    run()
