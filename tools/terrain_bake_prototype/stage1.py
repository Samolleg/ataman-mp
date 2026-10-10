"""Offline stage 1. Stock assets and all outputs stay in the private build root.

No network, game launch, live writes, or mapper changes. Uses existing numpy/PIL.
The RGB ownership fit is an estimator, not recovered source authoring alpha.
"""
import argparse
import collections
import hashlib
import json
import math
from pathlib import Path
import shutil
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.dont_write_bytecode = True
from research import read, write, uniform_sources, hash_noise, measure

ROOT_NAME = "build-tests-terrain-bake-stage-1"


def signature(slot):
    return tuple(slot.get("corners") or [slot.get("signature", {}).get(k, 0)
                  for k in ("northWest", "northEast", "southEast", "southWest")])


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for b in iter(lambda: f.read(1024 * 1024), b""):
            h.update(b)
    return h.hexdigest()


def blur(a, radius):
    # Float channel; GaussianBlur does not support Pillow F mode.
    return np.asarray(Image.fromarray(np.uint8(np.clip(a, 0, 1)*255)).filter(
        ImageFilter.GaussianBlur(radius)), dtype=np.float32)/255


def smooth(a):
    a = np.clip(a, 0, 1)
    return a*a*(3-2*a)


def seed(x, y, salt=0):
    b = f"{int(x)},{int(y)},{salt}".encode("ascii")
    return int.from_bytes(hashlib.sha256(b).digest()[:4], "little")


def extract_stock(root, atlas, materials):
    """Straight two-material catalog tiles, RGB least-squares against uniforms.

    Test all uniform pairs and retain lowest residual. Align boundary vertically;
    estimate contour nearest tile middle, measure width and contour spectrum.
    Store actual transition pixels, estimated ownership, residual and profiles.
    """
    sources = uniform_sources(atlas)
    tiles = root/"input"/"atlas"
    cache = {}

    def tex(s):
        if s not in cache:
            cache[s] = np.asarray(Image.open(tiles/f"{s}.png").convert("RGB"), np.float32)
        return cache[s]

    candidates = []
    for slot in atlas["catalog"]["slots"]:
        s = signature(slot)
        if not slot.get("assigned") or len(set(s)) != 2 or not set(s) <= set(materials):
            continue
        # Catalog uses physical texture corners. Straight adjacent pairs only.
        vertical = s[0] == s[3] and s[1] == s[2]
        horizontal = s[0] == s[1] and s[2] == s[3]
        if not (vertical or horizontal):
            continue
        a, b = (s[0], s[1]) if vertical else (s[0], s[3])
        if a not in sources or b not in sources:
            continue
        image = tex(slot["slot"])
        best = None
        for sa in sources[a]:
            for sb in sources[b]:
                ta, tb = tex(sa), tex(sb)
                delta = tb-ta
                alpha = np.clip(np.sum((image-ta)*delta, axis=2)/np.maximum(
                    np.sum(delta*delta, axis=2), 100), 0, 1)
                residual = float(np.sqrt(np.mean((image-(ta+alpha[..., None]*delta))**2)))
                if best is None or residual < best[0]:
                    best = residual, alpha, sa, sb
        residual, alpha, sa, sb = best
        if horizontal:
            alpha = alpha.T
            image = np.transpose(image, (1, 0, 2))
        # Orient by measured side means, not an assumed physical Y convention.
        if alpha[:, :16].mean() > alpha[:, -16:].mean():
            alpha = 1-alpha
        clean = blur(alpha, 1.0)
        if clean[:, :16].mean() > .25 or clean[:, -16:].mean() < .75:
            continue
        contour, widths = [], []
        for row in clean:
            cx = int(np.argmin(np.abs(row[24:104]-.5)))+24
            lo = int(np.argmin(np.abs(row[max(0, cx-30):cx+1]-.1)))+max(0,cx-30)
            hi = int(np.argmin(np.abs(row[cx:min(128,cx+31)]-.9)))+cx
            contour.append(cx)
            widths.append(max(1, hi-lo))
        contour = np.asarray(contour, np.float32)
        low = np.asarray(Image.fromarray(np.uint8(contour)[None, :]).filter(
            ImageFilter.GaussianBlur(5)), np.float32)[0]
        # Pool cross-boundary profile relative to each scanline's 50% contour.
        aligned = np.stack([np.interp(np.arange(-32, 33)+cx, np.arange(128), row)
                            for cx, row in zip(contour, clean)])
        aligned_raw = np.stack([np.interp(np.arange(-32, 33)+cx, np.arange(128), row)
                                for cx, row in zip(contour, alpha)])
        profile = np.maximum.accumulate(np.mean(aligned, axis=0))
        profile = np.clip((profile-profile[0])/max(.1, profile[-1]-profile[0]), 0, 1)
        candidates.append({"slot": slot["slot"], "materials": [a, b],
            "uniformFitSlots": [sa, sb], "rgbFitRmse": residual,
            "width10to90MedianPx": float(np.median(widths)),
            "width10to90P10P90Px": np.percentile(widths, [10,90]).tolist(),
            "contourRangePx": float(np.ptp(contour)),
            "largeTongueStdPx": float(np.std(low)),
            "fineFringeStdPx": float(np.std(contour-low)),
            "estimatedIntermediateFraction": float(np.mean((alpha>.1)&(alpha<.9))),
            "contour": contour, "profile": profile, "image": image,
            "alpha": alpha, "aligned": aligned_raw,
            "actualAlphaValues": sorted(int(v) for v in np.unique(np.asarray(
                Image.open(tiles/f"{slot['slot']}.png").convert("RGBA"))[...,3]))})
    if len(candidates) < 3:
        raise RuntimeError("Fewer than three verified straight stock transitions fit")
    candidates.sort(key=lambda c: c["rgbFitRmse"])
    selected = candidates[:8]
    sheet = Image.new("RGB", (1024, 280*len(selected)), (25,25,25))
    d = ImageDraw.Draw(sheet)
    records = []
    for i, c in enumerate(selected):
        y = i*280
        d.text((8,y+5), f"stock slot {c['slot']} | fit RMSE {c['rgbFitRmse']:.1f} RGB | width {c['width10to90MedianPx']:.1f}px", fill="white")
        for x, a in ((0,c["image"]),(270,np.repeat(c["alpha"][...,None]*255,3,axis=2))):
            sheet.paste(Image.fromarray(np.uint8(np.clip(a,0,255))).resize((256,256)), (x,y+24))
        d.text((545,y+28), "actual RGB / estimated ownership", fill="white")
        for k in range(64):
            d.line((560+k*6,y+240-c["profile"][k]*180,
                    566+k*6,y+240-c["profile"][k+1]*180), fill=(130,220,110), width=2)
        for k in range(127):
            d.line((560+k*3,y+85+(c["contour"][k]-64)*2,
                    563+k*3,y+85+(c["contour"][k+1]-64)*2), fill=(230,170,90))
        records.append({k:v for k,v in c.items() if k not in ("image","alpha","aligned")})
        for k in ("contour","profile"):
            records[-1][k] = records[-1][k].tolist()
    sheet.save(root/"stock-edge-analysis.png")
    write(root/"stock-edge-measurements.json", {"method":"RGB projection estimator; baked alpha/dither cannot be uniquely recovered", "tested":len(candidates), "selected":records})
    return selected


class Renderer:
    def __init__(self, root, m, atlas, crop, stock):
        self.root, self.m, self.atlas, self.crop = root, m, atlas, crop
        self.materials = crop["materials"]
        self.field = np.asarray(m["field"]).reshape(m["height"]+1,m["width"]+1)
        self.sources = uniform_sources(atlas)
        self.textures = {}
        for mat in self.materials:
            self.textures[mat] = []
            for slot in self.sources[mat]:
                im = Image.open(root/"input"/"atlas"/f"{slot}.png").convert("RGB")
                p=im.width//16
                self.textures[mat].append(np.asarray(im.crop((p,p,im.width-p,im.height-p)),np.float32))
        self.stock = stock
        self.profile = np.mean([s["profile"] for s in stock[:4]],axis=0)
        self.contour = stock[0]["contour"]-np.mean(stock[0]["contour"])
        # Use a grass edge with visible pixel fringe for the 2-D edge texture.
        fringe=max(stock[:8],key=lambda s:s["estimatedIntermediateFraction"])
        self.edge_texture=fringe["aligned"]-fringe["profile"][None,:]
        self.width = float(np.median([s["width10to90MedianPx"] for s in stock[:4]]))/128

    def raw(self, s, x, y):
        bs=((1-x)*(1-y),x*(1-y),x*y,(1-x)*y)
        return np.stack([sum(b*(v==mat) for b,v in zip(bs,s)) for mat in self.materials],axis=-1)

    def sample(self, mat, x, y, variant=0, rot=0):
        tex=self.textures[mat][variant%len(self.textures[mat])]
        if rot==1: x,y=y,1-x
        elif rot==2: x,y=1-x,1-y
        elif rot==3: x,y=1-y,x
        n=tex.shape[0]
        # Periodic sampling with identical mathematical samples at x=0 and 1.
        ix=np.floor(np.mod(x,1)*n+1e-7).astype(int)%n
        iy=np.floor(np.mod(y,1)*n+1e-7).astype(int)%n
        return tex[n-1-iy,ix]

    def blend(self, raw, x, y, strength=1, interior=None, variant=0):
        # Continuous score-distance proxy around the winning score. There is no
        # gradient normalization here; a metric SDF is future work.
        scores=raw.copy()
        mod = np.ones(x.shape) if interior is None else interior
        for i,mat in enumerate(self.materials):
            t=(x*.71+y*.83)*128
            contour=np.interp(np.mod(t,128),np.arange(129),np.r_[self.contour,self.contour[0]])/128
            noise=(.065*hash_noise(x*2.1,y*2.1,(mat&0xffff)+variant*71)
                   +.021*hash_noise(x*8,y*8,(mat&0xffff)+91+variant*71)
                   +.008*hash_noise(x*31,y*31,(mat&0xffff)+191))
            scores[...,i] += strength*(noise+.65*contour)*mod*4*raw[...,i]*(1-raw[...,i])
        delta=scores-scores.max(axis=-1,keepdims=True)
        # Stock mean cross-boundary profile, narrowed at A and broadened at C.
        width=max(.018,self.width)*(.75 if strength<.8 else 1 if strength<1.4 else 1.3)
        alpha=np.interp(delta/width*18+0, np.arange(-32,33),self.profile)
        for i,mat in enumerate(self.materials):
            # Bilinear wrapped sampling along the boundary: transfer the actual
            # irregular ownership residual, not just a procedural threshold.
            t=np.mod((x*.71+y*.83)*128+(mat&127),128)
            u=np.clip(delta[...,i]/width*18+32,0,64)
            ti=np.floor(t).astype(int);ui=np.minimum(63,np.floor(u).astype(int))
            ft=t-ti;fu=u-ui;et=self.edge_texture
            value=(et[ti,ui]*(1-ft)*(1-fu)+et[(ti+1)%128,ui]*ft*(1-fu)
                   +et[ti,ui+1]*(1-ft)*fu+et[(ti+1)%128,ui+1]*ft*fu)
            alpha[...,i]=np.clip(alpha[...,i]+.45*strength*value*mod
                                *4*raw[...,i]*(1-raw[...,i]),0,1)
        alpha=np.where(raw==0,0,alpha)
        alpha/=np.maximum(alpha.sum(axis=-1,keepdims=True),1e-9)
        return alpha

    def world(self, x, y, label):
        raw=self.world_raw(x,y)
        strength={"mask-A":.55,"mask-B":1.0,"mask-C":1.65,"antitile-baked":1.0}[label]
        blend=self.blend(raw,x,y,strength)
        rgb=np.zeros((*x.shape,3),np.float32)
        for i,mat in enumerate(self.materials):
            sampled=self.sample(mat,x,y)
            if label=="antitile-baked":
                # Continuous low-frequency mixture of three fixed rotated/phase
                # sources. No hex stochastic implementation claimed.
                w=smooth((hash_noise(x/3.7,y/3.7,(mat&0xffff)+7)+1)/2)
                v=smooth((hash_noise(x/6.1,y/6.1,(mat&0xffff)+17)+1)/2)
                a=self.sample(mat,x+.27,y+.43,1,1)
                b=self.sample(mat,x+.61,y+.19,2,3)
                sampled=sampled*(1-w[...,None])+a*w[...,None]
                sampled=sampled*(1-.45*v[...,None])+b*.45*v[...,None]
            rgb+=sampled*blend[...,i,None]
        if label=="antitile-baked":
            macro=.10*hash_noise(x/7,y/7,101)+.035*hash_noise(x/2.3,y/2.3,103)
            hue=.022*hash_noise(x/9,y/9,107)
            rgb*=1+macro[...,None]+hue[...,None]*np.array([1,-.25,-1])
        return rgb

    def world_raw(self,x,y):
        ix=np.floor(x).astype(int);iy=np.floor(y).astype(int)
        fx=x-ix;fy=y-iy
        fs=(self.field[iy,ix],self.field[iy,ix+1],self.field[iy+1,ix+1],self.field[iy+1,ix])
        bs=((1-fx)*(1-fy),fx*(1-fy),fx*fy,(1-fx)*fy)
        raw=np.stack([sum(b*(v==mat) for b,v in zip(bs,fs)) for mat in self.materials],axis=-1)
        return raw

    def recipe(self,s,variant,x,y):
        # Finite grammar: ordered corner signature and N interior seeds.
        # Shared edge endpoints imply shared bilinear scores; perturbations,
        # macro color and source mixtures fade to zero at every edge.
        raw=self.raw(s,x,y)
        fade=16*x*(1-x)*y*(1-y)
        blend=self.blend(raw,x,y,1,fade,variant)
        rgb=np.zeros((*x.shape,3),np.float32)
        for i,mat in enumerate(self.materials):
            base=self.sample(mat,x,y)
            alt=self.sample(mat,x+.27,y+.43,variant,variant%4)
            mix=fade*.55*smooth((hash_noise(x*2,y*2,(mat&0xffff)+variant*33)+1)/2)
            sampled=base*(1-mix[...,None])+alt*mix[...,None]
            rgb+=sampled*blend[...,i,None]
        rgb*=1+.07*hash_noise(x*2,y*2,variant+211)[...,None]*fade[...,None]
        return rgb


def slot_counts(im,m,crop):
    # All images are in conventional north-up presentation; invert crop Y.
    hashes=set();typed=set();tr=set();un=set()
    for y in range(32):
        for x in range(32):
            tile=im.crop((x*128,(31-y)*128,(x+1)*128,(32-y)*128))
            h=hashlib.sha256(tile.tobytes()).hexdigest()
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            cl=m["runtimeTypes"][m["land"][k]&0x3fff]
            hashes.add(h);typed.add((h,cl))
            (tr if len(set(m["landSignatures"][k]))>1 else un).add((h,cl))
    return {"pixelUnique":len(hashes),"typedUnique":len(typed),"transitionUnique":len(tr),"uniformUnique":len(un),"pages":math.ceil(len(typed)/64)}


def corpus_recipes(old,root):
    rows=[]
    for item in read(old/"inventory.json")["maps"]:
        m=read(old/item["export"])
        row=measure(m)
        # Fixed edge state: midpoint crossing, tangent from bilinear endpoint
        # scores; texture phase fixed. N labels are interior variants only.
        # Count exactly assigned labels, preserving layer/class identities.
        row["recipeN4"]=row["n4"]
        row["recipeN8"]=row["n8"]
        rows.append(row)
    summaries={}
    for group in ("exact","base","all"):
        rr=[r for r in rows if (r["semanticExact"] if group=="exact" else r["profile"]=="base" if group=="base" else True)]
        summaries[group]={"maps":len(rr)}
        for key in ("uniqueCellBudget","recipeN4","recipeN8"):
            summaries[group][key]={"fits2048":sum(r[key]<=2048 for r in rr),"fits4096":sum(r[key]<=4096 for r in rr),"max":max(r[key] for r in rr),"median":float(np.median([r[key] for r in rr]))}
    write(root/"corpus-slot-budgets.json",{"model":"one fixed midpoint edge state; N interior variants; H<=4 per layer/material/class; estimates not all-map pixel bake","rows":rows,"summary":summaries})
    return summaries


def run(old,root):
    if root.name!=ROOT_NAME or not old.is_dir():
        raise RuntimeError("Invalid input/output root")
    root.mkdir(exist_ok=True)
    crop=read(old/"crop.json");m=read(old/crop["export"])
    atlas=next(a for a in read(old/"inventory.json")["atlases"] if a["directory"]==m["atlasDirectory"])
    assert atlas["semanticExact"] and crop["recoveredChangedCells"]==0
    assert digest(Path(m["path"]))==m["sha256"], "Source map drift"
    tiles=root/"input"/"atlas";tiles.mkdir(parents=True,exist_ok=True)
    for p in (old/atlas["directory"]).glob("*.png"):
        shutil.copyfile(p,tiles/p.name)
    write(root/"input"/"atlas.json",atlas)
    write(root/"input"/"map.json",m)
    write(root/"memory-config.json",{"sourceMpr":m["path"],"sourceMob":str(Path(m["path"]).with_suffix(".mob")),"sourceMprSha256":m["sha256"],"atlasDirectory":str(tiles.resolve()),"sourceSlots":m["atlasSlots"],"sourceAtlas":atlas,"outputRoot":str(root.resolve())})
    for name in ("current","baked"):
        shutil.copyfile(old/f"{name}.png",root/("stage0.png" if name=="baked" else "current.png"))
    stock=extract_stock(root,atlas,crop["materials"])
    r=Renderer(root,m,atlas,crop,stock)
    images={name:Image.open(root/f"{name}.png").convert("RGB") for name in ("current","stage0")}
    for label in ("mask-A","mask-B","mask-C","antitile-baked"):
        im=Image.new("RGB",(4096,4096))
        for y in range(32):
            xw,yw=np.meshgrid(crop["x"]+(np.arange(4096)+.5)/128,crop["y"]+y+(np.arange(128)+.5)/128)
            rgb=np.uint8(np.clip(np.rint(r.world(xw,yw,label)),0,255))
            im.paste(Image.fromarray(rgb).transpose(Image.Transpose.FLIP_TOP_BOTTOM),(0,(31-y)*128))
        im.save(root/f"{label}.png");images[label]=im
        print("rendered",label,flush=True)
    # Zero added slots: only existing exact same-class homogeneous slots.
    im=images["current"].copy();eligible=changed=0;rotations=collections.Counter()
    eligible_slots={}
    for mat in crop["materials"]:
        for s in r.sources[mat]:
            if s<len(m["runtimeTypes"]):
                eligible_slots.setdefault((mat,m["runtimeTypes"][s]),[]).append(s)
    for y in range(32):
        for x in range(32):
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            s=m["landSignatures"][k];p=m["land"][k];cl=m["runtimeTypes"][p&0x3fff]
            if len(set(s))!=1 or (s[0],cl) not in eligible_slots: continue
            pool=eligible_slots[s[0],cl];z=seed(x+crop["x"],y+crop["y"],s[0])
            slot=pool[(z>>2)%len(pool)];rot=z%4;rotations[rot]+=1;eligible+=1
            changed+=int((slot|(rot<<14))!=p)
            tile=Image.open(tiles/f"{slot}.png").convert("RGB").transpose(Image.Transpose.FLIP_TOP_BOTTOM).rotate(rot*90)
            p=tile.width//16;tile=tile.crop((p,p,tile.width-p,tile.height-p)).resize((128,128),Image.Resampling.LANCZOS)
            im.paste(tile.transpose(Image.Transpose.FLIP_TOP_BOTTOM),(x*128,(31-y)*128))
    im.save(root/"antitile-zero-slots.png");images["antitile-zero-slots"]=im
    # Recipes: allocate balanced deterministic labels per signature/class group.
    groups=collections.defaultdict(list)
    for y in range(32):
        for x in range(32):
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            s=tuple(m["landSignatures"][k]);cl=m["runtimeTypes"][m["land"][k]&0x3fff]
            groups[s,cl].append((x,y))
    # Include the exact shared edge samples in each recipe raster. This is a
    # deliberate recipe sampling convention, distinct from preview pixel centers.
    xp,yp=np.meshgrid(np.linspace(0,1,128),np.linspace(0,1,128))
    recipe_keys={}
    for n in (4,8):
        im=Image.new("RGB",(4096,4096));keys=set()
        for (s,cl),cells in groups.items():
            cells.sort(key=lambda xy:seed(xy[0]+crop["x"],xy[1]+crop["y"],sum(s)))
            count=min(n if len(set(s))>1 else 4,len(cells))
            cache={}
            for rank,(x,y) in enumerate(cells):
                v=rank%count;keys.add((s,cl,v))
                if v not in cache:
                    cache[v]=Image.fromarray(np.uint8(np.clip(np.rint(r.recipe(s,v,xp,yp)),0,255))).transpose(Image.Transpose.FLIP_TOP_BOTTOM)
                im.paste(cache[v],(x*128,(31-y)*128))
        label=f"recipes-N{n}";im.save(root/f"{label}.png");images[label]=im;recipe_keys[n]=len(keys)
    # Exact RGB/ownership at common edges for all real adjacent signatures and
    # different extreme interior seeds. Check both orientations and corners.
    t=np.linspace(0,1,257);errs=[];checks=0
    for y in range(32):
        for x in range(32):
            k=(y+crop["y"])*m["width"]+x+crop["x"]
            s=tuple(m["landSignatures"][k])
            for dx,dy in ((1,0),(0,1)):
                if x+dx>=32 or y+dy>=32:continue
                q=tuple(m["landSignatures"][k+dx+dy*m["width"]])
                if dx:a=r.recipe(s,0,np.ones_like(t),t);b=r.recipe(q,7,np.zeros_like(t),t)
                else:a=r.recipe(s,0,t,np.ones_like(t));b=r.recipe(q,7,t,np.zeros_like(t))
                errs.append(float(np.max(np.abs(a-b))));checks+=1
    if max(errs)>1e-5:raise RuntimeError(f"Recipe RGB seam: {max(errs)}")
    continuity={}
    for axis in (0,1):
        a,b=np.meshgrid(np.arange(crop["x"]+1,crop["x"]+32) if axis==0
                       else np.arange(crop["y"]+1,crop["y"]+32),
                       np.linspace(crop["y"]+.01,crop["y"]+31.99,801) if axis==0
                       else np.linspace(crop["x"]+.01,crop["x"]+31.99,801))
        x,y=(a,b) if axis==0 else (b,a)
        xa,ya=x-(1e-6 if axis==0 else 0),y-(1e-6 if axis else 0)
        xb,yb=x+(1e-6 if axis==0 else 0),y+(1e-6 if axis else 0)
        for label,strength in (("A",.55),("B",1),("C",1.65)):
            jump=float(np.max(np.abs(r.blend(r.world_raw(xa,ya),xa,ya,strength)
                                    -r.blend(r.world_raw(xb,yb),xb,yb,strength))))
            continuity[f"{label}:{'vertical' if axis==0 else 'horizontal'}"]=jump
            if jump>.002:raise RuntimeError(f"World mask discontinuity {label}: {jump}")
    counts={k:slot_counts(im,m,crop) for k,im in images.items()}
    # Same scale for all panels; crop close-up makes edge detail reviewable.
    order=("current","stage0","mask-A","mask-B","mask-C","antitile-zero-slots","antitile-baked","recipes-N4","recipes-N8")
    sheet=Image.new("RGB",(2040,3*(680+36)),(24,24,24));d=ImageDraw.Draw(sheet)
    for i,label in enumerate(order):
        x=(i%3)*680;y=(i//3)*716
        d.text((x+8,y+8),f"{label} | {counts[label]['typedUnique']} typed pixel tiles",fill="white")
        sheet.paste(images[label].resize((680,680),Image.Resampling.LANCZOS),(x,y+36))
    sheet.save(root/"preview.png")
    close=Image.new("RGB",(2040,3*(680+36)),(24,24,24));d=ImageDraw.Draw(close)
    for i,label in enumerate(order):
        x=(i%3)*680;y=(i//3)*716
        d.text((x+8,y+8),label+" | SAME 5x5 CELL DETAIL",fill="white")
        close.paste(images[label].crop((17*128,12*128,22*128,17*128)).resize((680,680)),(x,y+36))
    close.save(root/"edge-closeup.png")
    # Homogeneous study is required: the selected crop is 85% transitions.
    mats=(crop["materials"][4],crop["materials"][5])
    study=Image.new("RGB",(2048,1080),(24,24,24));d=ImageDraw.Draw(study)
    autocorr={}
    for row,mat in enumerate(mats):
        arrays=[]
        for mode in range(3):
            a=np.zeros((512,512,3),np.float32)
            for yy in range(8):
                for xx in range(8):
                    u,v=np.meshgrid((np.arange(64)+.5)/64,(np.arange(64)+.5)/64)
                    z=seed(xx,yy,mat);variant=(z>>2)%len(r.textures[mat]) if mode==2 else 0
                    a[yy*64:(yy+1)*64,xx*64:(xx+1)*64]=r.sample(mat,u,v,variant,z%4 if mode else 0)
            arrays.append(a)
            gray=a.mean(axis=2);c=gray-gray.mean()
            corr=float(np.mean(c[:,:-64]*c[:,64:])/max(1e-6,np.mean(c*c)))
            autocorr[f"{mat}:{mode}"]=corr
            x=mode*680;y=row*540
            d.text((x+8,y+8),f"material {mat}: {'repeat' if mode==0 else 'rotation' if mode==1 else 'rotation + variants'}; corr 1 cell={corr:.3f}",fill="white")
            study.paste(Image.fromarray(np.uint8(a)).resize((512,512)),(x,y+28))
    study.save(root/"homogeneous-study.png")
    summary=corpus_recipes(old,root)
    write(root/"memory-config.json",{"sourceMpr":m["path"],"sourceMob":str(Path(m["path"]).with_suffix(".mob")),"sourceMprSha256":m["sha256"],"atlasDirectory":str(tiles.resolve()),"sourceSlots":m["atlasSlots"],"sourceAtlas":atlas,"outputRoot":str(root.resolve())})
    result={"crop":crop,"baselineMapSha256":m["sha256"],"baselineAtlasPixelSha256":atlas["pixelSha256"],"counts":counts,"recipeKeyCounts":recipe_keys,"edgeChecks":checks,"recipeRgbEdgeMaxError":max(errs),"worldMaskEpsilon1e6Jumps":continuity,"zeroSlots":{"additionalPhysicalSlots":0,"eligibleCells":eligible,"changedCells":changed,"rotationHistogram":dict(rotations)},"homogeneousAutocorrelation":autocorr,"corpusSummary":summary,"parameters":{"stockWidthMedianCells":r.width,"maskStrengths":[.55,1,1.65],"octaveAmplitudes":[.065,.021,.008],"frequenciesPerCell":[2.1,8,31],"stockContourGain":.65,"stockFringeGain":.45,"macroBrightness":[.10,.035],"macroHue":.022,"fixedEdgeCrossing":.5,"interiorFade":"16*x*(1-x)*y*(1-y)","N":"interior variation, one fixed edge state"},"limits":["stock ownership is estimated from baked RGB, original alpha/dither unrecoverable","metric signed distance not implemented: continuous score-distance proxy","recipe edge tangents need not match, RGB continuity is C0","preview is unlit and not DXT/mip/runtime acceptance","no stochastic/hex tiling claimed","whole-map budgets estimated; only crop recipes pixel-baked"]}
    write(root/"stage1-result.json",result)
    print(json.dumps(result,ensure_ascii=False,indent=2),flush=True)


if __name__=="__main__":
    p=argparse.ArgumentParser();p.add_argument("previous",type=Path);p.add_argument("output",type=Path)
    a=p.parse_args();run(a.previous.resolve(),a.output.resolve())
