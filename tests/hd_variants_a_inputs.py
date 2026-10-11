"""Export private frozen coverage inputs for the C++ algorithm-A corpus gate.

No synthesis here: all new pixels come from terrain_hd_variants.cpp. Game
pixels/BC1 stay in ignored build-tests-145, never in repository assets.
"""
from pathlib import Path
from collections import defaultdict
import hashlib
import json
import struct
import sys

sys.dont_write_bytecode = True
import hd_variants_proto as proto


def main():
    root = proto.ROOT / 'build-tests-145' / 'inputs'
    if root.exists():
        raise RuntimeError('Refusing to overwrite existing input snapshot')
    root.mkdir(parents=True)
    snapshot = proto.Snapshot(proto.DEFAULT_SNAPSHOT)
    groups = defaultdict(list)
    seen = defaultdict(set)
    for bank in ('dejavu', 'jamaisvu'):
        for row in snapshot.rows[bank]:
            if (row['kind'] != 'uniform' or row['size'] != 256 or
                    row['animated'] is not False or not row['opaque'] or
                    row['material'].startswith('Water /') or
                    row['physicalClass'] is None):
                continue
            for layer in row['layers']:
                key = (row['material'], row['reviewedClass'], row['physicalClass'], layer)
                if row['pixelSha256'] in seen[key]:
                    continue
                seen[key].add(row['pixelSha256'])
                groups[key].append((bank, row))
    # The owner-confirmed 144 source keeps the class-3 identity, physical MP2.
    desert = next(r for r in snapshot.rows['jamaisvu'] if r['namespace'] == 'bz16h' and r['slot'] == 463)
    groups[('Stone / Desert', 3, 2, 'land')].append(('jamaisvu', desert))
    manifest = []
    for index, (key, rows) in enumerate(sorted(groups.items())):
        folder = root / f'{index:03}'
        folder.mkdir()
        sources = []
        canonical = set()
        for bank, row in rows:
            image = snapshot.row_tile(row, bank)
            fingerprint = proto.canonical(image)
            if fingerprint in canonical:
                continue
            canonical.add(fingerprint)
            name = f'{len(sources):02}'
            png = folder / (name + '.png')
            proto.pil(image).save(png)
            pack = 'dejavu_hd' if bank == 'dejavu' else 'jamaisvu_hd'
            page_name = f"{row['namespace']}{row['slot']//64:03}.mmp"
            payload = proto.audit_tool.member(snapshot.streams[pack], snapshot.indexes[pack][page_name])
            _, width, height = struct.unpack_from('<III', payload)
            assert width == height == 2048, (key, width, height)
            slot = row['slot'] % 64
            tile_y, tile_x = 7 - slot // 8, slot % 8
            offset = 76
            blocks = []
            for level in range(7):
                size = 256 >> level
                page_blocks = width // 4
                tile_blocks = size // 4
                selected = b''.join(payload[offset + ((tile_y*tile_blocks+y)*page_blocks+tile_x*tile_blocks)*8:
                                            offset + ((tile_y*tile_blocks+y)*page_blocks+(tile_x+1)*tile_blocks)*8]
                                    for y in range(tile_blocks))
                assert len(selected) == size*size//2
                path = folder / f'{name}-mip{size}.bc1'
                path.write_bytes(selected)
                blocks.append(path.relative_to(root).as_posix())
                offset += width*height//2
                width //= 2
                height //= 2
            sources.append(dict(id=f"{pack}:{row['namespace']}:{row['slot']}:{row['pixelSha256']}",
                                png=png.relative_to(root).as_posix(), blocks=blocks, provenance=row))
        manifest.append(dict(key='|'.join(map(str, key)), sources=sources))
    snapshot.verify_end()
    (root/'inputs.json').write_text(json.dumps(dict(groups=manifest, sourceHashes=snapshot.hashes,
                                                  presetSha=snapshot.audit['presetSha256']), indent=2), encoding='utf-8')
    print(json.dumps(dict(groups=len(manifest), sources=sum(len(g['sources']) for g in manifest),
                          missing=sum(max(0,6-len(g['sources'])) for g in manifest))))


if __name__ == '__main__':
    main()
