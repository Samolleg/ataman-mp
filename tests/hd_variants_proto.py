"""EI-HD-VARIANTS-PROTO: offline, snapshot-only texture quilting and measurements.

Requires existing numpy/Pillow. No live reads, network, installation or mapper launch.
All image/data output belongs in an ignored build-tests-hd-variants-proto directory.
"""
from __future__ import annotations
import argparse
from collections import defaultdict, OrderedDict
import base64
import hashlib
import json
from pathlib import Path
import sys
import time
import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.dont_write_bytecode = True
import hd_coverage_audit as audit_tool

ROOT = Path(__file__).resolve().parents[1]
DEFAULT_SNAPSHOT = ROOT/'build-tests-hd-coverage/snapshot-20261011T113608976183Z'
DEFAULT_OUTPUT = ROOT/'build-tests-hd-variants-proto'


def write_json(path, obj):
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False)+'\n', encoding='utf-8')


def rgb(a):
    return np.asarray(a, dtype=np.uint8)[..., :3]


def pil(a):
    return Image.fromarray(np.asarray(a, dtype=np.uint8))


def resize(a, size):
    return np.asarray(pil(a).resize((size, size), Image.Resampling.LANCZOS))


def d4(a):
    return [np.rot90(b, k) for b in (a, np.fliplr(a)) for k in range(4)]


def canonical(a):
    return min(hashlib.sha256(x.tobytes()).hexdigest() for x in d4(a))


def compare(a, b):
    x, y = a.astype(np.float64), b.astype(np.float64)
    rmse = float(np.sqrt(np.mean((x-y)**2)))
    return dict(mae=float(np.mean(abs(x-y))), rmse=rmse,
                psnr=float(20*np.log10(255/rmse)) if rmse else None,
                correlation=float(np.corrcoef(x.ravel(), y.ravel())[0, 1]),
                lumaCorrelation=float(np.corrcoef((x @ [0.2126,0.7152,0.0722]).ravel(),
                                                  (y @ [0.2126,0.7152,0.0722]).ravel())[0,1]))


class Snapshot:
    def __init__(self, path):
        self.path = path.resolve()
        if not self.path.is_relative_to((ROOT/'build-tests-hd-coverage').resolve()):
            raise ValueError('Only existing private coverage snapshots are accepted')
        self.audit = json.loads((path/'audit.json').read_text(encoding='utf-8'))
        self.rows = self.audit['rows']
        self.streams, self.indexes, self.pages, self.hashes = {}, {}, OrderedDict(), {}
        for name in ('jamaisvu_hd', 'dejavu_hd', 'base_sd', 'jamaisvu_sd'):
            p = path/(name+'.res')
            actual = audit_tool.sha(p)
            assert actual == self.audit['manifest']['packs'][name]['sha256'], name
            self.hashes[name] = actual
            self.streams[name] = p.open('rb')
            self.indexes[name] = audit_tool.res_index(self.streams[name])
        assert audit_tool.sha(ROOT/'assets/resources/terrain_atlas_presets.json') == self.audit['presetSha256']

    def tile(self, pack, ns, slot, expected=None):
        key = (pack, ns, slot//64)
        if key not in self.pages:
            f = self.streams[pack]
            rec = self.indexes[pack][f'{ns}{slot//64:03}.mmp']
            self.pages[key] = audit_tool.decode_dxt1_current(audit_tool.member(f, rec))
            if len(self.pages)>12:
                self.pages.popitem(last=False)
        self.pages.move_to_end(key)
        page = self.pages[key]
        n = page.shape[0]//8
        row, col = 7-(slot%64)//8, slot%8
        a = page[row*n:(row+1)*n, col*n:(col+1)*n].copy()
        if expected:
            assert hashlib.sha256(f'{n}x{n}\n'.encode()+a.tobytes()).hexdigest() == expected
        return a

    def row_tile(self, row, bank='jamaisvu'):
        return self.tile('jamaisvu_hd' if bank=='jamaisvu' else 'dejavu_hd',
                         row['namespace'], row['slot'], row['pixelSha256'])

    def pool(self, name, cls, bank='jamaisvu'):
        rows = [r for r in self.rows[bank] if r['kind']=='uniform' and
                r['material']==name and r['reviewedClass']==cls and r['size']==256
                and r['animated'] is False and r['opaque']]
        images, sources, seen = [], [], set()
        for r in rows:
            a = self.row_tile(r, bank)
            h = canonical(a)
            if h not in seen:
                seen.add(h); images.append(rgb(a)); sources.append(r)
        return images, sources

    def verify_end(self):
        for name, h in self.hashes.items():
            assert audit_tool.sha(self.path/(name+'.res')) == h, name
        for f in self.streams.values():
            f.close()


def sheet(path, rows, width=256):
    """rows = [(row label, [(image, caption), ...]), ...]."""
    cols = max(len(cells) for _, cells in rows)
    out = Image.new('RGB', (cols*(width+8)+8, len(rows)*(width+56)+8), '#25282d')
    draw = ImageDraw.Draw(out)
    for y, (label, cells) in enumerate(rows):
        top = y*(width+56)+8
        draw.text((8, top), label, fill='white')
        for x, (a, caption) in enumerate(cells):
            left = x*(width+8)+8
            out.paste(pil(a).resize((width,width), Image.Resampling.NEAREST), (left,top+20))
            draw.text((left,top+width+24), caption, fill='white')
    out.save(path)


def stone_study(s, out):
    hd, sources = s.pool('Stone / Desert', 2)
    records, cells = [], []
    targets = [r for r in s.rows['jamaisvu'] if r['material']=='Stone / Desert' and r['size']==128]
    sd_arrays = []
    for r in targets:
        a = rgb(s.row_tile(r))
        sd_arrays.append(a)
        candidates = []
        for i, h in enumerate(hd):
            for k in range(4):
                b = np.rot90(resize(h,128),k)
                candidates.append(dict(hdSource=sources[i], quarterTurns=k, **compare(a,b)))
        best = min(candidates,key=lambda c:c['rmse'])
        # Cross-check native base SD, not merely retained 128 in the HD pack.
        base = rgb(s.tile('base_sd',r['namespace'],r['slot']))
        base_cmp = compare(resize(a,base.shape[0]),base)
        native = (rgb(s.tile('jamaisvu_sd',r['namespace'],r['slot']))
                  if f"{r['namespace']}{r['slot']//64:03}.mmp" in s.indexes['jamaisvu_sd'] else None)
        native_cmp = compare(resize(a,native.shape[0]),native) if native is not None else None
        records.append(dict(source=r, bestHdDownscale=best, allHdRotations=candidates,
                            baseSdSize=base.shape[0], baseSdComparison=base_cmp,
                            jamaisvuTextureSize=native.shape[0] if native is not None else None,
                            jamaisvuTextureComparison=native_cmp))
        cells.append((a,f"{r['namespace']}:{r['slot']} class {r['reviewedClass']}"))
    sd_pairs = []
    for i,a in enumerate(sd_arrays):
        for j in range(i):
            cs = [(compare(a,np.rot90(sd_arrays[j],k)),k) for k in range(4)]
            c,k = min(cs,key=lambda x:x[0]['rmse'])
            sd_pairs.append(dict(i=i,j=j,quarterTurns=k,**c))
    sheet(out/'stone-comparison.png', [('SD originals (nearest enlargement)',cells),
           ('HD class 2 / downscale128 / absolute difference x4',
            [(hd[0],'bz16h:463 HD256'),(resize(hd[0],128),'HD downscale LANCZOS128'),
             (np.clip(abs(sd_arrays[0].astype(float)-resize(hd[0],128))*4,0,255).astype(np.uint8),'difference versus zone18:463')])])
    result = dict(records=records,sdPairs=sd_pairs)
    write_json(out/'stone-study.json',result)
    return result


def min_cut(cost):
    """Top-to-bottom minimum-error seam; each step changes x by at most one."""
    h,w = cost.shape
    cumulative = cost[0].copy()
    parents = np.zeros((h,w),np.int32)
    for y in range(1,h):
        options = np.stack([np.r_[np.inf,cumulative[:-1]], cumulative,
                            np.r_[cumulative[1:],np.inf]])
        choice = np.argmin(options,axis=0)
        parents[y] = np.arange(w)+choice-1
        cumulative = options[choice,np.arange(w)]+cost[y]
    seam = np.empty(h,np.int32); seam[-1] = np.argmin(cumulative)
    for y in range(h-1,0,-1):
        seam[y-1] = parents[y,seam[y]]
    return seam


RIM = 24
PATCH = 80
OVERLAP = 16


def histogram_restore(a, anchor):
    """Exact per-channel histogram of the anchor interior; rim is never touched."""
    out = a.copy()
    for c in range(3):
        x = out[RIM:-RIM,RIM:-RIM,c].copy().ravel()
        reference = np.sort(anchor[RIM:-RIM,RIM:-RIM,c].ravel())
        order = np.argsort(x,kind='stable')
        x[order] = reference
        out[RIM:-RIM,RIM:-RIM,c] = x.reshape(256-2*RIM,256-2*RIM)
    return out


def quilt(pool, seed, anchor_index):
    """Four-sided patch quilting with minimum-error cuts, all donors used.

    Start from an original tile. Every stamp has a closed four-sided overlap,
    so no stamp tears the preserved surrounding context. This is 2D dynamic-
    programming seam quilting, not a claim of a globally optimal graph cut.
    """
    rng = np.random.Generator(np.random.PCG64(seed))
    anchor = pool[anchor_index]
    out = anchor.copy()
    provenance = []
    ownership = np.full((256,256),-1,np.int32)
    order = [(y,x) for y in (24,68,112,152) for x in (24,68,112,152)]
    rng.shuffle(order)
    donors = list(range(len(pool)))
    rng.shuffle(donors)
    for step,(y,x) in enumerate(order):
        source = donors[step%len(donors)]
        existing = out[y:y+PATCH,x:x+PATCH].astype(np.float32)
        ring = np.ones((PATCH,PATCH),bool)
        ring[OVERLAP:-OVERLAP,OVERLAP:-OVERLAP] = False
        candidates = []
        for _ in range(48):
            transform = int(rng.integers(8))
            sy,sx = rng.integers(0,256-PATCH+1,2)
            donor = d4(pool[source])[transform]
            piece = donor[sy:sy+PATCH,sx:sx+PATCH]
            cost = np.mean((piece.astype(np.float32)-existing)**2,axis=2)
            candidates.append((float(cost[ring].mean()),piece,cost,int(sy),int(sx),transform))
        _,piece,cost,sy,sx,transform = min(candidates,key=lambda c:c[0])
        keep = np.ones((PATCH,PATCH),bool)
        for flip in (False,True):
            z = np.fliplr(cost) if flip else cost
            seam = min_cut(z[:,:OVERLAP])
            mask = np.arange(PATCH)[None,:] > seam[:,None]
            keep &= np.fliplr(mask) if flip else mask
            z = np.flipud(cost) if flip else cost
            seam = min_cut(z[:OVERLAP,:].T)
            mask = np.arange(PATCH)[:,None] > seam[None,:]
            keep &= np.flipud(mask) if flip else mask
        region = out[y:y+PATCH,x:x+PATCH]
        region[keep] = piece[keep]
        ownership[y:y+PATCH,x:x+PATCH][keep] = step
        provenance.append(dict(x=x,y=y,source=source,sourceX=sx,sourceY=sy,
                               d4=transform,insertedPixels=int(keep.sum())))
    for step,record in enumerate(provenance):
        record['survivingPixels'] = int((ownership==step).sum())
    assert all(sum(r['survivingPixels'] for r in provenance if r['source']==i)>0
               for i in range(len(pool))), 'Every HD donor must contribute surviving pixels'
    return histogram_restore(out,anchor), provenance


def visible(a):
    # Current renderer samples 1/16 inset. Store 224 exact visible texels,
    # no artificial enlargement in measurements; PNG panels share this scale.
    return a[16:-16,16:-16]


def paint(pool, seed=143, side=12):
    rng = np.random.Generator(np.random.PCG64(seed))
    u = rng.random((side,side))
    turns = rng.integers(0,4,(side,side))
    n = visible(pool[0]).shape[0]
    result = np.empty((side*n,side*n,3),np.uint8)
    labels = (u*len(pool)).astype(int)
    for y in range(side):
        for x in range(side):
            result[y*n:(y+1)*n,x*n:(x+1)*n] = np.rot90(visible(pool[labels[y,x]]),int(turns[y,x]))
    return result,labels.tolist(),turns.tolist()


def luma(a):
    return a.astype(np.float32) @ np.array([0.2126,0.7152,0.0722],np.float32)


def corr(a,b):
    x,y = a.ravel().astype(np.float64),b.ravel().astype(np.float64)
    return float(np.corrcoef(x,y)[0,1])


def paint_metrics(a,n=224):
    y = luma(a)
    dx,dy = abs(np.diff(y,axis=1)),abs(np.diff(y,axis=0))
    boundaries = np.arange(n,y.shape[0],n)-1
    edge = float((dx[:,boundaries].mean()+dy[boundaries,:].mean())/2)
    # Same 8-pixel inset on every cell as Stage C; compare interior gradients.
    positions = np.arange(y.shape[0]-1)%n
    inside = (positions>=8)&(positions<n-9)
    interior = float((dx[:,inside].mean()+dy[inside,:].mean())/2)
    blurred = np.asarray(pil(a).filter(ImageFilter.GaussianBlur(3)),np.float32)
    hp = y-luma(blurred)
    lags = {}
    for lag in (n-8,n,n+8,2*n):
        lags[str(lag)] = [corr(hp[:,:-lag],hp[:,lag:]),corr(hp[:-lag,:],hp[lag:,:])]
    # Energy of periodic gradient at exactly one cell, normalized by DC.
    spectra = []
    for v in (dx.mean(axis=0),dy.mean(axis=1)):
        pos = np.arange(len(v))
        spectra.append(float(2*abs(np.sum(v*np.exp(-2j*np.pi*pos/n)))/np.sum(v)))
    return dict(edgeLuma=edge,interiorLuma=interior,gridRatio=edge/interior,
                highpassStd=float(hp.std()),highpassAutocorrelation=lags,
                gradientPeriodAmplitudeXY=spectra)


def diversity(pool):
    pairs = []
    for i in range(len(pool)):
        for j in range(i):
            values = [float(np.sqrt(np.mean((pool[i].astype(float)-b.astype(float))**2)))
                      for b in d4(pool[j])]
            pairs.append(dict(i=i,j=j,minD4Rmse=min(values)))
    return dict(uniqueD4=len({canonical(x) for x in pool}),pairs=pairs,
                minD4Rmse=min(x['minD4Rmse'] for x in pairs) if pairs else None,
                medianD4Rmse=float(np.median([x['minD4Rmse'] for x in pairs])) if pairs else None)


def edge_proof(final,original,anchors):
    comparisons = 0
    for i,a in enumerate(final):
        b = original[anchors[i]]
        for k in range(4):
            x,y = np.rot90(a,k),np.rot90(b,k)
            mask = np.ones((256,256),bool); mask[RIM:-RIM,RIM:-RIM] = False
            assert np.array_equal(x[mask],y[mask])
            # All four visible boundaries and their local 8px inward footprints.
            assert np.array_equal(visible(x)[:8],visible(y)[:8])
            assert np.array_equal(visible(x)[-8:],visible(y)[-8:])
            assert np.array_equal(visible(x)[:,:8],visible(y)[:,:8])
            assert np.array_equal(visible(x)[:,-8:],visible(y)[:,-8:])
            comparisons += 5
        for c in range(3):
            assert np.array_equal(np.sort(a[:,:,c].ravel()),np.sort(b[:,:,c].ravel()))
    return dict(rotatedRimAndVisibleChecks=comparisons,maxChangedRimValue=0,
                perChannelHistogramExact=True,
                claim='Relative to the anchor, including any unchanged neighbouring transition; not universal stock C0 continuity')


def pair_panel(path, left, right, left_label, right_label, preview=False):
    if preview:
        left=resize(left,768); right=resize(right,768)
    h,w = left.shape[:2]
    out = Image.new('RGB',(w*2+24,h+40),'#25282d')
    out.paste(pil(left),(8,32));out.paste(pil(right),(w+16,32))
    d=ImageDraw.Draw(out);d.text((8,8),left_label,fill='white');d.text((w+16,8),right_label,fill='white')
    out.save(path)


def hd_control(folder,pool,final):
    before,_,_=paint(pool)
    after,_,_=paint(final)
    pair_panel(folder/'paint-hd-control.png',before,after,
               'CONTROL: reused class-2 HD only','PROTOTYPE: same HD + 5 quilted variants',True)
    return dict(before=paint_metrics(before),after=paint_metrics(after))


def material_case(s,out,name,cls,slug,stone=False):
    pool,sources = s.pool(name,2 if stone else cls)
    if stone:
        # Explicit reuse, not generation: source pixels keep physical class 2,
        # reviewed output identity is Stone/Desert class 3.
        current_rows = [r for r in s.rows['jamaisvu'] if r['kind']=='uniform' and
                        r['material']==name and r['reviewedClass']==3]
        current=[]; seen=set()
        for r in current_rows:
            a=resize(rgb(s.row_tile(r)),256)
            if canonical(a) not in seen:
                seen.add(canonical(a)); current.append(a)
    else:
        current=pool
    assert 0<len(pool)<=6
    final=list(pool); anchors=list(range(len(pool))); traces=[]
    seed0=20261011+sum(slug.encode())*100
    while len(final)<6:
        i=len(final); anchor=i%len(pool)
        a,trace=quilt(pool,seed0+i,anchor)
        assert canonical(a) not in {canonical(x) for x in final}
        final.append(a);anchors.append(anchor)
        traces.append(dict(index=i,seed=seed0+i,anchor=anchor,patches=trace))
    # Reproduce one generated result independently from seed; no global RNG.
    a,_=quilt(pool,traces[0]['seed'],traces[0]['anchor'])
    assert np.array_equal(a,final[traces[0]['index']])
    proof=edge_proof(final,pool,anchors)
    case=out/slug;case.mkdir(exist_ok=True)
    for i,a in enumerate(final):
        pil(a).save(case/f'variant-{i+1:02}.png')
    sheet(case/'variants.png',[(f'{name} / reviewed class {cls}: original HD donors',
          [(a,f"original {i+1}: {r['namespace']}:{r['slot']}") for i,(a,r) in enumerate(zip(pool,sources))]),
          ('Final bank: 6 D4-distinct images (originals + quilted interiors)',
           [(a,f"{i+1}: {'original/reused' if i<len(pool) else 'quilted'}; edge {anchors[i]+1}") for i,a in enumerate(final)])])
    before,labels0,turns=paint(current)
    after,labels1,turns1=paint(final)
    assert turns==turns1
    pair_panel(case/'paint.png',before,after,'NOW: existing only'+(' (SD128 enlarged)' if stone else ''),'PROTOTYPE: six HD variants')
    pair_panel(case/'paint-preview.png',before,after,'NOW: 12 x 12, same scale','PROTOTYPE: 12 x 12, same scale',True)
    # Same 2x2 crop at native texel scale, 2x nearest zoom for edge inspection.
    pair_panel(case/'edges.png',np.repeat(np.repeat(before[:448,:448],2,0),2,1),
               np.repeat(np.repeat(after[:448,:448],2,0),2,1),
               'NOW: 2x2, visible inset, nearest x2','PROTOTYPE: 2x2, visible inset, nearest x2')
    result=dict(name=name,reviewedClass=cls,sources=sources,originalHd=len(pool),
                reusedFromClass2=stone,generated=6-len(pool),anchors=anchors,proof=proof,
                before=paint_metrics(before),after=paint_metrics(after),diversity=diversity(final),
                sourceDiversity=diversity(pool),traces=traces,
                pixelMeans=[a.mean(axis=(0,1)).tolist() for a in final],
                lumaMeanStd=[[float(luma(a).mean()),float(luma(a).std())] for a in final],
                layout=dict(seed=143,before=labels0,after=labels1,quarterTurns=turns))
    if stone:
        result['hdControl']=hd_control(case,pool,final)
    write_json(case/'metrics.json',result)
    print('CASE',slug,'grid',result['before']['gridRatio'],result['after']['gridRatio'],
          'min D4 RMSE',result['diversity']['minD4Rmse'],flush=True)
    return result


def budget(s,out):
    banks={}
    for bank in ('jamaisvu','dejavu'):
        grouped=defaultdict(list)
        for r in s.rows[bank]:
            if r['kind']=='uniform':
                grouped[audit_tool.material_key(r)].append(r)
        result=[]; excluded=[]; physical_result=[]
        for key,rows in sorted(grouped.items()):
            # Water surfaces are excluded. Static underwater ground, swamp grass
            # and static lava are materials, not automatically animation frames.
            if key[0].startswith('Water /'):
                excluded.append(dict(key=key,reason='water surface'));continue
            static=[r for r in rows if r['animated'] is False and r['opaque']]
            if not static:
                excluded.append(dict(key=key,reason='no confirmed static opaque source'));continue
            raw={r['pixelSha256']:r for r in static if r['size']==256}
            oriented={h:canonical(s.row_tile(r,bank)) for h,r in raw.items()}
            unique=set(oriented.values())
            result.append(dict(material=key[0],reviewedClass=key[1],layers=key[2],
                               existingExact=len(raw),existingD4=len(unique),
                               missing=max(0,6-len(unique)),
                               physicalClasses=sorted({r['physicalClass'] for r in static if r['physicalClass'] is not None})))
            for physical in sorted({r['physicalClass'] for r in static if r['physicalClass'] is not None}):
                hashes={oriented[r['pixelSha256']] for r in static if r['physicalClass']==physical and r['size']==256}
                physical_result.append(dict(material=key[0],reviewedClass=key[1],layers=key[2],
                                            physicalClass=physical,existingD4=len(hashes),missing=max(0,6-len(hashes))))
        count=sum(r['missing'] for r in result)
        per_tile=sum(max(1,(256>>m)//4)**2*8 for m in range(9))
        banks[bank]=dict(combinations=len(result),needsGeneration=sum(r['missing']>0 for r in result),
                         newSemanticTiles=count,bc1BytesPerTile=per_tile,bc1MiB=count*per_tile/1048576,
                         pages64=(count+63)//64,pageRoundedMiB=((count+63)//64)*sum(max(1,(2048>>m)//4)**2*8 for m in range(12))/1048576,
                         rows=result,excluded=excluded,
                         physicalSplit=dict(combinations=len(physical_result),
                                            needsGeneration=sum(r['missing']>0 for r in physical_result),
                                            newSemanticTiles=sum(r['missing'] for r in physical_result),
                                            bc1MiB=sum(r['missing'] for r in physical_result)*per_tile/1048576,
                                            rows=physical_result))
    write_json(out/'budget.json',banks)
    print('BUDGET',json.dumps({k:{q:v for q,v in val.items() if q not in ('rows','excluded','physicalSplit')} for k,val in banks.items()}),flush=True)
    return banks


def transitions(s,out,cases):
    presets={p['namespace'].lower():p for p in json.loads(
        (ROOT/'assets/resources/terrain_atlas_presets.json').read_text(encoding='utf-8'))['presets']}
    result=[]
    for case in cases:
        slug={'Grass / Tundra':'grass-tundra','Ground / Brown':'ground-brown',
              'Ground / Stony gray':'ground-stony-gray','Stone / Desert':'stone-desert-3'}[case['name']]
        folder=out/slug
        sources=case['sources']
        original=[rgb(s.row_tile(r)) for r in sources]
        generated=[np.asarray(Image.open(folder/f'variant-{i+1:02}.png')) for i in range(6)]
        ref=sources[0]
        # Class 3 has SD-only transitions in zone19; do not invent class-2
        # transitions merely because the uniform artwork is reused from bz16h.
        ns='zone19' if case['reusedFromClass2'] else ref['namespace']
        reference_slot=43 if case['reusedFromClass2'] else ref['slot']
        preset=presets[ns]
        signatures=np.frombuffer(base64.b64decode(preset['signaturesBase64']),np.uint8).reshape(-1,4)
        family=int(signatures[reference_slot,0])
        candidates=[]
        for row in s.rows['jamaisvu']:
            slot=row['slot']
            if row['namespace']!=ns or row['kind']!='transition' or row['animated'] is not False or not row['opaque']:
                continue
            sig=signatures[slot]
            for k in range(4):
                rotated=np.roll(sig,-k)
                if rotated[0]==family and rotated[3]==family:
                    a=rgb(s.row_tile(row))
                    if a.shape[0]!=256:
                        a=resize(a,256)
                    a=np.rot90(a,k)
                    candidates.append((a,row,k))
        assert candidates, 'No transition example: '+slug
        # Same original/new orientation; select a compatible stock edge from
        # this namespace by signature then minimum pixel seam error.
        panels0=[];panels1=[];checks=[]
        for rot in range(4):
            idx=case['originalHd']+(rot%(6-case['originalHd']))
            anchor=case['anchors'][idx]
            old=np.rot90(visible(original[anchor]),rot)
            new=np.rot90(visible(generated[idx]),rot)
            neighbor,row,turn=min(candidates,key=lambda item:float(abs(luma(old[:,-1])-luma(visible(item[0])[:,0])).mean()))
            neighbor=visible(neighbor)
            a=np.concatenate((old,neighbor),axis=1);b=np.concatenate((new,neighbor),axis=1)
            assert np.array_equal(a[:,216:232],b[:,216:232])
            delta0=float(abs(luma(old[:,-1])-luma(neighbor[:,0])).mean())
            delta1=float(abs(luma(new[:,-1])-luma(neighbor[:,0])).mean())
            assert delta0==delta1
            checks.append(dict(rotation=rot,variant=idx+1,transitionNamespace=ns,
                               transitionSlot=row['slot'],transitionRotation=turn,
                               nativeTransitionSize=row['size'],
                               edgeLumaBefore=delta0,edgeLumaAfter=delta1))
            panels0.append(a);panels1.append(b)
        pair_panel(folder/'transitions.png',np.concatenate(panels0),np.concatenate(panels1),
                   'ANCHORS + stock transitions (4 rotations)','NEW + identical stock transitions (4 rotations)')
        # All supplied donors from both audited banks are represented, modulo D4.
        other,_=s.pool(case['name'],2 if case['reusedFromClass2'] else case['reviewedClass'],'dejavu')
        assert {canonical(x) for x in other}.issubset({canonical(x) for x in original})
        result.append(dict(name=case['name'],checks=checks,allCurrentHdDonorsCovered=True,
                           transitionReviewedClass=case['reviewedClass']))
    write_json(out/'transition-checks.json',result)
    return result


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--snapshot',type=Path,default=DEFAULT_SNAPSHOT)
    p.add_argument('--output',type=Path,default=DEFAULT_OUTPUT)
    p.add_argument('--stone-only',action='store_true')
    p.add_argument('--postprocess-only',action='store_true',help='Recompute budget and transition diagnostics from saved variants')
    p.add_argument('--transitions-only',action='store_true')
    args = p.parse_args()
    out = args.output.resolve()
    if not out.is_relative_to(DEFAULT_OUTPUT.resolve()):
        raise ValueError('Output must stay in this task private directory')
    out.mkdir(parents=True,exist_ok=True)
    s = Snapshot(args.snapshot)
    if args.postprocess_only or args.transitions_only:
        summary=json.loads((out/'summary.json').read_text(encoding='utf-8'))
        assert summary['inputHashes']==s.hashes
        summary['budget']=budget(s,out) if args.postprocess_only else json.loads((out/'budget.json').read_text(encoding='utf-8'))
        summary['transitions']=transitions(s,out,summary['cases'])
        write_json(out/'summary.json',summary)
        s.verify_end()
        return
    stone = stone_study(s,out)
    if not args.stone_only:
        cases=[]
        for name,cls,slug,is_stone in [('Grass / Tundra',0,'grass-tundra',False),
                                      ('Ground / Brown',0,'ground-brown',False),
                                      ('Ground / Stony gray',2,'ground-stony-gray',False),
                                      ('Stone / Desert',3,'stone-desert-3',True)]:
            cases.append(material_case(s,out,name,cls,slug,is_stone))
        totals=budget(s,out)
        transition_results=transitions(s,out,cases)
        write_json(out/'summary.json',dict(cases=cases,budget=totals,transitions=transition_results,
                   inputHashes=s.hashes,snapshot=str(s.path),seed=20261011,
                   numpy=np.__version__,pillow=Image.__version__))
    else:
        print(json.dumps(stone,indent=2))
    s.verify_end()


if __name__ == '__main__':
    main()
