param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,

    [Parameter(Mandatory = $true)]
    [string]$GameRoot,

    [Parameter(Mandatory = $true)]
    [string]$ModRoot,

    [string]$QtBin = "",

    [string]$MinGwBin = ""
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testExecutablePath = (Resolve-Path -LiteralPath $TestExecutable).Path
$gameRootPath = (Resolve-Path -LiteralPath $GameRoot).Path
$modRootPath = (Resolve-Path -LiteralPath $ModRoot).Path

if ($QtBin) {
    $qtBinPath = (Resolve-Path -LiteralPath $QtBin).Path
    $env:Path = "$qtBinPath;$env:Path"
}
if ($MinGwBin) {
    $minGwBinPath = (Resolve-Path -LiteralPath $MinGwBin).Path
    $env:Path = "$minGwBinPath;$env:Path"
}

function Invoke-RuntimeSmoke {
    param(
        [Parameter(Mandatory = $true)]
        [string]$Name,
        [Parameter(Mandatory = $true)]
        [hashtable]$Values
    )

    $prefix = "EI_ENDURANCE_RUNTIME_SMOKE_"
    Get-ChildItem Env: | Where-Object Name -like "$prefix*" |
        ForEach-Object { Remove-Item "Env:$($_.Name)" }
    $env:EI_ENDURANCE_RUNTIME_SMOKE_GAME_ROOT = $gameRootPath
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOD_ROOT = $modRootPath
    foreach ($entry in $Values.GetEnumerator()) {
        Set-Item -Path "Env:$prefix$($entry.Key)" -Value ([string]$entry.Value)
    }

    $report = Join-Path (Split-Path -Parent $testExecutablePath) "$Name-runtime-smoke.txt"
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    & $testExecutablePath runtimeSmokeReachesGameplayReady -o "$report,txt"
    $exitCode = $LASTEXITCODE
    if (Test-Path -LiteralPath $report) {
        Get-Content -LiteralPath $report
    }
    if ($exitCode -ne 0) {
        throw "$Name runtime smoke failed with exit code $exitCode."
    }
}

$zone1Maps = Join-Path $modRootPath "Maps"
Invoke-RuntimeSmoke -Name "zone1" -Values @{
    MPR = Join-Path $zone1Maps "zone1.mpr"
    MOB = Join-Path $zone1Maps "zone1.mob"
    ZONE = "gz1g"
    MPR_NAME = "zone1"
    MOB_NAME = "zone1"
    EXIT = "1"
    X = "54"
    Y = "138"
}

$runtimeFixtures = Join-Path $repositoryRoot "tests\fixtures\runtime"
Invoke-RuntimeSmoke -Name "synthetic-empty-control" -Values @{
    MPR = Join-Path $runtimeFixtures "particle-flat-7x5.mpr"
    MOB = Join-Path $runtimeFixtures "empty-control.mob"
    GRAPH = Join-Path $runtimeFixtures "particle-flat-7x5-generated.grp"
    ZONE = "synthetic"
    MPR_NAME = "particleflat"
    MOB_NAME = "emptycontrol"
    SYNTHETIC = "1"
    SIZE_X = "7"
    SIZE_Y = "5"
    EXIT = "1"
    X = "34.768"
    Y = "43.009"
}

Write-Host "Zone 1 and synthetic empty-control runtime smoke tests passed."
