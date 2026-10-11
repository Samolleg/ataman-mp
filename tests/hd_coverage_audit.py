"""Read-only EI-HD-COVERAGE-AUDIT. Private snapshots; never writes game files.

Run with the project's existing Python runtime. No network or installs.
"""
from __future__ import annotations

import argparse
import base64
from collections import Counter, defaultdict
from datetime import datetime, timezone
import hashlib
import io
import json
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parents[1]
PRIVATE = ROOT / "build-tests-hd-coverage"
CORNERS = ('northWest', 'northEast', 'southEast', 'southWest')


def now():
    return datetime.now(timezone.utc).isoformat()


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


def write_json(path, value):
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def snapshot(source, dest):
    for attempt in range(1, 11):
        start = now()
        before = sha(source)
        shutil.copyfile(source, dest)
        copied = sha(dest)
        after = sha(source)
        if before == copied == after:
            return dict(source=str(source), copy=str(dest), sha256=copied,
                        beforeSha256=before, afterSha256=after,
                        size=dest.stat().st_size, startedUtc=start, finishedUtc=now(),
                        sourceMtimeNs=source.stat().st_mtime_ns, attempts=attempt)
        print(f'CHANGED: {source}; waiting 60 seconds', flush=True)
        time.sleep(60)
    raise RuntimeError(f'No consistent snapshot after 10 attempts: {source}')


def res_index(stream):
    stream.seek(0, 2)
    length = stream.tell()
    stream.seek(0)
    magic, count, table, names_size = struct.unpack('<4I', stream.read(16))
    if magic != 0x019CE23C or table + count * 22 + names_size > length:
        raise ValueError('Invalid RES header/table bounds')
    stream.seek(table)
    records = stream.read(count * 22)
    names = stream.read(names_size)
    result = {}
    for i in range(count):
        _, size, offset, _, nlen, noff = struct.unpack_from('<iIIIHI', records, i * 22)
        if noff + nlen > len(names) or offset + size > length:
            raise ValueError('Invalid RES member bounds')
        name = names[noff:noff+nlen].decode('cp1251')
        if name.lower() in result:
            raise ValueError('Duplicate RES member: ' + name)
        result[name.lower()] = dict(name=name, size=size, offset=offset)
    return result


def member(stream, record):
    stream.seek(record['offset'])
    data = stream.read(record['size'])
    if len(data) != record['size']:
        raise ValueError('Truncated member')
    return data


def read_mpr(path, group, map_root):
    for attempt in range(10):
        start = now()
        data = path.read_bytes()
        digest = hashlib.sha256(data).hexdigest()
        if sha(path) == digest:
            break
        print(f'CHANGED MPR {path}; waiting 60 seconds', flush=True)
        time.sleep(60)
    else:
        raise RuntimeError('MPR remains unstable: ' + str(path))
    f = io.BytesIO(data)
    entries = res_index(f)
    mp = [r for r in entries.values() if r['name'].lower().endswith('.mp')]
    if len(mp) != 1:
        raise ValueError(f'{path}: expected one MP, got {len(mp)}')
    payload = member(f, mp[0])
    if len(payload) < 38 or struct.unpack_from('<I', payload)[0] != 0xCE4AF672:
        raise ValueError('Invalid MP header: ' + str(path))
    textures, texture_size, tiles, tile_size = struct.unpack_from('<4I', payload, 16)
    materials, animations = struct.unpack_from('<HI', payload, 32)
    offset = 38 + materials * 44
    if offset + tiles * 4 + animations * 4 > len(payload):
        raise ValueError('Truncated MP arrays')
    types = list(struct.unpack_from(f'<{tiles}i', payload, offset))
    frames = []
    for n in range(animations):
        first, count = struct.unpack_from('<HH', payload, offset + tiles * 4 + n * 4)
        frames.extend(range(first, first + count))
    return dict(group=group, file=str(path.relative_to(map_root)), path=str(path),
                sha256=digest, size=len(data), readUtc=start, verifiedUtc=now(),
                namespace=mp[0]['name'][:-3], textureCount=textures,
                textureSize=texture_size, tileCount=tiles, tileSize=tile_size,
                runtimeTypes=types, animatedSlots=sorted(set(frames)))


def capture(game):
    PRIVATE.mkdir(exist_ok=True)
    run = PRIVATE / datetime.now(timezone.utc).strftime('snapshot-%Y%m%dT%H%M%S%fZ')
    run.mkdir()
    sources = dict(dejavu_hd=game/'MODS/Dejavu/res/HDPack_Terrain.res',
                   jamaisvu_hd=game/'MODS/JamaisVu135/res/HDPack_Terrain.res',
                   jamaisvu_sd=game/'MODS/JamaisVu135/res/textures.res',
                   base_sd=game/'Base/Res/textures.res')
    manifest = dict(startedUtc=now(), packs={}, maps=[],
                    head=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip())
    for label, source in sources.items():
        manifest['packs'][label] = snapshot(source, run/(label+'.res'))
        write_json(run/'manifest.json', manifest)
        print('SNAPSHOT', label, manifest['packs'][label]['sha256'], flush=True)
    capture_maps(game, run, manifest)
    return run


def capture_maps(game, run, manifest):
    manifest['maps'] = []
    for group, map_root in [('Dejavu', game/'MODS/Dejavu/Maps'), ('Base', game/'Base/Maps')]:
        paths = sorted(map_root.rglob('*.mpr'))
        if not paths:
            raise ValueError('No maps in ' + str(map_root))
        for path in paths:
            manifest['maps'].append(read_mpr(path, group, map_root))
        if paths != sorted(map_root.rglob('*.mpr')):
            raise RuntimeError('MPR file list changed during capture')
    manifest['finishedUtc'] = now()
    write_json(run/'manifest.json', manifest)
    print('CAPTURE', run, flush=True)


def pack_inventory(path):
    groups = defaultdict(dict)
    profiles = defaultdict(list)
    other = []
    with path.open('rb') as f:
        entries = res_index(f)
        for name, rec in entries.items():
            match = re.fullmatch(r'(.*?)(\d{3})\.mmp', name)
            if match:
                f.seek(rec['offset'])
                magic, width, height, _, fmt = struct.unpack('<5I', f.read(20))
                if magic != 0x00504d4d:
                    raise ValueError('Invalid MMP: ' + name)
                groups[match[1]][int(match[2])] = dict(rec, width=width, height=height, format=fmt)
            else:
                other.append(rec)
                if name.startswith('ei_atlas_profile_') and name.endswith('.json'):
                    profile = json.loads(member(f, rec))
                    profiles[profile['mprNamespace'].lower()].append(dict(profile, memberName=rec['name']))
    return dict(groups=groups, profiles=profiles, other=other, entries=len(entries))


def decode_dxt1_current(payload):
    """RGB from existing preset tool; alpha from current mmp_codec.cpp:169."""
    sys.path.insert(0, str(ROOT/'scripts'))
    from generate_terrain_atlas_presets import decode_dxt1
    import numpy as np
    atlas = decode_dxt1(payload)
    width, height = atlas.shape[1], atlas.shape[0]
    blocks = np.frombuffer(payload, dtype='<u4', count=(width//4)*(height//4)*2, offset=76).reshape(-1,2)
    c0, c1 = blocks[:,0] & 65535, blocks[:,0] >> 16
    selectors = (blocks[:,1,None] >> (2*np.arange(16,dtype=np.uint32))) & 3
    alpha = np.where((c0[:,None]<=c1[:,None]) & (selectors==3),0,255).astype(np.uint8)
    atlas[:,:,3] = alpha.reshape(height//4,width//4,4,4).transpose(0,2,1,3).reshape(height,width)
    return atlas


def decode_namespace(path, pages, prefix_slots):
    import numpy as np
    if sorted(pages) != list(range(len(pages))):
        raise ValueError('Atlas pages are not contiguous')
    total = len(pages) * 64
    digests = {n: hashlib.sha256(f'{n}\n'.encode('ascii')) for n in {total, prefix_slots}}
    tiles = []
    with path.open('rb') as f:
        for page, record in sorted(pages.items()):
            payload = member(f, record)
            # The older preset generator forces alpha=255. Current production
            # decodeDxtImage (mmp_codec.cpp:169) preserves DXT1 transparency.
            atlas = np.flipud(decode_dxt1_current(payload))
            if atlas.shape[0] != atlas.shape[1] or atlas.shape[0] % 8:
                raise ValueError('Unexpected atlas shape')
            size = atlas.shape[0] // 8
            for row in range(8):
                for column in range(8):
                    tile = np.flipud(atlas[row*size:(row+1)*size, column*size:(column+1)*size])
                    raw = tile.tobytes()
                    tagged = f'{size}x{size}\n'.encode('ascii') + raw
                    for n, digest in digests.items():
                        if len(tiles) < n:
                            digest.update(tagged)
                    tiles.append(dict(slot=len(tiles), size=size,
                                      opaque=bool(np.all(tile[:,:,3]==255)),
                                      pixelSha256=hashlib.sha256(tagged).hexdigest()))
    return tiles, {n: h.hexdigest() for n, h in digests.items()}


def preset_profile(preset):
    raw = base64.b64decode(preset['signaturesBase64'], validate=True)
    if len(raw) != preset['slotCount'] * 4:
        raise ValueError('Invalid preset signatures')
    return dict(families=preset['families'], slots=[dict(slot=i, assigned=True,
                signature=dict(zip(CORNERS, raw[4*i:4*i+4]))) for i in range(preset['slotCount'])])


def classify(tiles, profile, namespace, runtime_map=None):
    families = {f['id']: f for f in profile['families']}
    definitions = {s['slot']: s for s in profile['slots']}
    rows = []
    for tile in tiles:
        slot = tile['slot']
        definition = definitions.get(slot, {})
        assigned = definition.get('assigned', not definition.get('ambiguous', False))
        signature = [definition.get('signature', {}).get(k, 0) for k in CORNERS]
        known = assigned and not definition.get('ambiguous', False) and all(n in families for n in signature)
        uniform = known and len(set(signature)) == 1
        family = families[signature[0]] if uniform else {}
        runtime = runtime_map['runtimeTypes'][slot] if runtime_map and slot < runtime_map['tileCount'] else None
        rows.append(dict(tile, namespace=namespace, kind='uniform' if uniform else 'transition' if known else 'unknown',
                         unknownReason=('ambiguous' if definition.get('ambiguous') else 'unassigned' if definition and not assigned else 'no-definition') if not known else None,
                         material=family.get('name'), reviewedClass=family.get('tileType'), physicalClass=runtime,
                         layers=family.get('allowedLayers', [family.get('layer', 'land')]),
                         selectable=family.get('paintSelectable', True),
                         animated=(slot in runtime_map['animatedSlots']) if runtime_map else None,
                         sourceNamespace=definition.get('sourceNamespace'), sourceSlots=definition.get('sourceSlots'),
                         sourcePixelSha256=definition.get('sourcePixelSha256')))
    return rows


def analyze(run):
    manifest = json.loads((run/'manifest.json').read_text(encoding='utf-8'))
    inventories = {}
    for label, record in manifest['packs'].items():
        path = Path(record['copy'])
        if not path.resolve().is_relative_to(PRIVATE.resolve()):
            raise ValueError('Analysis accepts private copies only')
        if sha(path) != record['sha256']:
            raise ValueError('Snapshot hash mismatch: ' + label)
        inventories[label] = pack_inventory(path)
    preset_path = ROOT/'assets/resources/terrain_atlas_presets.json'
    presets = json.loads(preset_path.read_text(encoding='utf-8'))['presets']
    presets = {p['namespace'].lower(): p for p in presets}
    base_maps = {m['namespace'].lower(): m for m in manifest['maps'] if m['group'] == 'Base'}
    dj_maps = {m['namespace'].lower(): m for m in manifest['maps'] if m['group'] == 'Dejavu'}
    rows = dict(jamaisvu=[], dejavu=[])
    bindings = []
    for ns, preset in sorted(presets.items()):
        for label, dest in [('jamaisvu_hd', 'jamaisvu'), ('dejavu_hd', 'dejavu')]:
            inv = inventories[label]
            pages = inv['groups'].get(ns)
            if not pages:
                raise ValueError('Missing stock namespace: ' + label + '/' + ns)
            tiles, hashes = decode_namespace(Path(manifest['packs'][label]['copy']), pages, preset['slotCount'])
            matching = [p for p in inv['profiles'][ns] if p.get('approved') and
                        p.get('atlasSlotCount') == len(tiles) and p['atlasPixelSha256'] == hashes[len(tiles)]]
            approved = {b['pixelSha256'] for b in preset['bindings']}
            prefix_ok = hashes[preset['slotCount']] in approved
            if len(matching) > 1:
                raise ValueError('Ambiguous exact profiles: ' + ns)
            if matching:
                profile, authority = matching[0], matching[0]['memberName']
            elif prefix_ok:
                profile, authority = preset_profile(preset), 'built-in preset (verified stock prefix)'
            else:
                # Stale embedded profiles are evidence gaps, never classification
                # authority. Preserve every physical slot as explicitly unknown.
                profile, authority = dict(families=[], slots=[]), 'UNRESOLVED: no exact semantic binding'
            runtime_map = base_maps.get(ns) if dest == 'jamaisvu' or prefix_ok else None
            # A current MP may describe an extended stock atlas. Keep physical
            # classes from that exact namespace instead of guessing extra slots.
            if dest == 'dejavu' and ns in dj_maps:
                runtime_map = dj_maps[ns]
            classified = classify(tiles, profile, ns, runtime_map)
            if dest == 'jamaisvu' and len(tiles) != preset['slotCount']:
                raise ValueError('Unexpected stock source size')
            rows[dest].extend(classified)
            bindings.append(dict(pack=label, namespace=ns, slots=len(tiles),
                                 hash=hashes[len(tiles)], stockPrefixMatch=prefix_ok, authority=authority,
                                 unknown=sum(x['kind']=='unknown' for x in classified)))
            write_json(run/'classification-progress.json', dict(rows=rows, bindings=bindings))
        print('CLASSIFIED', ns, flush=True)
    # Exact stock origin: namespace/slot only when the complete original prefix
    # matched an approved binding. A repacked namespace uses profile provenance.
    jmv_by_slot = {(r['namespace'], r['slot']): r for r in rows['jamaisvu']}
    prefix_matches = {b['namespace'] for b in bindings if b['pack']=='dejavu_hd' and b['stockPrefixMatch']}
    hd_materials = {(r['material'],r['reviewedClass']) for r in rows['jamaisvu'] if r['kind']=='uniform' and r['size']==256}
    for row in rows['dejavu']:
        candidates = []
        if row['namespace'] in prefix_matches:
            candidates.append(jmv_by_slot.get((row['namespace'], row['slot'])))
        if row['sourceNamespace'] and row['sourceSlots']:
            candidates.extend(jmv_by_slot.get((row['sourceNamespace'].lower(), slot)) for slot in row['sourceSlots'])
        candidates = [r for r in candidates if r and r['kind']==row['kind'] and
                      (row['kind']!='uniform' or r['material']==row['material'])]
        row['hdOriginal'] = any(r['size']==256 for r in candidates)
        row['hdOriginalEvidence'] = 'verified-stock-prefix/profile-provenance' if candidates else 'unresolved'
        row['sameMaterialHdElsewhere'] = (row['material'],row['reviewedClass']) in hd_materials if row['kind']=='uniform' else False
    refs = []
    for ns, pages in sorted(inventories['dejavu_hd']['groups'].items()):
        maps = [m for m in manifest['maps'] if m['namespace'].lower()==ns]
        names = {g: [m['file'] for m in maps if m['group']==g] for g in ['Dejavu','Base']}
        counts, sizes = Counter(), Counter()
        for page, record in pages.items():
            users = {m['group'] for m in maps if page < m['textureCount']}
            category = 'Dejavu' if 'Dejavu' in users else 'BaseOnly' if 'Base' in users else 'Unreferenced'
            counts[category] += 1
            sizes[category] += record['size']
        refs.append(dict(namespace=ns, kind='stock' if ns in presets else 'published',
                         pages=len(pages), bytes=sum(r['size'] for r in pages.values()),
                         maps=names, pageCounts=dict(counts), pageBytes=dict(sizes)))
    missing = []
    for m in manifest['maps']:
        pages = inventories['dejavu_hd']['groups'].get(m['namespace'].lower(), {})
        for page in range(m['textureCount']):
            if page not in pages:
                missing.append(dict(map=m['group']+'/'+m['file'], namespace=m['namespace'], page=page))
    result = dict(manifest=manifest, presetSha256=sha(preset_path), bindings=bindings,
                  stockCatalog=[dict(namespace=ns, **f) for ns,p in presets.items() for f in p['families']],
                  rows=rows, references=refs, missingPages=missing,
                  otherMembers=inventories['dejavu_hd']['other'],
                  sdSource256Pages={ns: [page for page,rec in inventories['jamaisvu_sd']['groups'].get(ns,{}).items()
                                        if rec['width']==2048 and rec['height']==2048]
                                    for ns in sorted({r['namespace'] for r in rows['jamaisvu'] if r['size']==128})})
    validate_audit(result)
    write_json(run/'audit.json', result)
    print('AUDIT', run/'audit.json', flush=True)
    return result


def material_key(row):
    return (row['material'], row['reviewedClass'], ','.join(sorted(row['layers'])))


def variants(rows, size, static=False):
    return len({r['pixelSha256'] for r in rows if r['size']==size and
                (not static or (r['animated'] is False and r['opaque']))})


def validate_audit(audit):
    """Conservation checks independent of report formatting."""
    for label, rows in audit['rows'].items():
        pack = 'dejavu_hd' if label=='dejavu' else 'jamaisvu_hd'
        bindings = [b for b in audit['bindings'] if b['pack']==pack]
        assert len(rows) == sum(b['slots'] for b in bindings)
        assert len({(r['namespace'],r['slot']) for r in rows}) == len(rows)
        assert all(r['size'] in [128,256] for r in rows)
        assert all(r['reviewedClass'] in range(16) for r in rows if r['kind']=='uniform')
        assert all(r['physicalClass'] is None or r['physicalClass'] in range(16) for r in rows)
    refs = audit['references']
    assert len({r['namespace'] for r in refs}) == len(refs)
    for r in refs:
        assert sum(r['pageBytes'].values()) == r['bytes']
        assert sum(r['pageCounts'].values()) == r['pages']
    assert sum(r['bytes'] for r in refs)+sum(r['size'] for r in audit['otherMembers']) <= audit['manifest']['packs']['dejavu_hd']['size']
    for rec in audit['manifest']['packs'].values():
        assert rec['beforeSha256'] == rec['sha256'] == rec['afterSha256']
    print('AUDIT CONSERVATION PASS', flush=True)


def self_test():
    # Actual RES layout, CP1251 and bounds; no files or live data required.
    name = 'ZoneX000.mmp'.encode('cp1251')
    blob = struct.pack('<4I',0x019CE23C,1,19,len(name))+b'abc'+struct.pack('<iIIIHI',-1,3,16,0,len(name),0)+name
    f = io.BytesIO(blob)
    idx = res_index(f)
    assert member(f,idx['zonex000.mmp']) == b'abc'
    try:
        res_index(io.BytesIO(blob[:-1]))
        raise AssertionError('Truncation was accepted')
    except ValueError:
        pass
    header = struct.pack('<5I',0x00504d4d,4,4,1,0x31545844)+bytes(56)
    transparent = decode_dxt1_current(header+struct.pack('<HHI',0,0,0xffffffff))
    assert not transparent.any(), 'DXT1 transparent code must stay RGBA zero'
    red = decode_dxt1_current(header+struct.pack('<HHI',0xf800,0,0))
    assert tuple(red[0,0]) == (255,0,0,255)
    p = dict(families=[dict(id=1,name='A',tileType=0,allowedLayers=['land']),
                      dict(id=2,name='B',tileType=1,allowedLayers=['land'])],
             slots=[dict(slot=0,signature=dict(zip(CORNERS,[1]*4))),
                    dict(slot=1,signature=dict(zip(CORNERS,[1,1,2,2]))),
                    dict(slot=2,ambiguous=True,assigned=True,signature=dict(zip(CORNERS,[1]*4)))])
    tiles=[dict(slot=i,size=256,pixelSha256='same',opaque=True) for i in range(4)]
    rows=classify(tiles,p,'test')
    assert [r['kind'] for r in rows] == ['uniform','transition','unknown','unknown']
    assert variants([rows[0],rows[0]],256)==1, 'Duplicates/rotations must not multiply artwork'
    assert variants([rows[0]],256,True)==0, 'Unknown animation status must not certify static eligibility'
    print('SELF TEST PASS: RES bounds, DXT1 alpha/RGB, uniform/transition/ambiguous/unknown, dedup, animation uncertainty')


def render_report(run, audit, output):
    output = output.resolve()
    if output.suffix.lower() != '.md' or not any(output.is_relative_to(p.resolve()) for p in [ROOT/'docs/validation', PRIVATE]):
        raise ValueError('Report output must be Markdown inside docs/validation or the private audit directory')
    m = audit['manifest']
    validate_audit(audit)
    dj = audit['rows']['dejavu']
    jmv = audit['rows']['jamaisvu']
    bank = {label: defaultdict(list) for label in ['dejavu', 'jamaisvu']}
    universe = set()
    for f in audit['stockCatalog']:
        universe.add((f['name'], f['tileType'], ','.join(sorted(f['allowedLayers']))))
    for label, rows in audit['rows'].items():
        for r in rows:
            if r['kind']=='uniform':
                bank[label][material_key(r)].append(r)
                universe.add(material_key(r))
    totals = Counter()
    ns_totals = Counter()
    for ref in audit['references']:
        totals.update(ref['pageBytes'])
        category = 'Dejavu' if ref['maps']['Dejavu'] else 'BaseOnly' if ref['maps']['Base'] else 'Unreferenced'
        ns_totals[category] += ref['bytes']
    def hd_stats(label, static=False):
        counts = [variants(bank[label][key], 256, static) for key in sorted(universe)]
        return [sum(n>=threshold for n in counts) for threshold in [1,2,4]], sum(n==0 for n in counts)
    def table(headers, rows):
        lines.append('| '+' | '.join(headers)+' |')
        lines.append('|'+'|'.join(['---']*len(headers))+'|')
        for row in rows:
            lines.append('| '+' | '.join(str(v).replace('|','/').replace('\n',' ') for v in row)+' |')
        lines.append('')
    def names(rows):
        return ', '.join(rows) or '—'
    def fmt(value):
        return f'{value/2**20:.3f}'
    lines = ['# EI-HD-COVERAGE-AUDIT — однородные тайлы и страницы HDPack', '',
             'Дата: 2026-10-11. Аудит только для чтения; версия программы не менялась, коммит не создавался.', '',
             '## Краткий ответ', '']
    for label, title in [('dejavu','Текущий stock-банк Dejavu'), ('jamaisvu','Исходный stock-банк JamaisVu135')]:
        thresholds, no_hd = hd_stats(label)
        static, _ = hd_stats(label, True)
        lines.append(f'- {title}: из {len(universe)} комбинаций «материал / проверенный класс / слой» '
                     f'≥1 / ≥2 / ≥4 различных HD-изображения имеют **{thresholds[0]} / {thresholds[1]} / {thresholds[2]}**; '
                     f'у **{no_hd}** HD-изображений не найдено. Если исключить анимационные кадры и неизвестный статус анимации: '
                     f'**{static[0]} / {static[1]} / {static[2]}** (консервативная оценка статических вариантов).')
    source_keys = {material_key(r) for r in jmv if r['kind']=='uniform'}
    current_keys = {material_key(r) for r in dj if r['kind']=='uniform'}
    source_no_hd = [key for key in sorted(source_keys) if not variants(bank['jamaisvu'][key],256)]
    lines += ['', f'Для вопроса именно об однородных материалах: исходный банк содержит **{len(source_keys)}** '
              f'комбинаций материал/класс/слой с uniform, остальные **{len(universe-source_keys)}** представлены '
              'только переходами/углами. Среди исходных uniform без HD: '+
              '; '.join(f'**{k[0]}, класс {k[1]}, {k[2]}**' for k in source_no_hd)+'. '
              f'В текущих точных профилях Dejavu подтверждено **{len(current_keys)}** uniform-комбинаций. '
              'Разные классы одного имени не объединяются (например, Stone / Desert класса 2 имеет HD).', '',
              'Исходные uniform-комбинации, не подтверждённые как uniform в текущих профилях Dejavu: '+
              '; '.join(f'{k[0]} [класс {k[1]}, {k[2]}]' for k in sorted(source_keys-current_keys))+'. '
              'Это разница подтверждённой семантики банков, а не доказательство отсутствия похожих пикселей.', '']
    lines += ['', ('**Ответ «все материалы уже имеют несколько однородных HD-вариантов» не подтверждается.** '
                   if any(variants(bank['dejavu'][k],256)<2 for k in source_keys) else
                   '**Каждая исходная uniform-комбинация имеет минимум два подтверждённых HD-изображения в Dejavu.** ')+
              'Точные отсутствующие материалы и материалы только с угловыми/переходными сигнатурами перечислены ниже. '
              'Семантические варианты, runtime-классы и анимационные фазы не взаимозаменяемы.', '',
              f'Страницы Dejavu HDPack: карты Dejavu ссылаются на **{fmt(totals["Dejavu"])} МиБ**, '
              f'только Base — на **{fmt(totals["BaseOnly"])} МиБ**, ни одна из проверенных карт — на '
              f'**{fmt(totals["Unreferenced"])} МиБ**. Категории взаимоисключающие, при общей ссылке приоритет у Dejavu.', '',
              f'Если целиком относить namespace к одной категории: Dejavu **{fmt(ns_totals["Dejavu"])} МиБ**, '
              f'только Base **{fmt(ns_totals["BaseOnly"])} МиБ**, без ссылок **{fmt(ns_totals["Unreferenced"])} МиБ**. '
              f'Разница **{fmt(totals["Unreferenced"]-ns_totals["Unreferenced"])} МиБ** — страницы сверх '
              'textureCount внутри используемых namespace.', '',
              '## Метод и границы', '',
              '- Декодирование согласовано с текущим `source/resources/mmp_codec.cpp:169`: DXT1 сохраняет alpha. '
              'Старый `scripts/generate_terrain_atlas_presets.py` принудительно ставит alpha=255; его RGB-декодер '
              'переиспользован с восстановлением корректной alpha. После этого все 76 привязок (38×2) подтверждены. '
              'Первоначальное подозрение на неверные SHA профилей снято; это была разница декодеров.',
              '- Использован существующий признак uniform: назначенная, не ambiguous сигнатура с четырьмя одинаковыми материалами. '
              'Код: `source/render/terrain_atlas_profile.cpp:1511`, `source/documents/terrain_mixed_edge.cpp:11`, '
              '`source/documents/terrain_edit_model.cpp:53`. Встроенные семейства: `assets/resources/terrain_atlas_presets.json`; '
              'глобальная группировка: `source/render/terrain_atlas_presets.cpp:117`.',
              '- Класс в таблицах материалов — проверенный `family.tileType` (0..15). Физический класс взят отдельно '
              'из MP соответствующего namespace: для исходного stock — Base, для текущего Dejavu — текущий MP, '
              'либо Base только при подтверждённом неизменном stock-префиксе. Они могут различаться: mixed-edge 139 '
              'явно различает physical source class и reviewed output-cell class (`terrain_mixed_edge.cpp:156`). '
              'Неизвестные физические классы обозначены `?`, а не подставлены из имени материала.',
              '- Вариант в таблицах материалов — уникальный SHA декодированных RGBA-пикселей вместе с размером. '
              'Точные дубликаты в пределах строки и повороты не увеличивают число. Для сводки одинаковые '
              'изображения из разных namespace также объединены. Совпадение после поворота и визуальное сходство '
              'не проверялись; оригинальность рисунка и художественное качество не оценивались.',
              '- Колонки 256/128 означают физический размер тайла (страница 2048/1024 с сеткой 8×8), '
              'а не имя HDPack. 128 — сохранённые в 128 px варианты; это не утверждение, что материал целиком SD. '
              'Анимационные кадры входят в полное число изображений и вынесены отдельно. Для 139 требуется '
              'неанимированный opaque источник того же физического класса; наличие в этой таблице само по себе '
              'не обещает доступность всех вариантов в каждой конкретной карте.',
              '- Семантика применяется только после точного пиксельного SHA всего атласа или исходного stock-префикса. '
              'Вложенный профиль с неверным SHA не используется. Исходный bank JamaisVu135 не подменяется '
              'расширенными/перепакованными слотами Dejavu. Добавленные страницы текущих stock namespace учитываются '
              'по точному вложенному профилю; опубликованные namespace карт в таблицы stock-материалов не смешиваются.',
              '- HD-оригинал конкретного SD-варианта подтверждается проверенной stock-позицией или provenance '
              '`sourceNamespace/sourceSlots` точного профиля при совпадении материала. «Не установлен» означает '
              'нехватку такой связи; HD другого изображения того же материала не считается оригиналом этого слота. '
              'Пиксельное восстановление происхождения после произвольного масштабирования не выполняется.',
              '- Ссылки читаются из единственного `.MP` внутри каждого MPR, регистр namespace несущественен. '
              'Страницы 0..textureCount−1 считаются ссылками MP, даже если SEC сейчас не использует все слоты. '
              'Это консервативный анализ зависимости атласа, не анализ видимых клеток. Пути/названия внешних MPR '
              'не используются для угадывания namespace.',
              '- МиБ = сумма размеров MMP-членов архива / 1048576 (включая заголовки и mip-данные), '
              'не видеопамять и не размер RGBA. JSON-профили, семантика и служебная структура RES подсчитаны отдельно.',
              '- Обследованы только Maps Dejavu и Base, рекурсивно, без saves/backup/других модов и внешних карт. '
              '«Нет ссылок» — кандидат для последующего анализа переноса, не разрешение удалять. Stock-страницы '
              'нужны глобальной палитре маппера; старые published-страницы могут быть историческими остатками.',
              '- Навык evil-islands-modding не найден в доступном каталоге и локальных plugins/skills. '
              'Приватные исходники движка не использовались. AGENTS.md и docs/PROJECT_CONTRACT.md прочитаны. '
              'Сеть, установка зависимостей, интерактивный редактор, системный буфер и сборка не использовались.', '',
              '## Снимок и консистентность', '',
              f'База чтения: main `{m["head"]}`. Начало: `{m["startedUtc"]}`; завершение чтения MPR: `{m["finishedUtc"]}`. '
              'Все времена UTC. Пакеты читаются только из приватных копий.', '',
              '**Это последовательный снимок, не единая атомарная транзакция пак+все MPR.** Владелец переводит '
              'карты во время аудита. Для каждого RES совпали SHA живого файла до копирования, SHA копии и SHA '
              'живого файла после копирования; каждый MPR прочитан в память, его SHA сверён повторным чтением. '
              'SHA и время каждой карты приведены ниже. Уже после своего чтения файл мог измениться. '
              'После завершения перевода карт следует выполнить новый полный запуск.', '']
    table(['Копия','Байт','SHA256 (до = копия = после)','Время копирования UTC'],
          [(key,rec['size'],f'`{rec["sha256"]}`',rec['startedUtc']+' — '+rec['finishedUtc']) for key,rec in m['packs'].items()])
    lines += [f'SHA каталога семантики: `{audit["presetSha256"]}`. '
              f'Приватные manifest/audit: `{run.relative_to(ROOT) if run.is_absolute() else run}`. '
              'Игровые копии и подробный JSON не включать в коммит.', '', '## Материалы: сводка по уникальным изображениям', '',
              'Имена сохранены точно как в существующем каталоге; land = земля, overlay = верхний слой. '
              'Порядок классов не является рейтингом качества. Нулевая строка может означать материал, '
              'представленный только в углах переходов. Последняя колонка — статические HD с известным статусом.', '']
    table(['Материал','Класс','Слой','Dejavu 256','Dejavu 128','JMV 256','JMV 128','Статические HD D/J'],
          [(key[0],key[1],key[2],variants(bank['dejavu'][key],256),variants(bank['dejavu'][key],128),
            variants(bank['jamaisvu'][key],256),variants(bank['jamaisvu'][key],128),
            f"{variants(bank['dejavu'][key],256,True)}/{variants(bank['jamaisvu'][key],256,True)}") for key in sorted(universe)])
    lines += ['### Где HD нет', '']
    for label,title in [('dejavu','Dejavu'),('jamaisvu','JamaisVu135')]:
        missing = [key for key in sorted(universe) if not variants(bank[label][key],256)]
        lines.append(f'**{title}:** '+ '; '.join(f'{key[0]} [класс {key[1]}, {key[2]}]'+
                     (' (нет однородных сигнатур в подтверждённом банке)' if not bank[label][key] else '') for key in missing)+'.')
        lines.append('')
    lines += ['## Runtime-классы 0..15', '', 'Числа — однородные физические слоты (включая фазы), '
              'по реальному значению MP. Семантический класс семейства выше не подменяет этот столбец.', '']
    table(['Физический класс','Dejavu 256','Dejavu 128','JMV 256','JMV 128'],
          [(cls if cls is not None else '?', *[sum(r['kind']=='uniform' and r['physicalClass']==cls and r['size']==size for r in rows)
             for rows in [dj,jmv] for size in [256,128]]) for cls in [*range(16),None]])
    lines += ['## Материалы по исходным зонам (stock namespace)', '',
              'В каждой строке считаются уникальные изображения текущего Dejavu stock namespace. '
              'Числа JMV относятся к исходной зоне без добавленных страниц. '
              '«HD для 128» — сколько уникальных SD имеют подтверждённый HD-оригинал / '
              'сколько имеют установленное происхождение, но оригинал тоже 128 / сколько не установлены. '
              '«Фазы D» — анимационные слоты 256/128 по доступному MP; `?` — слоты без MP-доказательства. '
              '«Физ. D» — реально найденные физические классы соответствующих uniform-слотов.', '']
    zone_rows = []
    for ns in sorted({b['namespace'] for b in audit['bindings']}):
        keys = {material_key(r) for r in dj+jmv if r['namespace']==ns and r['kind']=='uniform'}
        keys.update((f['name'],f['tileType'],','.join(sorted(f['allowedLayers']))) for f in audit['stockCatalog'] if f['namespace']==ns)
        for key in sorted(keys):
            d = [r for r in dj if r['namespace']==ns and r['kind']=='uniform' and material_key(r)==key]
            j = [r for r in jmv if r['namespace']==ns and r['kind']=='uniform' and material_key(r)==key]
            sd = {r['pixelSha256']:r for r in d if r['size']==128}
            yes = sum(r['hdOriginal'] for r in sd.values())
            no = sum(not r['hdOriginal'] and r['hdOriginalEvidence']!='unresolved' for r in sd.values())
            unknown = len(sd)-yes-no
            phases = '/'.join(str(sum(r['animated'] is True and r['size']==size for r in d)) for size in [256,128])
            unknown_frames = sum(r['animated'] is None for r in d)
            physical = ','.join(sorted({str(r['physicalClass']) if r['physicalClass'] is not None else '?' for r in d})) or '—'
            zone_rows.append((ns,key[0],key[1],key[2],variants(d,256),variants(d,128),f'{yes}/{no}/{unknown}',
                              f'{variants(j,256)}/{variants(j,128)}',phases+(f'; ? {unknown_frames}' if unknown_frames else ''),physical))
    table(['Namespace','Материал','Класс','Слой','D 256','D 128','HD для 128 да/нет/?','JMV 256/128','Фазы D','Физ. D'],zone_rows)
    lines += ['## Переходы и полнота классификации по зонам', '',
              'Здесь единица — физический слот, без объединения одинаковых пикселей. U = uniform, T = переход, '
              '? = непроверенная/отсутствующая семантика. Полный набор страниц, включая padding, сверяется по сумме U+T+?. '
              'Подтверждённые профилем padding-слоты здесь остаются физическими слотами; это не дополнительные уникальные рисунки.', '']
    table(['Банк','Uniform 256','Uniform 128','Переходы 256','Переходы 128','Вне классификации'],
          [(name,*[sum(r['kind']==kind and r['size']==size for r in rows) for kind in ['uniform','transition'] for size in [256,128]],
            sum(r['kind']=='unknown' for r in rows)) for name,rows in [('Dejavu',dj),('JamaisVu135',jmv)]])
    sd_by_origin = Counter(('HD есть' if r['hdOriginal'] else 'исходник тоже 128' if r['hdOriginalEvidence']!='unresolved' else 'происхождение не установлено')
                           for r in dj if r['kind']=='uniform' and r['size']==128)
    unresolved_sd = [r for r in dj if r['kind']=='uniform' and r['size']==128 and r['hdOriginalEvidence']=='unresolved']
    lines += ['Для uniform-слотов 128 px в Dejavu (здесь считаются физические слоты, до объединения дубликатов): '+
              '; '.join(f'{key}: **{value}**' for key,value in sorted(sd_by_origin.items()))+'. '
              f'Для **{sum(r["sameMaterialHdElsewhere"] for r in unresolved_sd)}/{len(unresolved_sd)}** слотов '
              'с неустановленным происхождением в JMV есть HD того же материала и проверенного класса; '
              'это подтверждает наличие альтернативы, но не HD-оригинал того же рисунка. '
              'Дополнительная проверка JMV textures.res для исходных зон с SD-страницами: '+
              '; '.join(f'{ns}: {len(pages)} одноимённых страниц 256 px/тайл' for ns,pages in audit['sdSource256Pages'].items())+'. '
              'Варианты этих же материалов в других зонах могут иметь HD.', '']
    table(['Namespace','D U 256/128','D T 256/128','D ? 256/128','JMV U 256/128','JMV T 256/128','JMV ? 256/128'],
          [(ns,*['/'.join(str(sum(r['namespace']==ns and r['kind']==kind and r['size']==size for r in rows)) for size in [256,128])
                 for rows in [dj,jmv] for kind in ['uniform','transition','unknown']]) for ns in sorted({r['namespace'] for r in dj})])
    lines += ['## Страницы и ссылки MPR', '',
              'D/B/— в последней колонке — МиБ страниц, нужных Dejavu / только Base / ни одной карте. '
              'Страница внутри используемого namespace может оказаться вне textureCount всех карт. '
              'Ссылки на один namespace приведены полностью.', '']
    table(['Namespace','Тип','Страниц','Всего МиБ','MPR Dejavu','MPR Base','МиБ D/B/—'],
          [(r['namespace'],r['kind'],r['pages'],fmt(r['bytes']),names(r['maps']['Dejavu']),names(r['maps']['Base']),
            '/'.join(fmt(r['pageBytes'].get(k,0)) for k in ['Dejavu','BaseOnly','Unreferenced'])) for r in audit['references']])
    table(['Категория','По страницам MP, МиБ','Консервативно весь namespace, МиБ'],
          [(title,fmt(totals[k]),fmt(ns_totals[k])) for k,title in [('Dejavu','Есть ссылка Dejavu'),('BaseOnly','Только Base'),('Unreferenced','Нет ссылок')]])
    other_bytes = sum(r['size'] for r in audit['otherMembers'])
    page_bytes = sum(totals.values())
    overhead = m['packs']['dejavu_hd']['size']-other_bytes-page_bytes
    lines += [f'Контроль размера: страницы **{page_bytes} байт** + прочие члены '
              f'**{other_bytes} байт** ({len(audit["otherMembers"])} JSON/других членов; {fmt(other_bytes)} МиБ) + '
              f'структура/свободное пространство RES **{overhead} байт** = '
              f'**{m["packs"]["dejavu_hd"]["size"]} байт**. Взаимоисключающие категории страниц покрывают весь MMP-банк.', '',
              '### Ограничения и следующий этап переноса', '',
              f'- Недостающих в снимке Dejavu HDPack страниц, объявленных MP проверенных карт: **{len(audit["missingPages"])}**. '
              'Отсутствие здесь не доказывает невозможность загрузки: игра может разрешать ресурс из другого архива.',
              '- Вне uniform/transition: '+ '; '.join(f"{b['pack']}/{b['namespace']}: {b['unknown']}" for b in audit['bindings'] if b['unknown'])+'. '
              'Причины по слотам: '+str(dict(Counter(r.get('unknownReason','not-recorded') for r in dj+jmv if r['kind']=='unknown')))+'. '
              'Unassigned — профиль сознательно не назначил сигнатуру этому слоту; такие слоты не считаются переходами '
              'или вариантами для покраски. Все применённые профили при этом имеют точный SHA.',
              '- Перенос пока не выполнялся. Для будущего пака маппера требуется отдельно подтвердить порядок '
              'поиска ресурсов, доступность глобальной палитры, вложенные profile/semantic JSON, внешние карты '
              'и совместимость игрового загрузчика. Проверка MPR-ссылок сама по себе не разрешает удаление '
              'страниц, которыми пользуется только редактор.',
              '- После всех переводов повторить полный захват. В текущем отчёте нет игровой/визуальной '
              'приёмки, проверки швов и доказательства, что произвольный материал доступен в каждой карте.', '',
              '## Проверенные привязки семантики', '']
    table(['Пак','Namespace','Слотов','SHA пикселей','Авторитет','Stock-префикс'],
          [(b['pack'],b['namespace'],b['slots'],f"`{b['hash']}`",b['authority'],'да' if b['stockPrefixMatch'] else 'нет') for b in audit['bindings']])
    lines += ['## SHA MPR на момент чтения', '']
    table(['Профиль','MPR','Namespace','Страниц MP','Байт','SHA256','Чтение UTC'],
          [(x['group'],x['file'],x['namespace'],x['textureCount'],x['size'],f"`{x['sha256']}`",x['readUtc']) for x in m['maps']])
    lines += ['## Повторение и проверка результата', '',
              'Запуск из корня checkout (создаёт новый приватный снимок, живые файлы только читает):', '',
              '```powershell',
              "& 'C:/Users/samol/.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' tests/hd_coverage_audit.py --report docs/validation/ATAMAN_HD_COVERAGE_AUDIT_20261011.md",
              '```', '',
              '`--snapshot build-tests-hd-coverage/<snapshot>` повторно анализирует проверенные копии и '
              'зафиксированные MP-метаданные; `--render-only` использует уже рассчитанный audit.json. '
              '`--capture-only` только фиксирует входы; `--self-test` проверяет разбор и правила подсчёта '
              'на синтетике без игры. При изменении RES во время копирования скрипт ждёт 60 секунд и повторяет, '
              'не более 10 попыток. Новый полный запуск всегда создаёт отдельный каталог.', '',
              'Проверка: точные SHA RES до/после/копии; bounds/уникальность имён RES; MP-массивы; '
              'непрерывность страниц; привязка семантики по декодированным пикселям; суммы слотов и размеров. '
              'Непривязанный профиль даёт явное unknown, а не PASS классификации.', '',
              '`--self-test`: PASS (RES bounds, DXT1 alpha/RGB, uniform/transition/ambiguous/unknown, '
              'объединение дубликатов, неизвестный статус анимации). Контроль сохранения сумм: PASS. '
              'Это аудит данных, а не тест установленного редактора или игровая приёмка.', '',
              'Для коммита сопровождающим: `tests/hd_coverage_audit.py`, этот отчёт и только пункт '
              '`EI-HD-COVERAGE-AUDIT` в `ROADMAP.md`. `build-tests-hd-coverage/` приватен и исключён из Git.', '']
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text('\n'.join(lines), encoding='utf-8')
    print('REPORT', output, flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--game-root', type=Path, default=Path('C:/GOG Games/PZ'))
    parser.add_argument('--snapshot', type=Path, help='Reuse a private verified capture')
    parser.add_argument('--capture-only', action='store_true')
    parser.add_argument('--report', type=Path)
    parser.add_argument('--render-only', action='store_true')
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        self_test()
        return
    if args.snapshot and not args.snapshot.resolve().is_relative_to(PRIVATE.resolve()):
        raise ValueError('Snapshot must be inside build-tests-hd-coverage')
    if args.render_only and not args.snapshot:
        parser.error('--render-only requires --snapshot')
    run = args.snapshot or capture(args.game_root)
    manifest = json.loads((run/'manifest.json').read_text(encoding='utf-8'))
    if 'finishedUtc' not in manifest:
        capture_maps(args.game_root, run, manifest)
    if not args.capture_only:
        audit = json.loads((run/'audit.json').read_text(encoding='utf-8')) if args.render_only else analyze(run)
        if args.report:
            render_report(run, audit, args.report)


if __name__ == '__main__':
    main()
