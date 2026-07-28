param(
    [Parameter(Mandatory = $true)]
    [string]$TestExecutable,

    [Parameter(Mandatory = $true)]
    [string]$GameRoot,

    [Parameter(Mandatory = $true)]
    [string]$ModRoot,

    [Parameter(Mandatory = $true)]
    [string]$FixtureRoot,

    [string]$QtBin = "",
    [int]$DwellMilliseconds = 20000,
    [int]$ProbeTimeoutSeconds = 90,
    [switch]$ControlOnly
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
$testExecutablePath = (Resolve-Path -LiteralPath $TestExecutable).Path
$gameRootPath = (Resolve-Path -LiteralPath $GameRoot).Path
$modRootPath = (Resolve-Path -LiteralPath $ModRoot).Path
$fixtureRootPath = (Resolve-Path -LiteralPath $FixtureRoot).Path

$starterKey = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey(
    'Software\Gipat.Ru\EI_Starter\Settings', $false)
$starterAddonPath = if ($starterKey) {
    [string]$starterKey.GetValue('AddonPath', '')
} else { '' }
if ($starterKey) { $starterKey.Dispose() }
$normalizedSelectedMod = [IO.Path]::GetFullPath($modRootPath).TrimEnd('\')
$normalizedStarterMod = if ($starterAddonPath) {
    [IO.Path]::GetFullPath($starterAddonPath).TrimEnd('\')
} else { '' }
if (-not $normalizedStarterMod -or
    -not $normalizedStarterMod.Equals(
        $normalizedSelectedMod, [StringComparison]::OrdinalIgnoreCase)) {
    $shownStarter = if ($normalizedStarterMod) { $normalizedStarterMod } else { '<not configured>' }
    throw "Starter AddonPath does not select the requested JMV mod. Requested: $normalizedSelectedMod; Starter: $shownStarter"
}

if ($QtBin) {
    $qtBinPath = (Resolve-Path -LiteralPath $QtBin).Path
    $inheritedPath = [Environment]::GetEnvironmentVariable('Path', 'Process')
    # Start-Process constructs a case-insensitive dictionary and fails if its
    # parent inherited both Path and PATH. Canonicalize that common host quirk.
    [Environment]::SetEnvironmentVariable('PATH', $null, 'Process')
    [Environment]::SetEnvironmentVariable('Path', $null, 'Process')
    [Environment]::SetEnvironmentVariable(
        'Path', "$qtBinPath;$inheritedPath", 'Process')
}

if (Get-Process -Name game, EIMapperEnduranceRuntimeHost -ErrorAction SilentlyContinue) {
    throw "An existing game or Endurance runtime-host process is active; refusing to stage a probe."
}

$emptyMob = Join-Path $repositoryRoot "tests\fixtures\runtime\empty-control.mob"
if (-not (Test-Path -LiteralPath $emptyMob -PathType Leaf)) {
    throw "The empty-control MOB fixture is missing: $emptyMob"
}

function Invoke-ExpandedAtlasProbe {
    param([int]$Pages)

    $directory = Join-Path $fixtureRootPath ("page-{0:d2}" -f $Pages)
    $manifestPath = Join-Path $directory "manifest.json"
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
        throw "The $Pages-page probe manifest is missing: $manifestPath"
    }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ([int]$manifest.texture_pages -ne $Pages) {
        throw "Probe $Pages declares $($manifest.texture_pages) pages."
    }
    if ([int]$manifest.active_slots -le (($Pages - 1) * 64) -or
        [int]$manifest.active_slots -gt ($Pages * 64)) {
        throw "Probe $Pages has an invalid active-slot count: $($manifest.active_slots)."
    }
    if ([int]$manifest.highest_referenced_page -ne ($Pages - 1)) {
        throw "Probe $Pages does not reference its highest page."
    }

    $mpr = Join-Path $directory ([string]$manifest.mpr_file)
    $atlas = Join-Path $directory ([string]$manifest.atlas_file)
    foreach ($path in @($mpr, $atlas)) {
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Expanded-atlas probe payload is missing: $path"
        }
    }

    # Every smoke variable consumed below is assigned for every probe. Avoid
    # enumerating PowerShell's Env: provider here: Windows processes can inherit
    # differently-cased duplicate keys, which makes that provider throw before
    # the owned runtime test even starts.
    $env:EI_ENDURANCE_RUNTIME_SMOKE_GAME_ROOT = $gameRootPath
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOD_ROOT = $modRootPath
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MPR = $mpr
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOB = $emptyMob
    $env:EI_ENDURANCE_RUNTIME_SMOKE_TEXTURE_RES = $atlas
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_PAGES = [string]$Pages
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ATLAS_SLOTS = [string]$manifest.active_slots
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ZONE = "synthetic"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MPR_NAME = "atlasprobe$Pages"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MOB_NAME = "emptycontrol"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SYNTHETIC = "1"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SIZE_X = [string]$manifest.map_size_x
    $env:EI_ENDURANCE_RUNTIME_SMOKE_SIZE_Y = [string]$manifest.map_size_y
    $env:EI_ENDURANCE_RUNTIME_SMOKE_ALLOW_GRAPHLESS = "1"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_EXIT = "1"
    $env:EI_ENDURANCE_RUNTIME_SMOKE_X = [string]([double]$manifest.map_size_x * 16.0)
    $env:EI_ENDURANCE_RUNTIME_SMOKE_Y = [string]([double]$manifest.map_size_y * 16.0)
    $env:EI_ENDURANCE_RUNTIME_SMOKE_DWELL_MS = [string]$DwellMilliseconds
    $memoryEvidence = Join-Path $env:TEMP `
        ("ei-expanded-atlas-{0}-{1:d2}-memory.json" -f $PID, $Pages)
    Remove-Item -LiteralPath $memoryEvidence -Force -ErrorAction SilentlyContinue
    $env:EI_ENDURANCE_RUNTIME_SMOKE_MEMORY_EVIDENCE = $memoryEvidence

    # Qt 5.12's command-line logging cannot reliably create a report below a
    # non-ASCII worktree path. Use an owned ASCII temp path, then validate the
    # fresh report rather than ever accepting a stale build-tree result.
    $report = Join-Path $env:TEMP `
        ("ei-expanded-atlas-{0}-{1:d2}-runtime.txt" -f $PID, $Pages)
    Remove-Item -LiteralPath $report -Force -ErrorAction SilentlyContinue
    if (Test-Path -LiteralPath $report) {
        throw "Could not clear the previous runtime report: $report"
    }
    $arguments = "runtimeSmokeReachesGameplayReady -o `"$report,txt`""
    $process = Start-Process -FilePath $testExecutablePath `
        -ArgumentList $arguments -NoNewWindow -PassThru
    if (-not $process.WaitForExit($ProbeTimeoutSeconds * 1000)) {
        $process.Kill()
        $process.WaitForExit()
        $recoveryReport = Join-Path (Split-Path -Parent $testExecutablePath) `
            ("expanded-atlas-{0:d2}-timeout-recovery.txt" -f $Pages)
        & $testExecutablePath runtimeSmokeRecoveryClosesOwnedRun `
            -o "$recoveryReport,txt"
        if (Test-Path -LiteralPath $recoveryReport) {
            Get-Content -LiteralPath $recoveryReport
        }
        throw "$Pages-page probe exceeded the hard $ProbeTimeoutSeconds-second timeout; owned-run recovery was invoked."
    }
    # Complete asynchronous output draining before reading ExitCode; without the
    # second wait PowerShell can expose a blank value even though the process ended.
    $process.WaitForExit()
    $process.Refresh()
    $exitCode = [int]$process.ExitCode
    if (Get-Process -Name game, EIMapperEnduranceRuntimeHost -ErrorAction SilentlyContinue) {
        $recoveryReport = Join-Path $env:TEMP `
            ("ei-expanded-atlas-{0}-{1:d2}-post-exit-recovery.txt" -f $PID, $Pages)
        Remove-Item -LiteralPath $recoveryReport -Force -ErrorAction SilentlyContinue
        & $testExecutablePath runtimeSmokeRecoveryClosesOwnedRun `
            -o "$recoveryReport,txt"
        if ($LASTEXITCODE -ne 0) {
            throw "$Pages-page probe left an active process and owned-run recovery failed."
        }
        $cleanupDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $remainingProcesses = Get-Process -Name game, EIMapperEnduranceRuntimeHost `
                -ErrorAction SilentlyContinue
            if (-not $remainingProcesses) { break }
            Start-Sleep -Milliseconds 200
        } while ([DateTime]::UtcNow -lt $cleanupDeadline)
        if ($remainingProcesses) {
            throw "$Pages-page probe left a game/runtime-host process that could not be proven and cleaned as owned."
        }
    }
    if (-not (Test-Path -LiteralPath $report -PathType Leaf)) {
        throw "$Pages-page probe produced no Qt test report."
    }
    $reportText = Get-Content -LiteralPath $report -Raw
    $preservedReport = Join-Path (Split-Path -Parent $testExecutablePath) `
        ("expanded-atlas-{0:d2}-runtime-latest.txt" -f $Pages)
    Copy-Item -LiteralPath $report -Destination $preservedReport -Force
    Write-Host $reportText
    if ($exitCode -ne 0) {
        throw "$Pages-page expanded-atlas runtime probe failed with exit code $exitCode."
    }
    if ($reportText -match '(?m)^FAIL!' -or
        $reportText -notmatch 'Totals:\s+\d+ passed,\s+0 failed,') {
        throw "$Pages-page expanded-atlas runtime probe failed its Qt test assertions."
    }
    if ($reportText -notmatch 'RUNTIME_FRAME_EVIDENCE.*rendered=\s*true') {
        throw "$Pages-page probe produced no passing captured-frame terrain evidence."
    }
    if ($reportText -notmatch 'RUNTIME_MEMORY_EVIDENCE.*samples=') {
        throw "$Pages-page probe produced no process-memory evidence marker."
    }
    if (-not (Test-Path -LiteralPath $memoryEvidence -PathType Leaf)) {
        throw "$Pages-page probe did not preserve its process-memory evidence."
    }
    $memory = Get-Content -LiteralPath $memoryEvidence -Raw | ConvertFrom-Json
    if ($memory.schema -ne 'ei-runtime-memory-evidence-v1' -or
        [int]$memory.atlasPages -ne $Pages -or
        [int]$memory.atlasSlots -ne [int]$manifest.active_slots -or
        [int]$memory.sampleCount -lt 2 -or
        [double]$memory.observedPeakWorkingSetBytes -le 0 -or
        [double]$memory.peakPrivateBytes -le 0) {
        throw "$Pages-page probe produced malformed or incomplete process-memory evidence."
    }
    $preservedMemory = Join-Path (Split-Path -Parent $testExecutablePath) `
        ("expanded-atlas-{0:d2}-memory-latest.json" -f $Pages)
    Copy-Item -LiteralPath $memoryEvidence -Destination $preservedMemory -Force
    Write-Host (("RUNTIME_MEMORY_SUMMARY pages={0} slots={1} samples={2} " +
        "peak_working_set_mib={3:N1} peak_private_mib={4:N1} final_private_mib={5:N1}") `
        -f $Pages, [int]$manifest.active_slots, [int]$memory.sampleCount,
        ([double]$memory.observedPeakWorkingSetBytes / 1MB),
        ([double]$memory.peakPrivateBytes / 1MB),
        ([double]$memory.finalPrivateBytes / 1MB))
}

function Write-MemoryComparison {
    param([int[]]$Pages)
    $rows = @()
    foreach ($pageCount in $Pages) {
        $path = Join-Path (Split-Path -Parent $testExecutablePath) `
            ("expanded-atlas-{0:d2}-memory-latest.json" -f $pageCount)
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Memory comparison input is missing: $path"
        }
        $memory = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
        $rows += [ordered]@{
            pages = [int]$memory.atlasPages
            slots = [int]$memory.atlasSlots
            samples = [int]$memory.sampleCount
            duration_ms = [int64]$memory.durationMs
            gameplay_ready_working_set_mib = [math]::Round(
                [double]$memory.gameplayReadyWorkingSetBytes / 1MB, 3)
            gameplay_ready_private_mib = [math]::Round(
                [double]$memory.gameplayReadyPrivateBytes / 1MB, 3)
            observed_peak_working_set_mib = [math]::Round(
                [double]$memory.observedPeakWorkingSetBytes / 1MB, 3)
            reported_peak_working_set_mib = [math]::Round(
                [double]$memory.reportedPeakWorkingSetBytes / 1MB, 3)
            peak_private_mib = [math]::Round(
                [double]$memory.peakPrivateBytes / 1MB, 3)
            final_working_set_mib = [math]::Round(
                [double]$memory.finalWorkingSetBytes / 1MB, 3)
            final_private_mib = [math]::Round(
                [double]$memory.finalPrivateBytes / 1MB, 3)
            evidence = $path
        }
    }
    $comparison = [ordered]@{
        schema = 'ei-expanded-atlas-memory-comparison-v1'
        captured_utc = [DateTime]::UtcNow.ToString('o')
        source = 'GetProcessMemoryInfo sampled during owned loading and dwell'
        probes = $rows
    }
    $comparisonPath = Join-Path (Split-Path -Parent $testExecutablePath) `
        'expanded-atlas-memory-comparison-latest.json'
    $comparison | ConvertTo-Json -Depth 6 | Set-Content `
        -LiteralPath $comparisonPath -Encoding UTF8
    Write-Host "Memory comparison written: $comparisonPath"
}

$controlFailure = $null
try {
    Invoke-ExpandedAtlasProbe -Pages 8
}
catch {
    $controlFailure = "$($_.Exception.Message) $($_.ScriptStackTrace)"
}
if ($controlFailure) {
    throw "The 8-page/512-slot custom-RES control failed; higher-page probes were not started: $controlFailure"
}
if ($ControlOnly) {
    Write-MemoryComparison -Pages @(8)
    Write-Host "Custom 8-page/512-slot RES control passed; higher-page probes were intentionally not started."
    return
}

$failures = @()
foreach ($pages in @(9, 16)) {
    try {
        Invoke-ExpandedAtlasProbe -Pages $pages
    }
    catch {
        $failures += $_.Exception.Message
        Write-Warning $_.Exception.Message
    }
}
if ($failures.Count -gt 0) {
    throw ("Terrain-atlas runtime matrix failed:`n - " + ($failures -join "`n - "))
}
Write-MemoryComparison -Pages @(8, 9, 16)
Write-Host "Custom 8-page/512-slot RES control passed first; 9-page and 16-page owned-process terrain-atlas probes also passed."
