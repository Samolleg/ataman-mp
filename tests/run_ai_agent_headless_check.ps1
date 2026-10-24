param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$ReleaseRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $RunRoot){throw 'RunRoot must be new'}
$run=[IO.Path]::GetFullPath($RunRoot)
New-Item -ItemType Directory -Path $run | Out-Null
foreach($name in @('app','Maps','res','reference','AppData','LocalAppData','User','Temp','settings')) {
    New-Item -ItemType Directory -Path (Join-Path $run $name) | Out-Null
}
$maps=Join-Path $run 'Starter/MODS/DejaVu/Maps';$res=Join-Path $run 'Starter/MODS/DejaVu/res'
foreach($path in @($maps,$res,(Join-Path $run 'Starter/Engine'))){New-Item -ItemType Directory -Path $path -Force|Out-Null}
[IO.File]::WriteAllText((Join-Path $run 'Starter/MODS/DejaVu/main.lua'),'-- isolated profile marker; never executed')
[IO.File]::WriteAllText((Join-Path $run 'Starter/Engine/game.exe'),'isolated runtime presence marker - never executable')
# The stock Dejavu profile requires a RES in both mod/res and game/Res.
# Empty, valid public-format archive; no additional game fixture is copied.
[IO.File]::WriteAllBytes((Join-Path $run 'res/isolated-empty.res'),[byte[]](0x3c,0xe2,0x9c,0x01,0,0,0,0,16,0,0,0,0,0,0,0))
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$options=Get-Content -Encoding UTF8 (Join-Path $installation 'endurance-options.json') -Raw | ConvertFrom-Json
$sourceMaps=$options.Resource.mapRoots[0]
$sourceRes=$options.Resource.resourceRoots[0]
$inputs=@(
    @{source=(Join-Path $sourceMaps 'zone1.mpr');target=(Join-Path $maps 'gipat2.mpr')},
    @{source=(Join-Path $sourceMaps 'zone1.mob');target=(Join-Path $maps 'gipat2.mob')},
    @{source=$options.Resource.terrainAuthoringArchive;target=(Join-Path $run 'reference/HDPack_Terrain-reference.res')}
)
foreach($name in @('HDPack_Terrain.res','database.res','databaseJmv.res')) {
    $inputs+=@{source=(Join-Path $sourceRes $name);target=(Join-Path $res $name)}
}
$protected=@((Join-Path $repo '.mcp.json'),(Join-Path $installation 'endurance-options.json'),
    (Join-Path $options.General.gameRoot 'world-map-layouts.json'),
    (Join-Path (Split-Path -Parent $sourceMaps) 'world-map-layouts.json'),
    (Join-Path $env:USERPROFILE '.codex/config.toml'),(Join-Path $env:USERPROFILE '.claude.json'))
foreach($line in Get-Content (Join-Path $installation 'SHA256SUMS.txt')) {
    if($line -match '^[0-9a-fA-F]{64} \*(.+)$'){$protected+=Join-Path $installation $Matches[1]}
}
$receipts=@()
foreach($path in (($inputs|ForEach-Object {$_.source})+$protected|Select-Object -Unique)) {
    if(Test-Path -LiteralPath $path -PathType Leaf) {
        $f=Get-Item -LiteralPath $path
        $receipts+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;length=$f.Length;writeTicks=$f.LastWriteTimeUtc.Ticks}
    }
}
$receipts|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 (Join-Path $run 'protected-before.json')
foreach($input in $inputs){Copy-Item -LiteralPath $input.source -Destination $input.target}
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/ai_agent_headless_check.exe')
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperMcp.exe') -Destination (Join-Path $run 'app/EIMapperMcp.exe')
foreach($name in @('data','assets')) {Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'runtime') -Destination (Join-Path $run 'app/runtime') -Recurse
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperEndurance.exe') -Destination (Join-Path $run 'app/EIMapperEndurance.exe')
# A text marker satisfies file-presence checks for the isolated Base profile.
# It is intentionally not executable. No game or old mapper is started.
[IO.File]::WriteAllText((Join-Path $run 'game.exe'),'synthetic profile marker - never execute')
$options.General.gameRoot=$run.Replace('\','/')
$options.General.recentFiles=@();$options.General.autosaveEnabled=$true;$options.General.autosaveIntervalMinutes=1;$options.General.openLastSessionOnStartup=$false
$options.General.lastVisitedFolder=$maps.Replace('\','/')
$options.Resource.resourceProfile='deja-vu'
$options.Resource.mapRoots=@($maps.Replace('\','/'),(Join-Path $run 'Maps').Replace('\','/'))
$options.Resource.resourceRoots=@($res.Replace('\','/'),(Join-Path $run 'res').Replace('\','/'))
$options.Resource.terrainAuthoringArchive=(Join-Path $run 'reference/HDPack_Terrain-reference.res').Replace('\','/')
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User'
$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_MAPS=$maps;$env:EI_MCP_TEST_BRIDGE=Join-Path $run 'app/EIMapperMcp.exe'
$env:EI_MCP_TEST_RESOURCE_ROOTS=Join-Path $run 'res';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings';$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups';$env:EI_MAPPER_HEADLESS_IDLE_MS='7000';
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/ai_agent_headless_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(900000)){Stop-Process -Id $p.Id;throw 'Owned test process timeout'}
$p.WaitForExit();$code=$p.ExitCode
if($null -eq $code){throw 'Native exit code unavailable; not a PASS'}
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$p.Id;exe=(Get-FileHash (Join-Path $run 'app/ai_agent_headless_check.exe')).Hash}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
$drift=@()
foreach($receipt in $receipts) {
    $f=Get-Item -LiteralPath $receipt.path
    $hash=(Get-FileHash -LiteralPath $receipt.path -Algorithm SHA256).Hash
    if($hash -ne $receipt.sha256 -or $f.Length -ne $receipt.length -or $f.LastWriteTimeUtc.Ticks -ne $receipt.writeTicks){$drift+=@{path=$receipt.path;before=$receipt;after=@{sha256=$hash;length=$f.Length;writeTicks=$f.LastWriteTimeUtc.Ticks}}}
}
$drift|ConvertTo-Json -Depth 6|Set-Content -Encoding UTF8 (Join-Path $run 'external-drift.json')
$receipts|ConvertTo-Json|Set-Content (Join-Path $run 'preserved-inputs.json')
if($drift.Count){throw ('Protected input changed; child exitCode='+$code+'; see external-drift.json: '+(($drift|ForEach-Object {$_.path}) -join ', '))}
Get-Content (Join-Path $run 'stdout.txt') -Tail 12
exit $code
