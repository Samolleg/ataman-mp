param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$ReleaseRoot,

      [string]$BeforeRoot,
      [ValidateSet("zone1","basecam","simple274","zone1-dawn")][string]$Scenario="zone1")
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$run=[IO.Path]::GetFullPath($RunRoot)
if(Test-Path -LiteralPath $run){throw 'RunRoot must be new'}
New-Item -ItemType Directory -Path $run | Out-Null
foreach($name in @('app','Maps','res','reference','AppData','LocalAppData','User','Temp','settings','Starter/Engine','Starter/MODS/DejaVu/Maps','Starter/MODS/DejaVu/res','Starter/MODS/DejaVu/lua')) {
 New-Item -ItemType Directory -Path (Join-Path $run $name) | Out-Null
}
$maps=Join-Path $run 'Starter/MODS/DejaVu/Maps'
$modRes=Join-Path $run 'Starter/MODS/DejaVu/res'
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$options=Get-Content -LiteralPath (Join-Path $installation 'endurance-options.json') -Raw -Encoding UTF8 | ConvertFrom-Json
$inputs=@();$receipts=@()
$sourceMaps=$options.Resource.mapRoots[0]
# Maps are writable private copies. Resource archives are hard links until a
# native atomic publication replaces an isolated path. Original hashes guarded.
foreach($name in @('zone1.mpr','zone1.mob','basecam.mob','Autozone1cyclopus.mob','Autozone1newhunterscamp.mob')){
 $inputs+=@{source=(Join-Path $sourceMaps $name);target=(Join-Path $maps $name);link=$false}
}
# An isolated alias of the exact zone1 terrain opens basecam via native companion
# pairing. Both bytes are unchanged; no Base-game terrain is used.
$inputs+=@{source=(Join-Path $sourceMaps 'zone1.mpr');target=(Join-Path $maps 'basecam.mpr');link=$false}
$modSource=$options.Resource.resourceRoots[0];$baseSource=$options.Resource.resourceRoots[1]
foreach($pair in @(@{source=$modSource;target=$modRes},@{source=$baseSource;target=(Join-Path $run 'res')})){
 foreach($f in Get-ChildItem -LiteralPath $pair.source -File -Filter '*.res'){
  $inputs+=@{source=$f.FullName;target=(Join-Path $pair.target $f.Name);link=$true}
 }
 foreach($name in @('model-catalog.json','mapitems-catalog.json','mapweapons-catalog.json','music.reg','music.ini')){
  $catalog=Join-Path $pair.source $name
  if([IO.File]::Exists($catalog)){$inputs+=@{source=$catalog;target=(Join-Path $pair.target $name);link=$false}}
 }
}
$inputs+=@{source=$options.Resource.terrainAuthoringArchive;target=(Join-Path $run 'reference/terrain-reference.res');link=$true}
$inputs+=@{source=(Join-Path (Split-Path -Parent $options.Resource.resourceRoots[0]) 'lua/tables.lua');target=(Join-Path $run 'Starter/MODS/DejaVu/lua/tables.lua');link=$false}
$protected=@((Join-Path $installation 'endurance-options.json'))
foreach($line in Get-Content -LiteralPath (Join-Path $installation 'SHA256SUMS.txt')){
 if($line -match '^[a-fA-F0-9]{64} \*(.+)$'){$protected+=Join-Path $installation $Matches[1]}
}
foreach($path in (($inputs|ForEach-Object {$_.source})+$protected|Select-Object -Unique)){
 if(Test-Path -LiteralPath $path -PathType Leaf){$f=Get-Item -LiteralPath $path;$receipts+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;length=$f.Length;writeTicks=$f.LastWriteTimeUtc.Ticks}}
}
$receipts|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'protected-before.json') -Encoding UTF8
foreach($input in $inputs){if($input.link){New-Item -ItemType HardLink -Path $input.target -Target $input.source|Out-Null}else{Copy-Item -LiteralPath $input.source -Destination $input.target}}

[IO.File]::WriteAllText((Join-Path $run 'Starter/MODS/DejaVu/main.lua'),'-- isolated profile marker')
[IO.File]::WriteAllText((Join-Path $run 'Starter/Engine/game.exe'),'non-executable profile marker')
[IO.File]::WriteAllText((Join-Path $run 'game.exe'),'non-executable profile marker')
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/minimap_shading_check.exe')

Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperMcp.exe') -Destination (Join-Path $run 'app/EIMapperMcp.exe')
foreach($name in @('data','assets')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'runtime') -Destination (Join-Path $run 'app/runtime') -Recurse
$options.General.gameRoot=$run.Replace('\','/')
$options.General.recentFiles=@();$options.General.autosaveEnabled=$false;$options.General.openLastSessionOnStartup=$false
$options.General.lastVisitedFolder=$maps.Replace('\','/')
$options.Resource.resourceProfile='deja-vu'
$options.Resource.mapRoots=@($maps.Replace('\','/'))
$options.Resource.resourceRoots=@($modRes.Replace('\','/'),(Join-Path $run 'res').Replace('\','/'))
$options.Resource.terrainAuthoringArchive=(Join-Path $run 'reference/terrain-reference.res').Replace('\','/')
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User'
$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_MAPS=$maps;$env:EI_MCP_TEST_BRIDGE=Join-Path $run 'app/EIMapperMcp.exe'
$env:EI_MAPPER_TEST_LOCK_ROOT=Join-Path $run 'machine-locks';New-Item -ItemType Directory -Path $env:EI_MAPPER_TEST_LOCK_ROOT|Out-Null
$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation';$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings'
$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups';$env:EI_MAPPER_HEADLESS_IDLE_MS='600000'
$env:EI_MINIMAP_BEFORE=$BeforeRoot
$env:EI_MINIMAP_SCENARIO=$Scenario

$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/minimap_shading_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(900000)){Stop-Process -Id $p.Id;throw 'Owned test timeout'}
$p.WaitForExit();$code=$p.ExitCode
if($null -eq $code){throw 'Exit code unavailable'}
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$p.Id}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'process.json') -Encoding UTF8
$drift=@();foreach($receipt in $receipts){$f=Get-Item -LiteralPath $receipt.path;if((Get-FileHash -LiteralPath $receipt.path).Hash -ne $receipt.sha256 -or $f.Length -ne $receipt.length -or $f.LastWriteTimeUtc.Ticks -ne $receipt.writeTicks){$drift+=$receipt.path}}
ConvertTo-Json -InputObject $drift|Set-Content -LiteralPath (Join-Path $run 'external-drift.json') -Encoding UTF8
if($drift.Count){throw 'Protected input drift'}
Get-Content -LiteralPath (Join-Path $run 'stdout.txt') -Tail 15
exit $code
