param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$MockExe,
      [Parameter(Mandatory=$true)][string]$HandoffRoot,
      [string]$QtBin=$env:EI_MAPPER_QT_BIN,[string]$MinGwBin=$env:EI_MAPPER_MINGW_BIN,[switch]$BakeProbe)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $RunRoot){throw 'RunRoot must be new'}
$run=[IO.Path]::GetFullPath($RunRoot)
if(-not $run.StartsWith($repo+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture must stay inside worktree'}
$handoff=[IO.Path]::GetFullPath($HandoffRoot)
$baker=Join-Path $handoff 'Engine64/EIRebuild64NavBake.exe'
if((Get-FileHash -LiteralPath $baker -Algorithm SHA256).Hash -ne '85df0ef34432bfc3164cb83067aa9afd1153fdf96ae39e44b61b636ad6dc5ad7'){throw 'Expected r13 baker SHA256 mismatch'}
$install='C:\GOG Games\maped\ei-mapper-ataman\artifacts\EI-Mapper-Ataman-2-first-test'
$game='C:\GOG Games\PZ'
$mod='MODS/Dejavu'
foreach($dir in @('app','game/Base/Res','game/Base/Maps','game/MODS/Dejavu/res','game/MODS/Dejavu/Maps/Nav64','game/Engine','settings','AppData','LocalAppData','User','Temp')){[IO.Directory]::CreateDirectory((Join-Path $run $dir))|Out-Null}
$inputs=@()
foreach($folder in @('Base/Res','MODS/Dejavu/res')){foreach($f in Get-ChildItem -LiteralPath (Join-Path $game $folder) -File -Filter '*.res'){
 $target=Join-Path $run ('game/'+$folder+'/'+$f.Name);New-Item -ItemType HardLink -Path $target -Target $f.FullName|Out-Null
 $inputs+=@{path=$f.FullName;length=$f.Length;mtime=$f.LastWriteTimeUtc.Ticks;mode='hardlink read only'}
}}
foreach($name in @('bz19g','zone1','zone14','zone7','zone6','Autozone7letter')){foreach($ext in @('mpr','mob')){
 $source=Join-Path $game ($mod+'/Maps/'+$name+'.'+$ext)
 if(-not(Test-Path -LiteralPath $source)){continue}
 $target=Join-Path $run ('game/'+$mod+'/Maps/'+$name+'.'+$ext);Copy-Item -LiteralPath $source -Destination $target
 $f=Get-Item -LiteralPath $source;$inputs+=@{path=$source;sha256=(Get-FileHash -LiteralPath $source).Hash;length=$f.Length;mtime=$f.LastWriteTimeUtc.Ticks;mode='private copy'}
}}
foreach($name in @('bz19g','zone1','zone14')){foreach($f in Get-ChildItem -LiteralPath (Join-Path $handoff ($mod+'/Maps/Nav64')) -Filter ($name+'--'+$name+'.*.nav64')){
 Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $run ('game/'+$mod+'/Maps/Nav64/'+$f.Name))
 $inputs+=@{path=$f.FullName;sha256=(Get-FileHash -LiteralPath $f.FullName).Hash;length=$f.Length;mtime=$f.LastWriteTimeUtc.Ticks;mode='private copy'}
}}
foreach($folder in @('Base',$mod,'Base/Res',($mod+'/res'))){foreach($f in Get-ChildItem -LiteralPath (Join-Path $game $folder) -File|Where-Object Extension -in @('.reg','.lua')){Copy-Item -LiteralPath $f.FullName -Destination (Join-Path $run ('game/'+$folder+'/'+$f.Name))}}
foreach($folder in @('Base/config',($mod+'/config'),'Base/camera',($mod+'/camera'),'Base/lua',($mod+'/lua'))){if(Test-Path -LiteralPath (Join-Path $game $folder)){Copy-Item -LiteralPath (Join-Path $game $folder) -Destination (Join-Path $run ('game/'+$folder)) -Recurse}}
foreach($name in @('main.lua','world-map-layouts.json')){ $source=Join-Path $game ($mod+'/'+$name);if(Test-Path -LiteralPath $source){Copy-Item -LiteralPath $source -Destination (Join-Path $run ('game/'+$mod+'/'+$name))} }
[IO.File]::WriteAllText((Join-Path $run 'game/Engine/game.exe'),'profile marker only; never execute')
[IO.File]::WriteAllBytes((Join-Path $run 'game/MODS/Dejavu/res/empty.res'),[byte[]](0x3c,0xe2,0x9c,0x01,0,0,0,0,16,0,0,0,0,0,0,0))
$options=Get-Content -LiteralPath (Join-Path $install 'endurance-options.json') -Encoding UTF8 -Raw|ConvertFrom-Json
$options.General.gameRoot=(Join-Path $run 'game').Replace('\','/');$options.General.recentFiles=@();$options.General.autosaveEnabled=$false;$options.General.openLastSessionOnStartup=$false
$options.General.lastVisitedFolder=(Join-Path $run 'game/MODS/Dejavu/Maps').Replace('\','/')
$options.Resource.mapRoots=@((Join-Path $run 'game/MODS/Dejavu/Maps').Replace('\','/'),(Join-Path $run 'game/Base/Maps').Replace('\','/'))
$options.Resource.resourceRoots=@((Join-Path $run 'game/MODS/Dejavu/res').Replace('\','/'),(Join-Path $run 'game/Base/Res').Replace('\','/'))
$options.Resource.terrainAuthoringArchive=(Join-Path $run 'game/MODS/Dejavu/res/empty.res').Replace('\','/')
foreach($key in @('texPaths','figPaths')){if($options.Resource.PSObject.Properties[$key]){$options.Resource.$key=@()}}
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/nav64_check.exe')
Copy-Item -LiteralPath $MockExe -Destination (Join-Path $run 'app/nav64_mock_baker.exe')
foreach($name in @('data','assets','resource-profiles')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
foreach($path in @((Join-Path $install 'endurance-options.json'),(Join-Path $install 'VERSION'),(Join-Path $install 'SHA256SUMS.txt'),(Join-Path $handoff 'Engine64/EIRebuild64NavBake.exe'))){$f=Get-Item -LiteralPath $path;$inputs+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;length=$f.Length;mtime=$f.LastWriteTimeUtc.Ticks;mode='protected read only'}}
$inputs|ConvertTo-Json -Depth 5|Set-Content -Encoding UTF8 (Join-Path $run 'protected-before.json')
foreach($pair in @(@('APPDATA','AppData'),@('LOCALAPPDATA','LocalAppData'),@('USERPROFILE','User'),@('TEMP','Temp'),@('TMP','Temp'))){[Environment]::SetEnvironmentVariable($pair[0],(Join-Path $run $pair[1]),'Process')}
$env:EI_NAV64_TEST_ROOT=$run;$env:EI_NAV64_BAKER=Join-Path $handoff 'Engine64/EIRebuild64NavBake.exe'
if($BakeProbe){$env:EI_NAV64_BAKE_PROBE='1'}
$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation';$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups'
$env:PATH="$QtBin;$MinGwBin;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/nav64_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(1200000)){Stop-Process -Id $p.Id;throw 'Owned nav64 check timeout'}
$p.WaitForExit();$code=$p.ExitCode
@{exitCode=$code;pid=$p.Id;seconds=$timer.Elapsed.TotalSeconds;testExeSha256=(Get-FileHash -LiteralPath $TestExe).Hash}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
$drift=@();foreach($i in $inputs){$f=Get-Item -LiteralPath $i.path;if($f.Length -ne $i.length -or $f.LastWriteTimeUtc.Ticks -ne $i.mtime -or ($i.sha256 -and (Get-FileHash -LiteralPath $i.path).Hash -ne $i.sha256)){$drift+=$i.path}}
@{unchanged=($drift.Count -eq 0);drift=$drift}|ConvertTo-Json|Set-Content (Join-Path $run 'preservation.json')
Get-Content -LiteralPath (Join-Path $run 'stdout.txt') -Encoding UTF8 -Tail 12
if($drift.Count -gt 0 -or $null -eq $code){exit 1};exit $code
