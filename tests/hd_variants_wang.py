"""Offline EI-HD-VARIANTS-WANG. Private snapshots only; no mapper/network.

Edge Wang experiment: complete 2/3-colour banks, stock colour zero, directed
collars, SEC rotations with a shared frame. A negative visual gate is a result,
not permission to install. Requires the already installed numpy and Pillow.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import itertools
import json
from pathlib import Path
import sys
import time
sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw
import hd_variants_proto as old

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build-tests-hd-wang'
N = 224
SEED = 20261011
CASES = [('Grass / Tundra',0,'grass-tundra'),('Ground / Brown',0,'ground-brown'),
         ('Ground / Stony gray',2,'ground-stony-gray'),('Grass / Common',0,'grass-common')]

def sha(p):
    return hashlib.sha256(Path(p).read_bytes()).hexdigest()

def h(*parts):
    return int.from_bytes(hashlib.sha256('|'.join(map(str,parts)).encode()).digest()[:8],'little')

def tuples(c):
    return list(itertools.product(range(c),repeat=4)) # N,E,S,W

def rotate_key(key,k):
    return tuple(np.roll(key,-k).tolist())

def rotations_count(c):
    return len({min(rotate_key(t,k) for k in range(4)) for t in tuples(c)})

def layout(w,height,colors,seed=SEED,origin=(0,0),bounds=None,fixed=None,order=None):
    """Global edge IDs, outside=0. Fixed existing cells override hash proposals.

    All fixed cells have the same pixel bank and SEC frame; callers must reject
    incompatible frames/banks before this function. Returns no partial edits.
    """
    ox,oy=origin
    bounds=bounds or (ox,oy,ox+w,oy+height)
    x0,y0,x1,y1=bounds
    def edges(x,y):
        return [('H',x,y),('V',x+1,y),('H',x,y+1),('V',x,y)]
    def boundary(e):
        axis,x,y=e
        return y in (y0,y1) if axis=='H' else x in (x0,x1)
    pins={}
    for (x,y),key in (fixed or {}).items():
        for edge,c in zip(edges(x,y),key):
            if edge in pins and pins[edge]!=c:
                raise ValueError('conflicting existing neighbours')
            if boundary(edge) and c!=0:
                raise ValueError('nonzero edge at protected boundary')
            pins[edge]=c
    def color(e):
        if boundary(e): return 0
        # C2 includes stock zero inside; C3 reserves zero for boundaries.
        return pins.get(e, h(seed,*e) % (2 if colors==2 else colors-1) + (0 if colors==2 else 1))
    result={}
    coords=order or [(x,y) for y in range(oy,oy+height) for x in range(ox,ox+w)]
    for x,y in coords: result[(x,y)]=tuple(color(e) for e in edges(x,y))
    return result

def solver_checks():
    a=layout(24,24,3)
    reverse=layout(24,24,3,order=list(reversed(list(a))))
    assert a==reverse==layout(24,24,3)
    b=layout(24,24,3,seed=SEED+1)
    assert a!=b
    for (x,y),t in a.items():
        if x<23: assert t[1]==a[(x+1,y)][3]
        if y<23: assert t[2]==a[(x,y+1)][0]
    bounds=(0,0,24,24)
    split=layout(12,24,3,bounds=bounds)
    split.update(layout(12,24,3,origin=(12,0),bounds=bounds))
    assert split==a
    fixed={p:t for p,t in a.items() if p!=(12,12)}
    repaint=layout(24,24,3,seed=999,fixed=fixed)
    assert repaint==a # One physical tuple: repaint is necessarily a no-op.
    conflict=dict(fixed); conflict[(11,12)]=(1,2,1,1); conflict[(12,11)]=(1,1,2,2)
    try: layout(24,24,3,fixed=conflict)
    except ValueError: pass
    else: raise AssertionError('conflict must refuse atomically')
    return dict(orderIndependent=True,repeatable=True,differentSeed=True,
                splitWorldEqualsWhole=True,oneCellRepaintNoOp=True,conflictRefused=True,
                framePolicy='one shared SEC frame per connected bank component; mismatch refuses')

def solve_cohort(members,colors,seed,bounds):
    """Prototype preflight: no atlas writes, stage all members or raise."""
    identities={(m['bankSha'],m['frame'],m['anchor']) for m in members}
    if len(identities)!=1:raise ValueError('incompatible pixel banks, stock anchors or SEC frames')
    if any(m['seed']!=seed for m in members):raise ValueError('incompatible cohort seed')
    staged=[]
    for m in members:
        staged.append(layout(m['width'],m['height'],colors,seed,m['origin'],bounds,m.get('fixed')))
    return staged

def paste_collar(tile,target,side,width=40,lock=8):
    """Minimum-error cut 16..39 inward; edges/corner8 are hard constraints."""
    # Orient chosen side to the top. target is in the same oriented frame.
    a=np.rot90(tile,side).copy()
    b=np.rot90(target,side)
    cost=np.mean((a[:width].astype(float)-b[:width])**2,axis=2)
    cut=old.min_cut(cost[16:width].T)+16
    mask=np.arange(width)[:,None]<=cut[None,:]
    mask[:lock]=True
    # Do not modify intersections with other side constraints.
    mask[:,:lock]=False;mask[:,-lock:]=False
    a[:width][mask]=b[:width][mask]
    return np.rot90(a,-side).copy()

def make_templates(pool,seed):
    templates=[]; traces=[]
    for c in (1,2):
        rng=np.random.Generator(np.random.PCG64(seed+c))
        strip=np.empty((96,N,3),np.uint8);owner=np.full((96,N),-1,np.int16)
        order=list(range(len(pool)));rng.shuffle(order);trace=[]
        boundaries=np.linspace(0,N,len(pool)+1,dtype=int)
        for j,source in enumerate(order):
            x=max(0,int(boundaries[j])-8);end=int(boundaries[j+1]);width=end-x
            turn=int(rng.integers(4));sy=int(rng.integers(0,161));sx=int(rng.integers(0,257-width))
            piece=np.rot90(pool[source],turn)[sy:sy+96,sx:sx+width]
            mask=np.ones((96,width),bool)
            if j:
                overlap=int(boundaries[j])-x
                cost=np.mean((strip[:,x:x+overlap].astype(float)-piece[:,:overlap])**2,axis=2)
                cut=old.min_cut(cost)
                mask[:,:overlap]=np.arange(overlap)[None,:]>cut[:,None]
            strip[:,x:end][mask]=piece[mask];owner[:,x:end][mask]=source
            trace.append(dict(source=source,quarterTurn=turn,x=sx,y=sy,width=width,destination=x))
        for item in trace:
            item['retainedIn96Strip']=int((owner==item['source']).sum())
            item['retainedInVisibleTwoSides']=int((owner[40:56]==item['source']).sum())
            assert item['retainedInVisibleTwoSides']>0
        templates.append(strip);traces.append(trace)
    return templates,traces

def make_tile(pool,key,templates,seed):
    a,trace=old.quilt(pool,seed,0)
    result=old.visible(a).copy(); anchor=old.visible(pool[0])
    for side,color in enumerate(key):
        target=anchor.copy()
        if color:
            strip=templates[color-1]
            if side==0: target[:40]=strip[48:88]
            elif side==2: target[-40:]=strip[8:48]
            elif side==1: target[:,-40:]=strip[8:48].transpose(1,0,2)
            else: target[:,:40]=strip[48:88].transpose(1,0,2)
        # side index N,E,S,W differs from numpy CCW top-normal rotation.
        result=paste_collar(result,target,side)
    # Preserve original corner squares; only 8px instead of the whole rim24.
    for y in (0,N-8):
        for x in (0,N-8): result[y:y+8,x:x+8]=anchor[y:y+8,x:x+8]
    # Source-size carrier with explicit 1/16 gutters; no new detail invented.
    a[16:240,16:240]=result
    for side,color in enumerate(key):
        r=np.rot90(a,side).copy(); stock=np.rot90(pool[0],side)
        if color==0: r[:24]=stock[:24]
        else: r[:16,24:232]=r[16:17,24:232]
        a=np.rot90(r,-side).copy()
    return a,trace

def box(a,size):
    return np.asarray(old.pil(a).resize((size,size),Image.Resampling.BOX))

def sample_uv(a):
    """Bilinear RGB at cell-pixel centres, fixed visible UV 1/16..15/16.

    At mip4 the footprint touches several constraints; this cannot generally
    preserve all four independent stock sides in a single 4x4 texture.
    """
    size=a.shape[0]; m=max(4,round(size*7/8))
    p=size*(1/16+(np.arange(m)+.5)/m*7/8)-.5
    lo=np.floor(p).astype(int);f=p-lo; hi=np.clip(lo+1,0,size-1);lo=np.clip(lo,0,size-1)
    b=a.astype(float)
    q=b[lo]*(1-f[:,None,None])+b[hi]*f[:,None,None]
    return q[:,lo]*(1-f[None,:,None])+q[:,hi]*f[None,:,None]

def mip_tile(a,anchor,key,size,bank=None,keys=None):
    """Explicit per-level footprint lock against a deterministic edge-only tile.

    Every colour's border comes from this recipe, not unrelated interiors.
    For zero use original stock mip; intersecting constraints favour stock.
    Low mips intentionally expose intersection conflicts to metrics.
    """
    result=box(a,size).copy(); ref=box(anchor,size)
    if size<=8:
        # At this footprint all four collars overlap. Explicit conservative
        # stock fallback, not a claim of four independent Wang colours at mip4.
        return ref.copy()
    m=max(4,round(size*7/8))
    width=int(np.ceil(size*(1/16+.5/m*7/8)-.5))+1
    for side,color in enumerate(key):
        reference=ref
        if color and bank is not None:
            template_key=[0]*4;template_key[side]=color
            reference=box(bank[keys.index(tuple(template_key))],size)
        r=np.rot90(result,side).copy();s=np.rot90(reference,side)
        r[:width]=s[:width];result=np.rot90(r,-side).copy()
    for y in (0,size-width):
        for x in (0,size-width):result[y:y+width,x:x+width]=ref[y:y+width,x:x+width]
    return result

def fields(bank,keys,n,colors,frame=0):
    mapping=layout(n,n,colors)
    lookup={key:i for i,key in enumerate(keys)}
    result=np.empty((n*N,n*N,3),np.uint8)
    refs=[]
    for (x,y),key in mapping.items():
        # Rotate the complete field, hence both cell positions and SEC frame.
        idx=lookup[key];result[y*N:(y+1)*N,x*N:(x+1)*N]=old.visible(bank[idx])
        refs.append([x,y,idx])
    return np.rot90(result,frame).copy(),refs

def panel(path,images,labels,width=768):
    hgt=round(images[0].shape[0]*width/images[0].shape[1])
    out=Image.new('RGB',(len(images)*(width+8)+8,hgt+40),'#22262b');d=ImageDraw.Draw(out)
    for i,(a,label) in enumerate(zip(images,labels)):
        im=old.pil(a).resize((width,hgt),Image.Resampling.LANCZOS)
        out.paste(im,(8+i*(width+8),32));d.text((8+i*(width+8),8),label,fill='white')
    out.save(path)

def all_pairs(bank,keys,colors,size):
    """Every ordered matching pair, both axes and all 4 legal SEC frames.

    Equal frame is an explicit directed-collar constraint, not an accidental
    omission of mixed rotations. Also measure forbidden cross-frame pairs.
    """
    samples=np.stack([sample_uv(a) for a in bank]).astype(np.float32)
    lum=samples@np.array([.2126,.7152,.0722],np.float32)
    interior=float((abs(np.diff(lum,axis=1)).mean()+abs(np.diff(lum,axis=2)).mean())/2)
    metrics=[];bad=[]
    for k in range(4):
        r=np.rot90(lum,k,axes=(1,2));labels=np.array([rotate_key(t,k) for t in keys])
        for axis,sa,sb in ((0,1,3),(1,2,0)):
            left=r[:,:,-1] if axis==0 else r[:,-1,:]
            right=r[:,:,0] if axis==0 else r[:,0,:]
            errors=abs(left[:,None,:]-right[None,:,:]).mean(axis=2)
            for c in range(colors):
                selected=errors[(labels[:,sa,None]==c)&(labels[None,:,sb]==c)]
                metrics.append(dict(frame=k,axis=axis,color=c,count=selected.size,
                                    mean=float(selected.mean()),max=float(selected.max()),
                                    ratio=float(selected.mean()/max(interior,1e-6))))
        for other in range(4):
            if other==k:continue
            rr=np.rot90(lum,other,axes=(1,2));ll=np.array([rotate_key(t,other) for t in keys])
            errors=abs(r[:,:,-1][:,None,:]-rr[:,:,0][None,:,:]).mean(axis=2)
            mask=labels[:,1,None]==ll[None,:,3]
            bad.append(float(errors[mask].mean()))
    return dict(size=size,interior=interior,groups=metrics,
                legalPairCount=sum(x['count'] for x in metrics),
                crossFrameColorOnlyMean=max(bad),
                note='Colour equality alone is insufficient: bank/frame and stock anchor also matter')

def auto_review(bank,anchor,keys,pair_metrics,current_metrics,field_metrics):
    values=[];anchor_l=old.luma(old.visible(anchor));mean=float(anchor_l.mean());std=float(anchor_l.std())
    for idx,a in enumerate(bank):
        y=old.luma(old.visible(a)); reasons=[]
        if abs(float(y.mean())-mean)>8: reasons.append('luma mean drift >8')
        if not .8<=float(y.std())/std<=1.25: reasons.append('contrast outside 0.8..1.25')
        # Detect hard strip joins, not only the outer tile boundary.
        dx=abs(np.diff(y,axis=1));dy=abs(np.diff(y,axis=0))
        ratio=max(float(dx.mean(axis=0).max()/max(dx.mean(),1e-6)),float(dy.mean(axis=1).max()/max(dy.mean(),1e-6)))
        if ratio>2.5:reasons.append('internal straight line >2.5x mean gradient')
        values.append(dict(index=idx,key=keys[idx],lumaMean=float(y.mean()),std=float(y.std()),lineRatio=ratio,reject=reasons))
    gates=[]
    if max(field_metrics['gradientPeriodAmplitudeXY'])>.04:gates.append('cell-period amplitude >0.04')
    if max(v for v in field_metrics['highpassAutocorrelation'][str(N)])>.08:gates.append('lag-one autocorrelation >0.08')
    if field_metrics['gridRatio']>1.3:gates.append('field seam/interior >1.3')
    if any(g['max']/max(level['interior'],1e-6)>1.5
           for level in pair_metrics if level['size']>=16
           for g in level['groups'] if g['color']!=0):
        gates.append('one or more legal nonzero pairs >1.5x interior gradient at mip256..16')
    if any(x['reject'] for x in values):gates.append('one or more required grammar tiles rejected')
    return dict(thresholds='heuristic predeclared engineering filters, not human/art acceptance',
                tiles=values,bankReject=gates,accepted=not gates)

def stock_transition(s,name,cls,ref,anchor,straight=False):
    presets=json.loads((ROOT/'assets/resources/terrain_atlas_presets.json').read_text(encoding='utf-8'))['presets']
    ns=ref['namespace'];p=next(p for p in presets if p['namespace'].lower()==ns)
    signatures=np.frombuffer(base64.b64decode(p['signaturesBase64']),np.uint8).reshape(-1,4)
    family=int(signatures[ref['slot'],0]);candidates=[]
    for row in s.rows['jamaisvu']:
        if row['namespace']!=ns or row['kind']!='transition' or row['animated'] is not False or not row['opaque']:continue
        for k in range(4):
            sig=np.roll(signatures[row['slot']],-k)
            if sig[0]==family and sig[3]==family:
                if straight and not (sig[1]==sig[2] and sig[1]!=family):continue
                a=old.rgb(s.row_tile(row));a=old.resize(a,256) if len(a)!=256 else a
                a=np.rot90(a,k).copy()
                err=float(abs(old.luma(old.visible(anchor)[:,-1])-old.luma(old.visible(a)[:,0])).mean())
                candidates.append((err,a,row,k))
    assert candidates,(name,'no actual transition')
    return min(candidates,key=lambda x:x[0])

def material(s,name,cls,slug):
    start=time.perf_counter();folder=OUT/slug;folder.mkdir(exist_ok=True)
    pool,sources=s.pool(name,cls)
    other,_=s.pool(name,cls,'dejavu')
    for a in other:
        if old.canonical(a) not in {old.canonical(b) for b in pool}:pool.append(a)
    assert len({r['physicalClass'] for r in sources})==1
    assert len(pool)<=16
    seed=SEED+h(slug)%100000
    previous=ROOT/'build-tests-hd-variants-proto'/slug
    if previous.exists():
        common=[np.asarray(Image.open(previous/f'variant-{i:02}.png').convert('RGB')) for i in range(1,7)]
    else:
        common=list(pool)
        while len(common)<max(6,len(pool)+2):common.append(old.quilt(pool,seed+len(common),len(common)%len(pool))[0])
    templates,traces=make_templates(pool,seed)
    data=dict(name=name,reviewedClass=cls,sourceHd=len(pool),sources=sources,edgeTraces=traces,banks={})
    err,transition,trow,trot=stock_transition(s,name,cls,sources[0],pool[0])
    old.pil(transition).save(folder/'stock-transition.png')
    old.pil(pool[0]).save(folder/'stock-anchor.png')
    for i,a in enumerate(pool):old.pil(a).save(folder/f'donor-{i:02}.png')
    for colors in (2,3):
        ts=time.perf_counter();keys=tuples(colors);bank=[];provenance=[]
        for i,key in enumerate(keys):
            a,trace=make_tile(pool,key,templates,seed+1000+i)
            bank.append(a);provenance.append(trace)
        generation=time.perf_counter()-ts
        path=folder/f'c{colors}';path.mkdir(exist_ok=True)
        for i,a in enumerate(bank):old.pil(a).save(path/f'tile-{i:03}.png')
        rows=[]
        for i in range(0,len(bank),9):rows.append((f'ARTIFICIAL C{colors}; N E S W; fixed original corner8',[(a,''.join(map(str,key))) for a,key in zip(bank[i:i+9],keys[i:i+9])]))
        old.sheet(path/'tiles.png',rows,width=128)
        mm=[];mip_panels=[];zero=[]
        for size in (256,128,64,32,16,8,4):
            level=[a if size==256 else mip_tile(a,pool[0],key,size,bank,keys) for a,key in zip(bank,keys)]
            np.savez_compressed(path/f'mip-{size}.npz',pixels=np.stack(level),keys=np.array(keys))
            pm=all_pairs(level,keys,colors,size);mm.append(pm)
            # Reference-relative equality for every zero side in every SEC frame.
            ref=sample_uv(box(pool[0],size));worst=0
            for a,key in zip(level,keys):
                for k in range(4):
                    aa=sample_uv(np.rot90(a,k));bb=np.rot90(ref,k);kk=rotate_key(key,k)
                    for side,c in enumerate(kk):
                        if c==0:worst=max(worst,float(abs(np.rot90(aa,side)[0]-np.rot90(bb,side)[0]).max()))
            zero.append(dict(size=size,maxStockRelativeRGB=worst))
            chosen=keys.index((1,0,1,1));sample=sample_uv(level[chosen]);nb=sample_uv(box(transition,size))
            mip_panels.append(np.concatenate((sample,nb),axis=1).clip(0,255).astype(np.uint8))
        old.sheet(path/'mips-transition.png',[(f'colour0 / stock transition; bilinear UV, mip {size}',[(a,'explicit footprint lock')]) for size,a in zip((256,128,64,32,16,8,4),mip_panels)],width=256)
        metrics={};layouts={}
        for n in (12,24):
            before,_,_=old.paint(pool,seed=143,side=n);aa,_,_=old.paint(common,seed=143,side=n)
            bb,refs=fields(bank,keys,n,colors)
            labels=[f'NOW {n}x{n}',f'A shared rim24 {n}x{n}',f'B Wang C{colors} {n}x{n}']
            panel(path/f'paint-{n}-preview.png',[before,aa,bb],labels)
            # Full native scale uses three separate images plus a full triptych.
            panel(path/f'paint-{n}.png',[before,aa,bb],labels,width=n*N)
            metrics[str(n)]={lab:old.paint_metrics(a) for lab,a in zip(('now','A','B'),(before,aa,bb))}
            layouts[str(n)]=refs
            if n==12:panel(path/'closeup.png',[before[2*N:5*N,2*N:5*N],aa[2*N:5*N,2*N:5*N],bb[2*N:5*N,2*N:5*N]],labels,width=672)
        # Protected edge is next to an actual stock transition, not a uniform proxy.
        chosen=keys.index((1,0,1,1));lhs=np.concatenate([old.visible(pool[0]),old.visible(transition)],axis=1)
        rhs=np.concatenate([old.visible(bank[chosen]),old.visible(transition)],axis=1)
        panel(path/'transition.png',[lhs,rhs],['STOCK + existing transition','WANG E=0 + same transition'],width=896)
        review=auto_review(bank,pool[0],keys,mm,metrics['24']['now'],metrics['24']['B'])
        # No rotation quotient claimed unless the complete stock collar is covariant.
        covariance=max(float(abs(old.visible(pool[0]).astype(float)-np.rot90(old.visible(pool[0]),k)).max()) for k in range(1,4))
        result=dict(colors=colors,tiles=len(bank),rotationOrbitsAlgebraic=rotations_count(colors),
                    stockRotationDifference=covariance,generationSeconds=generation,keys=keys,
                    metrics=metrics,mipPairs=mm,zeroEdges=zero,review=review,layouts=layouts,
                    transition=dict(source=trow,rotation=trot,baselineEdgeLuma=err),provenance=provenance)
        old.write_json(path/'metrics.json',result);data['banks'][str(colors)]={k:v for k,v in result.items() if k!='provenance'}
        print(slug,'C',colors,'seconds',round(generation,2),'zero',zero,'reject',review['bankReject'],flush=True)
    data['seconds']=time.perf_counter()-start
    old.write_json(folder/'metrics.json',data)
    return data

def main():
    p=argparse.ArgumentParser();p.add_argument('--case',choices=[x[2] for x in CASES]);args=p.parse_args()
    OUT.mkdir(exist_ok=True)
    baseline=dict(head=__import__('subprocess').check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),
                  agentsSha=sha(ROOT/'AGENTS.md'),numpy=np.__version__,pillow=Image.__version__,seed=SEED)
    old.write_json(OUT/'solver-checks.json',solver_checks())
    s=old.Snapshot(old.DEFAULT_SNAPSHOT);start=time.perf_counter()
    for name,cls,slug in CASES:
        if args.case and args.case!=slug:continue
        material(s,name,cls,slug)
    s.verify_end()
    baseline.update(inputHashes=s.hashes,elapsedSeconds=time.perf_counter()-start)
    old.write_json(OUT/('run-'+(args.case or 'all')+'.json'),baseline)
    print('COMPLETE',round(baseline['elapsedSeconds'],2),flush=True)

if __name__=='__main__':main()
