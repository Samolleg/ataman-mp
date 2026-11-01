"""Owner-reviewed rigid-group revision of an existing private candidate.

All identities, correspondences and inventory choices are external run data.
No editor behavior, installed resource, or previous candidate is changed.
"""
from pathlib import Path
import argparse,base64,collections,csv,difflib,hashlib,json,math,re,struct
from migrate import load,save,flatten,payload,decrypt_script,calls,quaternion
from finish import encrypt,run
from revise import objects_with_ids,requests

def rigid_pair(old,new):
    """Two landmarks define one rotation and translation, never scale/shear."""
    a,b=old;c,d=new
    if math.dist(a,b)<1e-6 or math.dist(c,d)<1e-6:raise ValueError('Coincident landmarks')
    angle=math.atan2(d[1]-c[1],d[0]-c[0])-math.atan2(b[1]-a[1],b[0]-a[0])
    angle=math.atan2(math.sin(angle),math.cos(angle));co,si=math.cos(angle),math.sin(angle)
    ox,oy=[(a[i]+b[i])/2 for i in (0,1)];nx,ny=[(c[i]+d[i])/2 for i in (0,1)]
    return dict(angle=angle,translation=[nx-co*ox+si*oy,ny-si*ox-co*oy],
                landmarkLengthMismatch=abs(math.dist(a,b)-math.dist(c,d)))

def transform(point,frame):
    x,y=point[:2];a=frame['angle'];tx,ty=frame['translation']
    return [math.cos(a)*x-math.sin(a)*y+tx,math.sin(a)*x+math.cos(a)*y+ty]+point[2:]

def csv_write(path,rows,fields):
    with Path(path).open('w',encoding='utf-8-sig',newline='') as f:
        w=csv.DictWriter(f,fields,extrasaction='ignore');w.writeheader()
        for row in rows:w.writerow({k:json.dumps(v,ensure_ascii=False) if isinstance(v,(dict,list)) else v for k,v in row.items()})

def removal_edits(source,rows,config):
    selected=[r for r in rows if r['index'] in config['removeRows']]
    assert len(selected)==len(config['removeRows'])
    changes=[];removed=[]
    for row in selected:
        assert row['kind']=='script' and row['function']=='CreateParticleSource'
        offset=row['sourceOffset'];start=source.rfind('\n',0,offset)+1;end=source.find('\n',offset)+1
        assert end>start
        line=source[start:end];assert re.fullmatch(r'\s*CreateParticleSource\s*\([^\r\n]*\)\s*',line)
        parsed=list(calls(line));assert len(parsed)==1 and parsed[0][0][0]=='CreateParticleSource'
        changes.append(dict(start=start,end=end,old=line,new='',kind='authorized removed effect line',row=row['index']))
        removed.append(dict(index=row['index'],originalLine=source.count('\n',0,start)+1,text=line.encode('latin1').decode('cp1251'),old=row['old']))
    return changes,removed

def group_heights(root,cli,rows,leaves,sections):
    group=[r for r in rows if r.get('rigidGroup')]
    for key,name in [('old','basegipat.mpr'),('new','zone1.mpr')]:
        save(root/f'group-{key}-samples-request.json',dict(mpr=str(root/'in'/name),points=[dict(index=r['index'],x=r[key][0],y=r[key][1]) for r in group]))
        run(cli,'sample',root/f'group-{key}-samples-request.json',root/f'group-{key}-samples.json')
    old={q['index']:q for q in load(root/'group-old-samples.json')};new={q['index']:q for q in load(root/'group-new-samples.json')};changes=[]
    for row in group:
        a,b=old[row['index']],new[row['index']];assert a['available'] and b['available'];row['oldTerrainZ']=a['terrainZ'];row['terrainZ']=b['terrainZ']
        if row['kind']=='unit' or row['old'][2]==0:continue # entity offset / ground placeholder
        row['new'][2]=struct.unpack('<f',struct.pack('<f',b['terrainZ']+row['old'][2]-a['terrainZ']))[0]
        row['zMode']='absolute logic Z resampled; source surface-relative difference preserved'
        s=sections[row['path']];leaves[s['path']]=dict(path=s['path'],sha256=s['sha256'],payload=base64.b64encode(struct.pack('<3f',*row['new'])).decode())
        changes.append(dict(index=row['index'],old=row['old'][2],oldTerrain=a['terrainZ'],new=row['new'][2],newTerrain=b['terrainZ']))
    save(root/'group-heights.json',changes)

def prepare(root,previous,config_path,cli):
    root=Path(root).resolve();previous=Path(previous).resolve();config=load(config_path)
    b=load(root/'inspect/basecam.mob.json');z=load(root/'inspect/zone1.mob.json')
    bs={s['path']:s for s in flatten(b['sections'])};zs={s['path']:s for s in flatten(z['sections'])}
    bo={o.get('nid'):o for o in b['objects']};zo={o.get('nid'):o for o in z['objects']}
    pairs=config['landmarks'];old=[bo[q['old']]['position'][:2] for q in pairs];new=[zo[q['new']]['position'][:2] for q in pairs]
    frame=rigid_pair(old,new);assert frame['landmarkLengthMismatch']<config.get('maxLandmarkMismatch',.01)
    frame['landmarks']=[dict(q,oldPosition=old[i],newPosition=new[i],residual=math.dist(transform(old[i],frame),new[i])) for i,q in enumerate(pairs)]
    frame['degrees']=math.degrees(frame['angle']);save(root/'rigid-frame.json',frame)
    rows=load(previous/'rows.json');save(root/'prior-review.json',[r for r in rows if r.get('csvLine')]);draft=load(previous/'plan-draft.json');edits=load(previous/'script-edits.json')
    for k,n in [('baseMob','basecam.mob'),('zoneMob','zone1.mob'),('zoneMpr','zone1.mpr')]:draft[k]=str(root/'in'/n)
    draft['sharedMpr']=True;leaves={e['path']:e for e in draft['leaves']};group=set(config['unitIds'])
    assert group=={o['nid'] for o in b['objects'] if o['unit'] and o['nid'] in group}
    def replace(leaf,value):leaves[leaf['path']]=dict(path=leaf['path'],sha256=leaf['sha256'],payload=base64.b64encode(value).decode())
    for row in rows:
        if row.get('unitId') not in group:continue
        row['new']=list(struct.unpack('<3f',struct.pack('<3f',*transform(row['old'],frame))))
        row.update(anchor='rigid training group: retained house and fence',rotationDegrees=frame['degrees'],initialReason='',reason='',status='OK',rigidGroup=True)
        for key in ('adjustmentDistance','passabilityNote','passabilityClass','initialNewHumanPassable'):row.pop(key,None)
        replace(bs[row['path']],struct.pack('<3f',*row['new']))
    for unitId in group:
        o=bo[unitId];s=next(s for s in flatten([bs[o['path']]]) if s['type']==0xb00a)
        w,x,y,zq=quaternion(o,bs);co=math.cos(frame['angle']/2);si=math.sin(frame['angle']/2)
        replace(s,struct.pack('<4f',co*w-si*zq,co*x-si*y,co*y+si*x,co*zq+si*w))
    group_heights(root,cli,rows,leaves,bs)
    # Only carry an explicitly reviewed absent inventory item. Existing matches
    # stay exact source subtrees, including their original IDs.
    inventory=[];retained=[]
    for item in config['inventory']:
        o=bo[item['old']];assert not o['unit'];calculated=transform(o['position'],frame)
        if item.get('new') is not None:
            n=zo[item['new']];assert not n['unit'] and o.get('model')==n.get('model')
            assert math.dist(calculated[:2],n['position'][:2])<.05
            inventory.append(dict(item,action='existing zone1 object retained; no duplicate copied',oldPosition=o['position'],newPosition=n['position'],rigidResidual=math.dist(calculated[:2],n['position'][:2])))
        else:
            assert item.get('freeId') is not None
            assert item['freeId'] not in {n for n,o,l in objects_with_ids(b)}|{n for n,o,l in objects_with_ids(z)}
            retained.append(o['path'])
            for s in flatten([bs[o['path']]]):
                if s['type']==0xb002:replace(s,struct.pack('<I',item['freeId']))
                elif s['type']==0xb009:replace(s,struct.pack('<3f',*calculated))
                elif s['type']==0xb00a:
                    w,x,y,zq=quaternion(o,bs);co=math.cos(frame['angle']/2);si=math.sin(frame['angle']/2)
                    replace(s,struct.pack('<4f',co*w-si*zq,co*x-si*y,co*y+si*x,co*zq+si*w))
            inventory.append(dict(item,action='carried missing basecam inventory',newPosition=calculated))
    draft['retainedStaticPaths']=retained;draft['leaves']=list(leaves.values())
    source=decrypt_script(b);deletions,removed=removal_edits(source,rows,config)
    edits=[e for e in edits if e['row'] not in config['removeRows']]+deletions
    changed=source
    for e in sorted(edits,key=lambda e:e['start'],reverse=True):
        assert changed[e['start']:e['end']]==e['old'];changed=changed[:e['start']]+e['new']+changed[e['end']:]
    script=next(s for s in flatten(b['sections']) if s['type']==0xacceeccb)
    draft['leaves'].append(dict(path=script['path'],sha256=script['sha256'],payload=base64.b64encode(encrypt(changed.encode('latin1'),int.from_bytes(payload(script)[:4],'little'))).decode()))
    save(root/'plan.json',draft);save(root/'script-edits.json',edits);save(root/'removed-effects.json',removed);save(root/'inventory.json',inventory)
    rows=[r for r in rows if r['index'] not in config['removeRows']];save(root/'rows.json',rows);requests(root,rows)
    run(cli,'compose',root/'plan.json',root/'out')
    assert not (root/'out/basegipat.mpr').exists()
    (root/'script-before.cp1251').write_bytes(source.encode('latin1'));(root/'script-after.cp1251').write_bytes(changed.encode('latin1'))
    (root/'script.diff').write_text(''.join(difflib.unified_diff(source.encode('latin1').decode('cp1251').splitlines(True),changed.encode('latin1').decode('cp1251').splitlines(True),fromfile='original basecam',tofile='R3 basecam')),'utf8')
    original=base64.b64decode(load(root/'inspect/texts.res.json')['mapPayload']);catalog=(previous/'catalog/map.txt').read_bytes()
    match=re.search(br'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',catalog);block=match.group();assert block.count(b'basegipat basecam')==1
    catalog=catalog[:match.start()]+block.replace(b'basegipat basecam',b'zone1 basecam')+catalog[match.end():]
    assert b'basegipat' not in re.search(br'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',catalog).group()
    (root/'catalog/map.txt').write_bytes(catalog)
    (root/'catalog/map.diff').write_text(''.join(difflib.unified_diff(original.decode('cp1251').splitlines(True),catalog.decode('cp1251').splitlines(True),fromfile='original map.txt',tofile='R3 map.txt')),'utf8')
    req=dict(archive=str(root/'in/texts.res'),map=str(root/'catalog/map.txt'),mapOnly=True);save(root/'catalog/plan.json',req)
    run(cli,'catalog',root/'catalog/plan.json',root/'catalog/validation.json')
    req['mapOnly']=False;save(root/'catalog/archive-plan.json',req);run(cli,'catalog',root/'catalog/archive-plan.json',root/'catalog/texts.res')
    save(root/'config.json',config)
    focus=config['cameraFocus'];save(root/'scene.json',dict(mpr=str(root/'in/zone1.mpr'),catalog=str(root/'catalog/map.txt'),
        squadOld=dict(focus=focus,angle=-45,distance=60),squadNew=dict(focus=transform(focus,frame),angle=-45,distance=60)))
    print(json.dumps(dict(groupUnits=len(group),groupPoints=sum(r.get('rigidGroup',False) for r in rows),frame=frame,removedEffects=len(removed),inventory=inventory),ensure_ascii=False))

def heights(root,cli):
    """Upgrade already composed logic heights without replacing its final graph."""
    root=Path(root).resolve();plan=load(root/'plan.json');rows=load(root/'rows.json');base=load(root/'inspect/basecam.mob.json');sections={s['path']:s for s in flatten(base['sections'])}
    leaves={e['path']:e for e in plan['leaves']};group_heights(root,cli,rows,leaves,sections)
    before=load(root/'out/basecam.mob.json');actual={s['path']:s for s in flatten(before['sections'])};changed=[]
    for e in leaves.values():
        if e['path'] in actual and payload(actual[e['path']])!=base64.b64decode(e['payload']):changed.append(dict(e,sha256=actual[e['path']]['sha256']))
    save(root/'height-patch.json',dict(mob=str(root/'out/basecam.mob'),leaves=changed));run(cli,'patch',root/'height-patch.json',root/'out/basecam.mob')
    plan['leaves']=list(leaves.values());save(root/'plan.json',plan);save(root/'rows.json',rows)

def report(root,cli):
    root=Path(root).resolve();config=load(root/'config.json');b=load(root/'inspect/basecam.mob.json');z=load(root/'inspect/zone1.mob.json')
    run(cli,'inspect',root/'out/basecam.mob',root/'out/basecam.mob.json');d=load(root/'out/basecam.mob.json')
    assert not [q for q in d['validation'] if q['severity']!='info']
    plan=load(root/'plan.json');bs={s['path']:s for s in flatten(b['sections'])};zs={s['path']:s for s in flatten(z['sections'])};ds={s['path']:s for s in flatten(d['sections'])}
    bb=(root/'in/basecam.mob').read_bytes();zb=(root/'in/zone1.mob').read_bytes();db=(root/'out/basecam.mob').read_bytes()
    def raw(s,data):return data[s['offset']:s['offset']+s['size']]
    zoneEdits={q['path']:base64.b64decode(q['payload']) for q in plan['zoneLeaves']};expected=[]
    for obj in z['objects']:
        if obj['unit'] or obj['sectionType']=='0XBBAB0000':continue
        s=zs[obj['path']];v=bytearray(raw(s,zb))
        for leaf in flatten([s]):
            if leaf['path'] in zoneEdits:
                assert leaf['type'] in (0xb002,0xaa05,0xcc02,0xdd02) and len(zoneEdits[leaf['path']])==4
                begin=leaf['offset']-s['offset']+8;v[begin:begin+4]=zoneEdits[leaf['path']]
        expected.append(bytes(v))
    statics=[o for o in d['objects'] if not o['unit'] and o['sectionType']!='0XBBAB0000']
    assert expected==[raw(ds[o['path']],db) for o in statics[:len(expected)]]
    assert len(statics)==len(expected)+len(plan['retainedStaticPaths'])
    edits={q['path']:base64.b64decode(q['payload']) for q in plan['leaves']}
    units=[o for o in b['objects'] if o['unit']];finalUnits=[o for o in d['objects'] if o['unit']];assert len(units)==len(finalUnits)==51
    for o,n in zip(units,finalUnits):
        assert o['nid']==n['nid'];s=bs[o['path']];v=bytearray(raw(s,bb))
        for leaf in flatten([s]):
            if leaf['path'] not in edits:continue
            assert leaf['type'] in (0xb009,0xb00a,0xbbbc0005,0xbbbd0001,0xbbbe0001)
            begin=leaf['offset']-s['offset']+8;v[begin:begin+len(edits[leaf['path']])]=edits[leaf['path']]
        assert bytes(v)==raw(ds[n['path']],db)
    # Everything besides objects/script/explicit graph remains exactly authored.
    assert [s['type'] for s in b['sections']]==[s['type'] for s in d['sections']]
    for a,c in zip(b['sections'],d['sections']):
        if a['type'] not in (0xa000,0x31415926):assert raw(a,bb)==raw(c,db)
    assert len(b['sections'][0]['children'])==len(d['sections'][0]['children'])
    for a,c in zip(b['sections'][0]['children'],d['sections'][0]['children']):
        assert a['type']==c['type']
        if a['type'] not in (0xb000,0xacceeccb):assert raw(a,bb)==raw(c,db)
    source=decrypt_script(b);changed=source;scriptEdits=load(root/'script-edits.json')
    for e in sorted(scriptEdits,key=lambda e:e['start'],reverse=True):
        assert changed[e['start']:e['end']]==e['old'];changed=changed[:e['start']]+e['new']+changed[e['end']:]
    assert decrypt_script(d)==changed
    assert sum(e['kind']=='authorized removed effect line' for e in scriptEdits)==len(config['removeRows'])
    ids=collections.Counter(n for n,o,l in objects_with_ids(d));assert not [n for n,c in ids.items() if c>1]
    refs=[]
    for token,args in calls(changed):
        if token[0].lower() not in ('getobject','getobjectbyid'):continue
        assert len(args)==1 and len(args[0])==1
        n=int(args[0][0][0].strip('"\''));assert ids[n]==1;refs.append(n)
    oldStatic={n for n,o,l in objects_with_ids(b) if not o['unit']};assert not oldStatic.intersection(refs)
    rows=load(root/'rows.json');nav={q['index']:q for q in load(root/'navigation-review.json')};old={q['index']:q for q in load(root/'compare-old.json')['points']}
    drawn={int(q['id']):q['draw'] for q in load(root/'render-new.json')['units']};review=[];comparison=[]
    for row in rows:
        if row.get('rigidGroup'):row.pop('passabilityNote',None)
        if row['kind'] in ('unit','guard','patrol','look'):row['new']=list(struct.unpack('<3f',payload(ds[row['path']])))
        q=nav[row['index']];a=old[row['index']];was=a['inBounds'] and a['humanPassable'];now=q['inBounds'] and q['humanPassable']
        row['oldHumanPassable']=was;row['humanPassable']=now
        walk=row['kind'] in ('unit','guard','patrol') or row.get('navigationTarget',False)
        reasons=[]
        if row['kind']=='unit':row['displayPosition']=drawn[row['unitId']]
        if not q['inBounds']:reasons.append('outside map')
        if walk and was and not now:reasons.append('newly blocked; rigid group cannot be projected independently' if row.get('rigidGroup') else 'newly blocked after transfer')
        if walk and q.get('waterDepth',0)>.1:reasons.append('water')
        row['reason']='; '.join(reasons);row['status']='проверить' if reasons else 'OK'
        if not was and not now:row['passabilityNote']='blocked before and after: preserved source placement'
        if reasons:review.append(row)
        comparison.append(dict(index=row['index'],kind=row['kind'],identity=row['identity'],old=row['old'],new=row['new'],oldPassable=was,finalPassable=now,requiresWalkable=walk,rigidGroup=row.get('rigidGroup',False),status=row['status'],reason=row['reason']))
    # Preserve the previous reviewed numbering; reserve 2..43 even after findings
    # are resolved/removed, so the owner's image references never shift.
    used={r['csvLine'] for r in review if r.get('csvLine')};nextLine=44
    for row in review:
        if not row.get('csvLine'):
            while nextLine in used:nextLine+=1
            row['csvLine']=nextLine;used.add(nextLine)
    save(root/'rows.json',rows);save(root/'review-rows.json',review)
    fields=['csvLine','index','kind','identity','old','new','anchor','rotationDegrees','rigidGroup','oldTerrainZ','terrainZ','zMode','oldHumanPassable','humanPassable','displayPosition','passabilityNote','status','reason']
    csv_write(root/'transfer.csv',rows,fields);csv_write(root/'review-open.csv',review,fields)
    prior=load(root/'prior-review.json');current={r['index']:r for r in rows};numbered={}
    for p in prior:
        line=p['csvLine']
        if p['index'] in current:entry=dict(current[p['index']],csvLine=line)
        else:entry=dict(p,status='удалено',reason='authorized effect call removed in R3',new=[])
        numbered[line]=entry
    for row in review:numbered[row['csvLine']]=row
    assert sorted(numbered)==list(range(2,max(numbered,default=1)+1)),'Physical review.csv numbering must remain contiguous'
    csv_write(root/'review.csv',[numbered[n] for n in sorted(numbered)],fields)
    csv_write(root/'passability-comparison.csv',comparison,['index','kind','identity','old','new','oldPassable','finalPassable','requiresWalkable','rigidGroup','status','reason'])
    csv_write(root/'inventory.csv',load(root/'inventory.json'),['old','new','model','label','action','oldPosition','newPosition','rigidResidual'])
    csv_write(root/'removed-effects.csv',load(root/'removed-effects.json'),['index','originalLine','text','old'])
    save(root/'markers.json',[dict(csvLine=r['csvLine'],index=r['index'],old=r['old'],new=r['new'],reason=r['reason']) for r in review])
    group=[r for r in rows if r.get('rigidGroup')];errors=[];distances=[]
    for i,a in enumerate(group):
        for c in group[i+1:]:
            oldDistance=math.dist(a['old'][:2],c['old'][:2]);newDistance=math.dist(a['new'][:2],c['new'][:2]);error=abs(oldDistance-newDistance);errors.append(error)
            distances.append(dict(rowA=a['index'],rowB=c['index'],oldDistanceXY=oldDistance,newDistanceXY=newDistance,absoluteError=error))
    csv_write(root/'group-pairwise.csv',distances,['rowA','rowB','oldDistanceXY','newDistanceXY','absoluteError'])
    maxError=max(errors,default=0);assert maxError<.00005
    summary=dict(units=51,staticRecords=len(statics),staticIdExceptions=len(zoneEdits),staticOtherBytesExact=True,unitNonCoordinateBytesExact=True,otherScenarioBytesExact=True,
        scriptDiffOnlyDeclaredCoordinateTokensAndRemovedLines=True,removedEffects=len(config['removeRows']),remainingCoordinateTokens=sum(e['kind']=='coordinate' for e in scriptEdits),
        groupUnits=len(config['unitIds']),groupPoints=len(group),pairwiseComparisons=len(errors),maxPairwiseDistanceDeviationXY=maxError,duplicateIds=0,parserWarnings=0,
        literalReferences=len(refs),oldStaticReferences=0,reviewRows=len(review),newlyBlockedWalking=sum(q['requiresWalkable'] and q['oldPassable'] and not q['finalPassable'] for q in comparison),
        carriedInventory=len(plan['retainedStaticPaths']),existingInventory=len(load(root/'inventory.json')),sharedMpr='zone1.mpr',mob='basecam.mob')
    save(root/'summary.json',summary);print(json.dumps(summary,ensure_ascii=False))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=('prepare','heights','report'));p.add_argument('root');p.add_argument('--previous');p.add_argument('--config');p.add_argument('--cli',required=True);a=p.parse_args()
    if a.mode=='prepare':prepare(a.root,a.previous,a.config,a.cli)
    elif a.mode=='heights':heights(a.root,a.cli)
    else:report(a.root,a.cli)
