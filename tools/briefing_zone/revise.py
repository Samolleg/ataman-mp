"""Revision of a single private briefing-zone candidate, with auditable changes.

Inputs and reports are external. Unit IDs and scenario text identity are owned
by the original briefing MOB; only explicitly selected static ID leaves change.
"""
from pathlib import Path
import argparse,base64,collections,csv,hashlib,json,math,re,struct
from migrate import load,save,flatten,payload,decrypt_script,calls,plan
from finish import compose,report

ID_TYPES={0xb002,0xaa05,0xcc02,0xdd02}
def objects_with_ids(document):
    sections={s['path']:s for s in flatten(document['sections'])}
    for obj in document['objects']:
        for leaf in flatten([sections[obj['path']]]):
            if leaf['type'] in ID_TYPES and len(payload(leaf))==4:
                yield struct.unpack('<I',payload(leaf))[0],obj,leaf

def prepare(root,cli):
    root=Path(root).resolve();base=load(root/'inspect/basecam.mob.json');zone=load(root/'inspect/zone1.mob.json')
    audit=load(root/'reference-audit/native.json')['files']
    failures=[f['source'] for f in audit if f['kind']=='mob' and not f['strictParse']]
    if failures:raise ValueError(f'Incomplete reference audit: {failures}')
    plan(root,root/'landmarks.json',cli)
    draft=load(root/'plan-draft.json');old=list(objects_with_ids(base));new=list(objects_with_ids(zone))
    unit_ids={n for n,o,l in old if o['unit']};old_static={n:o for n,o,l in old if not o['unit']}
    occupied={n for n,o,l in old+new};references=[]
    for f in audit:
        occupied.update(f.get('objectIds',[]));occupied.update(q['id'] for q in f.get('references',[]));occupied.update(q['id'] for q in f.get('parentReferences',[]))
        for ref in f.get('references',[]):references.append(dict(ref,source=f['source'],member=f.get('member','')))
    changes=[];leaves=[];candidate=10000
    for n,obj,leaf in new:
        if obj['unit'] or n not in unit_ids:continue
        while candidate in occupied:candidate+=1
        if candidate>999999:raise ValueError('No free ID in the existing basecam declared range')
        changes.append(dict(oldId=n,newId=candidate,path=leaf['path'],objectPath=obj['path'],type=obj['sectionType'],name=obj.get('name',''),model=obj.get('model','')))
        leaves.append(dict(path=leaf['path'],sha256=leaf['sha256'],payload=base64.b64encode(struct.pack('<I',candidate)).decode()));occupied.add(candidate);candidate+=1
    assert len(changes)==22
    targets={c['oldId'] for c in changes}
    hits=[dict(q,scope='basecam retained unit' if Path(q['source']).name.lower()=='basecam.mob' else 'discarded zone1 scenario' if Path(q['source']).name.lower()=='zone1.mob' else 'different unchanged MOB scope') for q in references if q['id'] in targets]
    base_script=decrypt_script(base);lookup=[];computed=[]
    for token,args in calls(base_script):
        if token[0].lower() not in ('getobject','getobjectbyid'):continue
        value=args[0][0][0].strip('"\'') if len(args)==1 and len(args[0])==1 else ''
        try:n=int(value)
        except ValueError:computed.append(dict(line=base_script.count('\n',0,token[1])+1,call=token[0]));continue
        lookup.append(dict(id=n,line=base_script.count('\n',0,token[1])+1,oldStatic=n in old_static,retainedUnit=n in unit_ids))
    stale=[q for q in lookup if q['oldStatic']]
    if stale or computed:raise ValueError(f'Unresolved basecam ID references require mapping: {stale}, computed: {computed}')
    parent=[dict(path=s['path'],id=int.from_bytes(payload(s),'little')) for o in base['objects'] if o['unit'] or o['sectionType']=='0XBBAB0000'
            for s in flatten([next(t for t in flatten(base['sections']) if t['path']==o['path'])]) if s['type']==0xb012]
    assert not any(q['id'] in old_static for q in parent),'Map old static parent references explicitly'
    # Copied static records have no incoming ParentID links to the remapped IDs.
    static_paths={o['path'] for n,o,l in new if not o['unit']}
    assert not any(s['type']==0xb012 and int.from_bytes(payload(s),'little') in targets for o in zone['objects'] if o['path'] in static_paths for s in flatten([next(t for t in flatten(zone['sections']) if t['path']==o['path'])]))
    save(root/'id-remap.json',changes);draft['zoneLeaves']=leaves;save(root/'plan-draft.json',draft)
    save(root/'reference-audit/target-hits.json',hits)
    save(root/'reference-audit/basecam-references.json',dict(literalLookups=lookup,computedLookups=computed,oldStaticReferences=stale,parentFields=parent,unitLogicActionSections=[s['path'] for s in flatten(base['sections']) if s['type']==0xbbbd0002]))
    save(root/'reference-audit/summary.json',dict(mobFiles=sum(f['kind']=='mob' for f in audit),textSources=sum(f['kind']=='text' for f in audit),strictFailures=failures,
        allLiteralReferences=len(references),textLiteralReferences=sum(len(f.get('references',[])) for f in audit if f['kind']=='text'),
        basecamLiteralReferences=len(lookup),basecamOldStaticReferences=len(stale),basecamComputedLookups=len(computed),targetIdHits=len(hits),
        copiedStaticInboundParentLinks=0,basecamParentLinksToRemovedStatics=0,newIdsFreeAcrossAuditedObjectsAndLiteralReferences=True))
    compose(root,cli)
    # Classification must test the serialized coordinates, not unrounded math.
    rows=load(root/'rows.json');edits=load(root/'script-edits.json');leaf_edits={q['path']:base64.b64decode(q['payload']) for q in draft['leaves']}
    for row in rows:
        row['initialReason']=row['reason']
        if row['kind'] in ('unit','guard','patrol','look') and row['path'] in leaf_edits:row['new']=list(struct.unpack('<3f',leaf_edits[row['path']]))
        elif row['kind']=='script':row['new']=[float(e['new']) for e in edits if e['row']==row['index']]
    save(root/'rows.json',rows);requests(root,rows)

def requests(root,rows):
    for old in (False,True):
        key='old' if old else 'new';name='sample-old-request.json' if old else 'sample-request.json'
        save(root/name,dict(mpr=str(root/'in'/('basegipat.mpr' if old else 'zone1.mpr')),points=[dict(index=r['index'],x=r[key][0],y=r[key][1]) for r in rows if not r['inactive']]))

def adjust(root,cli):
    root=Path(root).resolve();old={q['index']:q for q in load(root/'compare-old.json')['points']};new={q['index']:q for q in load(root/'compare-new.json')['points']}
    rows=load(root/'rows.json');save(root/'passability-before-corrections.json',rows)
    draft=load(root/'plan-draft.json');edits=load(root/'script-edits.json');changes=[]
    for row in rows:
        if row['inactive']:continue
        a,b=old[row['index']],new[row['index']];was=a['inBounds'] and a['humanPassable'];now=b['inBounds'] and b['humanPassable']
        row['oldHumanPassable']=was;row['initialNewHumanPassable']=now
        row['passabilityClass']='passable' if now else 'blocked_before_and_after' if not was else 'newly_blocked'
        # Aim/effect points do not require a walkable destination. Still retain
        # their raw before/after classification for the owner's comparison.
        navigates=row['kind'] in ('unit','guard','patrol') or row.get('navigationTarget',False)
        row['requiresWalkablePosition']=navigates
        if was and not now and navigates and b.get('passableWithin2m'):
            before=row['new'][:];point=b['nearestPassable'];row['new'][:2]=point[:2]
            if row['kind']=='script' and len(row['new'])==3:row['new'][2]=point[2]
            row['adjustmentDistance']=b['projectionDistance'];assert row['adjustmentDistance']<=2.000001
            changes.append(dict(index=row['index'],kind=row['kind'],identity=row['identity'],calculated=before,corrected=row['new'][:],distance=b['projectionDistance']))
            if row['kind'] in ('unit','guard','patrol'):
                leaf=next(e for e in draft['leaves'] if e['path']==row['path']);leaf['payload']=base64.b64encode(struct.pack('<3f',*row['new'])).decode()
            else:
                for value,e in zip(row['new'],[e for e in edits if e['row']==row['index']]):e['new']=f'{value:.6f}'.rstrip('0').rstrip('.')
    save(root/'adjustments.json',changes);save(root/'plan-draft.json',draft);save(root/'script-edits.json',edits);save(root/'rows.json',rows)
    compose(root,cli);requests(root,rows)
    print(json.dumps(dict(newlyBlocked=sum(r.get('passabilityClass')=='newly_blocked' and r.get('requiresWalkablePosition') for r in rows),adjusted=len(changes),previouslyBlocked=sum(r.get('passabilityClass')=='blocked_before_and_after' and r.get('requiresWalkablePosition') for r in rows))))

def review(root,cli):
    root=Path(root).resolve();rows=load(root/'rows.json');summary=load(root/'summary.json');changes=load(root/'adjustments.json')
    final={q['index']:q for q in load(root/'navigation-review.json')};initial={q['index']:q for q in load(root/'compare-new.json')['points']}
    original={q['index']:q for q in load(root/'compare-old.json')['points']}
    assert summary['duplicateIds']==0 and summary['staticIdExceptions']==22
    assert summary['staticOnlyDeclaredIdChanges'] and summary['unitOnlyPlannedLeafEdits'] and summary['scriptCoordinateOnlyDiff']
    doc=load(root/'out/basecam.mob.json');assert not [q for q in doc['validation'] if q['severity']!='info']
    base=load(root/'inspect/basecam.mob.json');before=[n for n,o,l in objects_with_ids(base) if o['unit']];after=[n for n,o,l in objects_with_ids(doc) if o['unit']];assert before==after
    all_ids=collections.Counter(n for n,o,l in objects_with_ids(doc))
    for q in load(root/'reference-audit/basecam-references.json')['literalLookups']:assert all_ids[q['id']]==1 and q['id'] in before
    adjusted={q['index']:q for q in changes}
    for row in rows:
        if row['index'] in adjusted:assert final[row['index']]['humanPassable'] and row['adjustmentDistance']<=2.000001
        else:assert final[row['index']]['humanPassable']==initial[row['index']]['humanPassable'],'Classifier changed outside corrected points'
    def csv_file(name,values,fields):
        with (root/name).open('w',encoding='utf-8-sig',newline='') as stream:
            writer=csv.DictWriter(stream,fields,extrasaction='ignore');writer.writeheader()
            for row in values:writer.writerow({k:json.dumps(v,ensure_ascii=False) if isinstance(v,(dict,list)) else v for k,v in row.items()})
    csv_file('id-remap.csv',load(root/'id-remap.json'),['oldId','newId','name','model','type','objectPath','path'])
    csv_file('adjustments.csv',changes,['index','kind','identity','calculated','corrected','distance'])
    classifications=[]
    for row in rows:
        # This is exactly the predicate used for the original 79 flagged rows.
        if row['kind']=='catalog' or not row.get('navigationTarget',True) or initial[row['index']]['humanPassable']:continue
        was=original[row['index']]['humanPassable'];now=final[row['index']]['humanPassable']
        status='непроходима и раньше (норма)' if not was else 'цель взгляда: проход не требуется' if not row['requiresWalkablePosition'] else 'исправлена в пределах 2 м' if now else 'проверить: новой проходимой точки в пределах 2 м нет'
        classifications.append(dict(index=row['index'],kind=row['kind'],identity=row['identity'],old=row['old'],calculated=[initial[row['index']]['x'],initial[row['index']]['y']],new=row['new'],oldPassable=was,initialNewPassable=False,finalPassable=now,distance=row.get('adjustmentDistance',0),classification=status,reviewCsvLine=row.get('csvLine','')))
    assert len(classifications)==79
    csv_file('passability-comparison.csv',classifications,['index','kind','identity','old','calculated','new','oldPassable','initialNewPassable','finalPassable','distance','classification','reviewCsvLine'])
    doubtful=[row for row in rows if row['status']!='OK'];save(root/'markers.json',[dict(csvLine=row['csvLine'],index=row['index'],old=row['old'],new=row['new'],reason=row['reason']) for row in doubtful])
    # Repair the receipt from older tool revisions that saved this after edits.
    pre=load(root/'passability-before-corrections.json')
    for row in pre:
        if row['index'] in adjusted:row['new']=adjusted[row['index']]['calculated'];row.pop('adjustmentDistance',None)
    save(root/'passability-before-corrections.json',pre)
    catalog=(root/'catalog/map.txt').read_bytes().decode('latin1')
    source=base64.b64decode(load(root/'inspect/texts.res.json')['mapPayload']).decode('latin1')
    def mask(text):
        m=re.search(r'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',text);block=m.group()
        for tag in ('deploy','remove','restrict'):block=re.sub(r'(?m)(^#'+tag+r'\s*\r?\n)[^\r\n]+',lambda q:q[1]+'<coordinates>',block,count=1)
        return text[:m.start()]+block+text[m.end():]
    assert mask(catalog)==mask(source),'Catalog changed outside requested bz1g coordinates'
    assert not (root/'catalog/texts.res').exists(),'This revision must not write a texts archive'
    inv=load(root/'reference-audit/inventory.json');fallback=load(root/'reference-audit/fallback-inventory.json');extra=load(root/'reference-audit/fallback-native.json')
    assert not any(f.get('references') for f in extra['files'])
    refs=load(root/'reference-audit/summary.json')
    summary.update(dict(parserWarnings=0,unitIdsUnchanged=True,allBasecamIdLookupsResolveUniquely=True,oldStaticIdReferences=refs['basecamOldStaticReferences'],
        initialFlaggedRows=len(classifications),previouslyBlockedNormal=sum(not q['oldPassable'] for q in classifications),aimTargetNoWalkability=sum(q['classification'].startswith('цель') for q in classifications),
        genuinelyWorsened=sum(q['oldPassable'] and not q['classification'].startswith('цель') for q in classifications),correctedWithin2m=len(changes),
        unresolvedNewBlockages=sum(q['oldPassable'] and not q['finalPassable'] and not q['classification'].startswith('цель') for q in classifications),
        maxCorrectionDistance=max(q['distance'] for q in changes),reviewCsvRows=len(doubtful),archiveIndexesChecked=len(inv['archives'])+len(fallback['archives']),
        textualSourcesChecked=refs['textSources']+sum(a['textMembers'] for a in fallback['archives']),catalogOnlyRequestedCoordinatesChanged=True,textsArchiveWritten=False,
        destination='gz1g_bz1g 1'))
    save(root/'summary.json',summary);print(json.dumps(summary,ensure_ascii=False))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('mode',choices=('prepare','adjust','review'));p.add_argument('root');p.add_argument('--cli',required=True);a=p.parse_args();dict(prepare=prepare,adjust=adjust,review=review)[a.mode](a.root,a.cli)
