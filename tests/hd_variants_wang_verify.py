"""Readback, complete directed-pair metrics, diversity, budget and receipts.

Writes only build-tests-hd-wang. Aesthetic rejection is separately reported;
--require-art exits 2 when the specified no-grid gate fails.
"""
from __future__ import annotations
import argparse
import ast
import hashlib
import json
from pathlib import Path
import sys
import time
sys.dont_write_bytecode=True
import numpy as np
from PIL import Image
import hd_variants_wang as w
old=w.old

def load(p):return json.loads(p.read_text(encoding='utf-8'))

def diversity(bank):
    # Low-pass RMSE64 over all unordered physical pairs and all four rotations.
    # Exact full-resolution rotation hashes separately guard false duplicates.
    hashes=[min(hashlib.sha256(np.rot90(a,k).tobytes()).hexdigest() for k in range(4)) for a in bank]
    small=np.stack([w.box(a,64) for a in bank]).astype(np.float32)
    errors=np.full((len(bank),len(bank)),np.inf)
    x=small.reshape(len(bank),-1);xx=np.mean(x*x,axis=1)
    for k in range(4):
        y=np.rot90(small,k,axes=(1,2)).reshape(len(bank),-1)
        squared=xx[:,None]+np.mean(y*y,axis=1)[None,:]-2*(x@y.T)/x.shape[1]
        errors=np.minimum(errors,np.sqrt(np.maximum(squared,0)))
    v=errors[np.triu_indices(len(bank),1)]
    assert len(set(hashes))==len(bank)
    return dict(exactRotationUnique=len(set(hashes)),minRmse64=float(v.min()),medianRmse64=float(np.median(v)),
                pairs=len(v),rotations=4,note='RMSE on BOX64 copies; full-resolution exact C4 hashes also unique')

def boundary_regions():
    """Real two-material stock transition along a four-cell region edge."""
    s=old.Snapshot(old.DEFAULT_SNAPSHOT);result=[]
    presets={p['namespace'].lower():p for p in load(w.ROOT/'assets/resources/terrain_atlas_presets.json')['presets']}
    for name,cls,slug in w.CASES:
        folder=w.OUT/slug;case=load(folder/'metrics.json');anchor=np.asarray(Image.open(folder/'stock-anchor.png'))
        error,transition,row,rotation=w.stock_transition(s,name,cls,case['sources'][0],anchor,straight=True)
        sigs=np.frombuffer(w.base64.b64decode(presets[row['namespace']]['signaturesBase64']),np.uint8).reshape(-1,4)
        sig=np.roll(sigs[row['slot']],-rotation);family=int(sig[1])
        candidates=[r for r in s.rows['jamaisvu'] if r['namespace']==row['namespace'] and r['kind']=='uniform'
                    and r['animated'] is False and r['opaque'] and all(sigs[r['slot']]==family)]
        assert candidates,(slug,'missing other stock material')
        r=min(candidates,key=lambda r:(r['size']!=256,r['slot']));other=old.rgb(s.row_tile(r))
        if len(other)!=256:other=old.resize(other,256)
        stock=np.tile(old.visible(anchor),(4,4,1))
        edge=np.tile(old.visible(transition),(4,1,1));outside=np.tile(old.visible(other),(4,1,1))
        before=np.concatenate((stock,edge,outside),axis=1)
        for colors in (2,3):
            path=folder/f'c{colors}';keys=w.tuples(colors)
            bank=[np.asarray(Image.open(path/f'tile-{i:03}.png')) for i in range(colors**4)]
            painted,_=w.fields(bank,keys,12,colors);crop=painted[4*224:8*224,8*224:12*224]
            after=np.concatenate((crop,edge,outside),axis=1)
            assert np.array_equal(before[:,4*224-8:],after[:,4*224-8:])
            w.panel(path/'boundary-region.png',[before,after],['STOCK REGION / actual transition / other material',f'WANG C{colors} / same transition / same other material'],width=1344)
        result.append(dict(material=name,transition=row,rotation=rotation,other=r,signature=sig.tolist(),
                           protectedBorderDelta=0,baselineSeamLuma=error))
    s.verify_end();old.write_json(w.OUT/'boundary-regions.json',result)
    return len(result)*2

def run():
    p=argparse.ArgumentParser();p.add_argument('--require-art',action='store_true');args=p.parse_args()
    start=time.perf_counter();checks=0;rows=[]
    for source in ('hd_variants_wang.py','hd_variants_wang_verify.py'):
        ast.parse((w.ROOT/'tests'/source).read_text(encoding='utf-8'));checks+=1
    solver=w.solver_checks();checks+=6
    members=[dict(bankSha='verified-pixel-bank',frame=0,anchor='stock0',seed=w.SEED,width=12,height=24,origin=(x,0)) for x in (0,12)]
    parts=w.solve_cohort(members,3,w.SEED,(0,0,24,24));combined=dict(parts[0]);combined.update(parts[1])
    assert combined==w.layout(24,24,3);checks+=1
    for field,value in [('bankSha','other'),('frame',1),('anchor','other'),('seed',0)]:
        bad=[dict(x) for x in members];bad[1][field]=value
        try:w.solve_cohort(bad,3,w.SEED,(0,0,24,24))
        except ValueError:checks+=1
        else:raise AssertionError(field)
    solver['cohortMismatchRefusals']=['bankSha','frame','anchor','seed']
    checks+=boundary_regions()
    for name,cls,slug in w.CASES:
        folder=w.OUT/slug;case=load(folder/'metrics.json');anchor=np.asarray(Image.open(folder/'stock-anchor.png'))
        for colors in (2,3):
            path=folder/f'c{colors}';m=load(path/'metrics.json');keys=[tuple(t) for t in m['keys']]
            bank=[np.asarray(Image.open(path/f'tile-{i:03}.png').convert('RGB')) for i in range(colors**4)]
            assert all(a.shape==(256,256,3) for a in bank);checks+=len(bank)
            m['diversity']=diversity(bank)
            m['review']=w.auto_review(bank,anchor,keys,m['mipPairs'],m['metrics']['24']['now'],m['metrics']['24']['B'])
            if m['diversity']['minRmse64']<5:
                m['review']['bankReject'].append('min C4 RMSE64 <5')
                m['review']['accepted']=False
            for mi,size in enumerate((256,128,64,32,16,8,4)):
                saved=np.load(path/f'mip-{size}.npz');aa=saved['pixels']
                assert np.array_equal(saved['keys'],keys)
                assert aa.shape==(colors**4,size,size,3);checks+=2
                assert m['zeroEdges'][mi]['maxStockRelativeRGB']==0;checks+=1
                if size<=8:
                    assert all(np.array_equal(a,w.box(anchor,size)) for a in aa);checks+=1
                else:
                    # Readback every physical zero side (all frames were measured).
                    ref=w.sample_uv(w.box(anchor,size))
                    for a,key in zip(aa,keys):
                        s=w.sample_uv(a)
                        for side,c in enumerate(key):
                            if c==0:
                                assert np.array_equal(np.rot90(s,side)[0],np.rot90(ref,side)[0]);checks+=1
                # Separate stock inherited seams from synthesized colours.
                groups=m['mipPairs'][mi]['groups']
                m['mipPairs'][mi]['summary']={str(c):dict(
                    mean=float(np.mean([g['mean'] for g in groups if g['color']==c])),
                    worst=max(g['max'] for g in groups if g['color']==c),
                    ratio=float(np.mean([g['ratio'] for g in groups if g['color']==c]))) for c in range(colors)}
            for side in (12,24):
                full=path/f'paint-{side}.png'
                with Image.open(full) as im:
                    assert im.size==(3*(side*224+8)+8,side*224+40);im.verify();checks+=1
                assert len(m['layouts'][str(side)])==side**2;checks+=1
                slots=len({r[2] for r in m['layouts'][str(side)]})
                m.setdefault('usedSlots',{})[str(side)]=slots
            # New colours must really change the sampled original boundary.
            changed=[]
            for c in range(1,colors):
                key=(c,0,0,0);a=bank[keys.index(key)]
                changed.append(float(np.mean(abs(a[16,24:232].astype(float)-anchor[16,24:232]))))
            assert min(changed)>0;checks+=1;m['newEdgeMAE']=changed
            old.write_json(path/'metrics.json',m)
            case['banks'][str(colors)]={k:v for k,v in m.items() if k!='provenance'}
            rows.append(dict(material=name,slug=slug,colors=colors,sourceHd=case['sourceHd'],
                generationSeconds=m['generationSeconds'],tiles=len(bank),usedSlots=m['usedSlots'],
                diversity=m['diversity'],metrics=m['metrics']['24'],mips=[dict(size=v['size'],summary=v['summary']) for v in m['mipPairs']],
                accepted=m['review']['accepted'],rejectedTiles=sum(bool(t['reject']) for t in m['review']['tiles']),
                rejection=m['review']['bankReject'],allPairComparisons=sum(v['legalPairCount'] for v in m['mipPairs'])))
        old.write_json(folder/'metrics.json',case)
    # Independent byte readback of PNG-based determinism on a real synthesized tile.
    folder=w.OUT/'grass-tundra';pool=[np.asarray(Image.open(p).convert('RGB')) for p in sorted(folder.glob('donor-*.png'))]
    seed=w.SEED+w.h('grass-tundra')%100000
    templates,traces=w.make_templates(pool,seed)
    rebuilt,_=w.make_tile(pool,(1,1,1,1),templates,seed+1000+15)
    assert np.array_equal(rebuilt,np.asarray(Image.open(folder/'c2/tile-015.png')));checks+=1
    for trace in traces:assert all(t['retainedInVisibleTwoSides']>0 for t in trace);checks+=1
    previous=load(w.ROOT/'build-tests-hd-variants-proto/budget.json')
    bc1=sum(max(1,(256>>m)//4)**2*8 for m in range(9))
    budgets=[]
    for count,label in [(90,'requested 90 groups'),(93,'stock land groups'),(96,'stock static nonwater including overlay')]:
        for colors in (2,3):
            per=colors**4;seconds=np.mean([r['generationSeconds']/r['tiles'] for r in rows if r['colors']==colors])
            budgets.append(dict(groups=count,label=label,colors=colors,tiles=count*per,bytesPerTile=bc1,
                MiB=count*per*bc1/1048576,pages64=(count*per+63)//64,
                generationSeconds=float(count*per*seconds),slotsPerMaterial=per,
                extraInteriorReplicaTiles=count*per*2,extraInteriorReplicaMiB=count*per*2*bc1/1048576))
    # Physical-class split and anchor families increase banks, not interchangeable pixels.
    stock=previous['jamaisvu'];counts=dict(semanticGroups=stock['combinations'],physicalGroups=stock['physicalSplit']['combinations'])
    result=dict(structuralPass=checks,structuralFail=0,artAccepted=sum(r['accepted'] for r in rows),artRejected=sum(not r['accepted'] for r in rows),
        rows=rows,budgets=budgets,catalog=counts,bc1BytesPerTile=bc1,solver=solver,
        scriptHashes={p.name:w.sha(p) for p in (w.ROOT/'tests').glob('hd_variants_wang*.py')},
        elapsedSeconds=time.perf_counter()-start)
    old.write_json(w.OUT/'verification.json',result)
    # Exact artifact list, with hashes. Exclude only the self-referential manifest.
    files=[dict(path=str(p.relative_to(w.ROOT)).replace('\\','/'),bytes=p.stat().st_size,sha256=w.sha(p))
           for p in sorted(w.OUT.rglob('*')) if p.is_file() and p.name!='files.json']
    old.write_json(w.OUT/'files.json',files)
    print(json.dumps(dict(structuralPass=checks,artAccepted=result['artAccepted'],artRejected=result['artRejected'],artifacts=len(files)),indent=2))
    if args.require_art and result['artRejected']:raise SystemExit(2)

if __name__=='__main__':run()
