# Windows-only measurement runner; no network, dependencies or native clipboard access.
# Environment: EI_HDMEM_QT_BIN, EI_HDMEM_MINGW_BIN, optional EI_HDMEM_PROBE_EXE.
# Usage: python tests/terrain_hd_memory_measure.py baseline 3 [--benchmark]
# Requires private build-tests-hdmem/{maps/{zone9,zone3obr,zone6},stock,common-res,base-res,options-template.json}.
# Output directories must be new. Opening mode never saves preserved fixtures;
# benchmark mode paints/saves additional copies created inside build-tests-hdmem.
from pathlib import Path
import os,subprocess,time,json,uuid,sys,hashlib,re,shutil
import ctypes,uuid,subprocess,msvcrt,os
from ctypes import wintypes
class PMC(ctypes.Structure):
 _fields_=[('cb',wintypes.DWORD),('PageFaultCount',wintypes.DWORD)]+[(s,ctypes.c_size_t) for s in ('PeakWorkingSetSize','WorkingSetSize','QuotaPeakPagedPoolUsage','QuotaPagedPoolUsage','QuotaPeakNonPagedPoolUsage','QuotaNonPagedPoolUsage','PagefileUsage','PeakPagefileUsage','PrivateUsage')]
psapi=ctypes.WinDLL('psapi');psapi.GetProcessMemoryInfo.argtypes=[wintypes.HANDLE,ctypes.POINTER(PMC),wintypes.DWORD]
kernel=ctypes.WinDLL('kernel32',use_last_error=True);user=ctypes.WinDLL('user32',use_last_error=True)
class SI(ctypes.Structure):
 _fields_=[('cb',wintypes.DWORD),('lpReserved',wintypes.LPWSTR),('lpDesktop',wintypes.LPWSTR),('lpTitle',wintypes.LPWSTR)]+[(n,wintypes.DWORD) for n in ('dwX','dwY','dwXSize','dwYSize','dwXCountChars','dwYCountChars','dwFillAttribute','dwFlags')]+[('wShowWindow',wintypes.WORD),('cbReserved2',wintypes.WORD),('lpReserved2',ctypes.POINTER(ctypes.c_byte)),('hStdInput',wintypes.HANDLE),('hStdOutput',wintypes.HANDLE),('hStdError',wintypes.HANDLE)]
class PI(ctypes.Structure):
 _fields_=[('hProcess',wintypes.HANDLE),('hThread',wintypes.HANDLE),('dwProcessId',wintypes.DWORD),('dwThreadId',wintypes.DWORD)]
user.CreateDesktopW.argtypes=[wintypes.LPCWSTR,wintypes.LPCWSTR,ctypes.c_void_p,wintypes.DWORD,wintypes.DWORD,ctypes.c_void_p];user.CreateDesktopW.restype=wintypes.HANDLE
user.CloseDesktop.argtypes=[wintypes.HANDLE]
kernel.CreateProcessW.argtypes=[wintypes.LPCWSTR,wintypes.LPWSTR,ctypes.c_void_p,ctypes.c_void_p,wintypes.BOOL,wintypes.DWORD,ctypes.c_void_p,wintypes.LPCWSTR,ctypes.POINTER(SI),ctypes.POINTER(PI)]
kernel.WaitForSingleObject.argtypes=[wintypes.HANDLE,wintypes.DWORD];kernel.GetExitCodeProcess.argtypes=[wintypes.HANDLE,ctypes.POINTER(wintypes.DWORD)];kernel.TerminateProcess.argtypes=[wintypes.HANDLE,wintypes.UINT];kernel.CloseHandle.argtypes=[wintypes.HANDLE]
class HiddenProcess:
 def __init__(self,args,cwd,env,stdout=None):
  self.desktopName='AtamanHdMemory_'+uuid.uuid4().hex;self.desktop=user.CreateDesktopW(self.desktopName,None,None,0,0x10000000,None);assert self.desktop,ctypes.get_last_error()
  si=SI();si.cb=ctypes.sizeof(si);si.lpDesktop='WinSta0\\'+self.desktopName;si.dwFlags=1;si.wShowWindow=0;pi=PI()
  if stdout:
   handle=msvcrt.get_osfhandle(stdout.fileno());os.set_handle_inheritable(handle,True)
   si.dwFlags|=0x100;si.hStdOutput=si.hStdError=handle
  block=ctypes.create_unicode_buffer('\0'.join(k+'='+v for k,v in sorted(env.items()))+'\0\0');cmd=ctypes.create_unicode_buffer(subprocess.list2cmdline(args))
  ok=kernel.CreateProcessW(None,cmd,None,None,bool(stdout),0x08000400,block,str(cwd),ctypes.byref(si),ctypes.byref(pi));assert ok,ctypes.get_last_error();self._handle=pi.hProcess;kernel.CloseHandle(pi.hThread);self.returncode=None
 def poll(self):
  if kernel.WaitForSingleObject(self._handle,0)==0:
   n=wintypes.DWORD();assert kernel.GetExitCodeProcess(self._handle,ctypes.byref(n));self.returncode=n.value
  return self.returncode
 def wait(self):kernel.WaitForSingleObject(self._handle,0xffffffff);return self.poll()
 def kill(self):kernel.TerminateProcess(self._handle,99)
 def close(self):kernel.CloseHandle(self._handle);user.CloseDesktop(self.desktop)
def memory(proc):
 m=PMC();m.cb=ctypes.sizeof(m)
 if not psapi.GetProcessMemoryInfo(wintypes.HANDLE(int(proc._handle)),ctypes.byref(m),m.cb):return None
 return {s:getattr(m,s) for s in ('PeakWorkingSetSize','WorkingSetSize','PagefileUsage','PeakPagefileUsage','PrivateUsage')}

if __name__=='__main__' and (len(sys.argv)==1 or sys.argv[1] in ['--help','-h']):
 print('Usage: terrain_hd_memory_measure.py RUN_LABEL [REPETITIONS=1] [--benchmark]\nSet EI_HDMEM_QT_BIN, EI_HDMEM_MINGW_BIN and EI_HDMEM_PROBE_EXE.\nPlace copied fixtures and options-template.json in build-tests-hdmem as described above.\nBenchmark mode creates additional copies, then measures cold paint/save/full upload.\nNew output directories, shared UI lock and in-memory clipboard isolation are required.')
 raise SystemExit(0)
P=Path(__file__).resolve().parent.parent;R=P/'build-tests-hdmem'
QT=Path(os.environ['EI_HDMEM_QT_BIN']);MG=Path(os.environ['EI_HDMEM_MINGW_BIN'])
ENV=dict(os.environ);ENV['PATH']=str(QT)+';'+str(MG)+';'+ENV['PATH'];ENV['QT_QPA_PLATFORM_PLUGIN_PATH']=str(QT.parent/'plugins')
ENV.pop('EI_HDMEM_BENCHMARK',None) # The opening-only runner must never save source fixtures.
ENV['EI_MAPPER_TEST_BACKUP_ROOT']=str(R/'test-backups')
for key in ['LOCALAPPDATA','APPDATA','TEMP','TMP']:
 d=R/key;d.mkdir(exist_ok=True);ENV[key]=str(d)
class Lock:
 def __enter__(self):
  self.path=P/'build-locks/ui-tests.lock';self.token='EI-MAPPER-HD-MEMORY '+str(os.getpid())+' '+uuid.uuid4().hex;start=time.monotonic()-float(ENV.pop('EI_HDMEM_LOCK_WAIT_USED','0'))
  while True:
   try:self.path.mkdir();break
   except FileExistsError:
    if time.monotonic()-start>=3600:raise RuntimeError('UI lock TIMEOUT 60 minutes')
    print('Waiting for UI lock',round(time.monotonic()-start),flush=True);time.sleep(20)
  (self.path/'owner.txt').write_text(self.token);return self
 def __exit__(self,*args):
  owner=self.path/'owner.txt';assert owner.read_text()==self.token;owner.unlink();self.path.rmdir()
def hidden(args,name,cwd,env=ENV,timeout=600,clipboard=True):
 assert (P/'build-locks/ui-tests.lock').is_dir();t=time.monotonic();samples=[];events=[];offset=0;ready=None
 with (R/(name+'.out')).open('wb') as output:
  proc=HiddenProcess(list(map(str,args)),cwd,env,stdout=output)
  try:
   while proc.poll() is None:
    m=memory(proc)
    if m:samples.append(dict(seconds=time.monotonic()-t,**m))
    if ready is None and (Path(cwd)/'audit.json.ready').exists() and (Path(cwd)/'audit.json.ready').stat().st_size:ready=samples[-2] if len(samples)>1 else m
    log=Path(cwd)/'logs/workflow.log'
    if log.exists():
     with log.open('rb') as stream:stream.seek(offset);chunk=stream.read();offset=stream.tell()
     for line in chunk.decode('utf8',errors='replace').splitlines():
      if re.search(r'Loading|Resolved terrain atlas|Uploading terrain|ready map:|deferred editor load phases|Terrain.*catalog|atlas.*binding|Working terrain',line,re.I):events.append(dict(seconds=time.monotonic()-t,line=line,memory=m))
    if time.monotonic()-t>timeout:proc.kill();proc.wait();raise RuntimeError(name+' TIMEOUT')
    time.sleep(.02)
   rc=proc.wait()
  finally:proc.close()
 out=(R/(name+'.out')).read_text(encoding='utf8',errors='replace');assert rc==0,(name,rc,out[-2000:])
 if clipboard:assert 'nativeAttempts=0' in out and 'PASS: clipboard isolation' in out,out[-2000:]
 opening=[x for x in samples if ready is None or x['seconds']<=ready.get('seconds',1e99)]
 result=dict(exit=rc,seconds=time.monotonic()-t,peakWorkingSet=max(x['PeakWorkingSetSize'] for x in samples),peakPrivate=max(x['PrivateUsage'] for x in samples),openingPeakWorkingSet=max(x['PeakWorkingSetSize'] for x in opening),openingPeakPrivate=max(x['PrivateUsage'] for x in opening),samples=samples,stages=events,readyMemory=ready)
 (R/(name+'-memory.json')).write_text(json.dumps(result))
 return result
if __name__=='__main__':
 benchmark='--benchmark' in sys.argv;arguments=[a for a in sys.argv[1:] if a!='--benchmark'];kind=arguments[0];repeat=int(arguments[1]) if len(arguments)>1 else 1
 assert re.fullmatch(r'[A-Za-z0-9_-]+',kind) and 1<=repeat<=5,'Use a simple run label and 1..5 repetitions'
 exe=Path(os.environ.get('EI_HDMEM_PROBE_EXE',str(R/('probe-'+kind)/'release/probe.exe')));results=[]
 assert exe.resolve().is_relative_to(R.resolve()) and exe.name in ['probe.exe','terrain_hd_memory_probe.exe'],'Use the isolated production-linked clipboard-safe probe'
 for n in range(repeat):
  for stem in ['zone9','zone3obr','zone6']:
   d=R/(('bench-' if benchmark else 'opening-')+kind+'-'+stem+'-'+str(n));d.mkdir();maps=R/'maps'/stem
   assert maps.resolve().is_relative_to(R.resolve()),'Use private fixture directories only'
   if benchmark:
    originals=list(maps.glob('*.mpr'))+[maps/'HDPack_Terrain.res'];assert len(originals)==2 and all(p.resolve().is_relative_to(R.resolve()) for p in originals),'Use one copied MPR and one copied RES'
    destination=d/'maps';destination.mkdir()
    for p in originals:shutil.copy2(p,destination/p.name)
    maps=destination
   mapfile=next(maps.glob('*.mpr'));archive=maps/'HDPack_Terrain.res'
   assert mapfile.resolve().is_relative_to(R.resolve()) and archive.resolve().is_relative_to(R.resolve()) and archive.is_file(),'Use private fixture copies only'
   opts=json.loads((R/'options-template.json').read_text(encoding='utf8'));opts['General'].update(gameRoot='',autosaveEnabled=False,recentFiles=[],openLastSessionOnStartup=False,logLevel=2)
   opts['Resource'].update(resourceProfile='deja-vu',resourceRoots=[str(archive),str(R/'stock/HDPack_Terrain.res'),str(R/'common-res'),str(R/'base-res')],mapRoots=[str(maps)])
   (d/'endurance-options.json').write_text(json.dumps(opts,ensure_ascii=False),encoding='utf8')
   env=ENV.copy();env.update(EI_AUDIT_OUTPUT=str(d/'audit.json'),EI_AUDIT_MAP=str(mapfile),EI_AUDIT_ARCHIVE=str(archive),EI_AUDIT_GENERIC='1',EI_AUDIT_EXPECT_SEMANTIC_FAILURE='0')
   if benchmark:env.update(EI_HDMEM_BENCHMARK='1',EI_HDMEM_ROOT=str(R))
   with Lock():m=hidden([exe,'--desktop-opengl',mapfile],d.name,d,env)
   audit=json.loads((d/'audit.json').read_text());assert audit['status']=='PASS' and audit['exactProfile'] and audit['semanticVerticesExact'],audit
   m['workingSetReady']=m['readyMemory']['WorkingSetSize'];m['privateReady']=m['readyMemory']['PrivateUsage'];m.pop('samples')
   if benchmark:
    assert audit['savedReadback'] and len(audit['paintMs'])==20 and len(audit['fullAtlasUploadMs'])==3
    assert hashlib.sha256(mapfile.read_bytes()).hexdigest()==audit['savedMprSha256']
   else:m['peakWorkingSet']=m['openingPeakWorkingSet'];m['peakPrivate']=m['openingPeakPrivate']
   audit.update(m);results.append(dict(map=stem,run=n,**audit));(R/(('benchmark-' if benchmark else 'profile-')+kind+'.json')).write_text(json.dumps(results,indent=2))
   print(kind,stem,n,'peakMiB',round(m['peakWorkingSet']/2**20,1),'readyMiB',round(m['workingSetReady']/2**20,1),'readyMs',audit['readyMs'],flush=True)
