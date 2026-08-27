$ErrorActionPreference = 'Stop'

$testRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'ei-mapper-distribution-' + [Guid]::NewGuid().ToString('N'))
$validator = Join-Path $PSScriptRoot '..\packaging\validate_distribution.ps1'

try {
    $runtime = Join-Path $testRoot 'runtime'
    $collections = Join-Path $testRoot 'collections'
    New-Item -ItemType Directory -Path $runtime, $collections | Out-Null
    Set-Content -LiteralPath (Join-Path $runtime 'EIMapperEnduranceJmvBridge.plg') -Value ''
    Set-Content -LiteralPath (Join-Path $runtime 'EIMapperEnduranceRuntimeHost.exe') -Value ''
    Set-Content -LiteralPath (Join-Path $testRoot 'VERSION') -Value '0.3.0-alpha.26'
    Set-Content -LiteralPath (Join-Path $testRoot 'ALPHA26_COLLECTION_PATH_TEST.md') -Value '# test'
    Set-Content -LiteralPath (Join-Path $testRoot 'SHA256SUMS.txt') -Value ''
    $collectionPath = Join-Path $collections 'missing-map.collection'
    Set-Content -LiteralPath $collectionPath -Value @'
# EI Mapper Endurance Collection
[collection]
version=1
name="Missing map regression"
profile="jamais-vu"
map=""
mpr="maps/missing.mpr"
mob="zone2.mob"
'@

    $ErrorActionPreference = 'Continue'
    $missingOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $validator $testRoot 2>&1
    $missingExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if($missingExit -eq 0) {
        throw 'Distribution validation did not reject a missing collection MPR.'
    }

    $maps = Join-Path $collections 'maps'
    New-Item -ItemType Directory -Path $maps | Out-Null
    Set-Content -LiteralPath (Join-Path $maps 'missing.mpr') -Value ''
    $ErrorActionPreference = 'Continue'
    $validOutput = & powershell.exe -NoProfile -ExecutionPolicy Bypass `
        -File $validator $testRoot 2>&1
    $validExit = $LASTEXITCODE
    $ErrorActionPreference = 'Stop'
    if($validExit -ne 0) {
        throw "Distribution validation rejected a resolved collection MPR:`n$validOutput"
    }

    Write-Output 'PASS: distribution validator rejects missing collection MPR references'
}
finally {
    if(Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force
    }
}
