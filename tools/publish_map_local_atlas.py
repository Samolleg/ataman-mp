"""Publish the CURRENT MPR's exact sidecar members into a runtime terrain pack.

Dry-run by default. Requires Python 3.9+, only the standard library.
MPR, sidecar, historic backups and unrelated pack members are never edited.
Close the mapper/game before --apply. No slot remapping or artwork generation.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import tempfile
from datetime import datetime


def digest(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for b in iter(lambda: f.read(4 * 1024 * 1024), b''):
            h.update(b)
    return h.hexdigest()


class Archive:
    def __init__(self, path):
        self.path = path
        self.entries = {}
        with path.open('rb') as f:
            signature, count, offset, names_size = struct.unpack('<4I', f.read(16))
            assert signature == 0x019CE23C and count < 10000000, 'Invalid RES header'
            assert offset + 22 * count + names_size <= path.stat().st_size
            f.seek(offset)
            table, names = f.read(count * 22), f.read(names_size)
        for i in range(count):
            nxt, size, start, stamp, length, name_offset = struct.unpack_from('<4IHI', table, i * 22)
            assert nxt == 0xFFFFFFFF or nxt < count
            assert name_offset + length <= len(names) and start + size <= path.stat().st_size
            name = names[name_offset:name_offset + length].decode('cp1251')
            key = name.casefold()
            assert key and key not in self.entries, 'Ambiguous RES member'
            self.entries[key] = (name, start, size, stamp)
        # The runtime follows the CP1251 hash chains rather than enumerating
        # the table. Verify reachability as well as the payload offsets.
        for key in self.entries:
            slot = sum(key.encode('cp1251')) % count
            visited = set()
            while slot != 0xFFFFFFFF and slot not in visited:
                visited.add(slot)
                nxt, _, _, _, length, name_offset = struct.unpack_from('<4IHI', table, slot * 22)
                if names[name_offset:name_offset + length].decode('cp1251').casefold() == key:
                    break
                slot = nxt
            else:
                raise AssertionError('RES member unreachable through runtime hash chain: ' + key)

    def payload(self, key):
        _, start, size, _ = self.entries[key.casefold()]
        with self.path.open('rb') as f:
            f.seek(start)
            data = f.read(size)
        assert len(data) == size
        return data


def build_candidate(source, additions, output):
    """Stream unchanged payloads; reconstruct the native CP1251 hash chains."""
    records = []
    with source.path.open('rb') as src, output.open('wb') as dst:
        dst.write(bytes(16))
        for key, (name, start, size, stamp) in source.entries.items():
            assert key not in additions
            dst.write(bytes((-dst.tell()) % 16))
            new_start = dst.tell()
            src.seek(start)
            remaining = size
            while remaining:
                data = src.read(min(4 * 1024 * 1024, remaining))
                assert data, 'Source payload truncated'
                dst.write(data)
                remaining -= len(data)
            records.append((name, new_start, size, stamp))
        for name, data in additions.items():
            dst.write(bytes((-dst.tell()) % 16))
            records.append((name, dst.tell(), len(data), 0))
            dst.write(data)
        count = len(records)
        slots = [None] * count
        for record in records:
            bucket = sum(record[0].lower().encode('cp1251')) % count
            if slots[bucket] is None:
                slots[bucket] = [record, 0xFFFFFFFF]
            else:
                while slots[bucket][1] != 0xFFFFFFFF:
                    bucket = slots[bucket][1]
                free = next(i for i, row in enumerate(slots) if row is None)
                slots[bucket][1] = free
                slots[free] = [record, 0xFFFFFFFF]
        table, names = bytearray(), bytearray()
        for (name, start, size, stamp), nxt in slots:
            encoded = name.encode('cp1251')
            table += struct.pack('<4IHI', nxt, size, start, stamp, len(encoded), len(names))
            names += encoded
        table_offset = dst.tell()
        assert table_offset + len(table) + len(names) < 2**32
        dst.write(table)
        dst.write(names)
        dst.seek(0)
        dst.write(struct.pack('<4I', 0x019CE23C, count, table_offset, len(names)))
        dst.flush()
        os.fsync(dst.fileno())


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--map', type=Path, required=True)
    p.add_argument('--atlas', type=Path, required=True)
    p.add_argument('--pack', type=Path, required=True)
    p.add_argument('--apply', action='store_true')
    a = p.parse_args()
    paths = [x.resolve(strict=True) for x in (a.map, a.atlas, a.pack)]
    assert len(set(map(str.casefold, map(str, paths)))) == 3, 'Inputs must differ'
    mpr_path, atlas_path, pack_path = paths
    assert pack_path.name.casefold() == 'hdpack_terrain.res'
    original = {str(x): digest(x) for x in paths}
    mpr, atlas, pack = map(Archive, paths)
    mp_names = [x for x in mpr.entries if x.endswith('.mp')]
    assert len(mp_names) == 1, 'Expected one MPR namespace'
    ns = mp_names[0][:-3]
    count, = struct.unpack_from('<I', mpr.payload(mp_names[0]), 16)
    assert 1 <= count <= 126
    profiles = [(name, json.loads(atlas.payload(name))) for name in atlas.entries
                if name.startswith('ei_atlas_profile_') and name.endswith('.json')]
    profiles = [(name, j) for name, j in profiles if j.get('mprNamespace', '').casefold() == ns]
    assert len(profiles) == 1, 'Missing or ambiguous exact sidecar profile'
    profile_name, profile = profiles[0]
    assert profile['textureCount'] == count and profile['atlasSlotCount'] == count * 64
    selected = [f'{ns}{i:03}.mmp' for i in range(count)] + [profile_name]
    for name in atlas.entries:
        if name.startswith('ei_terrain_semantics_') and name.endswith('.json'):
            if json.loads(atlas.payload(name)).get('mprNamespace', '').casefold() == ns:
                selected.append(name)
    additions = {}
    for name in selected:
        data = atlas.payload(name)
        if name.endswith('.mmp'):
            assert data[:4] == b'MMP\x00' and data[16:20] == b'DXT1'
            width, height = struct.unpack_from('<II', data, 4)
            assert width == height and width in (1024, 2048)
        if name in pack.entries:
            assert pack.payload(name) == data, 'Conflicting pack member: ' + name
        else:
            additions[name] = data
    print(json.dumps({'map': str(mpr_path), 'namespace': ns, 'pages': count,
                      'pack': str(pack_path), 'add': list(additions),
                      'apply': a.apply, 'sourceSHA256': original}, ensure_ascii=False, indent=2))
    if not a.apply or not additions:
        return
    lock_path = pack_path.with_name(pack_path.name + '.mapper-save.lock')
    lock = os.open(lock_path, os.O_CREAT | os.O_EXCL | os.O_WRONLY)
    candidate = None
    try:
        fd, name = tempfile.mkstemp(prefix='atlas-publish-', suffix='.res', dir=pack_path.parent)
        os.close(fd)
        candidate = Path(name)
        build_candidate(pack, additions, candidate)
        reopened = Archive(candidate)
        assert set(reopened.entries) == set(pack.entries) | set(additions)
        for name, (_, _, _, stamp) in pack.entries.items():
            assert reopened.payload(name) == pack.payload(name), 'Unrelated payload changed'
            assert reopened.entries[name][3] == stamp
        for name, data in additions.items():
            assert reopened.payload(name) == data
        candidate_hash = digest(candidate)
        assert all(digest(x) == original[str(x)] for x in paths), 'Input changed; refused'
        backup = pack_path.with_name(pack_path.name + '.before-map-atlas-' + datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '.backup')
        with backup.open('xb') as dst, pack_path.open('rb') as src:
            shutil.copyfileobj(src, dst, 4 * 1024 * 1024)
        assert digest(backup) == original[str(pack_path)], 'Backup verification failed'
        assert all(digest(x) == original[str(x)] for x in paths), 'Input changed before replacement'
        os.replace(candidate, pack_path)
        candidate = None
        assert digest(pack_path) == candidate_hash
        assert all(digest(x) == original[str(x)] for x in paths[:2])
        print(json.dumps({'result': 'PUBLISHED', 'backup': str(backup), 'packSHA256': candidate_hash}))
    finally:
        if candidate is not None:
            candidate.unlink(missing_ok=True)
        os.close(lock)
        lock_path.unlink()


if __name__ == '__main__':
    main()
