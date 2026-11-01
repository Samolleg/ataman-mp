"""Materialize the reviewed plan and prove the requested byte boundaries.

Run `compose` after planning; after native rendering/graph regeneration run
`report`. All private input, output, tables and receipts stay in the run root.
"""
from pathlib import Path
import argparse,base64,collections,csv,difflib,hashlib,json,math,re,struct,subprocess
from migrate import load,save,flatten,payload,decrypt_script

def run(cli,*args):
    subprocess.run([str(Path(cli).resolve()),*map(str,args)],check=True)

def encrypt(plain,key):
    data=bytearray(struct.pack('<I',key))
    for c in plain:
        t=((((key*13)<<4)+key)<<8)-key;key=(key+(t<<2)+2531011)&0xffffffff
        data.append(c^((key>>16)&255))
    return bytes(data)

def compose(root,cli):
    root=Path(root).resolve();out=root/'out';out.mkdir(exist_ok=True)
    b=load(root/'inspect/basecam.mob.json');plan=load(root/'plan-draft.json');edits=load(root/'script-edits.json');rows=load(root/'rows.json')
    source=decrypt_script(b);changed=source
    for e in sorted(edits,key=lambda e:e['start'],reverse=True):
        assert changed[e['start']:e['end']]==e['old']
        changed=changed[:e['start']]+e['new']+changed[e['end']:]
    script=next(s for s in flatten(b['sections']) if s['type']==0xacceeccb)
    raw=encrypt(changed.encode('latin1'),int.from_bytes(payload(script)[:4],'little'))
    plan['leaves'].append(dict(path=script['path'],sha256=script['sha256'],payload=base64.b64encode(raw).decode()))
    save(root/'plan.json',plan);run(cli,'compose',root/'plan.json',out)
    # Ground-truth plaintext byte diff, independent of QString decoding.
    (root/'script-before.cp1251').write_bytes(source.encode('latin1'));(root/'script-after.cp1251').write_bytes(changed.encode('latin1'))
    (root/'script-coordinate.diff').write_text(''.join(difflib.unified_diff(source.encode('latin1').decode('cp1251',errors='replace').splitlines(True),changed.encode('latin1').decode('cp1251',errors='replace').splitlines(True),fromfile='basecam original',tofile='basecam candidate')),'utf8')
    text=base64.b64decode(load(root/'inspect/texts.res.json')['mapPayload']).decode('latin1')
    m=re.search(r'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',text);block=m.group();new=block
    for tag in ('deploy','remove'):
        corners=[r for r in rows if r['kind']=='catalog' and r.get('tag')==tag]
        # The gate rotates slightly, so use the transformed quadrilateral's
        # axis-aligned bounds required by map.txt's rectangle representation.
        # Reconstruct all four corners from the same rigid gate transform.
        values=list(map(float,re.search(r'(?m)^#'+tag+r'\s*\r?\n([^\r\n]+)',block).group(1).split()))
        anchor=next(a for a in load(root/'anchors.json') if a['label']=='gate')
        ox,oy,_=anchor['oldPosition'];nx,ny,_=anchor['newPosition'];a=anchor['rotationDelta'];pts=[]
        for x,y in ((values[0],values[1]),(values[0],values[3]),(values[2],values[1]),(values[2],values[3])):
            dx,dy=x-ox,y-oy;pts.append((nx+math.cos(a)*dx-math.sin(a)*dy,ny+math.sin(a)*dx+math.cos(a)*dy))
        v=[min(x for x,y in pts),min(y for x,y in pts),max(x for x,y in pts),max(y for x,y in pts)]
        new=re.sub(r'(?m)(^#'+tag+r'\s*\r?\n)[^\r\n]+',lambda q:q[1]+' '.join(f'{f:.6f}' for f in v),new,count=1)
        for r in corners:r['writtenRectangle']=v
    center=next(r for r in rows if r['identity']=='bz1g camera restrict center')
    new=re.sub(r'(?m)(^#restrict\s*\r?\n)[^\r\n]+',lambda q:q[1]+f"{center['new'][0]:.6f} {center['new'][1]:.6f} {center['radius']:g}",new,count=1)
    new=re.sub(r'(?m)(^#exit 1\s*\r?\n)[^\r\n]+',lambda q:q[1]+'gz1g 2',new,count=1)
    candidate=text[:m.start()]+new+text[m.end():]
    catalog=root/'catalog';catalog.mkdir(exist_ok=True);(catalog/'map.txt').write_bytes(candidate.encode('latin1'))
    (catalog/'map.diff').write_text(''.join(difflib.unified_diff(text.splitlines(True),candidate.splitlines(True),fromfile='original map.txt',tofile='candidate map.txt')),'utf8')
    save(catalog/'plan.json',dict(archive=str(root/'in/texts.res'),map=str(catalog/'map.txt')))
    run(cli,'catalog',catalog/'plan.json',catalog/'texts.res')
    save(root/'rows.json',rows)
    print('Composed work candidate, all original IDs retained. No live publication.')

def report(root,cli):
    root=Path(root).resolve();b=load(root/'inspect/basecam.mob.json');z=load(root/'inspect/zone1.mob.json');rows=load(root/'rows.json')
    for name in ('basecam.mob','basegipat.mpr'):
        run(cli,'inspect',root/'out'/name,root/'out'/(name+'.json'))
    result=load(root/'out/basecam.mob.json');rsecs={s['path']:s for s in flatten(result['sections'])}
    units=[o for o in b['objects'] if o['unit']];static=[o for o in z['objects'] if not o['unit'] and o['sectionType']!='0XBBAB0000']
    bs={s['path']:s for s in flatten(b['sections'])};zs={s['path']:s for s in flatten(z['sections'])}
    bbytes=(root/'in/basecam.mob').read_bytes();zbytes=(root/'in/zone1.mob').read_bytes();obytes=(root/'out/basecam.mob').read_bytes()
    def raw(s,data):return data[s['offset']:s['offset']+s['size']]
    source_static=[raw(zs[o['path']],zbytes) for o in static]
    actual_static=[raw(rsecs[o['path']],obytes) for o in result['objects'] if not o['unit'] and o['sectionType']!='0XBBAB0000']
    assert source_static==actual_static,'static subtree byte mismatch'
    assert len([o for o in result['objects'] if o['unit']])==len(units),'unit count mismatch'
    planned={e['path']:base64.b64decode(e['payload']) for e in load(root/'plan.json')['leaves']}
    for old,new in zip(units,[o for o in result['objects'] if o['unit']]):
        source_section=bs[old['path']];expected=bytearray(raw(source_section,bbytes))
        for s in flatten([source_section]):
            if s['path'] not in planned:continue
            replacement=planned[s['path']];assert len(replacement)==s['size']-8
            begin=s['offset']-source_section['offset']+8;expected[begin:begin+len(replacement)]=replacement
        assert bytes(expected)==raw(rsecs[new['path']],obytes),'unit non-coordinate fields changed'
    # Root scenario blocks must be identical except the encrypted script and
    # object sequence. AI_GRAPH is independently and explicitly regenerated.
    before_root=b['sections'][0]['children'];after_root=result['sections'][0]['children']
    assert [s['type'] for s in b['sections']]==[s['type'] for s in result['sections']]
    for s,t in zip(b['sections'],result['sections']):
        if s['type'] not in (0xa000,0x31415926):assert raw(s,bbytes)==raw(t,obytes)
    assert len(before_root)==len(after_root)
    for s,t in zip(before_root,after_root):
        assert s['type']==t['type']
        if s['type'] not in (0xb000,0xacceeccb):assert raw(s,bbytes)==raw(t,obytes),s['path']
    source=decrypt_script(b);final=decrypt_script(result);expected=source;edits=load(root/'script-edits.json')
    for e in sorted(edits,key=lambda e:e['start'],reverse=True):
        assert expected[e['start']:e['end']]==e['old'];expected=expected[:e['start']]+e['new']+expected[e['end']:]
    assert final==expected,'non-coordinate script mutation'
    assert (root/'in/zone1.mpr').read_bytes()==(root/'out/basegipat.mpr').read_bytes(),'MPR not a full exact copy'
    ids=collections.defaultdict(list)
    # Include IDs from auxiliary light/sound/particle records as parsed leaves.
    id_types={0xb002,0xaa05,0xcc02,0xdd02}
    for o in result['objects']:
        for s in flatten([rsecs[o['path']]]):
            if s['type'] in id_types and len(payload(s))==4:ids[struct.unpack('<I',payload(s))[0]].append(o)
    duplicates={k:v for k,v in ids.items() if len(v)>1}
    nav={q['index']:q for q in load(root/'navigation-review.json')}
    drawn={int(q['id']):q['draw'] for q in load(root/'render-new.json')['units']}
    catalog_text=(root/'catalog/map.txt').read_bytes().decode('latin1')
    catalog_block=re.search(r'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',catalog_text).group()
    for r in rows:
        # The table records the actual serialized values, including float32
        # rounding in MOB fields and the literal decimal tokens in the script.
        if r['kind'] in ('unit','guard','patrol','look'):
            r['new']=list(struct.unpack('<3f',payload(rsecs[r['path']])))
        elif r['kind']=='script':
            r['new']=[float(e['new']) for e in edits if e['row']==r['index']]
        elif r['kind']=='catalog':
            tag=r.get('tag','restrict');v=list(map(float,re.search(r'(?m)^#'+tag+r'\s*\r?\n([^\r\n]+)',catalog_block).group(1).split()))
            start=r.get('corner',0)*2;r['new']=v[start:start+2]
            if tag!='restrict':r['writtenRectangle']=v
        if r['kind']=='unit':r['displayPosition']=drawn[r['unitId']]
        reasons=[r['reason']] if r['reason'] else []
        if r['kind']=='unit' and r['unitId'] in duplicates:reasons.append('ID also used by another object; original IDs preserved')
        if not r['inactive']:
            q=nav[r['index']];r['waterDepth']=q.get('waterDepth',0);r['humanPassable']=q.get('humanPassable',False)
            if not q['inBounds']:reasons.append('outside map')
            elif r['kind'] not in ('catalog',) and r.get('navigationTarget',True):
                if q.get('waterDepth',0)>0.1:reasons.append('water')
                if not q.get('humanPassable'):reasons.append('blocked in native human passability')
        r['reason']='; '.join(reasons);r['status']='проверить' if reasons else 'OK'
    save(root/'rows.json',rows)
    fields=['index','kind','identity','old','anchor','anchorDistance','rotationDegrees','new','terrainZ','oldTerrainZ','displayPosition','zMode','writtenRectangle','status','reason','waterDepth','humanPassable']
    for name,values in [('transfer.csv',rows),('review.csv',[r for r in rows if r['status']!='OK'])]:
        with (root/name).open('w',encoding='utf-8-sig',newline='') as f:
            writer=csv.DictWriter(f,fields,extrasaction='ignore');writer.writeheader()
            for r in values:writer.writerow({k:json.dumps(v,ensure_ascii=False) if isinstance(v,(list,dict)) else v for k,v in r.items()})
    conflicts=[dict(id=k,objects=[dict(path=o['path'],name=o.get('name'),unit=o['unit']) for o in v]) for k,v in sorted(duplicates.items())]
    save(root/'id-conflicts.json',conflicts)
    summary=dict(units=len(units),nonlivingObjects=len(static),zoneUnitsExcluded=sum(o['unit'] for o in z['objects']),
                 scriptPoints=sum(r['kind']=='script' for r in rows),scriptNumericCoordinates=len(edits),
                 binaryLogicPoints=sum(r['kind'] in ('guard','patrol','look') for r in rows),
                 inactiveZeroPoints=sum(r['inactive'] for r in rows),reviewRows=sum(r['status']!='OK' for r in rows),
                 reviewUnits=sum(r['kind']=='unit' and r['status']!='OK' for r in rows),
                 reviewScriptPoints=sum(r['kind']=='script' and r['status']!='OK' for r in rows),
                 duplicateIds=len(duplicates),scriptCoordinateOnlyDiff=True,staticByteIdentical=True,scenarioBlocksByteIdentical=True,mprByteIdentical=True,
                 unitOnlyPlannedLeafEdits=True,
                 nativeNoOpByteIdentical=result['noOpByteIdentical'],nativeScriptNoOpByteIdentical=result['scriptNoOpByteIdentical'],
                 scriptDefinitions=len(re.findall(r'(?m)^Script\s+',source)),addMobCopied=False,
                 sha256={n:hashlib.sha256((root/'out'/n).read_bytes()).hexdigest() for n in ('basecam.mob','basegipat.mpr')})
    save(root/'summary.json',summary);print(json.dumps(summary,ensure_ascii=False))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=('compose','report'));p.add_argument('root');p.add_argument('--cli',required=True);a=p.parse_args();(compose if a.mode=='compose' else report)(a.root,a.cli)
