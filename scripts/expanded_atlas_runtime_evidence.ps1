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
