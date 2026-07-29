function Get-ExpandedAtlasProbeClassification {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][bool]$PassShape,
        [Parameter(Mandatory = $true)][bool]$TimedOut,
        [Parameter(Mandatory = $true)][bool]$NativeRuntimeFailure,
        [Parameter(Mandatory = $true)][bool]$MemoryFailure,
        [Parameter(Mandatory = $true)][bool]$AuthoritativeEvidence
    )

    # Failure evidence is authoritative. A stale GameplayReady line, a colorful
    # crash-dialog capture, or a successful cleanup must never turn the same
    # attempt back into a pass.
    if ($MemoryFailure) { return 'memory-or-address-space-exhaustion' }
    if ($NativeRuntimeFailure) { return 'native-runtime-crash' }
    if ($TimedOut) { return 'timeout-unclassified' }
    if ($PassShape -and $AuthoritativeEvidence) { return 'pass' }
    return 'unrelated-runtime-or-staging-failure'
}

function Test-ExpandedAtlasProbePassed {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][string]$Classification
    )
    return $Classification -eq 'pass'
}

function Test-ExpandedAtlasMemoryPressure {
    [CmdletBinding()]
    param(
        [Parameter(Mandatory = $true)][int64]$CombinedRgbaBytes,
        [Parameter(Mandatory = $true)][int64]$GameplayReadyPrivateBytes,
        [Parameter(Mandatory = $true)][int64]$PeakPrivateBytes,
        [Parameter(Mandatory = $true)][int64]$MinimumLargestFreeRegionBytes
    )
    if ($CombinedRgbaBytes -le 0 -or $MinimumLargestFreeRegionBytes -le 0) {
        return $false
    }
    $growth = $PeakPrivateBytes - $GameplayReadyPrivateBytes
    $fragmentedNominalCopy = $growth -ge $CombinedRgbaBytes `
        -and $MinimumLargestFreeRegionBytes -lt $CombinedRgbaBytes
    # Some DirectDraw backends retain several decoded/staging copies before
    # the next allocation fails.  In that shape the largest remaining region
    # can still exceed one nominal RGBA atlas even though the 32-bit process
    # has already crossed 2 GiB and amplified the atlas by an order of
    # magnitude.  Treat that measured surge as capacity pressure rather than
    # an unrelated native crash.
    $amplified32BitLoad = $PeakPrivateBytes -ge 2GB `
        -and $growth -ge (8 * $CombinedRgbaBytes)
    return $fragmentedNominalCopy -or $amplified32BitLoad
}

function Get-ChangedNativeDumpRows {
    [CmdletBinding()]
    param(
        [AllowEmptyCollection()][object[]]$Before = @(),
        [AllowEmptyCollection()][object[]]$After = @()
    )
    $changed = @()
    foreach ($entry in @($After)) {
        $prior = @($Before | Where-Object { $_.path -eq $entry.path } |
            Select-Object -First 1)
        if ($prior.Count -eq 0 -or [int64]$prior[0].length -ne [int64]$entry.length `
            -or $prior[0].modified_utc -ne $entry.modified_utc) {
            $changed += $entry
        }
    }
    return $changed
}
