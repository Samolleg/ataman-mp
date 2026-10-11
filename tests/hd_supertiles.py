"""EI-HD-SUPERTILES: deterministic offline quilting, snapshot pixels only.

Writes only build-tests-hd-supertiles; no application, clipboard or network.
Full-size fields, fixed-scale camera views and predeclared Wang filters.
The 1/16 renderer inset requires overlapping 256 carriers at stride224.
"""
from __future__ import annotations
import argparse
import base64
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import time
sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
import hd_variants_proto as p
import hd_variants_wang as w

ROOT = Path(__file__).resolve().parents[1]
OUT = ROOT/'build-tests-hd-supertiles'
N, S, SEED = 224, 4, 20261011
LEVELS = (256,128,64,32,16,8,4)
CASES = w.CASES
THRESHOLDS = dict(meanDrift=8, contrastMin=.8, contrastMax=1.25,
                  straightLine=2.5, minC4Rmse64=5, seam=1.3,
                  period=.04, lag=.08, newPair=1.5)


def save(path, a):
    assert path.resolve().is_relative_to(OUT.resolve())
    p.pil(a).save(path, compress_level=1)


def write(path, obj):
    assert path.resolve().is_relative_to(OUT.resolve())
    p.write_json(path,obj)


def load(path):
    return json.loads(path.read_text(encoding='utf-8'))


def patch_quilt(pool, size, seed):
    """Raster quilting128/overlap32; local DP cuts, not global graph cut."""
    rng=np.random.Generator(np.random.PCG64(seed));patch=128;overlap=32
    positions=list(range(0,size-patch+1,patch-overlap))
    if positions[-1]!=size-patch:positions.append(size-patch)
    out=np.zeros((size,size,3),np.uint8);owners=np.full((size,size),-1,np.int16)
    order=rng.permutation(len(pool));trace=[]
    transforms=[p.d4(a) for a in pool]
    for yi,y in enumerate(positions):
        for xi,x in enumerate(positions):
            donor=int(order[len(trace)%len(pool)])
            ox=positions[xi-1]+patch-x if xi else 0
            oy=positions[yi-1]+patch-y if yi else 0
            existing=out[y:y+patch,x:x+patch].astype(np.float32)
            ring=np.zeros((patch,patch),bool);ring[:oy]=True;ring[:,:ox]=True
            best=None
            for _ in range(32):
                t=int(rng.integers(8));sy,sx=map(int,rng.integers(0,129,2))
                piece=transforms[donor][t][sy:sy+patch,sx:sx+patch]
                cost=np.mean((piece.astype(np.float32)-existing)**2,axis=2)
                error=float(cost[ring].mean()) if ring.any() else 0
                if best is None or error<best[0]:best=(error,piece,cost,t,sy,sx)
            error,piece,cost,t,sy,sx=best
            mask=np.ones((patch,patch),bool)
            if ox:mask[:,:ox] &= np.arange(ox)[None,:]>p.min_cut(cost[:,:ox])[:,None]
            if oy:mask[:oy] &= np.arange(oy)[:,None]>p.min_cut(cost[:oy].T)[None,:]
            out[y:y+patch,x:x+patch][mask]=piece[mask]
            owners[y:y+patch,x:x+patch][mask]=donor
            trace.append(dict(x=x,y=y,source=donor,d4=t,sourceX=sx,sourceY=sy,error=error))
    return out,owners,trace


def carrier_canvas(master, owners, anchor, k):
    """Use an unscaled central crop, then constrain ONLY the block perimeter."""
    length=k*N+32;start=(len(master)-length)//2
    out=master[start:start+length,start:start+length].copy()
    ownership=owners[start:start+length,start:start+length].copy()
    q=(np.arange(length)-16)%N+16;q[:16]=np.arange(16);q[-16:]=np.arange(240,256)
    target=anchor[q[:,None],q[None,:]]
    for turn in range(4):
        a=np.rot90(out,turn).copy();b=np.rot90(target,turn)
        own=np.rot90(ownership,turn).copy()
        cost=np.mean((a[:48].astype(np.float32)-b[:48])**2,axis=2)
        cut=p.min_cut(cost[24:48].T)+24
        mask=np.arange(48)[:,None]<=cut[None,:]
        a[:48][mask]=b[:48][mask];own[:48][mask]=-1
        out=np.rot90(a,-turn).copy();ownership=np.rot90(own,-turn).copy()
    return out,ownership


def slice_parts(canvas,k):
    return np.stack([canvas[y*N:y*N+256,x*N:x*N+256] for y in range(k) for x in range(k)])


def assemble(parts,k,visible=True):
    n=N if visible else parts.shape[1]
    out=np.empty((n*k,n*k,3),np.uint8)
    for y in range(k):
        for x in range(k):out[y*n:(y+1)*n,x*n:(x+1)*n]=p.visible(parts[y*k+x]) if visible else parts[y*k+x]
    return out


def outer_sides(x,y,k):
    return [side for side,yes in enumerate((y==0,x==k-1,y==k-1,x==0)) if yes]


def mip_parts(parts, anchor, k, size):
    """BOX, then exact stock footprint only on EXTERNAL sides. No whole-tile fallback."""
    result=np.stack([w.box(a,size) for a in parts]);ref=w.box(anchor,size)
    if size==256:return result
    m=max(4,round(size*7/8))
    width=int(np.ceil(size*(1/16+.5/m*7/8)-.5))+1
    for idx,a in enumerate(result):
        for side in outer_sides(idx%k,idx//k,k):
            r=np.rot90(a,side).copy();r[:width]=np.rot90(ref,side)[:width]
            a=np.rot90(r,-side).copy()
        result[idx]=a
    return result


def tone_bounds(pool):
    means=np.array([p.luma(p.visible(a)).mean() for a in pool])
    local=np.array([p.luma(a[y:y+64,x:x+64]).mean() for a in pool for y in (16,80,144) for x in (16,80,144)])
    lo,hi=map(float,np.quantile(local,[.1,.9]));center=float(means.mean())
    # Local source tones, not an invented palette; cap at Stage C +/-6%.
    delta=max(0,min(center*.06,(hi-lo)/2,center-lo,hi-center))
    return dict(sourceMeanMin=float(means.min()),sourceMeanMax=float(means.max()),
                sourceLocalP10=lo,sourceLocalP90=hi,center=center,delta=delta,
                strictWholeVariantRange=float(np.ptp(means)))


def tone_canvas(a, delta, sign):
    length=len(a);q=np.minimum(np.arange(length),np.arange(length)[::-1])
    distance=np.minimum(q[:,None],q[None,:])
    f=np.clip((distance-24)/64,0,1);f=f*f*(3-2*f)
    # Linked brightness/chroma, same single axis as Stage C; fade protects zero.
    rgb_delta=delta*np.array([1.2,1.0666666667,.8],np.float32)
    return np.clip(np.rint(a.astype(np.float32)+sign*f[:,:,None]*rgb_delta),0,255).astype(np.uint8)


def noise(x,y,seed=SEED):
    u,v=x/12,y/12;ix,iy=int(np.floor(u)),int(np.floor(v));u-=ix;v-=iy
    u=u*u*(3-2*u);v=v*v*(3-2*v)
    def value(dx,dy):return (w.h('tone',seed,ix+dx,iy+dy)%1000000)/999999
    return (1-v)*((1-u)*value(0,0)+u*value(1,0))+v*((1-u)*value(0,1)+u*value(1,1))


def cell_key(x,y,k,means,mode='super',seed=SEED):
    bx,by=x//k,y//k;px,py=x%k,y%k
    label=w.h('block',seed,bx,by)%S;rotation=w.h('rotate',seed,bx,by)%4;tone=1
    v=noise(bx*k+k/2,by*k+k/2,seed)
    if mode=='select':label=int(np.argsort(means)[min(S-1,int(v*S))])
    elif mode=='tone':tone=min(2,int(v*3))
    # np.rot90 of the entire KxK block: inverse coordinate transform.
    sx,sy=px,py
    for _ in range(rotation):sx,sy=k-1-sy,sx
    return int(label),int(tone),int(sy*k+sx),int(rotation)


def layout(n,k,means,mode,origin=(0,0),allowed=None,seed=SEED,order=None):
    """All K² cells must be eligible. Otherwise every cell of block is A.

    allowed is a GLOBAL predicate with cohort-wide halo, not local map bounds.
    """
    ox,oy=origin;coords=order or [(x,y) for y in range(oy,oy+n) for x in range(ox,ox+n)]
    result={}
    for x,y in coords:
        bx,by=x//k*k,y//k*k
        valid=allowed is None or all(allowed(bx+u,by+v) for v in range(k) for u in range(k))
        result[(x,y)]=cell_key(x,y,k,means,mode,seed) if valid else None
    return result


def cohort_layout(members,means,mode,allowed=None):
    """Read-only planner; mismatched manifests refuse before staging any member."""
    identity=('bankSha','anchorSha','seed','k','s','mipRevision','worldFrame')
    if len({tuple(m[key] for key in identity) for m in members})!=1:
        raise ValueError('incompatible cohort contract')
    return [layout(m['side'],m['k'],means,mode,m['origin'],allowed,m['seed']) for m in members]


def render(n,k,bank,means,mode,pool=None,origin=(0,0),allowed=None):
    field=np.empty((n*N,n*N,3),np.uint8);keys=layout(n,k,means,mode,origin,allowed)
    for (x,y),key in keys.items():
        if key is None:
            a=p.visible(pool[w.h('fallback',SEED,x,y)%len(pool)])
            a=np.rot90(a,w.h('fallback-rotation',SEED,x,y)%4)
        else:
            s,t,part,r=key;a=np.rot90(p.visible(bank[s,t,part]),r)
        xx,yy=x-origin[0],y-origin[1]
        field[yy*N:(yy+1)*N,xx*N:(xx+1)*N]=a
    return field,keys


def field_metrics(a,k=2):
    """Wang metric definitions at FULL native scale, bounded temporary memory."""
    y=p.luma(a);height,width=y.shape
    dxsum=np.zeros(width-1,np.float64);dysum=np.zeros(height-1,np.float64)
    for row in range(0,height,256):
        z=y[row:row+256];dxsum+=abs(np.diff(z,axis=1)).sum(axis=0,dtype=np.float64)
        end=min(height-1,row+256)
        dysum[row:end]=abs(y[row+1:end+1]-y[row:end]).sum(axis=1,dtype=np.float64)
    dxsum/=height;dysum/=width
    q=np.arange(width-1)%N;inside=(q>=8)&(q<N-9)
    interior=float((dxsum[inside].mean()+dysum[inside].mean())/2)
    edge=float((dxsum[N-1::N].mean()+dysum[N-1::N].mean())/2)
    hp=np.empty_like(y)
    for row in range(0,height,256):
        lo=max(0,row-16);hi=min(height,row+256+16)
        blurred=np.asarray(p.pil(a[lo:hi]).filter(ImageFilter.GaussianBlur(3)))
        end=min(height,row+256)
        hp[row:end]=y[row:end]-p.luma(blurred[row-lo:end-lo])
    del y
    def correlation(lag,axis):
        stats=np.zeros(6,np.float64)
        h=height-lag if axis==0 else height
        for row in range(0,h,128):
            x=hp[row:min(row+128,h)];z=hp[row+lag:min(row+128,h)+lag] if axis==0 else x[:,lag:]
            if axis==1:x=x[:,:-lag]
            xx=x.astype(np.float64);zz=z.astype(np.float64)
            stats+=np.array([xx.size,xx.sum(),zz.sum(),(xx*xx).sum(),(zz*zz).sum(),(xx*zz).sum()])
        count,sx,sy,sxx,syy,sxy=stats
        return float((sxy-sx*sy/count)/np.sqrt(max(1e-20,(sxx-sx*sx/count)*(syy-sy*sy/count))))
    lags={str(l):[correlation(l,1),correlation(l,0)] for l in sorted({N-8,N,N+8,2*N,k*N})}
    periods={}
    for period in (N,2*N,3*N):
        periods[str(period)]=[float(2*abs(np.sum(v*np.exp(-2j*np.pi*np.arange(len(v))/period)))/v.sum()) for v in (dxsum,dysum)]
    return dict(edgeLuma=edge,interiorLuma=interior,gridRatio=edge/interior,
                blockGridRatio=float((dxsum[k*N-1::k*N].mean()+dysum[k*N-1::k*N].mean())/2/interior),
                highpassStd=float(hp.std(dtype=np.float64)),highpassAutocorrelation=lags,
                gradientPeriodAmplitudeXY=periods[str(N)],periods=periods)


def diversity(parts):
    small=np.stack([w.box(a,64) for a in parts]).astype(np.float32)
    values=[]
    for i,a in enumerate(small):
        for b in small[:i]:values.append(min(float(np.sqrt(np.mean((a-np.rot90(b,r))**2))) for r in range(4)))
    return dict(uniqueC4=len({min(hashlib.sha256(np.rot90(a,r).tobytes()).hexdigest() for r in range(4)) for a in parts}),
                count=len(parts),minRmse64=min(values),medianRmse64=float(np.median(values)))


def mip_metrics(level,anchor,k,size):
    # level shape S,T,K²,n,n,3; all tones, all block rotations.
    samples=np.stack([w.sample_uv(a) for a in level.reshape(-1,size,size,3)]).reshape(S,3,k*k,-1,max(4,round(size*7/8)),3)
    lum=samples@np.array([.2126,.7152,.0722]);ref=w.sample_uv(w.box(anchor,size))
    interior=float((abs(np.diff(lum,axis=-1)).mean()+abs(np.diff(lum,axis=-2)).mean())/2)
    internal=[];zero=0;parts=level.reshape(-1,size,size,3)
    for s in range(S):
        for t in range(3):
            for idx in range(k*k):
                y,x=divmod(idx,k);a=samples[s,t,idx]
                for r in range(4):
                    aa=np.rot90(a,r);rr=np.rot90(ref,r)
                    for side in outer_sides(x,y,k):
                        side2=(side-r)%4
                        zero=max(zero,float(abs(np.rot90(aa,side2)[0]-np.rot90(rr,side2)[0]).max()))
                if x<k-1:internal.append(float(abs(lum[s,t,idx][:,-1]-lum[s,t,idx+1][:,0]).mean()))
                if y<k-1:internal.append(float(abs(lum[s,t,idx][-1]-lum[s,t,idx+k][0]).mean()))
    # Diversity by supertile (same part coordinates), and across all physical parts.
    base=level[:,1].astype(float)
    rms=[float(np.sqrt(np.mean((base[i]-base[j])**2))) for i in range(S) for j in range(i)]
    return dict(size=size,stockRelativeMaxRGB=zero,interior=interior,
                internalPairMean=float(np.mean(internal)),internalPairMax=max(internal),
                internalPairMaxRatio=max(internal)/max(interior,1e-6),internalPairCount=len(internal)*4,
                uniqueParts=len({a.tobytes() for a in parts}),physicalParts=len(parts),
                supertileMinRmse=min(rms),supertileMeanRmse=float(np.mean(rms)))


def tile_review(parts,anchor):
    ref=p.luma(p.visible(anchor));result=[]
    for i,a in enumerate(parts):
        z=p.luma(p.visible(a));dx=abs(np.diff(z,axis=1));dy=abs(np.diff(z,axis=0))
        line=max(float(dx.mean(axis=0).max()/dx.mean()),float(dy.mean(axis=1).max()/dy.mean()))
        mean=float(z.mean());ratio=float(z.std()/ref.std());reasons=[]
        if abs(mean-float(ref.mean()))>8:reasons.append('mean drift >8')
        if not .8<=ratio<=1.25:reasons.append('contrast outside 0.8..1.25')
        if line>2.5:reasons.append('straight line >2.5')
        result.append(dict(index=i,lumaMean=mean,contrastRatio=ratio,lineRatio=line,reject=reasons))
    return result


def review(fields,tiles,diversity_result,mips,k):
    reasons=[]
    for n,methods in fields.items():
        for method in ('super','tone','select'):
            if method not in methods:continue
            m=methods[method];prefix=f'{n}/{method}: '
            if m['gridRatio']>1.3:reasons.append(prefix+'seam >1.3')
            for lag in (N,k*N):
                if max(m['periods'][str(lag)])>.04:reasons.append(prefix+f'period {lag//N} cells >0.04')
                if max(m['highpassAutocorrelation'][str(lag)])>.08:reasons.append(prefix+f'lag {lag//N} cells >0.08')
    if any(t['reject'] for t in tiles):reasons.append('required physical parts rejected')
    if diversity_result['minRmse64']<5:reasons.append('min C4 RMSE64 <5')
    if any(m['internalPairMaxRatio']>1.5 for m in mips if m['size']>=16):reasons.append('internal pair >1.5 at mip256..16')
    return dict(accepted=not reasons,reject=reasons,thresholds=THRESHOLDS)


def panel(path,images,labels):
    hgt,width=images[0].shape[:2]
    out=Image.new('RGB',(len(images)*(width+8)+8,hgt+40),'#22262b');d=ImageDraw.Draw(out)
    for i,(a,label) in enumerate(zip(images,labels)):
        out.paste(p.pil(a),(8+i*(width+8),32));d.text((8+i*(width+8),8),label,fill='white')
    out.save(path,compress_level=1)


def shrink(a,factor):
    return np.asarray(p.pil(a).resize((a.shape[1]//factor,a.shape[0]//factor),Image.Resampling.BOX))


def get_pool(snapshot,name,cls):
    pool=[];sources=[];seen=set()
    for pack in ('jamaisvu','dejavu'):
        arrays,rows=snapshot.pool(name,cls,pack)
        for a,row in zip(arrays,rows):
            code=p.canonical(a)
            if code not in seen:seen.add(code);pool.append(a);sources.append(dict(bank=pack,**row))
    assert len({r['physicalClass'] for r in sources})==1
    return pool,sources


def get_transition(snapshot,name,cls,source,anchor):
    error,a,row,rotation=w.stock_transition(snapshot,name,cls,source,anchor,straight=True)
    presets=load(ROOT/'assets/resources/terrain_atlas_presets.json')['presets']
    preset=next(z for z in presets if z['namespace'].lower()==row['namespace'])
    sigs=np.frombuffer(base64.b64decode(preset['signaturesBase64']),np.uint8).reshape(-1,4)
    family=int(np.roll(sigs[row['slot']],-rotation)[1])
    rows=[r for r in snapshot.rows['jamaisvu'] if r['namespace']==row['namespace'] and r['kind']=='uniform' and r['animated'] is False and r['opaque'] and all(sigs[r['slot']]==family)]
    r=min(rows,key=lambda r:(r['size']!=256,r['slot']));other=p.rgb(snapshot.row_tile(r))
    if len(other)!=256:other=p.resize(other,256)
    return a,other,dict(transition=row,rotation=rotation,other=r,stockSeam=error)


def boundary_demo(folder,bank,means,common,anchor,transition,other,k):
    # Valid straight stock transition at x7 cuts both K2 and K3 blocks.
    # Jagged masks/holes are planner tests: corner transitions are not invented.
    n=12;edges=[7]*n
    allowed=lambda x,y:0<=y<n and 0<=x<edges[y]
    images=[]
    baseline,_=render(n,k,bank,means,'super',pool=[anchor],allowed=lambda x,y:False)
    after,keys=render(n,k,bank,means,'tone',pool=common,allowed=allowed)
    for a in (baseline,after):
        for y,x in enumerate(edges):
            a[y*N:(y+1)*N,x*N:(x+1)*N]=p.visible(transition)
            for xx in range(x+1,n):a[y*N:(y+1)*N,xx*N:(xx+1)*N]=p.visible(other)
        images.append(a)
    # Pixel identity of transition + other material.
    assert all(np.array_equal(images[0][y*N:(y+1)*N,x*N:],images[1][y*N:(y+1)*N,x*N:]) for y,x in enumerate(edges))
    panel(folder/'boundary-region.png',[shrink(a,2) for a in images],['STOCK / actual transition / other','SUPERTILES + A fallback / same transition'])
    strip=np.tile(p.visible(transition),(k,1,1));outside=np.tile(p.visible(other),(k,1,1))
    reference=np.concatenate((np.tile(p.visible(anchor),(k,k,1)),strip,outside),axis=1)
    candidate=np.concatenate((assemble(bank[0,1],k),strip,outside),axis=1)
    assert np.array_equal(reference[:,k*N-8:],candidate[:,k*N-8:])
    panel(folder/'boundary-proof.png',[reference,candidate],['EXACT STOCK ANCHOR / transition / other','SUPERTILE frame0 / same transition / other'])
    # Deliberately unsafe one-cell replacement, then legal whole-block dissolution.
    before,_=render(6,k,bank,means,'super');unsafe=before.copy();fixed=before.copy()
    xx=yy=k+1
    unsafe[yy*N:(yy+1)*N,xx*N:(xx+1)*N]=p.visible(common[0])
    bx,by=xx//k*k,yy//k*k
    for y in range(by,by+k):
        for x in range(bx,bx+k):fixed[y*N:(y+1)*N,x*N:(x+1)*N]=p.visible(common[w.h(x,y)%len(common)])
    panel(folder/'repaint.png',[shrink(a,2) for a in (before,unsafe,fixed)],['BLOCK original','UNSAFE single internal cell -> A','SAFE dissolve entire block -> A'])
    return dict(incompleteCells=sum(v is None for v in keys.values()),transitionPixelsUnchanged=True,
                repaintChangedBlockCells=k*k,unsafeSingleCellMeasured=True,
                repaintMetrics={label:field_metrics(a,k) for label,a in zip(('before','unsafe','dissolved'),(before,unsafe,fixed))})


def material(snapshot,name,cls,slug):
    started=time.perf_counter();folder=OUT/slug;folder.mkdir(exist_ok=True)
    pool,sources=get_pool(snapshot,name,cls);anchor=pool[0];seed=SEED+w.h(slug)%100000
    previous=ROOT/'build-tests-hd-variants-proto'/slug
    if previous.exists():
        common=[np.asarray(Image.open(previous/f'variant-{i:02}.png').convert('RGB')) for i in range(1,7)]
    else:
        common=list(pool)
        while len(common)<len(pool)+2:common.append(p.quilt(pool,seed+len(common),len(common)%len(pool))[0])
    for i,a in enumerate(pool):save(folder/f'donor-{i:02}.png',a)
    for i,a in enumerate(common):save(folder/f'A-{i:02}.png',a)
    transition,other,boundary_source=get_transition(snapshot,name,cls,sources[0],anchor)
    save(folder/'stock-transition.png',transition);save(folder/'other-material.png',other)
    bounds=tone_bounds(pool);result=dict(name=name,reviewedClass=cls,sources=sources,sourceCount=len(pool),toneBounds=bounds,boundarySource=boundary_source,banks={})
    # Baselines are shared by K2 and K3, same PCG143 as Wang.
    baseline={};camera={};crops={}
    for n in (24,48):
        baseline[str(n)]={};camera[n]={};crops[n]={}
        for label,tiles in (('now',pool),('A',common)):
            a,_,_=p.paint(tiles,seed=143,side=n)
            save(folder/f'field-{n}-{label}.png',a)
            baseline[str(n)][label]=field_metrics(a,3)
            camera[n][label]={f:shrink(a,f) for f in (4,8)};crops[n][label]=a[:6*N,:6*N].copy()
            del a
            print(slug,n,label,'baseline complete',flush=True)
    for k in (2,3):
        path=folder/f'k{k}';path.mkdir(exist_ok=True);bank=[];traces=[];canvases=[]
        ts=time.perf_counter()
        for s in range(S):
            master,owner,trace=patch_quilt(pool,k*256,seed+k*100+s)
            canvas,owner=carrier_canvas(master,owner,anchor,k)
            counts=[int((owner==i).sum()) for i in range(len(pool))]
            assert min(counts)>0,'Every source must survive the final crop and perimeter'
            save(path/f'super-{s}-source-{k*256}.png',master)
            tones=[tone_canvas(canvas,bounds['delta'],sign) for sign in (-1,0,1)]
            parts=np.stack([slice_parts(a,k) for a in tones]);bank.append(parts);canvases.append(canvas)
            save(path/f'super-{s}-continuous.png',canvas[16:-16,16:-16])
            save(path/f'super-{s}-carriers-{k*256}.png',assemble(parts[1],k,False))
            for t in range(3):
                for idx,a in enumerate(parts[t]):save(path/f'part-s{s}-t{t}-{idx:02}.png',a)
            traces.append(dict(seed=seed+k*100+s,retainedDonorPixels=counts,patches=trace))
        generation=time.perf_counter()-ts;bank=np.stack(bank)
        means=[float(p.luma(a[16:-16,16:-16]).mean()) for a in canvases]
        p.sheet(path/'supertiles.png',[(f'{name} K{k} S4 neutral / full coherent blocks',[(a[16:-16,16:-16],f'S{i}, mean={means[i]:.2f}') for i,a in enumerate(canvases)]),
            ('tone copies: dark / neutral / light (S0)',[(assemble(bank[0,t],k),f'tone {t-1:+}') for t in range(3)])],width=k*N)
        mips=[];mip_images=[]
        for size in LEVELS:
            level=np.stack([np.stack([mip_parts(parts,anchor,k,size) for parts in tones]) for tones in bank])
            np.savez_compressed(path/f'mip-{size}.npz',pixels=level)
            mm=mip_metrics(level,anchor,k,size);mips.append(mm)
            assert mm['stockRelativeMaxRGB']==0,(slug,k,size,mm['stockRelativeMaxRGB'])
            row=[]
            for s in range(S):
                samples=[w.sample_uv(a).clip(0,255).astype(np.uint8) for a in level[s,1]]
                n0=len(samples[0]);im=np.empty((k*n0,k*n0,3),np.uint8)
                for idx,a in enumerate(samples):y,x=divmod(idx,k);im[y*n0:(y+1)*n0,x*n0:(x+1)*n0]=a
                row.append((im,f'S{s}; {mm["uniqueParts"]} unique parts'))
            mip_images.append((f'mip{size}: external0 lock; internal parts remain distinct',row))
        p.sheet(path/'mips.png',mip_images,width=256)
        fields={};used={}
        for n in (24,48):
            fields[str(n)]={key:dict(value) for key,value in baseline[str(n)].items()}
            # K2 baseline has all needed lag2 fields; add requested period2 without re-rendering later.
            views={f:[camera[n]['now'][f],camera[n]['A'][f]] for f in (4,8)}
            close=[crops[n]['now'],crops[n]['A']]
            for mode in ('super','tone','select'):
                a,keys=render(n,k,bank,means,mode)
                save(path/f'field-{n}-{mode}.png',a)
                fields[str(n)][mode]=field_metrics(a,k)
                used[f'{n}/{mode}']=len({(s,t,part) for s,t,part,r in keys.values()})
                if mode!='select':
                    for f in (4,8):views[f].append(shrink(a,f))
                    close.append(a[:6*N,:6*N].copy())
                else:
                    panel(path/f'tone-comparison-{n}.png',[views[8][2],shrink(a,8),views[8][3]],['RANDOM S4: no tone','SELECT EXISTING S4: no extra slots','THREE TONE COPIES: extra slots'])
                del a
                print(slug,'K',k,n,mode,'seam',round(fields[str(n)][mode]['gridRatio'],3),flush=True)
            for f in (4,8):panel(path/f'compare-{n}-x{f}.png',views[f],[f'NOW {n}x{n} /{f}',f'A {n}x{n} /{f}',f'SUPERTILES K{k} S4 /{f}',f'SUPERTILES + TONE /{f}'])
            if n==24:panel(path/'closeup.png',close,['NOW native 6x6','A native 6x6',f'K{k} S4 native 6x6','K + TONE native 6x6'])
        neutral_div=diversity(bank[:,1].reshape(-1,256,256,3))
        div=diversity(bank.reshape(-1,256,256,3));tiles=tile_review(bank.reshape(-1,256,256,3),anchor)
        boundary=boundary_demo(path,bank,means,common,anchor,transition,other,k)
        data=dict(k=k,s=S,generationSeconds=generation,means=means,fields=fields,usedPhysicalParts=used,mips=mips,
                  diversity=div,neutralDiversity=neutral_div,tiles=tiles,review=review(fields,tiles,div,mips,k),boundary=boundary,provenance=traces)
        write(path/'metrics.json',data);result['banks'][str(k)]={key:value for key,value in data.items() if key not in ('tiles','provenance')}
        print('BANK',slug,k,'REJECT',data['review']['reject'],flush=True)
    result['seconds']=time.perf_counter()-started;write(folder/'metrics.json',result)
    return result


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--case',choices=[x[2] for x in CASES]);args=parser.parse_args()
    OUT.mkdir(exist_ok=True)
    baseline=dict(head=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),agentsSha=w.sha(ROOT/'AGENTS.md'),
                  numpy=np.__version__,pillow=Image.__version__,seed=SEED,thresholds=THRESHOLDS)
    snap=p.Snapshot(p.DEFAULT_SNAPSHOT);start=time.perf_counter()
    for name,cls,slug in CASES:
        if args.case and slug!=args.case:continue
        material(snap,name,cls,slug)
    snap.verify_end();baseline.update(inputHashes=snap.hashes,seconds=time.perf_counter()-start)
    write(OUT/f'run-{args.case or "all"}.json',baseline)
    print('GENERATION COMPLETE',baseline['seconds'],flush=True)


if __name__=='__main__':main()
