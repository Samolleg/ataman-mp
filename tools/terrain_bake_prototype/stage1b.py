"""Private offline stock-ownership edge brush experiment; no mapper/live writes.

Usage: python -B stage1b.py <research-0> <stage-1> <stage-1b>
Only the last directory is written. Baked RGB yields estimated ownership,
not the original artist's alpha. No extra dependencies beyond numpy/Pillow.
"""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import shutil
import sys

sys.dont_write_bytecode = True
import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from research import read, write, uniform_sources
from stage1 import Renderer, signature, digest, seed, smooth, slot_counts, hash_noise

ROOT_NAME = "build-tests-terrain-bake-stage-1b"
VARIANTS = ("stock-edge", "stock-tongues", "mixed-edge")


def nearest(a, x, y):
    ix = np.clip(np.rint(x).astype(int), 0, a.shape[1]-1)
    iy = np.clip(np.rint(y).astype(int), 0, a.shape[0]-1)
    return a[iy, ix]


def components(mask):
    """4-connected component sizes, for diagnostics of isolated stock flecks."""
    seen = np.zeros(mask.shape, bool)
    sizes = []
    for yy, xx in zip(*np.where(mask)):
        if seen[yy, xx]:
            continue
        todo = [(yy, xx)]; seen[yy, xx] = True; size = 0
        while todo:
            y, x = todo.pop(); size += 1
            for ny, nx in ((y-1,x),(y+1,x),(y,x-1),(y,x+1)):
                if 0 <= ny < mask.shape[0] and 0 <= nx < mask.shape[1]:
                    if mask[ny,nx] and not seen[ny,nx]:
                        seen[ny,nx] = True; todo.append((ny,nx))
        sizes.append(size)
    return sizes


def extract_brushes(root, atlas, materials):
    """Fit every two-material topology and unroll its coarse curved boundary.

    The *whole* unsmoothed ownership strip is transported. No monotonic mean
    profile, no per-row centering on the fine ragged contour, no residual gain.
    Straight 2/2, corner 1/3 and alternating diagonal 2/2 all participate.
    Physical image orientation is selected by measured 16px corner ownership.
    """
    sources = uniform_sources(atlas); cache = {}; records = []; by_pair = collections.defaultdict(list)

    def tex(slot):
        if slot not in cache:
            cache[slot] = np.asarray(Image.open(root/"input"/"atlas"/f"{slot}.png").convert("RGB"), np.float32)
        return cache[slot]

    for slot in atlas["catalog"]["slots"]:
        s = signature(slot); pair = tuple(sorted(set(s)))
        if not slot.get("assigned") or len(pair) != 2 or not set(pair) <= set(materials):
            continue
        if not all(mat in sources for mat in pair):
            continue
        a, b = pair; image = tex(slot["slot"]); best = None
        for sa in sources[a]:
            for sb in sources[b]:
                ta, tb = tex(sa), tex(sb); delta = tb-ta
                alpha = np.clip(np.sum((image-ta)*delta, axis=2)/np.maximum(np.sum(delta*delta,axis=2),100),0,1)
                rmse = float(np.sqrt(np.mean((image-(ta+alpha[...,None]*delta))**2)))
                if best is None or rmse < best[0]:
                    best = rmse, alpha, sa, sb
        rmse, alpha, sa, sb = best
        # Infer the physical corner convention using observed pixels; semantic
        # IDs stay fixed. No fabricated material pair is allowed.
        vals = np.asarray([alpha[:16,:16].mean(),alpha[:16,-16:].mean(),
                           alpha[-16:,-16:].mean(),alpha[-16:,:16].mean()])
        rotations = [s[k:]+s[:k] for k in range(4)]
        orientations = rotations + [tuple(reversed(v)) for v in rotations]
        oriented = min(orientations,key=lambda v:np.mean((vals-np.asarray([m==b for m in v]))**2))
        corner_error = float(np.sqrt(np.mean((vals-np.asarray([m==b for m in oriented]))**2)))
        # Rotate both ownership and ideal geometry so the contour has a useful
        # x root. This also yields the second branch of alternating 2/2 tiles.
        candidates = []
        for turn in range(4):
            aa = np.rot90(alpha,turn)
            cv = np.rot90(np.array([[oriented[0]==b,oriented[1]==b],
                                   [oriented[3]==b,oriented[2]==b]],float),turn)
            c0,c1,c3,c2 = cv.flat
            rows = np.arange(8,120,dtype=float); y = rows/127
            left = c0*(1-y)+c3*y; right = c1*(1-y)+c2*y
            slope = 2*(right-left); intercept = 2*left-1
            cx = -intercept/np.where(np.abs(slope)>.001,slope,1)
            good = (np.abs(slope)>.35)&(cx>.06)&(cx<.94)
            if np.count_nonzero(good)<28:
                continue
            rows,y,cx,slope = (v[good] for v in (rows,y,cx,slope))
            # Native transition geometry need not follow bilinear catalog
            # corners. Remove only its coarse curve: locate the low-pass
            # ownership crossing near the ideal branch, then smooth that
            # guide. The raw pixels still supply tongues, flecks and width.
            coarse=np.asarray(Image.fromarray(np.uint8(aa*255)).filter(ImageFilter.GaussianBlur(6)),float)/255
            centers=[]
            for row,ideal in zip(rows,cx*127):
                lo=max(4,int(ideal)-30);hi=min(124,int(ideal)+31)
                centers.append(lo+int(np.argmin(np.abs(coarse[int(row),lo:hi]-.5))))
            centers=np.asarray(Image.fromarray(np.uint8(centers)[None,:]).filter(ImageFilter.GaussianBlur(8)),float)[0]
            cx=centers/127
            dxdy=np.gradient(centers,rows)
            sign=np.sign(slope);norm=np.sqrt(1+dxdy**2)
            nx,ny=sign/norm,-sign*dxdy/norm
            # Arc-length resampling avoids stretching diagonal/corner flecks.
            arc = np.r_[0,np.cumsum(np.hypot(np.diff(cx*127),np.diff(rows)))]
            length = int(arc[-1])+1; along = np.linspace(0,arc[-1],length)
            px,py = np.interp(along,arc,cx*127),np.interp(along,arc,rows)
            nnx,nny = np.interp(along,arc,nx),np.interp(along,arc,ny)
            normal = np.arange(-32,33,dtype=float)
            strip = nearest(aa,px[:,None]+nnx[:,None]*normal,
                            py[:,None]+nny[:,None]*normal)
            # Keep the branch with clean pure sides and actual source flecks.
            hard = strip>=.5
            purity = float((hard[:,:4].mean()+1-hard[:,-4:].mean())/2)
            far = float(np.mean(hard[:,8:28])+np.mean(~hard[:,37:57]))
            candidates.append((purity, -far, strip, turn))
        if not candidates:
            continue
        purity, _, strip, turn = min(candidates,key=lambda v:v[:2])
        unique, hist = np.unique(np.asarray(Image.open(root/"input"/"atlas"/f"{slot['slot']}.png").convert("RGBA"))[...,3],return_counts=True)
        topology = "1/3" if sorted(collections.Counter(s).values())==[1,3] else "2/2 diagonal" if s[0]==s[2] else "2/2 straight"
        hard = strip>=.5
        islands_b = [n for n in components(hard) if n<128]
        islands_a = [n for n in components(~hard) if n<128]
        # Width measured as per-row 10/90 *occupancy* extent, not blurred alpha.
        profile = hard.mean(axis=0)
        width = int(np.count_nonzero((profile>.1)&(profile<.9)))
        rec = {"slot":slot["slot"],"pair":list(pair),"topology":topology,
               "uniformFitSlots":[sa,sb],"rgbFitRmse":rmse,
               "cornerOwnershipRmse":corner_error,"physicalOrientedCorners":list(oriented),
               "samplingQuarterTurn":turn,"stripLengthPx":len(strip),
               "outerSideContamination":purity,"occupancyWidth10to90Px":width,
               "smallComponentsA":len(islands_a),"smallComponentsB":len(islands_b),
               "smallComponentAreaA":sum(islands_a),"smallComponentAreaB":sum(islands_b),
               "estimatedIntermediateFraction":float(np.mean((strip>.1)&(strip<.9))),
               "actualAlphaHistogram":dict(zip(map(str,unique.tolist()),hist.tolist()))}
        rec["eligible"] = corner_error<.3 and purity<.3 and rmse<14
        # Coarse/native artwork has residual texture even in pure-side RGB
        # fits. Confine that uncertain ownership to the edge band; force the
        # tails to their known semantic side with a 12px score taper. This is
        # a geometric prior, not recovered source authoring alpha.
        normal=np.arange(-32,33,dtype=float)
        taper=smooth((32-np.abs(normal))/12)
        strip=strip*taper[None,:]+(normal>=0)[None,:]*(1-taper[None,:])
        records.append(rec)
        if rec["eligible"]:
            by_pair[pair].append((rec,strip))
    if not by_pair:
        raise RuntimeError("No trusted ownership brushes")
    banks = {}; bank_records = {}
    # One periodic strip bank per *pair*, never a material-agnostic mean edge.
    # 64px fragments are selected deterministically and concatenated without
    # alpha interpolation. Fragment joins may still be visible after warping.
    for pair, pool in by_pair.items():
        # Prefer the native ragged edge among reasonably fitted sources;
        # otherwise a sub-RGB-unit clean HD edge dominates the art choice.
        pool.sort(key=lambda v:v[0]["rgbFitRmse"]-min(8,(v[0]["smallComponentsA"]+v[0]["smallComponentsB"])*.08))
        selected = pool[:6]
        # Include each topology where available, provided fit is acceptable.
        for topology in ("1/3","2/2 straight","2/2 diagonal"):
            match = next((p for p in pool if p[0]["topology"]==topology),None)
            if match is not None and all(p[0]["slot"]!=match[0]["slot"] for p in selected):
                selected.append(match)
        pieces = []
        selection = []
        for chunk in range(8):
            idx = seed(chunk,pair[0],pair[1])%len(selected)
            rec,strip = selected[idx]
            start = seed(chunk,pair[1],pair[0])%len(strip)
            pieces.append(strip[(start+np.arange(64))%len(strip)])
            selection.append({"slot":rec["slot"],"start":start})
        bank = np.concatenate(pieces)
        # Force pure ends. These are beyond the transferred transition band.
        bank[:,:2]=0;bank[:,-2:]=1
        banks[pair]=bank
        bank_records[str(pair)]={"selectedSlots":[r["slot"] for r,_ in selected],
                                 "fragmentSelection":selection,"shape":list(bank.shape)}
        Image.fromarray(np.uint8((bank>=.5)*255)).save(root/f"brush-{pair[0]}-{pair[1]}.png")
    write(root/"stock-brush-measurements.json",{"method":"unblurred RGB ownership estimate; coarse source contour arc length; nearest pixel sampling",
          "tested":len(records),"eligible":sum(r["eligible"] for r in records),
          "pureSidePrior":"12px score taper at +/-32px strip tails; pre-prior contamination reported",
          "records":records,"banks":bank_records})
    # Show actual sources, fitted ownership and transported raw/binary strips.
    witnesses=[]
    for pair,pool in sorted(by_pair.items()):
        best=min(pool,key=lambda v:v[0]["rgbFitRmse"])
        witnesses.append(best)
    sheet=Image.new("RGB",(1400,len(witnesses)*280),(24,24,24));d=ImageDraw.Draw(sheet)
    for row,(rec,strip) in enumerate(witnesses):
        yy=row*280;d.text((8,yy+6),f"pair {rec['pair']} | slot {rec['slot']} | RGB fit {rec['rgbFitRmse']:.2f} | occupancy width {rec['occupancyWidth10to90Px']} px",fill="white")
        im=Image.open(root/"input"/"atlas"/f"{rec['slot']}.png").convert("RGB")
        sheet.paste(im.resize((256,256),Image.Resampling.NEAREST),(0,yy+24))
        for xx,array in ((270,strip),(550,(strip>=.5).astype(float))):
            sheet.paste(Image.fromarray(np.uint8(array*255)).resize((260,256),Image.Resampling.NEAREST),(xx,yy+24))
        bank=banks[tuple(rec["pair"])]
        sheet.paste(Image.fromarray(np.uint8((bank>=.5)*255)).transpose(Image.Transpose.TRANSPOSE).resize((560,256),Image.Resampling.NEAREST),(830,yy+24))
    sheet.save(root/"stock-brush-analysis.png")
    return banks,records,bank_records


class BrushRenderer(Renderer):
    def __init__(self,root,m,atlas,crop,banks):
        # Reuse only texture and bilinear helpers; stock profile is NOT used.
        self.root,self.m,self.atlas,self.crop=root,m,atlas,crop
        self.materials=crop["materials"];self.sources=uniform_sources(atlas)
        self.textures={};self.banks=banks;self.fallbacks=collections.Counter()
        for mat in self.materials:
            self.textures[mat]=[]
            for slot in self.sources[mat]:
                im=Image.open(root/"input"/"atlas"/f"{slot}.png").convert("RGB");p=im.width//16
                self.textures[mat].append(np.asarray(im.crop((p,p,im.width-p,im.height-p)),np.float32))

    def scores(self,s,x,y,variant):
        raw=self.raw(s,x,y);fade=16*x*(1-x)*y*(1-y)
        for i,mat in enumerate(self.materials):
            # Smooth shape perturbation only. Fine detail is entirely stock.
            raw[...,i]+=.045*hash_noise(x*2,y*2,(mat&0xffff)+variant*71)*fade*4*raw[...,i]*(1-raw[...,i])
        return raw

    def ownership(self,s,variant,x,y,mode):
        scores=self.scores(s,x,y,variant)
        active=np.argsort(scores,axis=-1)[...,-2:]
        winner=active[...,1];runner=active[...,0]
        ownership=np.eye(len(self.materials),dtype=np.float32)[winner]
        fade=16*x*(1-x)*y*(1-y)
        eps=1e-4
        # Numerical gradient of the shared smooth score field. The edge
        # derivative is canonical: discard the off-edge component on each
        # boundary, so neighbouring signatures use the same warp/normal.
        gx=(self.scores(s,x+eps,y,variant)-self.scores(s,x-eps,y,variant))/(2*eps)
        gy=(self.scores(s,x,y+eps,variant)-self.scores(s,x,y-eps,variant))/(2*eps)
        ex=smooth(np.minimum(x,1-x)*8);ey=smooth(np.minimum(y,1-y)*8)
        gx*=ex[...,None];gy*=ey[...,None]
        for ai,a in enumerate(self.materials):
            for bi in range(ai+1,len(self.materials)):
                b=self.materials[bi]
                chosen=((winner==ai)&(runner==bi))|((winner==bi)&(runner==ai))
                if not chosen.any() or a not in s or b not in s:
                    continue
                pair=(a,b);bank=self.banks.get(pair)
                if bank is None:
                    # Explicit measured limitation; use no unrelated pair.
                    self.fallbacks[str(pair)]+=int(chosen.sum());continue
                diff=scores[...,bi]-scores[...,ai]
                nx=gx[...,bi]-gx[...,ai];ny=gy[...,bi]-gy[...,ai]
                norm=np.maximum(np.hypot(nx,ny),.15)
                nx/=norm;ny/=norm
                # First-order metric distance, not an exact Euclidean SDF.
                distance=diff/norm*127
                # Canonical shared phase at every edge; only the interior
                # arc coordinate/label may change. No extra edge-state key.
                # A fixed per-signature tangent avoids the swirling marks
                # caused by differentiating dot(position, changing normal).
                # It approximates arc phase; it is not exact curve arc length.
                q=np.asarray([int(v==b)-int(v==a) for v in s],float)
                cx=.5*((q[1]-q[0])+(q[2]-q[3]))
                cy=.5*((q[3]-q[0])+(q[2]-q[1]))
                cn=float(np.hypot(cx,cy))
                if cn<1e-6:cx,cy,cn=1.,1.,float(np.sqrt(2))
                along=(((-cy*x+cx*y)/cn)*128+variant*47)*fade+(seed(a,b)%512)
                if mode=="stock-tongues":
                    # 1.45x normal extent keeps the source coarse shapes;
                    # an additional measured low-frequency contour creates
                    # longer tongues without inventing a pixel noise layer.
                    contour=np.mean(bank>=.5,axis=1)*65-32.5
                    t=np.floor(np.mod(along/2,512)).astype(int)
                    distance=(distance+contour[t]*.5*fade)/1.45
                elif mode=="mixed-edge":
                    distance/=1.15
                ti=np.floor(np.mod(along,512)).astype(int)
                di=np.clip(np.rint(distance).astype(int)+32,0,64)
                alpha=bank[ti,di]
                if mode!="mixed-edge":
                    alpha=(alpha>=.5).astype(float)
                else:
                    # Quantized RGB estimate keeps partially owned source
                    # pixels. It does not assert original alpha recovery.
                    alpha=np.rint(alpha*4)/4
                # Do not leak secondary material into authored pure corners.
                pure_a=(scores[...,bi]<=0);pure_b=(scores[...,ai]<=0)
                alpha=np.where(pure_a,0,np.where(pure_b,1,alpha))
                ownership[chosen]=0
                ownership[...,ai][chosen]=1-alpha[chosen]
                ownership[...,bi][chosen]=alpha[chosen]
        return ownership

    def recipe(self,s,variant,x,y,mode):
        blend=self.ownership(s,variant,x,y,mode)
        fade=16*x*(1-x)*y*(1-y);rgb=np.zeros((*x.shape,3),np.float32)
        # Identical texture selection/color modulation to stage1 N4 baseline.
        for i,mat in enumerate(self.materials):
            base=self.sample(mat,x,y)
            alt=self.sample(mat,x+.27,y+.43,variant,variant%4)
            mix=fade*.55*smooth((hash_noise(x*2,y*2,(mat&0xffff)+variant*33)+1)/2)
            rgb+=(base*(1-mix[...,None])+alt*mix[...,None])*blend[...,i,None]
        rgb*=1+.07*hash_noise(x*2,y*2,variant+211)[...,None]*fade[...,None]
        return rgb


def groups_and_labels(m,crop):
    groups=collections.defaultdict(list)
    for y in range(32):
        for x in range(32):
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            s=tuple(m["landSignatures"][k]);cl=m["runtimeTypes"][m["land"][k]&0x3fff]
            groups[s,cl].append((x,y))
    for (s,cl),cells in groups.items():
        cells.sort(key=lambda xy:seed(xy[0]+crop["x"],xy[1]+crop["y"],sum(s)))
    return groups


def contact(root,images,counts,filename,box=None,subtitle="Zone6 (88,136) 32x32 | N4"):
    order=("current","stage1-B-N4",*VARIANTS)
    # All five panels have exactly the same size/scale. Sixth is explanatory.
    sheet=Image.new("RGB",(2040,1432),(24,24,24));d=ImageDraw.Draw(sheet)
    for i,label in enumerate(order):
        xx=(i%3)*680;yy=(i//3)*716
        d.text((xx+8,yy+6),f"{label} | {counts[label]['typedUnique']} typed tiles",fill="white")
        d.text((xx+8,yy+21),subtitle,fill=(205,205,205))
        im=images[label] if box is None else images[label].crop(box)
        sheet.paste(im.resize((680,680),Image.Resampling.LANCZOS if box is None else Image.Resampling.NEAREST),(xx,yy+36))
    xx,yy=1368,740
    for line in ("current: original oriented stock tiles",
                 "stage1-B-N4: stage1 B brush, finite N4",
                 "stock-edge: binary full stock ownership strip",
                 "stock-tongues: stock strip + coarse shapes",
                 "mixed-edge: stock estimate, 5 ownership levels",
                 "Recipe keys/labels/textures unchanged: N4",
                 "No game/mapper run; unlit RGB, no DXT/mips",
                 "Original authoring alpha is not recoverable"):
        d.text((xx,yy),line,fill="white");yy+=24
    sheet.save(root/filename)


def run(old,stage1,root):
    if root.name!=ROOT_NAME or root.parent!=Path.cwd().resolve():
        raise RuntimeError("Output must be owned private root under this checkout")
    root.mkdir(exist_ok=True);tiles=root/"input"/"atlas";tiles.mkdir(parents=True,exist_ok=True)
    crop=read(old/"crop.json");m=read(stage1/"input"/"map.json");atlas=read(stage1/"input"/"atlas.json")
    assert atlas["semanticExact"] and atlas["catalog"]["approved"] and crop["recoveredChangedCells"]==0
    assert digest(Path(m["path"]))==m["sha256"],"Source map drift"
    snapshots={str(p):digest(p) for p in (old/"crop.json",stage1/"input"/"map.json",stage1/"input"/"atlas.json",stage1/"recipes-N4.png",stage1/"current.png")}
    for p in (stage1/"input"/"atlas").glob("*.png"):
        shutil.copyfile(p,tiles/p.name)
    write(root/"input"/"map.json",m);write(root/"input"/"atlas.json",atlas);write(root/"crop.json",crop)
    # Verify every source slot's decoded RGBA fingerprint against the exact
    # atlas snapshot, before using any pixels. Catalog hash binds semantics.
    h=hashlib.sha256()
    h.update(f"{atlas['catalog']['textureCount']*64}\n".encode("ascii"))
    for i in range(atlas["catalog"]["textureCount"]*64):
        im=Image.open(tiles/f"{i}.png").convert("RGBA")
        h.update(f"{im.width}x{im.height}\n".encode("ascii"))
        h.update(im.tobytes())
    assert h.hexdigest()==atlas["pixelSha256"],"Stock decoded pixels drift"
    banks,records,bank_records=extract_brushes(root,atlas,crop["materials"])
    r=BrushRenderer(root,m,atlas,crop,banks);groups=groups_and_labels(m,crop)
    images={"current":Image.open(stage1/"current.png").convert("RGB"),
            "stage1-B-N4":Image.open(stage1/"recipes-N4.png").convert("RGB")}
    for label,im in images.items():
        im.save(root/f"{label}.png")
    xp,yp=np.meshgrid(np.linspace(0,1,128),np.linspace(0,1,128));keycount=0
    for (s,cl),cells in groups.items():
        keycount+=min(4,len(cells))
    for mode in VARIANTS:
        im=Image.new("RGB",(4096,4096))
        for (s,cl),cells in groups.items():
            cache={};count=min(4,len(cells))
            for rank,(x,y) in enumerate(cells):
                v=rank%count
                if v not in cache:
                    rgb=np.uint8(np.clip(np.rint(r.recipe(s,v,xp,yp,mode)),0,255))
                    cache[v]=Image.fromarray(rgb).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                im.paste(cache[v],(x*128,(31-y)*128))
        im.save(root/f"{mode}.png");images[mode]=im
        print("rendered",mode,flush=True)
    counts={k:slot_counts(im,m,crop) for k,im in images.items()}
    contact(root,images,counts,"preview.png")
    box=(17*128,12*128,22*128,17*128)
    contact(root,images,counts,"edge-closeup.png",box,"same stage1 5x5 detail | world x105..109 y151..155")
    # Choose additional windows by their actual dominant two-material cells.
    pairwindows={}
    for yy in range(28):
        for xx in range(28):
            paircounts=collections.Counter()
            for y in range(yy,yy+5):
                for x in range(xx,xx+5):
                    k=(y+crop["y"])*m["width"]+x+crop["x"]
                    pair=tuple(sorted(set(m["landSignatures"][k])))
                    if len(pair)==2:
                        paircounts[pair]+=1
            for pair,n in paircounts.items():
                if pair in banks and (pair not in pairwindows or n>pairwindows[pair][0]):
                    pairwindows[pair]=(n,xx,yy)
    mainpairs=collections.Counter()
    for y in range(15,20):
        for x in range(17,22):
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            pair=tuple(sorted(set(m["landSignatures"][k])))
            if len(pair)==2:mainpairs[pair]+=1
    dominant=mainpairs.most_common(1)[0][0]
    extras=[]
    for pair,(n,xx,yy) in sorted(pairwindows.items(),key=lambda p:-p[1][0]):
        if pair==dominant:continue
        filename=f"edge-extra-{len(extras)+1}.png"
        box=(xx*128,(27-yy)*128,(xx+5)*128,(32-yy)*128)
        contact(root,images,counts,filename,box,f"5x5 x{crop['x']+xx} y{crop['y']+yy} | pair {pair}")
        extras.append({"file":filename,"x":crop["x"]+xx,"y":crop["y"]+yy,"pair":list(pair),"twoMaterialCells":n})
        if len(extras)==3:break
    # Independent PNG seam readback, all internal edges and all pixel channels.
    seams={}
    for mode in ("stage1-B-N4",*VARIANTS):
        a=np.asarray(Image.open(root/f"{mode}.png").convert("RGB"),int)
        errors=[]
        for t in range(1,32):
            errors.append(int(np.max(np.abs(a[:,t*128-1]-a[:,t*128]))))
            errors.append(int(np.max(np.abs(a[t*128-1]-a[t*128]))))
        seams[mode]={"adjacentCellEdges":1984,"rgbSamples":1984*128*3,"maxChannelDifference":max(errors),
                     "failedFullGridLines":sum(v!=0 for v in errors)}
        assert max(errors)==0,f"Raster seam {mode}: {max(errors)}"
    # Evaluate both sides using independent recipes/labels at 257 samples,
    # which also verifies categorical ownership before color blending.
    t=np.linspace(0,1,257);checks={}
    for mode in VARIANTS:
        maxmask=maxrgb=0;checked=0
        for y in range(32):
            for x in range(32):
                k=(y+crop["y"])*m["width"]+x+crop["x"];s=tuple(m["landSignatures"][k])
                for dx,dy in ((1,0),(0,1)):
                    if x+dx>=32 or y+dy>=32:continue
                    q=tuple(m["landSignatures"][k+dx+dy*m["width"]])
                    xa,ya,xb,yb=(np.ones_like(t),t,np.zeros_like(t),t) if dx else (t,np.ones_like(t),t,np.zeros_like(t))
                    maxmask=max(maxmask,float(np.max(np.abs(r.ownership(s,0,xa,ya,mode)-r.ownership(q,3,xb,yb,mode)))))
                    maxrgb=max(maxrgb,float(np.max(np.abs(r.recipe(s,0,xa,ya,mode)-r.recipe(q,3,xb,yb,mode)))))
                    checked+=1
        checks[mode]={"adjacentEdges":checked,"samplesPerEdge":257,"ownershipMaxError":maxmask,"rgbMaxError":maxrgb}
        assert maxmask==0 and maxrgb<1e-5,f"Analytic seam {mode}: {maxmask}, {maxrgb}"
        print("edges verified",mode,checked,flush=True)
    for p,h in snapshots.items():assert digest(Path(p))==h,"Input changed during run"
    result={"crop":crop,"sourceMprSha256":m["sha256"],"sourceAtlasPixelSha256":atlas["pixelSha256"],
            "inputSnapshots":snapshots,"counts":counts,"recipeKeyCount":keycount,
            "rasterSeams":seams,"analyticSeams":checks,"extraCloseups":extras,
            "stock":{"tested":len(records),"eligible":sum(v["eligible"] for v in records),
                     "eligibleTopologies":dict(collections.Counter(v["topology"] for v in records if v["eligible"])),
                     "pairBanks":len(banks),"unavailablePairsFallbackPixelEvaluations":dict(r.fallbacks)},
            "parameters":{"bandNormalExtentPx":32,"fragmentLengthPx":64,"bankPeriodPx":512,
                          "edgeStateCount":1,"interiorLabels":4,"tonguesNormalScale":1.45,"mixedNormalScale":1.15,
                          "mixedOwnershipLevels":[0,.25,.5,.75,1],"gradientEdgeFade":"smooth(min(x,1-x)*8), same for y"},
            "limitations":["estimated RGB ownership; original artist alpha/dither unknown",
                           "first-order gradient distance, not exact Euclidean SDF",
                           "C0 edge guarantee only; C1 and free-world curves not guaranteed",
                           "top-two ownership at triple junctions; no authored three-material brush",
                           "one fixed edge state and periodic fragment phases can repeat",
                           "no DXT1/mip/gutter/runtime/game/owner acceptance"]}
    write(root/"stage1b-result.json",result)
    print(json.dumps({"counts":counts,"recipeKeyCount":keycount,"stock":result["stock"],"seams":seams,"extras":extras},indent=2),flush=True)


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("research",type=Path);p.add_argument("stage1",type=Path);p.add_argument("output",type=Path)
    args=p.parse_args();run(args.research.resolve(),args.stage1.resolve(),args.output.resolve())
