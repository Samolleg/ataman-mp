"""Plan a reviewed landmark migration from native briefing_zone inspect JSON.

Private map data and landmark correspondences are supplied externally. This file
contains no game data. Coordinates are recognized by call signatures, not by
replacing arbitrary numbers. Native CMobDocument performs the actual writes.
"""
from pathlib import Path
import argparse, base64, csv, hashlib, json, math, re, struct, subprocess, os

def flatten(sections):
    for s in sections:
        yield s
        yield from flatten(s.get('children', []))

def payload(s):
    return base64.b64decode(s.get('payload', ''))

def load(path):
    return json.loads(Path(path).read_text('utf-8-sig'))

def save(path, value):
    Path(path).write_text(json.dumps(value, ensure_ascii=False, indent=2), 'utf8')

def decrypt_script(document):
    raw=payload(next(s for s in flatten(document['sections']) if s['type']==0xacceeccb))
    key=int.from_bytes(raw[:4],'little');decoded=bytearray()
    for c in raw[4:]:
        t=((((key*13)<<4)+key)<<8)-key
        key=(key+(t<<2)+2531011)&0xffffffff
        decoded.append(c^((key>>16)&255))
    # latin1 preserves every byte, including an undefined CP1251 value, while
    # all recognized command names, delimiters and numeric tokens are ASCII.
    return decoded.decode('latin1')

def quaternion(o, sections):
    return struct.unpack('<4f', payload(next(s for s in flatten([sections[o['path']]]) if s['type'] == 0xb00a)))

def yaw(q):
    w, x, y, z = q
    return math.atan2(2*(w*z+x*y), 1-2*(y*y+z*z))

def tokens(text):
    # The original text and all source offsets are retained, including CRLF.
    pattern = r'''//[^\r\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*'|[A-Za-z_#][\w#-]*|[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?|[^\s]'''
    return [(m.group(), m.start(), m.end()) for m in re.finditer(pattern, text)
            if not m.group().startswith(('//', '/*'))]

def calls(text):
    ts = tokens(text)
    for i, t in enumerate(ts[:-1]):
        if not re.match(r'^[A-Za-z_#]', t[0]) or ts[i+1][0] != '(':
            continue
        args, depth, start = [], 0, i+2
        for j in range(i+2, len(ts)):
            v = ts[j][0]
            if v == ')' and depth == 0:
                args.append(ts[start:j]); yield t, args; break
            if v == ',' and depth == 0:
                args.append(ts[start:j]); start = j+1
            elif v == '(':
                depth += 1
            elif v == ')':
                depth -= 1

def number(arg):
    return len(arg) == 1 and bool(re.fullmatch(r'[-+]?(?:\d+\.?\d*|\.\d+)(?:[eE][-+]?\d+)?', arg[0][0]))

SIGNATURES = {'MoveToPoint': [(1,2)], 'RotateTo': [(1,2)],
              'UMSentry': [(1,2)], 'DistanceUnitPoint': [(1,2)],
              'SetCP': [(1,2,3)], 'SetCPFast': [(1,2,3)],
              'CreateParticleSource': [(1,2,3)], 'CreateFX': [(0,1,2)],
              '#DontBeIdiot': [(1,2),(3,4)]}

def plan(root, anchors_path, cli):
    root = Path(root).resolve();cli=str(Path(cli).resolve()); b = load(root/'inspect/basecam.mob.json'); z = load(root/'inspect/zone1.mob.json')
    bs = {s['path']:s for s in flatten(b['sections'])}; zs = {s['path']:s for s in flatten(z['sections'])}
    old = {o.get('nid'):o for o in b['objects'] if o.get('nid')}; new = {o.get('nid'):o for o in z['objects'] if o.get('nid')}
    anchors = []
    for v in load(anchors_path):
        a, c = old[v['old']], new[v['new']]
        aq, cq = quaternion(a,bs), quaternion(c,zs)
        delta = math.atan2(math.sin(yaw(cq)-yaw(aq)), math.cos(yaw(cq)-yaw(aq)))
        anchors.append(dict(v, oldPosition=a['position'], newPosition=c['position'],
                            oldModel=a.get('model'), newModel=c.get('model'),
                            rotationDelta=delta, rotationDegrees=math.degrees(delta)))
    save(root/'anchors.json', anchors)
    rows, edits, replacements = [], [], []
    def transform(x, y, force=None):
        candidates = [a for a in anchors if not force or a['label'] == force]
        anchor = min(candidates, key=lambda a:math.hypot(x-a['oldPosition'][0],y-a['oldPosition'][1]))
        ox,oy,_=anchor['oldPosition'];nx,ny,_=anchor['newPosition'];angle=anchor['rotationDelta']
        dx,dy=x-ox,y-oy
        return (nx+math.cos(angle)*dx-math.sin(angle)*dy,
                ny+math.sin(angle)*dx+math.cos(angle)*dy, anchor, math.hypot(dx,dy))
    def row(kind, identity, before, force=None, inactive=False, **details):
        if inactive:
            after,anchor,dist=before[:],None,0
        else:
            x,y,anchor,dist=transform(*before[:2],force)
            after=[x,y]+before[2:]
        r=dict(index=len(rows),kind=kind,identity=identity,old=before,new=after,
               anchor=anchor['label'] if anchor else 'inactive zero sentinel',
               anchorDistance=dist,rotationDegrees=anchor['rotationDegrees'] if anchor else 0,
               status='проверить' if dist>22 or anchor and anchor.get('review') else 'OK',
               reason='no nearby reliable landmark' if dist>22 else 'landmark correspondence needs review' if anchor and anchor.get('review') else '',inactive=inactive,**details)
        rows.append(r);return r,anchor
    for o in b['objects']:
        if not o['unit'] and o['sectionType'] != '0XBBAB0000':continue
        for s in flatten([bs[o['path']]]):
            if s['type'] not in (0xb009,0xbbbc0005,0xbbbd0001,0xbbbe0001):continue
            before=list(struct.unpack('<3f',payload(s)))
            kind={0xb009:'unit',0xbbbc0005:'guard',0xbbbd0001:'patrol',0xbbbe0001:'look'}[s['type']]
            r,a=row(kind,f"{o.get('name','')} [{o.get('nid')}] {s['path']}",before,
                    inactive=kind!='unit' and before==[0,0,0],path=s['path'],unitId=o.get('nid'),zMode='terrain-relative offset')
            if not r['inactive']:
                edits.append(dict(path=s['path'],sha256=s['sha256'],payload=base64.b64encode(struct.pack('<3f',*r['new'])).decode()))
                if kind=='unit' and abs(a['rotationDelta'])>1e-6:
                    rot=next(t for t in flatten([bs[o['path']]]) if t['type']==0xb00a)
                    w,x,y,zq=quaternion(o,bs);c=math.cos(a['rotationDelta']/2);sn=math.sin(a['rotationDelta']/2)
                    edits.append(dict(path=rot['path'],sha256=rot['sha256'],payload=base64.b64encode(struct.pack('<4f',c*w-sn*zq,c*x-sn*y,c*y+sn*x,c*zq+sn*w)).decode()))
    script=decrypt_script(b)
    for t,args in calls(script):
        for signature in SIGNATURES.get(t[0],[]):
            if max(signature)>=len(args):
                if re.search(r'\bScript\s*$',script[max(0,t[1]-20):t[1]]):continue
                raise ValueError(f'Bad arity {t}')
            if not all(number(args[i]) for i in signature):continue # symbolic parameters are migrated at their call sites
            before=[float(args[i][0][0]) for i in signature]
            r,a=row('script',f"line {script.count(chr(10),0,t[1])+1} {t[0]}",before,
                    function=t[0],sourceOffset=t[1],spans=[list(args[i][0][1:]) for i in signature],
                    zMode='absolute height, preserve altitude above old terrain' if len(before)==3 else 'XY only',
                    navigationTarget=t[0] not in ('RotateTo','CreateParticleSource','CreateFX'))
    # map.txt is a separate scenario resource: its local map coordinates must follow the gate too.
    text=base64.b64decode(load(root/'inspect/texts.res.json')['mapPayload']).decode('latin1');match=re.search(r'(?m)^#zone bz1g\s[\s\S]*?(?=^#zone )',text)
    if not match:raise ValueError('bz1g registration not found')
    block=match.group();catalog=[]
    for tag in ('deploy','remove'):
        m=re.search(r'(?m)^#'+tag+r'\s*\r?\n([^\r\n]+)',block)
        v=list(map(float,m.group(1).split()))
        if len(v)!=4:raise ValueError(f'Unsupported {tag} rectangle')
        for i in (0,2):
            r,a=row('catalog',f'bz1g exit 1 {tag} corner {i//2+1}',v[i:i+2],force='gate',tag=tag,corner=i//2)
            catalog.append(r['index'])
    # Camera restriction local XY is not the world-map #position (which stays unchanged).
    m=re.search(r'(?m)^#restrict\s*\r?\n([^\r\n]+)',block)
    if m:
        v=list(map(float,m.group(1).split()));r,_=row('catalog','bz1g camera restrict center',v[:2],radius=v[2]);catalog.append(r['index'])
    samples=[dict(index=r['index'],x=r['new'][0],y=r['new'][1]) for r in rows if not r['inactive']]
    old_samples=[dict(index=r['index'],x=r['old'][0],y=r['old'][1]) for r in rows if not r['inactive']]
    save(root/'sample-request.json',dict(mpr=str(root/'in/zone1.mpr'),points=samples))
    save(root/'sample-old-request.json',dict(mpr=str(root/'in/basegipat.mpr'),points=old_samples))
    for stem in ('sample','sample-old'):
        subprocess.run([cli,'sample',str(root/f'{stem}-request.json'),str(root/f'{stem}-result.json')],check=True)
    heights={int(q['index']):q for q in load(root/'sample-result.json')};oldheights={int(q['index']):q for q in load(root/'sample-old-result.json')}
    for r in rows:
        if r['inactive']:continue
        q=heights[r['index']];r['terrainZ']=q['terrainZ'];r['oldTerrainZ']=oldheights[r['index']]['terrainZ']
        if not q['available']:r['status']='проверить';r['reason']+='; outside terrain'
        if r['kind']=='script':
            if len(r['new'])==3:
                # Zero is a ground-placement convention used by SetCP in this MOB.
                # Effects have explicit altitude; retain their height above the terrain.
                if r['function'] not in ('SetCP','SetCPFast'):
                    r['new'][2]=q['terrainZ']+r['old'][2]-r['oldTerrainZ']
                else:
                    r['new'][2]=q['terrainZ'];r['zMode']='absolute destination on new terrain'
            for span,val in zip(r['spans'],r['new']):
                replacements.append(dict(start=span[0],end=span[1],old=script[span[0]:span[1]],new=f'{val:.6f}'.rstrip('0').rstrip('.'),row=r['index'],kind='coordinate'))
    save(root/'rows.json',rows);save(root/'script-edits.json',replacements)
    save(root/'plan-draft.json',dict(baseMob=str(root/'in/basecam.mob'),zoneMob=str(root/'in/zone1.mob'),zoneMpr=str(root/'in/zone1.mpr'),leaves=edits))
    print(json.dumps(dict(units=sum(r['kind']=='unit' for r in rows),scriptPoints=sum(r['kind']=='script' for r in rows),binaryPoints=sum(r['kind'] in ('guard','patrol','look') for r in rows),review=sum(r['status']!='OK' for r in rows))))

if __name__=='__main__':
    p=argparse.ArgumentParser();p.add_argument('root');p.add_argument('anchors');p.add_argument('--cli',required=True);a=p.parse_args();plan(a.root,a.anchors,a.cli)
