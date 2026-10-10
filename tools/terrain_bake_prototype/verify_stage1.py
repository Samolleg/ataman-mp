"""Independent private-output readback; no live writes or game launch."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
sys.dont_write_bytecode=True
import numpy as np
from PIL import Image
from research import read, write


def sha(path):
    h=hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda:f.read(1<<20),b''):h.update(b)
    return h.hexdigest()


def archive(path):
    b=path.read_bytes()
    magic,count,offset,names=struct.unpack_from('<4I',b)
    assert magic==0x019ce23c and offset+22*count+names<=len(b)
    name_base=offset+count*22;members={}
    for i in range(count):
        nxt,size,data,time,nlen,noff=struct.unpack_from('<4IHI',b,offset+i*22)
        if nlen==0:continue
        assert noff+nlen<=names and data+size<=len(b)
        name=b[name_base+noff:name_base+noff+nlen].decode('cp1251').lower()
        assert name not in members
        members[name]=b[data:data+size]
    return members


def run(root):
    assert root.name=='build-tests-terrain-bake-stage-1'
    result=read(root/'stage1-result.json')
    images={}
    for label in result['counts']:
        with Image.open(root/f'{label}.png') as im:
            assert im.size==(4096,4096)
            im.load();images[label]=np.asarray(im)
    recipe_seams={}
    for label in ('recipes-N4','recipes-N8'):
        a=images[label].astype(np.int16);errs=[]
        for k in range(1,32):
            errs.append(int(np.max(np.abs(a[:,k*128-1]-a[:,k*128]))))
            errs.append(int(np.max(np.abs(a[k*128-1]-a[k*128]))))
        recipe_seams[label]=max(errs)
        assert max(errs)==0,(label,max(errs))
    fixtures=read(root/'memory-kit'/'fixtures.json')
    checked=[]
    original=archive(Path(read(root/'memory-config.json')['sourceMpr']))
    src_mp=next(b for n,b in original.items() if n.endswith('.mp'))
    original_material_count=struct.unpack_from('<H',src_mp,32)[0]
    original_tile_count=struct.unpack_from('<I',src_mp,24)[0]
    src_types=struct.unpack_from(f'<{original_tile_count}i',src_mp,38+44*original_material_count)
    for item in fixtures['fixtures']:
        fid=item['id'];folder=root/'memory-kit'/fid;manifest=read(folder/'manifest.json')
        for ext,key in (('.mpr','mprSha256'),('-atlas.res','resSha256'),('.mob','mobSha256')):
            assert sha(folder/(fid+ext))==item[key]
        mpr=archive(folder/(fid+'.mpr'));res=archive(folder/(fid+'-atlas.res'))
        assert set(res)==set(item['resourceMembers']) and len(res)==item['pages']
        mp=mpr[fid+'.mp'];texcount,texsize,tilecount,tilesize=struct.unpack_from('<4I',mp,16)
        assert (texcount,texsize,tilecount,tilesize)==(item['pages'],item['pixels']*8,item['slots'],item['pixels'])
        nm=struct.unpack_from('<H',mp,32)[0];assert nm==original_material_count
        assert mp[38:38+nm*44]==src_mp[38:38+nm*44]
        types=struct.unpack_from(f'<{tilecount}i',mp,38+44*nm)
        expected=tuple(src_types[s] if s<len(src_types) else 0 for s in manifest['sourceForSlot'])
        assert types==expected
        for payload in res.values():
            # MMP header documented by the ordinary project decoder.
            width,height=struct.unpack_from('<II',payload,4)
            assert width==height==item['pixels']*8
            magic,mips,fmt=struct.unpack_from('<I8xII',payload,0)
            assert magic==0x00504d4d and fmt==0x31545844
            expected_levels=int(np.log2(width))+1
            assert mips==expected_levels
            expected_size=76;size=width
            while True:
                expected_size+=max(1,(size+3)//4)**2*8
                if size==1:break
                size=max(1,size//2)
            assert len(payload)==expected_size
        # All unrecognized non-MP/SEC members must survive isolated cloning.
        for name,payload in original.items():
            if name.endswith(('.mp','.sec')):continue
            assert mpr[name]==payload
        used_slots=set();used_pages=set();land_slots=set();class_mismatches=0;sectors=0
        for name,old_sec in original.items():
            if not name.endswith('.sec'):continue
            suffix=name[len(fixtures['sourceNamespace']):]
            new_sec=mpr[fid+suffix]
            land_offset=5+33*33*8*(2 if old_sec[4]==3 else 1)
            assert old_sec[:land_offset]==new_sec[:land_offset]
            assert old_sec[land_offset+512:]==new_sec[land_offset+512:]
            before=struct.unpack_from('<256H',old_sec,land_offset)
            after=struct.unpack_from('<256H',new_sec,land_offset)
            for p,q in zip(before,after):
                a,b=p&0x3fff,q&0x3fff
                assert p&0xc000==q&0xc000
                assert b<item['slots'] and manifest['sourceForSlot'][b]==a
                class_mismatches+=int(src_types[a]!=types[b])
                used_slots.add(b);land_slots.add(b);used_pages.add(b//64)
            if old_sec[4]==3:
                over=struct.unpack_from('<256H',new_sec,land_offset+512)
                indices=struct.unpack_from('<256h',new_sec,land_offset+1024)
                for p,material in zip(over,indices):
                    if material>=0 and p!=0xffff:used_slots.add(p&0x3fff);used_pages.add((p&0x3fff)//64)
            sectors+=1
        assert class_mismatches==0 and len(used_pages)==item['pages']
        assert len(used_slots)==item['referencedSlots']
        assert len(land_slots)==item['referencedLandSlots']
        checked.append({'id':fid,'archiveHashes':'PASS','members':len(res),'mpMetadata':'PASS','runtimeClassTable':'PASS','unrelatedMembers':'PASS','sectors':sectors,'allPagesReferenced':True,'classesRotationsAndOverlayPreserved':True})
    with Image.open(root/'preview.png') as im:assert im.width<=2048
    source=read(root/'memory-config.json');assert sha(Path(source['sourceMpr']))==source['sourceMprSha256']
    write(root/'verification.json',{'recipeRasterEdgeMaxRgbDifference':recipe_seams,'imageDimensions':'9 x 4096x4096; preview <=2048 wide','memoryFixtures':checked,'sourceMapUnchanged':True,'runtimeAcceptance':'NOT RUN'})
    print('PASS: 9 PNGs, exact recipe raster seams, 4 MPR/RES/MOB fixtures, original source hash')


if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();run(a.root.resolve())
