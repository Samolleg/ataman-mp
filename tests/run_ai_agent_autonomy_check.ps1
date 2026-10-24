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
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$options=Get-Content -Encoding UTF8 (Join-Path $installation 'endurance-options.json') -Raw | ConvertFrom-Json
$sourceMaps=$options.Resource.mapRoots[0]
$sourceRes=$options.Resource.resourceRoots[0]
$inputs=@(
    @{source=(Join-Path $sourceMaps 'zone1.mpr');target=(Join-Path $run 'Maps/gipat2.mpr')},
    @{source=(Join-Path $sourceMaps 'zone1.mob');target=(Join-Path $run 'Maps/gipat2.mob')},
    @{source=$options.Resource.terrainAuthoringArchive;target=(Join-Path $run 'reference/HDPack_Terrain-reference.res')}
)
foreach($name in @('HDPack_Terrain.res','database.res','databaseJmv.res')) {
    $inputs+=@{source=(Join-Path $sourceRes $name);target=(Join-Path $run ('res/'+$name))}
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
foreach($input in $inputs){Copy-Item -LiteralPath $input.source -Destination $input.target}
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/ai_agent_autonomy_check.exe')
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperMcp.exe') -Destination (Join-Path $run 'app/EIMapperMcp.exe')
foreach($name in @('data','assets')) {Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'runtime') -Destination (Join-Path $run 'app/runtime') -Recurse
# A text marker satisfies file-presence checks for the isolated Base profile.
# It is intentionally not executable. No game or old mapper is started.
[IO.File]::WriteAllText((Join-Path $run 'game.exe'),'synthetic profile marker - never execute')
$options.General.gameRoot=$run.Replace('\','/')
$options.General.recentFiles=@();$options.General.autosaveEnabled=$false;$options.General.openLastSessionOnStartup=$false
$options.General.lastVisitedFolder=(Join-Path $run 'Maps').Replace('\','/')
$options.Resource.resourceProfile='base-game'
$options.Resource.mapRoots=@((Join-Path $run 'Maps').Replace('\','/'))
$options.Resource.resourceRoots=@((Join-Path $run 'res').Replace('\','/'))
$options.Resource.terrainAuthoringArchive=(Join-Path $run 'reference/HDPack_Terrain-reference.res').Replace('\','/')
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User'
$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_MAPS=Join-Path $run 'Maps';$env:EI_MCP_TEST_BRIDGE=Join-Path $run 'app/EIMapperMcp.exe'
$env:EI_MCP_TEST_RESOURCE_ROOTS=Join-Path $run 'res';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/ai_agent_autonomy_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(600000)){Stop-Process -Id $p.Id;throw 'Owned test process timeout'}
$p.WaitForExit();$code=$p.ExitCode
if($null -eq $code){throw 'Native exit code unavailable; not a PASS'}
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$p.Id;exe=(Get-FileHash (Join-Path $run 'app/ai_agent_autonomy_check.exe')).Hash}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
foreach($receipt in $receipts) {
    $f=Get-Item -LiteralPath $receipt.path
    if((Get-FileHash -LiteralPath $receipt.path -Algorithm SHA256).Hash -ne $receipt.sha256 -or $f.Length -ne $receipt.length -or $f.LastWriteTimeUtc.Ticks -ne $receipt.writeTicks){throw ('Protected input changed: '+$receipt.path)}
}
$receipts|ConvertTo-Json|Set-Content (Join-Path $run 'preserved-inputs.json')
Get-Content (Join-Path $run 'stdout.txt') -Tail 12
exit $code
