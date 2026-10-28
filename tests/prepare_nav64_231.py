"""Private wire fixtures and preservation inventory; no game writes."""
import ctypes as C
import hashlib
import json
import pathlib
import shutil
import struct
import sys

run = pathlib.Path(sys.argv[1]).resolve()
game = pathlib.Path(sys.argv[2]).resolve()
nav = game / 'MODS/DejaVu/Maps/Nav64'
maps = run / 'maps/Maps'
(maps / 'Nav64').mkdir(parents=True)
for profile in ('tiny', 'small', 'human', 'large', 'giant', 'levitate_tiny', 'levitate_small', 'levitate'):
    shutil.copy2(nav / f'zone1--zone1.{profile}.nav64', maps / 'Nav64')
shutil.copy2(nav / 'bodies.navbody', maps / 'Nav64')
for ext in ('mpr', 'mob'):
    shutil.copy2(game / f'MODS/DejaVu/Maps/zone1.{ext}', maps)
protected = [game / f'MODS/DejaVu/Maps/zone1.{e}' for e in ('mpr', 'mob')]
protected += sorted(p for p in nav.iterdir() if p.is_file())
protected += [game / 'Engine64/EIRebuild64NavBake.exe']
def inventory(paths):
    return [{'path': str(p), 'size': p.stat().st_size, 'mtime': p.stat().st_mtime_ns,
             'sha256': hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths]
(run / 'live-before.json').write_text(json.dumps(inventory(protected), indent=2), encoding='utf-8')

cab = C.WinDLL('cabinet', use_last_error=True)
for name in ('CreateCompressor', 'CreateDecompressor'):
    getattr(cab, name).argtypes = [C.c_uint32, C.c_void_p, C.POINTER(C.c_void_p)]
for name in ('Compress', 'Decompress'):
    getattr(cab, name).argtypes = [C.c_void_p, C.c_void_p, C.c_size_t, C.c_void_p, C.c_size_t, C.POINTER(C.c_size_t)]
for name in ('CloseCompressor', 'CloseDecompressor'):
    getattr(cab, name).argtypes = [C.c_void_p]
def codec(data, expanded=None):
    handle = C.c_void_p()
    assert (cab.CreateDecompressor if expanded is not None else cab.CreateCompressor)(5, None, C.byref(handle))
    try:
        size = C.c_size_t(expanded or 0)
        fn = cab.Decompress if expanded is not None else cab.Compress
        if expanded is None:
            fn(handle, data, len(data), None, 0, C.byref(size))
        output = C.create_string_buffer(size.value)
        assert fn(handle, data, len(data), output, len(output), C.byref(size)), C.get_last_error()
        return output.raw[:size.value]
    finally:
        (cab.CloseDecompressor if expanded is not None else cab.CloseCompressor)(handle)
u32 = lambda b, at: struct.unpack_from('<I', b, at)[0]
pack = lambda n: struct.pack('<I', n)
raw = (maps / 'Nav64/zone1--zone1.human.nav64').read_bytes()
assert u32(raw, 8) == 8 and u32(raw, 12) == 21
at = 28
for _ in range(3):
    at += 4 + u32(raw, at)
class_at = at + 28
params_at = class_at + 4
sections_at = params_at + 28
assert u32(raw, sections_at) == 2
section_at = sections_at + 4
sections = {}
while section_at < len(raw):
    kind, size = struct.unpack_from('<II', raw, section_at)
    sections[kind] = raw[section_at + 8:section_at + 8 + size]
    section_at += 8 + size
assert section_at == len(raw) and set(sections) == {1, 2}
payload = sections[1]
algorithm, expanded, compressed = struct.unpack_from('<III', payload)
assert algorithm == 5 and compressed == len(payload) - 12
tiles = codec(payload[12:], expanded)
def emit(name, stream=tiles, prefix=None, domain=None, legacy=False):
    compressed = codec(bytes(stream))
    tile_section = pack(5) + pack(len(stream)) + pack(len(compressed)) + compressed
    result = (prefix if prefix is not None else raw[:sections_at]) + pack(3 if legacy else 2)
    for kind, data in [(1, tile_section), (2, domain if domain is not None else sections[2])]:
        result += pack(kind) + pack(len(data)) + data
    if legacy:
        result += pack(3) + pack(4) + pack(0)
    (run / f'{name}.nav64').write_bytes(result)
legacy_tiles = bytearray(tiles[:4])
at = 4
for _ in range(u32(tiles, 0)):
    size = u32(tiles, at)
    legacy_tiles += tiles[at:at+4+size]
    poly_count = u32(tiles, at + 4 + 24)
    at += 4 + size
    assert u32(tiles, at) == poly_count
    at += 4
    for _ in range(poly_count):
        count = u32(tiles, at)
        at += 4 + count * 8
assert at == len(tiles)
for algorithm in (8, 9, 10):
    prefix = bytearray(raw[:28])
    struct.pack_into('<II', prefix, 8, 3, algorithm)
    prefix += pack(64) + b'0'*64 + raw[28:class_at] + raw[params_at:sections_at]
    emit(f'legacy{algorithm}', legacy_tiles, bytes(prefix), pack(1)+pack(1), True)
bad = bytearray(raw)
struct.pack_into('<I', bad, 12, 22)
(run/'algorithm22.nav64').write_bytes(bad)
bad = bytearray(raw)
struct.pack_into('<I', bad, 12, 10)
(run/'wrong-pair.nav64').write_bytes(bad)
bad = bytearray(raw)
struct.pack_into('<I', bad, class_at, 99)
(run/'wrong-class.nav64').write_bytes(bad)
emit('wrong-domain', domain=pack(1)+pack(1))
support_at = 8 + u32(tiles, 4)
bad = bytearray(tiles)
struct.pack_into('<I', bad, support_at, 0)
emit('wrong-support-count', bad)
bad = bytearray(tiles)
struct.pack_into('<Q', bad, support_at+8, 0)
emit('zero-support-id', bad)
(run/'truncated.nav64').write_bytes(raw[:-1])
(run/'mock-output/Maps/Nav64').mkdir(parents=True)
for name in ('zone1--zone1.navgeom', 'zone1--zone1.navworld', 'bodies.navbody'):
    (run/'mock-output/Maps/Nav64'/name).write_text('owned backup sentinel', encoding='utf-8')
print('Prepared private container8 and legacy/malformed wire fixtures')
