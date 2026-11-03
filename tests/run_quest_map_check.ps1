param([Parameter(Mandatory=$true)][string]$RunRoot,
 [Parameter(Mandatory=$true)][string]$TestExe,
 [Parameter(Mandatory=$true)][string]$ReleaseRoot,
 [ValidateSet('zone1','zone7')][string]$Zone='zone1')
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
$run=[IO.Path]::GetFullPath($RunRoot)
if(Test-Path -LiteralPath $run){throw 'RunRoot must be new'}
New-Item -ItemType Directory -Path $run|Out-Null
foreach($name in @('app','Maps','res','reference','AppData','LocalAppData','User','Temp','settings','machine-locks','Starter/Engine','Starter/MODS/DejaVu/Maps','Starter/MODS/DejaVu/res','Starter/MODS/DejaVu/lua')){New-Item -ItemType Directory -Path (Join-Path $run $name)|Out-Null}
$maps=Join-Path $run 'Starter/MODS/DejaVu/Maps';$modRes=Join-Path $run 'Starter/MODS/DejaVu/res'
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$optionSource=Join-Path $installation 'endurance-options.json'
$options=Get-Content -LiteralPath $optionSource -Raw -Encoding UTF8|ConvertFrom-Json
$sourceMaps=$options.Resource.mapRoots[0];$inputs=@();$receipts=@()
$inputs+=@{source=(Join-Path $sourceMaps ($Zone+'.mpr'));target=(Join-Path $maps ($Zone+'.mpr'));link=$false}
# Discover family members with the real MOB decoder (SS_TEXT is encrypted).
# A raw byte regex cannot see AddMob and must never define fixture completeness.
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User';$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_QUEST_DISCOVER_MAPS=$sourceMaps;$env:EI_QUEST_DISCOVER_OUTPUT=Join-Path $run 'family.json';$env:EI_QUEST_TEST_ZONE=$Zone
$discovery=Start-Process -FilePath $TestExe -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'discover.stdout') -RedirectStandardError (Join-Path $run 'discover.stderr')
$null=$discovery.Handle;if(-not $discovery.WaitForExit(60000)){Stop-Process -Id $discovery.Id;throw 'Owned family discovery timeout'};$discovery.WaitForExit();if($discovery.ExitCode -ne 0){throw 'Native family discovery failed'}
Remove-Item Env:EI_QUEST_DISCOVER_MAPS,Env:EI_QUEST_DISCOVER_OUTPUT
$family=Get-Content -LiteralPath (Join-Path $run 'family.json') -Raw -Encoding UTF8|ConvertFrom-Json
foreach($path in $family){$inputs+=@{source=$path;target=(Join-Path $maps ([IO.Path]::GetFileName($path)));link=$false}}
$env:EI_QUEST_TEST_FAMILY_COUNT=@($family).Count
foreach($pair in @(@{source=$options.Resource.resourceRoots[0];target=$modRes},@{source=$options.Resource.resourceRoots[1];target=(Join-Path $run 'res')})){
 foreach($file in Get-ChildItem -LiteralPath $pair.source -Filter '*.res'){$inputs+=@{source=$file.FullName;target=(Join-Path $pair.target $file.Name);link=$true}}
 foreach($name in @('model-catalog.json','mapitems-catalog.json','mapweapons-catalog.json','music.reg','music.ini')){$file=Join-Path $pair.source $name;if(Test-Path -LiteralPath $file){$inputs+=@{source=$file;target=(Join-Path $pair.target $name);link=$false}}}
}
$inputs+=@{source=$options.Resource.terrainAuthoringArchive;target=(Join-Path $run 'reference/terrain-reference.res');link=$true}
$lua=Join-Path (Split-Path -Parent $options.Resource.resourceRoots[0]) 'lua/tables.lua';if(Test-Path $lua){$inputs+=@{source=$lua;target=(Join-Path $run 'Starter/MODS/DejaVu/lua/tables.lua');link=$false}}
$protected=@($optionSource)
foreach($line in Get-Content -LiteralPath (Join-Path $installation 'SHA256SUMS.txt')){if($line -match '^[a-fA-F0-9]{64} \*(.+)$'){$protected+=Join-Path $installation $Matches[1]}}
foreach($path in (($inputs|ForEach-Object {$_.source})+$protected|Select-Object -Unique)){
 $file=Get-Item -LiteralPath $path;$receipts+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path).Hash;length=$file.Length;writeTicks=$file.LastWriteTimeUtc.Ticks}
}
$receipts|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $run 'protected-before.json') -Encoding UTF8
foreach($item in $inputs){if($item.link){New-Item -ItemType HardLink -Path $item.target -Target $item.source|Out-Null}else{Copy-Item -LiteralPath $item.source -Destination $item.target}}
# Only the independent physical copy below is an export destination. Read-only
# source links are never supplied to any save/export operation.
Copy-Item -LiteralPath (Join-Path $options.Resource.resourceRoots[0] 'textures.res') -Destination (Join-Path $run 'export-copy.res')
[IO.File]::WriteAllText((Join-Path $run 'Starter/MODS/DejaVu/main.lua'),'-- isolated profile marker')
[IO.File]::WriteAllText((Join-Path $run 'Starter/Engine/game.exe'),'non-executable profile marker')
[IO.File]::WriteAllText((Join-Path $run 'game.exe'),'non-executable profile marker')
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/quest_map_check.exe')
foreach($name in @('data','assets')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
New-Item -ItemType Directory -Path (Join-Path $run 'app/imageformats')|Out-Null
$ddsPlugin=Join-Path (Split-Path -Parent $env:EI_MAPPER_QT_BIN) 'plugins/imageformats/qdds.dll'
if(Test-Path -LiteralPath $ddsPlugin){Copy-Item -LiteralPath $ddsPlugin -Destination (Join-Path $run 'app/imageformats/qdds.dll')}
$options.General.gameRoot=$run.Replace('\','/');$options.General.recentFiles=@();$options.General.autosaveEnabled=$false;$options.General.openLastSessionOnStartup=$false;$options.General.lastVisitedFolder=$maps.Replace('\','/')
$options.Resource.resourceProfile='deja-vu';$options.Resource.mapRoots=@($maps.Replace('\','/'));$options.Resource.resourceRoots=@($modRes.Replace('\','/'),(Join-Path $run 'res').Replace('\','/'));$options.Resource.terrainAuthoringArchive=(Join-Path $run 'reference/terrain-reference.res').Replace('\','/')
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User';$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_MAPS=$maps;$env:EI_QUEST_TEST_ZONE=$Zone
$env:EI_MAPPER_TEST_LOCK_ROOT=Join-Path $run 'machine-locks';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation';$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings';$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew();$process=Start-Process -FilePath (Join-Path $run 'app/quest_map_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$process.Handle;if(-not $process.WaitForExit(900000)){Stop-Process -Id $process.Id;throw 'Owned test timeout'};$process.WaitForExit();$code=$process.ExitCode
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$process.Id;zone=$Zone}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'process.json') -Encoding UTF8
$drift=@();$metadataDrift=@();foreach($item in $receipts){$file=Get-Item -LiteralPath $item.path;if((Get-FileHash -LiteralPath $item.path).Hash -ne $item.sha256 -or $file.Length -ne $item.length){$drift+=$item.path};if($file.LastWriteTimeUtc.Ticks -ne $item.writeTicks){$metadataDrift+=@{path=$item.path;beforeTicks=$item.writeTicks;afterTicks=$file.LastWriteTimeUtc.Ticks}}}
ConvertTo-Json -InputObject $drift|Set-Content -LiteralPath (Join-Path $run 'external-drift.json') -Encoding UTF8
ConvertTo-Json -InputObject $metadataDrift|Set-Content -LiteralPath (Join-Path $run 'external-metadata-drift.json') -Encoding UTF8
if($null -eq $code -or $code -ne 0 -or $drift.Count){throw "Native check failed: exit=$code externalDrift=$($drift.Count)"}
