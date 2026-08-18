$ErrorActionPreference = 'Stop'
. (Join-Path (Split-Path -Parent $PSScriptRoot) `
    'scripts\expanded_atlas_runtime_evidence.ps1')

$cases = @(
    @{ name='clean authoritative pass'; shape=$true; timeout=$false; native=$false; memory=$false; authoritative=$true; expected='pass' },
    @{ name='stale pass cannot override crash'; shape=$true; timeout=$false; native=$true; memory=$false; authoritative=$true; expected='native-runtime-crash' },
    @{ name='colorful crash frame is not authoritative'; shape=$true; timeout=$false; native=$false; memory=$false; authoritative=$false; expected='unrelated-runtime-or-staging-failure' },
    @{ name='memory evidence classifies capacity endpoint'; shape=$true; timeout=$false; native=$true; memory=$true; authoritative=$true; expected='memory-or-address-space-exhaustion' },
    @{ name='hard timeout cannot pass'; shape=$true; timeout=$true; native=$false; memory=$false; authoritative=$true; expected='timeout-unclassified' },
    @{ name='ordinary staging failure remains retryable'; shape=$false; timeout=$false; native=$false; memory=$false; authoritative=$false; expected='unrelated-runtime-or-staging-failure' }
)

$passed = 0
foreach ($case in $cases) {
    $actual = Get-ExpandedAtlasProbeClassification `
        -PassShape $case.shape -TimedOut $case.timeout `
        -NativeRuntimeFailure $case.native -MemoryFailure $case.memory `
        -AuthoritativeEvidence $case.authoritative
    if ($actual -ne $case.expected) {
        throw "FAIL: $($case.name): expected $($case.expected), got $actual"
    }
    $isPass = Test-ExpandedAtlasProbePassed -Classification $actual
    if ($isPass -ne ($case.expected -eq 'pass')) {
        throw "FAIL: $($case.name): pass predicate disagrees with classification $actual"
    }
    Write-Output "PASS: $($case.name)"
    ++$passed
}
$before = @(
    [pscustomobject]@{ path='old.dmp'; length=100; modified_utc='2026-07-29T01:00:00Z' },
    [pscustomobject]@{ path='grown.dmp'; length=100; modified_utc='2026-07-29T01:00:00Z' }
)
$after = @(
    [pscustomobject]@{ path='old.dmp'; length=100; modified_utc='2026-07-29T01:00:00Z' },
    [pscustomobject]@{ path='grown.dmp'; length=140; modified_utc='2026-07-29T01:00:01Z' },
    [pscustomobject]@{ path='new.dmp'; length=80; modified_utc='2026-07-29T01:00:02Z' }
)
$changed = @(Get-ChangedNativeDumpRows -Before $before -After $after)
if ($changed.Count -ne 2 -or $changed.path -notcontains 'grown.dmp' `
    -or $changed.path -notcontains 'new.dmp') {
    throw 'FAIL: native dump delta must include new and changed dumps only'
}
Write-Output 'PASS: native dump delta includes new and changed dumps only'
++ $passed
$pressure = Test-ExpandedAtlasMemoryPressure `
    -CombinedRgbaBytes 553648128 -GameplayReadyPrivateBytes 613986304 `
    -PeakPrivateBytes 2579759104 -MinimumLargestFreeRegionBytes 298991616
if (-not $pressure) {
    throw 'FAIL: observed 8192-slot allocation pressure was not recognized'
}
Write-Output 'PASS: observed 8192-slot allocation pressure is recognized'
++ $passed
$controlledLowresPressure = Test-ExpandedAtlasMemoryPressure `
    -CombinedRgbaBytes 138412032 -GameplayReadyPrivateBytes 513822720 `
    -PeakPrivateBytes 2317262848 -MinimumLargestFreeRegionBytes 450052096
if (-not $controlledLowresPressure) {
    throw 'FAIL: controlled 512px 8192-slot amplification pressure was not recognized'
}
Write-Output 'PASS: controlled 512px 8192-slot amplification pressure is recognized'
++ $passed
$healthy = Test-ExpandedAtlasMemoryPressure `
    -CombinedRgbaBytes 268435456 -GameplayReadyPrivateBytes 500000000 `
    -PeakPrivateBytes 1232080896 -MinimumLargestFreeRegionBytes 1927675904
if ($healthy) {
    throw 'FAIL: healthy 4096-slot address space was classified as exhausted'
}
Write-Output 'PASS: healthy 4096-slot address space is not classified as exhausted'
++ $passed
Write-Output "Totals: $passed passed, 0 failed"
