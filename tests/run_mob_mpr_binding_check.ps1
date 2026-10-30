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
    @{source=(Join-Path $sourceMaps 'gipat2.mpr');target=(Join-Path $run 'Maps/gipat2.mpr')},
    @{source=(Join-Path $sourceMaps 'gipat2.mob');target=(Join-Path $run 'Maps/forest-source.mob')},
    @{source=$options.Resource.terrainAuthoringArchive;target=(Join-Path $run 'reference/HDPack_Terrain-reference.res')}
)
foreach($name in @('HDPack_Terrain.res','database.res','databaseJmv.res')) {
    $inputs+=@{source=(Join-Path $sourceRes $name);target=(Join-Path $run ('res/'+$name))}
}
$protected=@((Join-Path $repo '.mcp.json'),(Join-Path $installation 'endurance-options.json'),
    (Join-Path $options.General.gameRoot 'world-map-layouts.json'),
    (Join-Path (Split-Path -Parent $sourceMaps) 'world-map-layouts.json'))
foreach($name in @('zone7.mpr','zone7.mob','zone9.mpr','zone9.mob')) {$protected+=Join-Path $sourceMaps $name}
foreach($line in Get-Content (Join-Path $installation 'SHA256SUMS.txt')) {
    if($line -match '^[0-9a-fA-F]{64} \*(.+)$'){$protected+=Join-Path $installation $Matches[1]}
}
$receipts=@()
foreach($path in (($inputs|ForEach-Object {$_.source})+$protected|Select-Object -Unique)) {
    if(Test-Path -LiteralPath $path -PathType Leaf) {
        $file=Get-Item -LiteralPath $path
        $receipts+=@{path=$path;sha256=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash;length=$file.Length;writeTicks=$file.LastWriteTimeUtc.Ticks}
    }
}
foreach($input in $inputs){Copy-Item -LiteralPath $input.source -Destination $input.target}
$exe=Join-Path $run 'app/mob_mpr_binding_check.exe'
Copy-Item -LiteralPath $TestExe -Destination $exe
foreach($name in @('data','assets')) {Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'runtime') -Destination (Join-Path $run 'app/runtime') -Recurse
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
$env:EI_MOB_BIND_ROOT=$run
$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$results=@();$failure=$null
try {
    foreach($phase in @('scenario','readback')) {
        if($phase -eq 'readback'){$env:EI_MOB_BIND_READBACK='1'}
        $timer=[Diagnostics.Stopwatch]::StartNew()
        $process=Start-Process -FilePath $exe -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run "$phase.stdout.txt") -RedirectStandardError (Join-Path $run "$phase.stderr.txt")
        $null=$process.Handle
        if(-not $process.WaitForExit(240000)){Stop-Process -Id $process.Id;throw "Owned $phase test process timeout"}
        $process.WaitForExit();$code=$process.ExitCode
        if($null -eq $code){throw 'Native exit code unavailable; not a PASS'}
        $results+=@{phase=$phase;exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$process.Id;exe=(Get-FileHash $exe).Hash}
        Get-Content (Join-Path $run "$phase.stdout.txt") -Tail 8
        if($code -ne 0){throw "Native $phase test failed: $code"}
    }
} catch {$failure=$_} finally {
    $results|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
    foreach($receipt in $receipts) {
        $file=Get-Item -LiteralPath $receipt.path
        if((Get-FileHash -LiteralPath $receipt.path -Algorithm SHA256).Hash -ne $receipt.sha256 -or $file.Length -ne $receipt.length -or $file.LastWriteTimeUtc.Ticks -ne $receipt.writeTicks){throw ('Protected input changed: '+$receipt.path)}
    }
    $receipts|ConvertTo-Json|Set-Content (Join-Path $run 'preserved-inputs.json')
}
if($failure){throw $failure}
