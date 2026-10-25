param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$QtBin,
      [Parameter(Mandatory=$true)][string]$MinGwBin,
      [string]$ReleaseRoot,
      [switch]$Headless)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $RunRoot){throw 'RunRoot must be new'}
$run=[IO.Path]::GetFullPath($RunRoot)
foreach($name in @('app','Maps','res','Starter/MODS/DejaVu/Maps','Starter/MODS/DejaVu/res','Starter/Engine','settings','AppData','LocalAppData','User','Temp')){
 [IO.Directory]::CreateDirectory((Join-Path $run $name))|Out-Null
}
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$options=Get-Content -Encoding UTF8 (Join-Path $installation 'endurance-options.json') -Raw|ConvertFrom-Json
$maps=Join-Path $run 'Starter/MODS/DejaVu/Maps';$res=Join-Path $run 'Starter/MODS/DejaVu/res'
$sourceMaps=$options.Resource.mapRoots[0];$sourceRes=$options.Resource.resourceRoots[0]
$sourceMod=Split-Path -Parent $sourceMaps
$worldSource=Join-Path $sourceMod 'world-map-layouts.json'
$world=Get-Content -Encoding UTF8 $worldSource -Raw|ConvertFrom-Json
$names=@('zone7.mpr','zone7.mob','zone6.mpr','zone6.mob')
foreach($profile in $world.profiles){foreach($allod in $profile.allods){if($allod.allodId -eq 'gipat'){foreach($zone in $allod.zones){
 foreach($p in @($zone.mprPath,$zone.mobPath)){if($p){$names+=[IO.Path]::GetFileName($p)}}
}}}}
$names+=@(Get-ChildItem -LiteralPath $sourceMaps -Filter 'Autozone7*.mob'|ForEach-Object Name)
$inputs=@();foreach($name in ($names|Sort-Object -Unique)){$inputs+=@{source=(Join-Path $sourceMaps $name);target=(Join-Path $maps $name)}}
$inputs+=@{source=$worldSource;target=(Join-Path $run 'Starter/MODS/DejaVu/world-map-layouts.json')}
foreach($archive in Get-ChildItem -LiteralPath $sourceRes -Filter '*.res'){
 $inputs+=@{source=$archive.FullName;target=(Join-Path $res $archive.Name);link=($archive.Name -ne 'texts.res')}
}
foreach($archive in Get-ChildItem -LiteralPath $options.Resource.resourceRoots[1] -Filter '*.res'){
 $inputs+=@{source=$archive.FullName;target=(Join-Path $run ('res/'+$archive.Name));link=$true}
}
$guard=@((Join-Path $repo '.mcp.json'),(Join-Path $installation 'endurance-options.json'),(Join-Path $installation 'VERSION'),(Join-Path $installation 'SHA256SUMS.txt'))
$receipts=foreach($path in (($inputs|ForEach-Object {$_.source})+$guard)){
 $file=Get-Item -LiteralPath $path
 @{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;length=$file.Length;mtime=$file.LastWriteTimeUtc.Ticks}
}
$receipts|ConvertTo-Json|Set-Content -Encoding UTF8 (Join-Path $run 'protected-before.json')
foreach($i in $inputs){if($i.link){New-Item -ItemType HardLink -Path $i.target -Target $i.source|Out-Null}else{Copy-Item -LiteralPath $i.source -Destination $i.target}}
[IO.File]::WriteAllBytes((Join-Path $run 'res/empty.res'),[byte[]](0x3c,0xe2,0x9c,0x01,0,0,0,0,16,0,0,0,0,0,0,0))
foreach($name in @('game.exe','Starter/Engine/game.exe','Starter/MODS/DejaVu/main.lua')){[IO.File]::WriteAllText((Join-Path $run $name),'isolated profile marker; never execute')}
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/neighbor_retain_check.exe')
if($Headless){
 if(-not $ReleaseRoot){throw 'Headless requires fresh ReleaseRoot'}
 foreach($name in @('EIMapperEndurance.exe','EIMapperMcp.exe')){Copy-Item -LiteralPath (Join-Path $ReleaseRoot $name) -Destination (Join-Path $run ('app/'+$name))}
}
foreach($name in @('data','assets','resource-profiles')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
$options.General.gameRoot=$run.Replace('\','/');$options.General.recentFiles=@();$options.General.autosaveEnabled=$false;$options.General.openLastSessionOnStartup=$false
$options.General.lastVisitedFolder=$maps.Replace('\','/')
$options.Resource.resourceProfile='deja-vu';$options.Resource.mapRoots=@($maps.Replace('\','/'),(Join-Path $run 'Maps').Replace('\','/'))
$options.Resource.resourceRoots=@($res.Replace('\','/'),(Join-Path $run 'res').Replace('\','/'));$options.Resource.terrainAuthoringArchive=(Join-Path $run 'res/empty.res').Replace('\','/')
# Remove legacy explicit resource paths as well; all resource access uses the isolated profile.
if($options.Resource.PSObject.Properties['texPaths']){$options.Resource.texPaths=@()}
if($options.Resource.PSObject.Properties['figPaths']){$options.Resource.figPaths=@()}
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
foreach($pair in @(@('APPDATA','AppData'),@('LOCALAPPDATA','LocalAppData'),@('USERPROFILE','User'),@('TEMP','Temp'),@('TMP','Temp'))){[Environment]::SetEnvironmentVariable($pair[0],(Join-Path $run $pair[1]),'Process')}
$env:EI_NEIGHBOR_TEST_ROOT=$run;$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation';$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups'
$env:EI_NEIGHBOR_HEADLESS=if($Headless){'1'}else{''}
$env:PATH="$QtBin;$MinGwBin;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/neighbor_retain_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(900000)){Stop-Process -Id $p.Id;throw 'Owned test timeout'}
$p.WaitForExit();$code=$p.ExitCode
@{exitCode=$code;pid=$p.Id;seconds=$timer.Elapsed.TotalSeconds}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
$drift=@();foreach($r in $receipts){$file=Get-Item -LiteralPath $r.path;if((Get-FileHash -LiteralPath $r.path).Hash -ne $r.sha256 -or $file.Length -ne $r.length -or $file.LastWriteTimeUtc.Ticks -ne $r.mtime){$drift+=$r.path}}
@{unchanged=($drift.Count -eq 0);drift=$drift}|ConvertTo-Json|Set-Content (Join-Path $run 'preservation.json')
Get-Content -Encoding UTF8 (Join-Path $run 'stdout.txt') -Tail 8
if($drift.Count -gt 0 -or $null -eq $code){exit 1};exit $code
