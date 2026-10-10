"""Offline PE evidence, model and Win32 probe runner. Standard library only.

No game process, DLL loading, UI, network or live writes. Outputs must be under
build-tests-terrain-memory-synthetic. See dated validation report for commands.
"""
import argparse
import hashlib
import json
import os
import shutil
import re
from datetime import datetime, timezone
from pathlib import Path
import struct
import math
import subprocess
import sys
sys.dont_write_bytecode = True

MIB = 1 << 20


def output_root(path):
    path = path.resolve()
    if path.name != 'build-tests-terrain-memory-synthetic':
        raise ValueError('Output must be owned build-tests-terrain-memory-synthetic root')
    path.mkdir(exist_ok=True)
    return path


def write(path, obj):
    path.write_text(json.dumps(obj, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')


def pe(path):
    b = path.read_bytes()
    def u16(n): return struct.unpack_from('<H', b, n)[0]
    def u32(n): return struct.unpack_from('<I', b, n)[0]
    if b[:2] != b'MZ': raise ValueError('Not MZ')
    off = u32(0x3c)
    if b[off:off+4] != b'PE\0\0': raise ValueError('Not PE')
    coff = off+4
    machine, sections = u16(coff), u16(coff+2)
    opt = coff+20
    magic = u16(opt)
    if magic not in (0x10b, 0x20b): raise ValueError('Unknown optional header')
    sec = opt+u16(coff+16)
    rows = []
    for i in range(sections):
        p = sec+i*40
        rows.append((u32(p+12), max(u32(p+8), u32(p+16)), u32(p+20)))
    def rva(n):
        if n < u32(opt+60): return n
        for va, size, raw in rows:
            if va <= n < va+size: return raw+n-va
        raise ValueError(f'Unmapped RVA {n:x}')
    def string(p):
        end = b.index(0, p)
        return b[p:end].decode('ascii', errors='replace')
    imports = []
    import_symbols = {}
    dd = opt+(96 if magic == 0x10b else 112)
    import_rva = u32(dd+8)
    if import_rva:
        p = rva(import_rva)
        while any(b[p:p+20]):
            name=string(rva(u32(p+12)))
            imports.append(name)
            thunk_rva=u32(p) or u32(p+16)
            symbols=[]
            if thunk_rva:
                thunk=rva(thunk_rva);stride=4 if magic==0x10b else 8
                while True:
                    value=struct.unpack_from('<I' if stride==4 else '<Q',b,thunk)[0]
                    if not value:break
                    symbols.append('ordinal:'+str(value&0xffff) if value&(1<<(stride*8-1)) else string(rva(value)+2))
                    thunk+=stride
            import_symbols[name]=symbols
            p += 20
    exports=[]
    export_rva,export_size=u32(dd),u32(dd+4)
    if export_rva:
        p=rva(export_rva);base=u32(p+16);functions=u32(p+20);names=u32(p+24)
        addresses=rva(u32(p+28));name_table=rva(u32(p+32));ordinals=rva(u32(p+36))
        for i in range(names):
            ordinal=u16(ordinals+2*i)
            if ordinal>=functions:raise ValueError('Export ordinal out of bounds')
            function_rva=u32(addresses+4*ordinal)
            exports.append(dict(name=string(rva(u32(name_table+4*i))),ordinal=base+ordinal,rva=hex(function_rva),
                forwarder=string(rva(function_rva)) if export_rva<=function_rva<export_rva+export_size else None))
    resource_types=[]
    resource_rva=u32(dd+16)
    if resource_rva:
        p=rva(resource_rva)
        for i in range(u16(p+12)+u16(p+14)):
            name=u32(p+16+i*8)
            if not name&0x80000000:resource_types.append(name)
    debug_types=[]
    debug_rva,debug_size=u32(dd+48),u32(dd+52)
    if debug_rva:
        p=rva(debug_rva)
        for i in range(debug_size//28):debug_types.append(u32(p+i*28+12))
    graphics = [n for n in imports if n.lower().startswith(('d3d', 'ddraw', 'opengl', 'dxgi'))]
    return dict(path=str(path.resolve()), size=len(b), sha256=hashlib.sha256(b).hexdigest(),
                machine=hex(machine), bits=32 if magic == 0x10b else 64,
                characteristics=hex(u16(coff+18)), largeAddressAware=bool(u16(coff+18)&0x20),
                imports=imports, graphicsImports=graphics,importSymbols=import_symbols,exports=exports,
                resourceTypeIds=resource_types,hasVersionResource=16 in resource_types,
                debugTypeIds=debug_types,reproducibleBuildMarker=16 in debug_types,
                coffTimestamp=u32(coff+4),coffTimestampUtc=datetime.fromtimestamp(u32(coff+4),timezone.utc).isoformat(),
                coffTimestampIsBuildDate=False if 16 in debug_types else None)


def renderer(args):
    """Inspect bytes/resources and neighbouring docs; never LoadLibrary the DLL."""
    import ctypes
    from ctypes import wintypes
    out=output_root(args.output)
    # Windows version.dll reads the file's resource table; no game code runs.
    version=ctypes.WinDLL('version',use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoSizeW.restype=wintypes.DWORD
    version.GetFileVersionInfoW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p]
    version.GetFileVersionInfoW.restype=wintypes.BOOL
    version.VerQueryValueW.argtypes=[ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(ctypes.c_void_p),ctypes.POINTER(wintypes.UINT)]
    version.VerQueryValueW.restype=wintypes.BOOL
    def version_fields(path):
        unused=wintypes.DWORD();size=version.GetFileVersionInfoSizeW(str(path.resolve()),ctypes.byref(unused))
        if not size:return dict(present=False,win32Error=ctypes.get_last_error(),fields={})
        buffer=ctypes.create_string_buffer(size)
        if not version.GetFileVersionInfoW(str(path.resolve()),0,size,buffer):raise OSError(ctypes.get_last_error())
        pointer=ctypes.c_void_p();length=wintypes.UINT();pairs=[];fields={}
        if version.VerQueryValueW(buffer,r'\VarFileInfo\Translation',ctypes.byref(pointer),ctypes.byref(length)):
            pairs=list(struct.iter_unpack('<HH',ctypes.string_at(pointer,length.value)))
        for language,encoding in pairs:
            for key in ('FileVersion','ProductVersion','FileDescription','ProductName','CompanyName','OriginalFilename','InternalName'):
                name=f'\\StringFileInfo\\{language:04x}{encoding:04x}\\{key}'
                if version.VerQueryValueW(buffer,name,ctypes.byref(pointer),ctypes.byref(length)):
                    fields[key]=ctypes.wstring_at(pointer,max(0,length.value-1))
        return dict(present=True,fields=fields)
    dlls=[]
    expressions={
        'identity':r'^EID3D11|^[0-9a-f]{40}$|\.pdb$',
        'terrain':r'(terrain.*(BC1|mip|surface|texture|atlas)|MMP.*(allocation|compressed|format)|GPU-only surfaces|CPU surface storage|surface_cpu_bytes|surface_gpu_bytes|surface_double_resident_bytes|staging texture failed|bounded staging|D3D7.EvictManagedTextures|Surface.Lock|Surface.Unlock)',
        'process':r'CreateProcess|D3D11CreateDevice|4 GB mode|only 2 GB of memory',
    }
    for path in args.files:
        facts=pe(path);b=path.read_bytes();stat=path.stat()
        # Scan data sections only: DXT immediates in instructions aren't messages.
        off=struct.unpack_from('<I',b,0x3c)[0]+4;opt=off+20;sec=opt+struct.unpack_from('<H',b,off+16)[0]
        data_ranges=[]
        for i in range(struct.unpack_from('<H',b,off+2)[0]):
            p=sec+40*i;flags=struct.unpack_from('<I',b,p+36)[0]
            if flags&0x20:continue  # IMAGE_SCN_CNT_CODE
            size,raw=struct.unpack_from('<II',b,p+16);data_ranges.append((raw,size))
        strings=[]
        for raw,size in data_ranges:
            section=b[raw:raw+size]
            for match in re.finditer(rb'[\x20-\x7e]{6,}',section):
                strings.append((raw+match.start(),match.group().decode('ascii'),'ascii'))
            for match in re.finditer(rb'(?:[\x20-\x7e]\x00){6,}',section):
                strings.append((raw+match.start(),match.group().decode('utf-16le'),'utf16le'))
        selected={key:[dict(offset=hex(offset),encoding=encoding,text=text) for offset,text,encoding in strings if re.search(expr,text,re.I)] for key,expr in expressions.items()}
        facts.update(versionInfo=version_fields(path),creationUtc=datetime.fromtimestamp(stat.st_ctime,timezone.utc).isoformat(),
                     lastWriteUtc=datetime.fromtimestamp(stat.st_mtime,timezone.utc).isoformat(),strings=selected)
        dlls.append(facts)
    files=[]
    for path in args.documents:
        b=path.read_bytes();stat=path.stat()
        files.append(dict(path=str(path.resolve()),sha256=hashlib.sha256(b).hexdigest(),size=len(b),
                          creationUtc=datetime.fromtimestamp(stat.st_ctime,timezone.utc).isoformat(),
                          lastWriteUtc=datetime.fromtimestamp(stat.st_mtime,timezone.utc).isoformat()))
    write(out/'renderer-facts.json',dict(dlls=dlls,documents=files,notes='Static byte/resource evidence. Timestamps not proof of initial installation. Strings show mechanisms, not exercised paths.'))
    for row in dlls:
        print(Path(row['path']).name,row['bits'],row['sha256'],'VERSIONINFO',row['versionInfo'])
        print('exports',[x['name'] for x in row['exports']])
        print('revision strings',[(x['offset'],x['text']) for x in row['strings']['identity'] if re.fullmatch('[a-f0-9]{40}',x['text'])])
        print('process imports',[s for symbols in row['importSymbols'].values() for s in symbols if re.search('CreateProcess|ShellExecute|D3D11CreateDevice|FileMapping|MapView',s)])


def facts(args):
    out = output_root(args.output)
    profile = json.loads(args.profile.read_text(encoding='utf-8'))
    # Current mapper schema stores paths in nested option groups.
    def find(v):
        if isinstance(v, dict):
            if 'gameRoot' in v: return v['gameRoot']
            for item in v.values():
                found = find(item)
                if found: return found
        return None
    game = Path(find(profile))
    records = [pe(p) for p in sorted((game/'Engine').iterdir()) if p.suffix.lower() in ('.exe', '.dll')]
    write(out/'pe-facts.json', dict(profile=str(args.profile.resolve()), gameRoot=str(game),
          host64Bit=bool(os.environ.get('PROCESSOR_ARCHITEW6432') or os.environ.get('PROCESSOR_ARCHITECTURE') == 'AMD64'), records=records))
    for r in records:
        print(Path(r['path']).name, r['bits'], 'LAA='+str(r['largeAddressAware']), ','.join(r['graphicsImports']))


def model(args):
    out = output_root(args.output)
    rows=[]
    for pixels in (128,256):
        for slots in (1024,2048,4096,8192):
            dxt=rgba=0
            size=pixels*8
            while True:
                dxt+=max(1,(size+3)//4)**2*8
                rgba+=size*size*4
                if size==1:break
                size//=2
            pages=slots//64;d=dxt*pages;r=rgba*pages
            columns=math.ceil(math.sqrt(pages));combined=columns*math.ceil(pages/columns)*(pixels*8)**2*4
            rows.append(dict(slots=slots,pixels=pixels,pages=pages,dxtBytes=d,rgbaBytes=r,combinedRgbaBytes=combined,
                dxtOnlyBytes=d,systemCopyBytes=2*d,streamDecodeBytes=2*d+rgba+dxt,
                retainedRgbaBytes=2*d+r,amplifiedBytes=2*d+2*r+rgba+dxt,
                assembledBytes=max(2*d+2*r+rgba+dxt,2*d+2*r+combined),
                wrapperDxtPeakVaBytes=2*d+2*dxt,wrapperOnePageRgbaPeakVaBytes=2*d+2*dxt+rgba,
                wrapperDxtPeakPrivateBytes=d+2*dxt,wrapperHypotheticalMappedBytes=d,
                wrapperGpuBytes=d,wrapperNotes='Hypothesis: compressed cache D, driver mapping envelope D, page-sized read/upload staging. GPU bytes are not allocated in real GPU.'))
    write(out/'memory-model.json',dict(units='bytes; GPU VRAM is not automatically process VA',rows=rows))
    for r in rows:print(r['slots'],r['pixels'],*[round(r[k]/MIB,3) for k in ('dxtBytes','rgbaBytes','retainedRgbaBytes','amplifiedBytes')])


def probes(args):
    out=output_root(args.output)
    if Path(args.label).name!=args.label or args.label in ('.','..'):
        raise ValueError('Run label must be a single directory name')
    game_facts=json.loads((out/'pe-facts.json').read_text(encoding='utf-8'))
    game=next(r for r in game_facts['records'] if Path(r['path']).name.lower()=='game.exe')
    current_game=pe(Path(game['path']))
    if current_game['sha256']!=game['sha256']:
        raise ValueError('Installed game.exe changed since facts; refresh evidence first')
    probe=pe(args.probe)
    if probe['bits']!=32 or probe['largeAddressAware']!=game['largeAddressAware']:
        raise ValueError('Probe bitness/LAA does not match game.exe')
    write(out/'probe-pe.json',probe)
    run_dir=out/args.label;run_dir.mkdir(exist_ok=False)
    shutil.copy2(args.probe,run_dir/'memory_probe32.exe')
    for name in ('memory_probe32.cpp','memory_probe32.ps1','memory_validate.py'):
        shutil.copy2(Path(__file__).with_name(name),run_dir/name)
    write(run_dir/'probe-pe.json',probe)
    records=[]
    for pixels in args.pixels:
        for slots in args.slots:
            source_slots=2048 if slots<=2048 else 4096
            source=args.kit/f'tb1_{source_slots}_{pixels}'/f'tb1_{source_slots}_{pixels}-atlas.res'
            for background in args.backgrounds:
                for mode in args.modes:
                    stem=f'{slots}-{pixels}-{background}-{mode}'
                    command=[str((run_dir/'memory_probe32.exe').resolve()),str(source.resolve()),str(slots),str(pixels),str(background),mode,str(args.reserve_mib)]
                    if mode.startswith('d3d11'):command.append(str(args.wrapper_staging_mib))
                    try:
                        process=subprocess.run(command,capture_output=True,text=True,timeout=120,cwd=run_dir)
                        (run_dir/(stem+'.jsonl')).write_text(process.stdout,encoding='utf-8')
                        (run_dir/(stem+'.stderr.txt')).write_text(process.stderr,encoding='utf-8')
                        samples=[json.loads(line) for line in process.stdout.splitlines()]
                        end=samples[-1] if samples else {}
                        atlas_samples=[s for s in samples if s.get('stage') not in ('baseline','background','background_with_holes')]
                        status='PASS' if process.returncode==0 and end.get('event')=='complete' else ('ALLOC_FAIL' if process.returncode==10 else 'ERROR')
                        record=dict(slots=slots,pixels=pixels,backgroundMiB=background,reserveMiB=args.reserve_mib,mode=mode,status=status,
                            exitCode=process.returncode,samples=len(samples),peakOccupiedVA=max((s.get('occupiedVA',0) for s in samples),default=0),
                            peakCommittedVA=max((s.get('committedVA',0) for s in samples),default=0),
                            peakPrivateBytes=max((s.get('privateBytes',0) for s in samples),default=0),
                            minimumLargestFree=min((s.get('largestFree',0) for s in samples),default=0),last=end,
                            peakAtlasOccupiedVA=max((s.get('occupiedVA',0) for s in atlas_samples),default=0),
                            minimumAtlasLargestFree=min((s.get('largestFree',0) for s in atlas_samples),default=0),
                            input=str(source.resolve()),coverage='subset' if slots==1024 else ('repeated pages extrapolation' if slots==8192 else 'exact kit'))
                    except subprocess.TimeoutExpired as e:
                        (run_dir/(stem+'.timeout.txt')).write_text(str(e),encoding='utf-8')
                        record=dict(slots=slots,pixels=pixels,backgroundMiB=background,mode=mode,status='TIMEOUT')
                    records.append(record)
                    write(run_dir/'summary.json',dict(probeSha256=probe['sha256'],gameLAA=game['largeAddressAware'],records=records))
                    print(stem,record['status'],round(record.get('peakOccupiedVA',0)/MIB,1),flush=True)


def summarize(args):
    out=output_root(args.output)
    records=[];samples_count=0
    for label in args.labels:
        folder=out/label
        summary=json.loads((folder/'summary.json').read_text(encoding='utf-8'))
        for record in summary['records']:
            stem=f"{record['slots']}-{record['pixels']}-{record['backgroundMiB']}-{record['mode']}"
            samples=[json.loads(line) for line in (folder/(stem+'.jsonl')).read_text(encoding='utf-8').splitlines()]
            if not samples: raise ValueError('Missing samples')
            for sample in samples:
                if sample['pointerBits']!=32 or not sample['laa'] or sample['scannedBytes']!=sample['scanCeiling']:
                    raise ValueError('Incomplete x86 LAA address-space walk')
                if sample['occupiedVA']+sample['freeVA']!=sample['scannedBytes']:
                    raise ValueError('VA accounting mismatch')
            atlas=[s for s in samples if s['stage'] not in ('baseline','background','background_with_holes')]
            record.update(label=label,probeSha256=summary['probeSha256'],reserveMiB=record.get('reserveMiB',512),
                          peakAtlasOccupiedVA=max(s['occupiedVA'] for s in atlas),
                          minimumAtlasLargestFree=min(s['largestFree'] for s in atlas),
                          peakAtlasPrivateBytes=max(s['privateBytes'] for s in atlas),
                          peakAtlasCommittedVA=max(s['committedVA'] for s in atlas))
            samples_count+=len(samples);records.append(record)
    result=dict(runs=len(records),samples=samples_count,
                statusCounts={s:sum(r['status']==s for r in records) for s in ('PASS','ALLOC_FAIL','ERROR','TIMEOUT')},
                vaAccounting='PASS: all samples cover self VA and sum to scan ceiling',records=records)
    write(out/'all-probes.json',result)
    print(result['runs'],result['samples'],result['statusCounts'])


if __name__=='__main__':
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,default=Path('build-tests-terrain-memory-synthetic'))
    sub=parser.add_subparsers(dest='action',required=True)
    p=sub.add_parser('facts');p.add_argument('--profile',type=Path,default=Path('artifacts/EI-Mapper-Ataman-2-first-test/endurance-options.json'));p.set_defaults(func=facts)
    p=sub.add_parser('model');p.set_defaults(func=model)
    p=sub.add_parser('renderer');p.add_argument('--files',nargs='+',type=Path,required=True)
    p.add_argument('--documents',nargs='*',type=Path,default=[]);p.set_defaults(func=renderer)
    p=sub.add_parser('probes');p.add_argument('--probe',type=Path,required=True);p.add_argument('--kit',type=Path,default=Path('build-tests-terrain-bake-stage-1/memory-kit'))
    p.add_argument('--backgrounds',type=int,nargs='+',default=[768,1024]);p.add_argument('--modes',nargs='+',default=['dxt','system','stream','rgba','amplified'])
    p.add_argument('--reserve-mib',type=int,default=512)
    p.add_argument('--slots',type=int,nargs='+',choices=(1024,2048,4096,8192),default=[1024,2048,4096,8192])
    p.add_argument('--pixels',type=int,nargs='+',choices=(128,256),default=[128,256])
    p.add_argument('--wrapper-staging-mib',type=int,default=0)
    p.add_argument('--label',required=True);p.set_defaults(func=probes)
    p=sub.add_parser('summarize');p.add_argument('--labels',nargs='+',default=['matrix','assembled','pressure']);p.set_defaults(func=summarize)
    args=parser.parse_args();args.func(args)
