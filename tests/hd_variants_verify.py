"""Read back EI-HD-VARIANTS-PROTO outputs; no mapper, game, network or clipboard."""
from __future__ import annotations
import hashlib
import itertools
import json
import sys
import time
import argparse
import numpy as np
from PIL import Image

sys.dont_write_bytecode=True
import hd_variants_proto as p


def inventory(out, receipt):
    expected={'stone-comparison.png':(1064,632),'variants.png':(1592,632),
              'paint.png':(5400,2728),'paint-preview.png':(1560,808),
              'paint-hd-control.png':(1560,808),'edges.png':(1816,936),
              'transitions.png':(920,936)}
    files=[]
    for path in sorted(out.rglob('*')):
        if path.is_file() and path.name not in ('verification.json','files.json'):
            if path.suffix=='.png':
                with Image.open(path) as im:
                    im.load()
                    assert im.mode=='RGB'
                    assert im.size==(expected[path.name] if path.name in expected else (256,256)),str(path)
            files.append(dict(path=str(path.relative_to(p.ROOT)),bytes=path.stat().st_size,
                              sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
    summary=json.loads((out/'summary.json').read_text())
    for bank,b in summary['budget'].items():
        assert b['newSemanticTiles']==sum(max(0,6-r['existingD4']) for r in b['rows'])
        phys=b['physicalSplit']
        assert phys['newSemanticTiles']==sum(max(0,6-r['existingD4']) for r in phys['rows'])
        assert phys['combinations']==len(phys['rows'])
    receipt.update(files=files,pngCount=sum(f['path'].endswith('.png') for f in files),
                   allPngDimensionsReadBack=True,physicalBudgetConservation=True)
    receipt['taskFileHashes']={name:p.audit_tool.sha(p.ROOT/name) for name in (
        'tests/hd_variants_proto.py','tests/hd_variants_verify.py',
        'docs/validation/ATAMAN_HD_VARIANTS_PROTO_20261011.md')}
    p.write_json(out/'verification.json',receipt)
    p.write_json(out/'files.json',[f['path'] for f in files]+[
        str((out/'verification.json').relative_to(p.ROOT)),str((out/'files.json').relative_to(p.ROOT))])
    return receipt


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--inventory-only',action='store_true',help='Check new diagnostics/file sizes, keep completed generator checks')
    args=parser.parse_args()
    out=p.DEFAULT_OUTPUT
    if args.inventory_only:
        receipt=inventory(out,json.loads((out/'verification.json').read_text()))
        print('INVENTORY PASS',receipt['pngCount'],'PNGs',len(receipt['files'])+2,'files')
        return
    summary=json.loads((out/'summary.json').read_text(encoding='utf-8'))
    s=p.Snapshot(p.DEFAULT_SNAPSHOT)
    assert summary['inputHashes']==s.hashes
    # Small exhaustive oracle verifies the minimum-cut DP independently.
    cost=np.array([[7,3,9,5],[2,8,1,4],[6,1,5,2],[3,9,1,6]],np.float32)
    paths=[xs for xs in itertools.product(range(4),repeat=4)
           if all(abs(xs[y]-xs[y-1])<=1 for y in range(1,4))]
    expected=min(sum(cost[y,x] for y,x in enumerate(xs)) for xs in paths)
    cut=p.min_cut(cost)
    assert sum(cost[y,x] for y,x in enumerate(cut))==expected
    checks=[];timings=[]
    slugs=['grass-tundra','ground-brown','ground-stony-gray','stone-desert-3']
    for case,slug in zip(summary['cases'],slugs):
        folder=out/slug
        pool=[p.rgb(s.row_tile(r)) for r in case['sources']]
        final=[np.asarray(Image.open(folder/f'variant-{i+1:02}.png').convert('RGB')) for i in range(6)]
        assert all(x.shape==(256,256,3) for x in final)
        assert len({p.canonical(x) for x in final})==6
        assert all(np.array_equal(a,b) for a,b in zip(pool,final))
        for r in case['traces']:
            start=time.perf_counter()
            a,trace=p.quilt(pool,r['seed'],r['anchor'])
            timings.append(time.perf_counter()-start)
            assert np.array_equal(a,final[r['index']]), 'Deterministic readback '+slug
            retained={i:sum(t['survivingPixels'] for t in trace if t['source']==i) for i in range(len(pool))}
            assert all(n>0 for n in retained.values())
            r['donorRetainedPixels']=retained
            r['patches']=trace
        p.edge_proof(final,pool,case['anchors'])
        # Four rotations, all original/new neighbour pairs: absolute seam data
        # must equal the corresponding anchor seam, not merely average error.
        pair_checks=0
        for i,a in enumerate(final):
            for j,b in enumerate(final):
                for ka in range(4):
                    for kb in range(4):
                        x,y=np.rot90(p.visible(a),ka),np.rot90(p.visible(b),kb)
                        ax,by=np.rot90(p.visible(pool[case['anchors'][i]]),ka),np.rot90(p.visible(pool[case['anchors'][j]]),kb)
                        assert np.array_equal(x[:,-1].astype(int)-y[:,0],ax[:,-1].astype(int)-by[:,0])
                        assert np.array_equal(x[-1].astype(int)-y[0],ax[-1].astype(int)-by[0])
                        pair_checks+=2
        # Verify every cell in the saved full paint sheet independently using
        # serialized labels/turns. No image recomputation is used for comparison.
        image=np.asarray(Image.open(folder/'paint.png'))
        assert image.shape==(2728,5400,3)
        labels=case['layout']['after'];turns=case['layout']['quarterTurns']
        for y in range(12):
            for x in range(12):
                left=2704+x*224;top=32+y*224
                expected=np.rot90(p.visible(final[labels[y][x]]),turns[y][x])
                assert np.array_equal(image[top:top+224,left:left+224],expected)
        # Explicit mip footprint diagnostic. BOX is a documented model only;
        # it is not production BC1 encoding nor the game's anisotropic filter.
        mip=[]
        for level in (1,2,3,4):
            n=256>>level; inset=n//16; max_error=0
            for i,a in enumerate(final):
                x=np.asarray(p.pil(a).resize((n,n),Image.Resampling.BOX)).astype(int)
                y=np.asarray(p.pil(pool[case['anchors'][i]]).resize((n,n),Image.Resampling.BOX)).astype(int)
                boundary=np.zeros((n,n),bool)
                boundary[inset,inset:n-inset]=True;boundary[n-inset-1,inset:n-inset]=True
                boundary[inset:n-inset,inset]=True;boundary[inset:n-inset,n-inset-1]=True
                max_error=max(max_error,int(abs(x[boundary]-y[boundary]).max()))
            mip.append(dict(level=level,size=n,visibleEdgeMaxRgbDifference=max_error))
        assert all(x['visibleEdgeMaxRgbDifference']==0 for x in mip[:3])
        case['mipFootprint']=mip
        p.write_json(folder/'metrics.json',case)
        checks.append(dict(material=case['name'],variants=6,regeneratedReadbacks=len(case['traces']),
                           allDonorsSurvive=True,pairRotationEdgeChecks=pair_checks,
                           paintCellsReadBack=144,mipFootprint=mip))
    for bank,b in summary['budget'].items():
        assert b['newSemanticTiles']==sum(max(0,6-r['existingD4']) for r in b['rows'])
        assert b['bc1BytesPerTile']==43704
    # Snapshot hashes checked at start and end of actual artifact verification.
    s.verify_end()
    p.write_json(out/'summary.json',summary)
    receipt=dict(status='PASS',cases=checks,minCutOracle=True,
                 generatorSeconds=dict(min=min(timings),median=float(np.median(timings)),max=max(timings)))
    receipt=inventory(out,receipt)
    print(json.dumps({k:v for k,v in receipt.items() if k!='files'},indent=2))


if __name__=='__main__':
    main()
