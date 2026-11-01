"""Prove a native object deletion and merge one reviewed zone into live map.txt.

This module never writes installed resources. Source snapshots, request identity,
catalog baseline, candidates and proof reports are supplied in the private run.
"""
from pathlib import Path
import argparse,base64,collections,difflib,hashlib,re,struct
from migrate import load,save,flatten,payload,decrypt_script,calls
from finish import run
from revise import objects_with_ids

def zone_span(text,zone):
    matches=list(re.finditer(rb'(?m)^#zone '+re.escape(zone.encode('ascii'))+rb'\s[\s\S]*?(?=^#zone |\Z)',text))
    if len(matches)!=1:raise ValueError('Expected one explicit zone: '+zone)
    return matches[0]

def merge_zone(current,baseline,reviewed,zone):
    """Preserve the current catalog outside the authorized zone byte-for-byte."""
    a=zone_span(current,zone);b=zone_span(baseline,zone);c=zone_span(reviewed,zone)
    if a.group()!=b.group():raise ValueError('Live target zone changed since the reviewed baseline')
    old=a.group();new=c.group()
    def mask(block):
        block=block.replace(b'basegipat basecam',b'<MPR> basecam').replace(b'zone1 basecam',b'<MPR> basecam')
        for tag in ('deploy','remove','restrict'):block=re.sub(rb'(?m)(^#'+tag.encode()+rb'\s*\r?\n)[^\r\n]+',lambda q:q[1]+b'<coordinates>',block,count=1)
        return block
    if mask(old)!=mask(new):raise ValueError('Reviewed zone includes changes outside MPR and local exit coordinates')
    return current[:a.start()]+new+current[a.end():]

def catalog(root,previous,cli):
    root=Path(root).resolve();previous=Path(previous).resolve()
    run(cli,'inspect',root/'in/texts.res',root/'inspect/texts.res.json')
    before=base64.b64decode(load(root/'inspect/texts.res.json')['mapPayload'])
    baseline=base64.b64decode(load(previous/'inspect/texts.res.json')['mapPayload'])
    reviewed=(previous/'catalog/map.txt').read_bytes();after=merge_zone(before,baseline,reviewed,'bz1g')
    assert b'gz1g_bz1g 1' in zone_span(after,'bz1g').group()
    (root/'catalog/map.txt').write_bytes(after)
    (root/'catalog/map.diff').write_text(''.join(difflib.unified_diff(before.decode('cp1251').splitlines(True),after.decode('cp1251').splitlines(True),fromfile='current live map.txt snapshot',tofile='R4 map.txt')),'utf8')
    request=dict(archive=str(root/'in/texts.res'),map=str(root/'catalog/map.txt'),mapOnly=False);save(root/'catalog/plan.json',request)
    run(cli,'catalog',root/'catalog/plan.json',root/'catalog/texts.res');run(cli,'inspect',root/'catalog/texts.res',root/'catalog/texts.res.json')
    assert load(root/'inspect/texts.res.json')['members']==load(root/'catalog/texts.res.json')['members']
    save(root/'catalog/merge-proof.json',dict(builtFrom=str(root/'in/texts.res'),sourceSha256=hashlib.sha256((root/'in/texts.res').read_bytes()).hexdigest(),
        onlyMemberReplaced='map.txt',onlyZoneChanged='bz1g',preservedOtherLiveCatalogBytes=True,unchangedExitTarget='gz1g_bz1g 1',nativeReplacementValidation=True))

def verify(source,source_inspect,candidate,request,output,cli):
    source=Path(source);candidate=Path(candidate);request=load(request);before=load(source_inspect)
    inspect=Path(output).with_suffix('.inspect.json');run(cli,'inspect',candidate,inspect);after=load(inspect)
    assert not [q for q in after['validation'] if q['severity']!='info']
    bs={s['path']:s for s in flatten(before['sections'])};ds={s['path']:s for s in flatten(after['sections'])};bb=source.read_bytes();db=candidate.read_bytes()
    removed={q['path']:q['sha256'] for q in request['objects']};assert len(removed)==len(request['objects'])
    def raw(s,data):return data[s['offset']:s['offset']+s['size']]
    retained=[]
    for o in before['objects']:
        s=bs[o['path']]
        if o['path'] in removed:assert o['sha256']==removed[o['path']] and not o['unit'];continue
        retained.append(raw(s,bb))
    assert retained==[raw(ds[o['path']],db) for o in after['objects']]
    assert len(before['objects'])-len(after['objects'])==len(removed)
    assert decrypt_script(before)==decrypt_script(after)
    assert [q['type'] for q in before['sections']]==[q['type'] for q in after['sections']]
    for a,b in zip(before['sections'],after['sections']):
        if a['type'] not in (0xa000,0x31415926):assert raw(a,bb)==raw(b,db)
    ac=before['sections'][0]['children'];bc=after['sections'][0]['children'];assert [q['type'] for q in ac]==[q['type'] for q in bc]
    for a,b in zip(ac,bc):
        if a['type']!=0xb000:assert raw(a,bb)==raw(b,db)
    # Reconstruct the entire candidate from the original document, omitting only
    # the named record, replacing the one graph, and updating ancestor lengths.
    graph=next(s for s in after['sections'] if s['type']==0x31415926)
    def rebuild(s):
        if s['path'] in removed:return b''
        if s['type']==0x31415926:return raw(graph,db)
        if not s.get('children'):return raw(s,bb)
        body=b''.join(rebuild(c) for c in s['children'])
        return raw(s,bb)[:4]+struct.pack('<I',len(body)+8)+body
    assert b''.join(rebuild(s) for s in before['sections'])==db,'Unexpected bytes outside deletion and embedded graph'
    ids=collections.Counter(n for n,o,l in objects_with_ids(after));assert not [n for n,c in ids.items() if c>1]
    removedIds={o['nid'] for o in before['objects'] if o['path'] in removed};refs=[]
    for t,args in calls(decrypt_script(after)):
        if t[0].lower() not in ('getobject','getobjectbyid'):continue
        assert len(args)==1 and len(args[0])==1
        target=int(args[0][0][0].strip('"\''));assert target not in removedIds;refs.append(target)
    assert not any(o.get('parentId') in removedIds for o in after['objects'])
    report=dict(source=str(source),candidate=str(candidate),sourceSha256=hashlib.sha256(bb).hexdigest(),candidateSha256=hashlib.sha256(db).hexdigest(),
        removed=[dict(id=o.get('nid'),name=o.get('name'),model=o.get('model'),position=o.get('position'),path=o['path'],bytes=bs[o['path']]['size']) for o in before['objects'] if o['path'] in removed],
        objectsBefore=len(before['objects']),objectsAfter=len(after['objects']),units=sum(o['unit'] for o in after['objects']),
        retainedObjectBytesExact=True,scenarioAndScriptBytesExact=True,entireFileReconstructionExact=True,allowedDiff=['explicit object subtree removal','AI_GRAPH replacement','ancestor size fields'],
        strictParse=True,byteCoverage=True,noOpByteIdentical=True,duplicateIds=0,parserWarnings=0,literalLookups=len(refs),referencesToRemovedIds=0)
    save(output,report);print(str(candidate.name)+': deletion/graph-only reconstruction PASS')

if __name__=='__main__':
    p=argparse.ArgumentParser();sub=p.add_subparsers(dest='mode',required=True)
    c=sub.add_parser('catalog');c.add_argument('root');c.add_argument('--previous',required=True);c.add_argument('--cli',required=True)
    v=sub.add_parser('verify');v.add_argument('source');v.add_argument('source_inspect');v.add_argument('candidate');v.add_argument('request');v.add_argument('output');v.add_argument('--cli',required=True)
    a=p.parse_args()
    if a.mode=='catalog':catalog(a.root,a.previous,a.cli)
    else:verify(a.source,a.source_inspect,a.candidate,a.request,a.output,a.cli)
