param(
    [Parameter(Mandatory = $true)][string]$TestExecutable,
    [Parameter(Mandatory = $true)][string]$GameRoot,
    [Parameter(Mandatory = $true)][string]$ModRoot,
    [Parameter(Mandatory = $true)][string]$FixtureRoot,
    [string]$QtBin = "",
    [ValidateNotNullOrEmpty()][int[]]$ProbePages = @(16, 32, 64, 128, 256),
    [int]$DwellMilliseconds = 20000,
    [int]$ProbeTimeoutSeconds = 120
)

$ErrorActionPreference = "Stop"
foreach ($pages in $ProbePages) {
    if ($pages -notin @(16, 32, 64, 128, 256)) {
        throw "ProbePages accepts only the generated Zone18 page counts: 16, 32, 64, 128, 256."
    }
}
$testExecutablePath = (Resolve-Path -LiteralPath $TestExecutable).Path
$gameRootPath = (Resolve-Path -LiteralPath $GameRoot).Path
$modRootPath = (Resolve-Path -LiteralPath $ModRoot).Path
$fixtureRootPath = (Resolve-Path -LiteralPath $FixtureRoot).Path
$evidenceRoot = Join-Path (Split-Path -Parent $testExecutablePath) `
    ("expanded-atlas-zone18-stress-{0}" -f [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidenceRoot | Out-Null

if ($QtBin) {
    $qtBinPath = (Resolve-Path -LiteralPath $QtBin).Path
    $inheritedPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
    [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
    [Environment]::SetEnvironmentVariable('Path', $null, 'Process')
    [Environment]::SetEnvironmentVariable('Path', "$qtBinPath;$inheritedPath", 'Process')
}

$starterKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
    'Software\Gipat.Ru\EI_Starter\Settings', $false)
$starterAddonPath = if ($starterKey) { [string]$starterKey.GetValue('AddonPath', '') } else { '' }
if ($starterKey) { $starterKey.Dispose() }
$selected = [IO.Path]::GetFullPath($modRootPath).TrimEnd('\')
$configured = if ($starterAddonPath) { [IO.Path]::GetFullPath($starterAddonPath).TrimEnd('\') } else { '' }
if (-not $configured.Equals($selected, [StringComparison]::OrdinalIgnoreCase)) {
    throw "Starter AddonPath does not select $selected (configured: $configured)."
}
if (Get-Process -Name game, EIMapperEnduranceRuntimeHost -ErrorAction SilentlyContinue) {
    throw "An existing game or Endurance runtime-host process is active; refusing to stage."
}

$zone18Mpr = Join-Path $modRootPath 'Maps\zone18.mpr'
$zone18Mob = Join-Path $modRootPath 'Maps\zone18.mob'
foreach ($source in @($zone18Mpr, $zone18Mob)) {
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) { throw "Missing Zone18 source: $source" }
}

function Get-Sha256([string]$Path) {
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-ProxyLogState {
    $rows = @()
    foreach ($path in @(
        (Join-Path $gameRootPath 'Starter\ddraw_proxy.log'),
        (Join-Path $gameRootPath 'Starter\Engine\ddraw_proxy.log'),
        (Join-Path $gameRootPath 'ddraw_proxy.log'))) {
        if (Test-Path -LiteralPath $path -PathType Leaf) {
            $item = Get-Item -LiteralPath $path
            $rows += [pscustomobject]@{ path=$item.FullName; length=[int64]$item.Length }
        }
    }
    return $rows
}

function Preserve-ProxyLogDelta($Before, [string]$Destination) {
    $output = New-Object System.Collections.Generic.List[string]
    foreach ($entry in (Get-ProxyLogState)) {
        $prior = $Before | Where-Object { $_.path -eq $entry.path } | Select-Object -First 1
        $offset = if ($prior) { [int64]$prior.length } else { 0 }
        $stream = [IO.File]::Open($entry.path, 'Open', 'Read', 'ReadWrite')
        try {
            if ($offset -gt $stream.Length) { $offset = 0 }
            [void]$stream.Seek($offset, 'Begin')
            $reader = New-Object IO.StreamReader($stream, [Text.Encoding]::UTF8, $true, 4096, $true)
            try { $output.Add("===== $($entry.path) offset=$offset ====="); $output.Add($reader.ReadToEnd()) }
            finally { $reader.Dispose() }
        } finally { $stream.Dispose() }
    }
    $output | Set-Content -LiteralPath $Destination -Encoding UTF8
}

function Invoke-OwnedRecovery([int]$Pages) {
    $path = Join-Path $evidenceRoot ("page-{0:d3}-recovery.txt" -f $Pages)
    & $testExecutablePath runtimeSmokeRecoveryClosesOwnedRun -o "$path,txt"
    return $LASTEXITCODE
}

function Invoke-AtlasProbe([int]$Pages) {
    $Attempt = $script:CurrentProbeAttempt
    if (-not $Attempt) { $Attempt = 1 }
    $directory = Join-Path $fixtureRootPath ("page-{0:d2}" -f $Pages)
    if (-not (Test-Path -LiteralPath $directory)) {
        $directory = Join-Path $fixtureRootPath ("page-{0}" -f $Pages)
    }
    $manifestPath = Join-Path $directory 'manifest.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The $Pages-page Zone18 fixture is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    $slots = [int]$manifest.active_slots
    if ([int]$manifest.texture_pages -ne $Pages -or $slots -ne $Pages * 64 -or
        [int]$manifest.highest_referenced_page -ne $Pages - 1 -or
        $manifest.workload -ne 'installed-zone18-mpr-mob-with-drawall' -or
        -not [bool]$manifest.drawall_required) {
        throw "Fixture $Pages does not satisfy the Zone18/drawall stress contract."
    }
    if ((Get-Sha256 $zone18Mpr) -ne $manifest.source_mpr_sha256 -or
        (Get-Sha256 $zone18Mob) -ne $manifest.source_mob_sha256) {
        throw "Installed Zone18 source hashes no longer match fixture $Pages."
    }
    $mpr = Join-Path $directory ([string]$manifest.mpr_file)
    $atlas = Join-Path $directory ([string]$manifest.atlas_file)
    foreach ($payload in @($mpr, $atlas)) {
        if (-not (Test-Path -LiteralPath $payload -PathType Leaf)) { throw "Missing payload: $payload" }
    }

    $prefix = "page-{0:d3}-slots-{1}-attempt-{2}" -f $Pages, $slots, $Attempt
    $report = Join-Path $evidenceRoot "$prefix-runtime.txt"
    $memory = Join-Path $evidenceRoot "$prefix-memory.json"
    # Qt 5.12 command-line logging is unreliable below the non-ASCII worktree
    # path. Capture to owned ASCII temp names, then preserve into the campaign.
    $tempReport = Join-Path $env:TEMP ("ei-zone18-{0}-{1}-runtime.txt" -f $PID,$Pages)
    $tempMemory = Join-Path $env:TEMP ("ei-zone18-{0}-{1}-memory.json" -f $PID,$Pages)
    $proxy = Join-Path $evidenceRoot "$prefix-proxy-log-delta.txt"
    $proxyBefore = Get-ProxyLogState

    $env:EI_ENDURANCE_RUNTIME_SMOKE_GAME_ROOT = $gameRootPath
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOD_ROOT = $modRootPath
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MPR = $mpr
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOB = $zone18Mob
    $env:EI_ENDURANCE_RUNTIME_SMOKE_TEXTURE_RES = $atlas
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_PAGES = [string]$Pages
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_SLOTS = [string]$slots
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ZONE = 'gz18h'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MPR_NAME = 'zone18'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOB_NAME = 'zone18'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SYNTHETIC = '0'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SIZE_X = [string]$manifest.map_size_x
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SIZE_Y = [string]$manifest.map_size_y
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ALLOW_GRAPHLESS = '0'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_DRAWALL = '1'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_EXIT = '1'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_X = '0'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_Y = '0'
    $env:EI_ENDURANCE_RUNTIME_SMOKE_DWELL_MS = [string]$DwellMilliseconds
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MEMORY_EVIDENCE = $tempMemory

    Remove-Item -LiteralPath $report,$memory,$tempReport,$tempMemory -Force -ErrorAction SilentlyContinue
    $process = Start-Process -FilePath $testExecutablePath -ArgumentList `
        "runtimeSmokeReachesGameplayReady -o `"$tempReport,txt`"" -NoNewWindow -PassThru
    $timedOut = -not $process.WaitForExit($ProbeTimeoutSeconds * 1000)
    if ($timedOut) {
        $process.Kill(); $process.WaitForExit()
        [void](Invoke-OwnedRecovery $Pages)
    } else {
        $process.WaitForExit(); $process.Refresh()
    }
    $exitCode = if ($timedOut) { -1001 } else { [int]$process.ExitCode }
    if (Get-Process -Name game, EIMapperEnduranceRuntimeHost -ErrorAction SilentlyContinue) {
        if ((Invoke-OwnedRecovery $Pages) -ne 0) { throw "Owned recovery failed after page $Pages." }
    }
    Preserve-ProxyLogDelta $proxyBefore $proxy
    if (Test-Path -LiteralPath $tempReport) { Copy-Item -LiteralPath $tempReport -Destination $report -Force }
    if (Test-Path -LiteralPath $tempMemory) { Copy-Item -LiteralPath $tempMemory -Destination $memory -Force }
    $reportText = if (Test-Path -LiteralPath $tempReport) { Get-Content -LiteralPath $tempReport -Raw } else { '' }
    $memoryObject = if (Test-Path -LiteralPath $tempMemory) {
        Get-Content -LiteralPath $tempMemory -Raw | ConvertFrom-Json
    } else { $null }
    $passed = -not $timedOut -and $exitCode -eq 0 -and
        $reportText -match 'Totals:\s+\d+ passed,\s+0 failed,' -and
        $reportText -match 'RUNTIME_FRAME_EVIDENCE.*rendered=\s*true' -and
        $reportText -match 'RUNTIME_MEMORY_EVIDENCE.*samples=' -and
        $memoryObject -and
        $memoryObject.schema -eq 'ei-runtime-memory-evidence-v2'
    $combinedFailure = $reportText + "`n" + (Get-Content -LiteralPath $proxy -Raw)
    $memorySignature = $combinedFailure -match `
        '(?i)out of memory|E_OUTOFMEMORY|bad_alloc|0xC0000017|not enough (storage|memory)|allocation fail'
    $vaExhausted = $memoryObject -and
        [double]$memoryObject.minimumLargestFreeRegionBytes -gt 0 -and
        [double]$memoryObject.minimumLargestFreeRegionBytes -lt 64MB
    $classification = if ($passed) { 'pass' } elseif ($memorySignature -or $vaExhausted) {
        'memory-or-address-space-exhaustion'
    } elseif ($timedOut) { 'timeout-unclassified' } else { 'unrelated-runtime-or-staging-failure' }
    return [pscustomobject]@{
        pages=$Pages; slots=$slots; passed=$passed; classification=$classification
        timed_out=$timedOut; exit_code=$exitCode; report=$report; memory=$memory
        proxy_log_delta=$proxy; memory_data=$memoryObject
    }
}

function Get-RegressionSlope($Rows, [string]$Property) {
    $usable = @($Rows | Where-Object { $_.passed -and $_.memory_data })
    if ($usable.Count -lt 2) { return $null }
    $meanX = ($usable | Measure-Object -Property slots -Average).Average
    $meanY = ($usable | ForEach-Object { [double]$_.memory_data.$Property } | Measure-Object -Average).Average
    $numerator = 0.0; $denominator = 0.0
    foreach ($row in $usable) {
        $dx = [double]$row.slots - $meanX
        $numerator += $dx * ([double]$row.memory_data.$Property - $meanY)
        $denominator += $dx * $dx
    }
    if ($denominator -eq 0) { return $null }
    return $numerator / $denominator
}

function Write-CampaignSummary($Rows) {
    $probes = @()
    $prior = $null
    foreach ($row in $Rows) {
        $item = [ordered]@{
            pages=$row.pages; slots=$row.slots; passed=$row.passed
            classification=$row.classification; timed_out=$row.timed_out
            exit_code=$row.exit_code; report=$row.report; memory=$row.memory
            proxy_log_delta=$row.proxy_log_delta
        }
        if ($row.memory_data) {
            foreach ($name in @('gameplayReadyWorkingSetBytes','gameplayReadyPrivateBytes',
                'gameplayReadyCommittedAddressBytes','gameplayReadyMappedAddressBytes',
                'observedPeakWorkingSetBytes','peakPrivateBytes','peakCommittedAddressBytes',
                'peakMappedAddressBytes','minimumFreeAddressBytes','minimumLargestFreeRegionBytes')) {
                $item[$name] = [int64]$row.memory_data.$name
            }
        }
        if ($prior -and $prior.memory_data -and $row.memory_data) {
            $deltaSlots = $row.slots - $prior.slots
            $cost = [ordered]@{}
            foreach ($name in @('gameplayReadyPrivateBytes','gameplayReadyCommittedAddressBytes',
                'gameplayReadyMappedAddressBytes','peakPrivateBytes','peakCommittedAddressBytes')) {
                $cost[$name + 'PerAddedSlot'] =
                    ([double]$row.memory_data.$name - [double]$prior.memory_data.$name) / $deltaSlots
            }
            $item['adjacentObservedCost'] = $cost
        }
        $probes += $item; $prior = $row
    }
    $regression = [ordered]@{}
    foreach ($name in @('gameplayReadyPrivateBytes','gameplayReadyCommittedAddressBytes',
        'gameplayReadyMappedAddressBytes','peakPrivateBytes','peakCommittedAddressBytes')) {
        $regression[$name + 'PerSlot'] = Get-RegressionSlope $Rows $name
    }
    $attempts = @()
    foreach ($row in @($script:AllProbeAttempts)) {
        $attempts += [ordered]@{
            pages=$row.pages; slots=$row.slots; passed=$row.passed
            classification=$row.classification; timed_out=$row.timed_out
            exit_code=$row.exit_code; report=$row.report; memory=$row.memory
            proxy_log_delta=$row.proxy_log_delta
            sample_count=if ($row.memory_data) { [int]$row.memory_data.sampleCount } else { 0 }
            peak_private_bytes=if ($row.memory_data) { [int64]$row.memory_data.peakPrivateBytes } else { 0 }
            peak_committed_address_bytes=if ($row.memory_data) { [int64]$row.memory_data.peakCommittedAddressBytes } else { 0 }
        }
    }
    $summary = [ordered]@{
        schema='ei-zone18-expanded-atlas-stress-v2'
        captured_utc=[DateTime]::UtcNow.ToString('o')
        workload='run-owned Zone18 MPR+MOB; drawall 1; installed JMV atlas imagery'
        source_mpr=$zone18Mpr; source_mpr_sha256=Get-Sha256 $zone18Mpr
        source_mob=$zone18Mob; source_mob_sha256=Get-Sha256 $zone18Mob
        cost_note='Observed deltas include page/resource/runtime overhead; regression is an estimate, not a constant allocation law.'
        linear_regression_bytes_per_slot=$regression
        all_attempts=$attempts
        probes=$probes
    }
    $path = Join-Path $evidenceRoot 'campaign-summary.json'
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $path -Encoding UTF8
    $latest = Join-Path (Split-Path -Parent $testExecutablePath) 'expanded-atlas-zone18-stress-latest.json'
    $summary | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $latest -Encoding UTF8
    return $path
}

$rows = @()
$script:AllProbeAttempts = @()
foreach ($pages in $ProbePages) {
    Write-Host "Starting Zone18 atlas stress: $($pages * 64) slots / $pages pages"
    $row = $null
    for ($attempt = 1; $attempt -le 2; ++$attempt) {
        $script:CurrentProbeAttempt = $attempt
        $row = Invoke-AtlasProbe $pages
        $script:AllProbeAttempts += $row
        if ($row.passed -or $row.classification -eq 'memory-or-address-space-exhaustion') { break }
        if ($attempt -lt 2) {
            Write-Warning "Probe $($row.slots) had unrelated attempt-$attempt failure; evidence preserved, retrying once."
        }
    }
    $rows += $row
    $summaryPath = Write-CampaignSummary $rows
    if ($row.passed) {
        $m = $row.memory_data
        Write-Host ("PASS slots={0} private={1:N1}MiB committedVA={2:N1}MiB mappedVA={3:N1}MiB largestFree={4:N1}MiB" -f `
            $row.slots, ([double]$m.peakPrivateBytes/1MB),
            ([double]$m.peakCommittedAddressBytes/1MB), ([double]$m.peakMappedAddressBytes/1MB),
            ([double]$m.minimumLargestFreeRegionBytes/1MB))
        continue
    }
    if ($row.classification -eq 'memory-or-address-space-exhaustion') {
        Write-Host "Capacity endpoint reached at $($row.slots) slots: $($row.classification). Evidence: $summaryPath"
        return
    }
    throw "Probe $($row.slots) stopped for $($row.classification), not a capacity result. Evidence: $summaryPath"
}
Write-Host "All reported packed-format slots passed through 16384. Evidence: $(Write-CampaignSummary $rows)"
