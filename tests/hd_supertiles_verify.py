"""Independent readback/invariants, slot budgets, manifest and negative art gate.

Default exit reports structural validation; --require-art exits2 for rejected
banks AFTER writing all receipts. No threshold can be changed by command line.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import sys
import time
sys.dont_write_bytecode=True
import numpy as np
from PIL import Image
Image.MAX_IMAGE_PIXELS=150_000_000  # Known 48*224 square fields: 115605504 pixels.
import hd_supertiles as s
p=s.p
w=s.w


def planner_checks():
    means=[90,91,92,93];checks=0
    for k in (2,3):
        a=s.layout(24,k,means,'tone',origin=(-7,-5))
        assert a==s.layout(24,k,means,'tone',origin=(-7,-5),order=list(reversed(a)));checks+=1
        b=s.layout(24,k,means,'tone',origin=(-7,-5),seed=s.SEED+1)
        assert a!=b;checks+=1
        # Separate rectangular map requests share global origin, seed and halo.
        for x,y in a:assert a[x,y]==s.cell_key(x,y,k,means,'tone');checks+=1
        chunks={}
        for dy in (0,12):
            for dx in (0,12):chunks.update(s.layout(12,k,means,'tone',origin=(-7+dx,-5+dy)))
        assert chunks==a;checks+=1
        members=[dict(bankSha='bank',anchorSha='anchor',seed=s.SEED,k=k,s=4,mipRevision=1,worldFrame='global',side=12,origin=(-7+dx,-5+dy)) for dy in (0,12) for dx in (0,12)]
        planned=s.cohort_layout(members,means,'tone');merged={}
        for part in planned:merged.update(part)
        assert merged==a;checks+=1
        for key in ('bankSha','anchorSha','seed','k','s','mipRevision','worldFrame'):
            bad=[dict(m) for m in members];bad[-1][key]='mismatch'
            try:s.cohort_layout(bad,means,'tone')
            except ValueError:checks+=1
            else:raise AssertionError('cohort mismatch accepted: '+key)
        mask=lambda x,y: -7<=x<17 and -5<=y<19 and (x,y)!=(4,4)
        masked=s.layout(24,k,means,'tone',origin=(-7,-5),allowed=mask)
        for (x,y),v in masked.items():
            bx,by=x//k*k,y//k*k
            expected=all(mask(bx+u,by+v) for u in range(k) for v in range(k))
            assert (v is not None)==expected;checks+=1
        touched=[(x,y) for y in range(4//k*k,4//k*k+k) for x in range(4//k*k,4//k*k+k)]
        assert len(touched)==k*k and all(masked[z] is None for z in touched);checks+=1
        # Neighbour can be missing: protect entire boundary blocks, no assumed world data.
        unknown=lambda x,y:0<=x<11 and 0<=y<12
        protected=s.layout(12,k,means,'super',allowed=unknown)
        assert all(protected[(x,y)] is None for y in range(12) for x in range(11//k*k,12));checks+=1
        # Algebraic rotation check with unique texels, not repeated real artwork.
        canvas=np.arange(k*8*k*8).reshape(k*8,k*8)
        for r in range(4):
            out=np.empty_like(canvas)
            for y in range(k):
                for x in range(k):
                    sx,sy=x,y
                    for _ in range(r):sx,sy=k-1-sy,sx
                    out[y*8:(y+1)*8,x*8:(x+1)*8]=np.rot90(canvas[sy*8:(sy+1)*8,sx*8:(sx+1)*8],r)
            assert np.array_equal(out,np.rot90(canvas,r));checks+=1
    return dict(checks=checks,negativeCoordinates=True,reverseOrder=True,
                splitWorldEqualsWhole=True,fullBlockFallback=True,wholeBlockRotation=True,
                singleCellPolicy='no-op unless whole block transaction is permitted',
                cohortContract=['same world integer origin','same bank and anchor hashes','same seed','same K/S/mip revision','complete eligibility halo'],
                runtimeImplemented=False)


def texture(path):
    with Image.open(path) as image:return np.asarray(image.convert('RGB'))


def budget(rows):
    tile_bytes=sum(max(1,math.ceil((256>>i)/4))**2*8 for i in range(9))
    records=[]
    for k in (2,3):
        secs=[x['generationSeconds']/4 for x in rows if x['k']==k]
        for count in (2,3,4):
            for tones in (1,3):
                tiles=90*k*k*count*tones;pages=math.ceil(tiles/64)
                records.append(dict(materials=90,k=k,s=count,tones=tones,tiles=tiles,
                    slotsPerMaterial=k*k*count*tones,bc1MiB=tiles*tile_bytes/2**20,
                    rgbaBaseMiB=tiles*.25,rgbaAllMipsMiB=tiles/3,
                    pageRoundedSlots=pages*64,pageRoundedBc1MiB=pages*64*tile_bytes/2**20,
                    generationOnlyMinutesMedian=90*count*float(np.median(secs))/60,
                    measuredSecondsPerSuperMin=min(secs),measuredSecondsPerSuperMax=max(secs)))
    return dict(tileBc1Bytes=tile_bytes,scenarios=records,
                scope='90 requested semantic groups; eligibility/physical-class split may change count; not a corpus bake',
                mapFormula='ceil((existingAllocated + sum(distinctUsedTypedParts) + fallbackAImports)/64)*64',
                rotationsMultiplySlots=False,tonalSelectionAddsSlots=0,
                omitted=['BC1 encoding time','corpus I/O','GPU memory and drivers','human selection','transition recipes','existing/A source slots'])


def tables(receipt, costs):
    lines=['| Материал / K | Поле | Шов Now / A / Super / Tone | lag1 Super | lagK Super | period1 / K Super | HP std Now / Super / Tone |',
           '|---|---:|---|---:|---:|---|---|']
    for row in receipt['rows']:
        k=row['k']
        for n,methods in row['fields'].items():
            m=methods['super'];v=[methods[x] for x in ('now','A','super','tone')]
            seam=' / '.join(f'{x["gridRatio"]:.3f}' for x in v)
            hp=' / '.join(f'{methods[x]["highpassStd"]:.2f}' for x in ('now','super','tone'))
            lines.append(f'| {row["slug"]} / {k} | {n} | {seam} | {max(m["highpassAutocorrelation"]["224"]):.3f} | {max(m["highpassAutocorrelation"][str(k*224)]):.3f} | {max(m["periods"]["224"]):.3f} / {max(m["periods"][str(k*224)]):.3f} | {hp} |')
    lines+=['','| Материал / K | RMSE64 neutral min | RMSE64 + tone min | mip8/4 S min RMSE | worst pair256..16 | used48 Super / Tone / Select |','|---|---:|---:|---|---:|---|']
    for row in receipt['rows']:
        low=[m['supertileMinRmse'] for m in row['mips'][-2:]]
        used=row['usedPhysicalParts']
        lines.append(f'| {row["slug"]} / {row["k"]} | {row["neutralDiversity"]["minRmse64"]:.2f} | {row["diversity"]["minRmse64"]:.2f} | {low[0]:.2f} / {low[1]:.2f} | {max(m["internalPairMaxRatio"] for m in row["mips"] if m["size"]>=16):.3f} | {used["48/super"]} / {used["48/tone"]} / {used["48/select"]} |')
    lines+=['','| K | S | Частей/материал | 90 материалов | BC1 МиБ | С тремя тонами: части / МиБ | Время S, мин |','|---|---|---:|---:|---:|---|---:|']
    for a,b in zip(costs['scenarios'][::2],costs['scenarios'][1::2]):
        lines.append(f'| {a["k"]} | {a["s"]} | {a["slotsPerMaterial"]} | {a["tiles"]} | {a["bc1MiB"]:.3f} | {b["tiles"]} / {b["bc1MiB"]:.3f} | {a["generationOnlyMinutesMedian"]:.2f} |')
    (s.OUT/'report-tables.md').write_text('\n'.join(lines)+'\n',encoding='utf-8')


def run():
    parser=argparse.ArgumentParser();parser.add_argument('--require-art',action='store_true');args=parser.parse_args()
    started=time.perf_counter();checks=0;rows=[];rejected=0;pixel_cells=0
    planner=planner_checks();checks+=planner['checks'];s.write(s.OUT/'planner-checks.json',planner)
    snap=p.Snapshot(p.DEFAULT_SNAPSHOT)
    for name,cls,slug in s.CASES:
        folder=s.OUT/slug;case=s.load(folder/'metrics.json')
        pool,sources=s.get_pool(snap,name,cls);anchor=pool[0]
        for i,a in enumerate(pool):assert np.array_equal(a,texture(folder/f'donor-{i:02}.png'));checks+=1
        common=[texture(z) for z in sorted(folder.glob('A-*.png'))]
        for k in (2,3):
            path=folder/f'k{k}';data=s.load(path/'metrics.json')
            bank=np.stack([np.stack([np.stack([texture(path/f'part-s{i}-t{t}-{idx:02}.png') for idx in range(k*k)]) for t in range(3)]) for i in range(s.S)])
            assert bank.shape==(4,3,k*k,256,256,3);checks+=1
            # Final edge study uses a semantically valid straight stock transition.
            # Holes/jagged masks are tested by the planner, not fake corner artwork.
            data['boundary']=s.boundary_demo(path,bank,data['means'],common,anchor,
                                             texture(folder/'stock-transition.png'),texture(folder/'other-material.png'),k)
            for i in range(4):
                assembled=s.assemble(bank[i,1],k)
                assert np.array_equal(assembled,texture(path/f'super-{i}-continuous.png'));checks+=1
                assert np.array_equal(s.assemble(bank[i,1],k,False),texture(path/f'super-{i}-carriers-{k*256}.png'));checks+=1
                for t in range(3):
                    for y in range(k):
                        for x in range(k):
                            a=bank[i,t,y*k+x]
                            if x<k-1:assert np.array_equal(a[:,-32:],bank[i,t,y*k+x+1][:,:32]);checks+=1
                            if y<k-1:assert np.array_equal(a[-32:],bank[i,t,(y+1)*k+x][:32]);checks+=1
                # Actual artwork rotation: permuted carriers + SEC turn == rotated block.
                for r in range(4):
                    field=np.empty_like(assembled)
                    for y in range(k):
                        for x in range(k):
                            sx,sy=x,y
                            for _ in range(r):sx,sy=k-1-sy,sx
                            field[y*s.N:(y+1)*s.N,x*s.N:(x+1)*s.N]=np.rot90(p.visible(bank[i,1,sy*k+sx]),r)
                    assert np.array_equal(field,np.rot90(assembled,r));checks+=1
            # One full independent regeneration per bank, after disk readback.
            record=data['provenance'][0];master,owner,trace=s.patch_quilt(pool,k*256,record['seed'])
            canvas,owner=s.carrier_canvas(master,owner,anchor,k)
            assert np.array_equal(s.slice_parts(canvas,k),bank[0,1]);checks+=1
            assert all((owner==i).any() for i in range(len(pool)));checks+=1
            neutral=s.diversity(bank[:,1].reshape(-1,256,256,3))
            assert neutral==data.get('neutralDiversity',data['diversity']);checks+=1
            # Apply the SAME Wang diversity gate to tonal copies as well, not just neutral parts.
            data['neutralDiversity']=neutral
            data['diversity']=s.diversity(bank.reshape(-1,256,256,3))
            neutral_mips=[]
            for mi,size in enumerate(s.LEVELS):
                with np.load(path/f'mip-{size}.npz') as archive:level=archive['pixels']
                assert level.shape==(4,3,k*k,size,size,3);checks+=1
                for i in range(4):
                    for t in range(3):assert np.array_equal(level[i,t],s.mip_parts(bank[i,t],anchor,k,size));checks+=1
                mm=s.mip_metrics(level,anchor,k,size)
                assert mm==data['mips'][mi] and mm['stockRelativeMaxRGB']==0;checks+=1
                neutral_mips.append(s.mip_metrics(np.repeat(level[:,1:2],3,axis=1),anchor,k,size))
                # External compatibility is pointwise anchor-relative under EVERY independent rotation.
                # Equality against anchor implies same seam with ANY fixed neighbour, including A.
                ref=w.sample_uv(w.box(anchor,size));comparisons=0
                for i in range(4):
                    for t in range(3):
                        for idx,a in enumerate(level[i,t]):
                            sample=w.sample_uv(a)
                            for r in range(4):
                                for side in s.outer_sides(idx%k,idx//k,k):
                                    ar=np.rot90(np.rot90(sample,r),(side-r)%4)[0]
                                    br=np.rot90(np.rot90(ref,r),(side-r)%4)[0]
                                    assert np.array_equal(ar,br);comparisons+=1
                checks+=comparisons
            # Verify ALL field cells after PNG decode against actual saved physical parts.
            for n in (24,48):
                for mode in ('super','tone','select'):
                    actual=texture(path/f'field-{n}-{mode}.png')
                    assert actual.shape==(n*s.N,n*s.N,3);checks+=1
                    for y in range(n):
                        for x in range(n):
                            i,t,idx,r=s.cell_key(x,y,k,data['means'],mode)
                            assert np.array_equal(actual[y*s.N:(y+1)*s.N,x*s.N:(x+1)*s.N],np.rot90(p.visible(bank[i,t,idx]),r));pixel_cells+=1
                    del actual
                for factor in (4,8):
                    with Image.open(path/f'compare-{n}-x{factor}.png') as im:
                        assert im.size==(4*(n*s.N//factor+8)+8,n*s.N//factor+40);checks+=1
            # Quantized world tone field and actual shifts; labels are constant per block.
            n=48;noise=np.empty((n,n),np.float32);tone=np.empty((n,n),np.uint8);selected=np.empty((n,n),np.uint8)
            for y in range(n):
                for x in range(n):
                    noise[y,x]=s.noise(x//k*k+k/2,y//k*k+k/2)
                    selected[y,x]=s.cell_key(x,y,k,data['means'],'select')[0]
                    tone[y,x]=s.cell_key(x,y,k,data['means'],'tone')[1]
            maps=[np.repeat(np.repeat(np.repeat(a[:,:,None],3,2),16,0),16,1).astype(np.uint8) for a in (noise*255,selected*85,tone*127)]
            s.panel(path/'tone-map.png',maps,['WORLD NOISE / wavelength12 cells','EXISTING S0..S3 sorted by mean','TONE COPY -1 / 0 / +1'])
            data['toneOccupancy']={str(t):int((tone==t).sum()) for t in range(3)}
            # Expose why literal disjoint 256 slicing is wrong with renderer inset16.
            raw=texture(path/f'super-0-source-{k*256}.png')
            naive=np.concatenate([np.concatenate([p.visible(raw[y*256:(y+1)*256,x*256:(x+1)*256]) for x in range(k)],axis=1) for y in range(k)],axis=0)
            crop=(len(raw)-k*s.N)//2;continuous=raw[crop:crop+k*s.N,crop:crop+k*s.N]
            s.panel(path/'packing-inset.png',[naive,continuous,s.assemble(bank[0,1],k)],['NAIVE 256 slices: UV drops32 at each joint','COHERENT central crop, no perimeter lock','OVERLAPPING carriers + stock perimeter'])
            data['packing']=dict(nominalMaster=k*256,visibleBlock=k*s.N,carrierStride=224,overlap=32,
                                 naiveDroppedPixelsPerInternalAxisJoint=32,exactReassembly=True)
            review=s.review(data['fields'],data['tiles'],data['diversity'],data['mips'],k)
            assert set(data['review']['reject']).issubset(set(review['reject']));checks+=1
            data['review']=review;s.write(path/'metrics.json',data);rejected+=int(not review['accepted'])
            neutral_tiles=s.tile_review(bank[:,1].reshape(-1,256,256,3),anchor)
            modes={}
            for mode in ('super','tone','select'):
                fields={n:{mode:m[mode]} for n,m in data['fields'].items()}
                modes[mode]=s.review(fields,data['tiles'] if mode=='tone' else neutral_tiles,
                                     data['diversity'] if mode=='tone' else neutral,
                                     data['mips'] if mode=='tone' else neutral_mips,k)
            data['methodReviews']=modes;s.write(path/'metrics.json',data)
            rows.append(dict(material=name,slug=slug,k=k,generationSeconds=data['generationSeconds'],
                             review=review,methodReviews=modes,fields=data['fields'],mips=data['mips'],diversity=data['diversity'],neutralDiversity=neutral,
                             usedPhysicalParts=data['usedPhysicalParts'],toneBounds=case['toneBounds']))
            case['banks'][str(k)]={key:value for key,value in data.items() if key not in ('tiles','provenance')}
            print('READBACK',slug,k,'checks',checks,'cells',pixel_cells,flush=True)
        s.write(folder/'metrics.json',case)
        for n in (24,48):
            for mode,tiles in (('now',pool),('A',common)):
                actual=texture(folder/f'field-{n}-{mode}.png')
                rng=np.random.Generator(np.random.PCG64(143));ids=(rng.random((n,n))*len(tiles)).astype(int);rot=rng.integers(0,4,(n,n))
                for y in range(n):
                    for x in range(n):assert np.array_equal(actual[y*s.N:(y+1)*s.N,x*s.N:(x+1)*s.N],np.rot90(p.visible(tiles[ids[y,x]]),int(rot[y,x])));pixel_cells+=1
                del actual
    snap.verify_end()
    # Cross-check streaming field metrics against unmodified Wang/PROTO formulas.
    probe,_,_=p.paint(pool,143,6);a=s.field_metrics(probe,3);b=p.paint_metrics(probe)
    for key in ('gridRatio','highpassStd'):
        assert abs(a[key]-b[key])<2e-5,(key,a[key],b[key]);checks+=1
    for key in b['highpassAutocorrelation']:
        assert np.allclose(a['highpassAutocorrelation'][key],b['highpassAutocorrelation'][key],atol=2e-7);checks+=1
    receipt=dict(structural='PASS',checks=checks,fieldCellsCompared=pixel_cells,artRejected=rejected,banks=len(rows),
                 thresholds=s.THRESHOLDS,rows=rows,inputHashes=snap.hashes,seconds=time.perf_counter()-started,
                 limits=['CPU RGB only','BOX-derived mip, no BC1 recompression','not GPU/trilinear/game acceptance'])
    s.write(s.OUT/'verification.json',receipt);costs=budget(rows);s.write(s.OUT/'budget.json',costs);tables(receipt,costs)
    files=[]
    for path in sorted(s.OUT.rglob('*')):
        if path.is_file() and path.name!='files.json' and not path.name.endswith('.log'):
            files.append(dict(path=str(path.relative_to(s.OUT)).replace('\\','/'),bytes=path.stat().st_size,sha256=w.sha(path)))
    s.write(s.OUT/'files.json',dict(files=files,excluded=['files.json self','*.log active process logs']))
    print('STRUCTURAL PASS',checks,'FIELD CELLS',pixel_cells,'ART REJECTED',rejected,'/',len(rows),flush=True)
    if args.require_art and rejected:sys.exit(2)


if __name__=='__main__':run()
