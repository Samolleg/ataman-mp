param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$removed = [System.Collections.Generic.List[string]]::new()

function Remove-PackageFile([System.IO.FileInfo]$File) {
    $relative = $File.FullName.Substring($root.Length + 1)
    Remove-Item -LiteralPath $File.FullName -Force
    $removed.Add($relative)
}

# Every tester and release package is a clean first-run artifact. Never carry
# developer/tester paths, recent sessions, logs, saves, or dumps forward from a
# folder that has already been launched.
foreach ($name in @('endurance-options.json', 'options.json')) {
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name) {
        Remove-PackageFile $file
    }
}

$logsRoot = Join-Path $root 'logs'
if (Test-Path -LiteralPath $logsRoot) {
    foreach ($file in Get-ChildItem -LiteralPath $logsRoot -Recurse -File) {
        Remove-PackageFile $file
    }
}

foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.dmp', '.mdmp', '.sav')
}) {
    Remove-PackageFile $file
}

# Tester packages carry one concise changelist: the file matching the alpha
# number in VERSION. Carrying every historical ALPHA*.md forward made the
# distribution root noisy and obscured which notes apply to the executable.
$versionPath = Join-Path $root 'VERSION'
if (Test-Path -LiteralPath $versionPath -PathType Leaf) {
    $version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    if ($version -match 'alpha\.(\d+)') {
        $alphaNumber = [int]$Matches[1]
        $expectedPattern = "^ALPHA$alphaNumber(?:_|$).*\.md$"
        $alphaChangelists = @(Get-ChildItem -LiteralPath $root -File -Filter 'ALPHA*.md')
        foreach ($file in $alphaChangelists) {
            if ($file.Name -notmatch $expectedPattern) {
                Remove-PackageFile $file
            }
        }
        $currentChangelists = @(Get-ChildItem -LiteralPath $root -File -Filter 'ALPHA*.md' |
            Where-Object { $_.Name -match $expectedPattern })
        if ($currentChangelists.Count -ne 1) {
            throw "Expected exactly one alpha.$alphaNumber changelist, found $($currentChangelists.Count)."
        }
    }
}

$checksums = Join-Path $root 'SHA256SUMS.txt'
if (Test-Path -LiteralPath $checksums) {
    Remove-Item -LiteralPath $checksums -Force
}
Get-ChildItem -LiteralPath $root -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash *$relative"
    } | Set-Content -LiteralPath $checksums -Encoding ascii

& (Join-Path $PSScriptRoot 'validate_distribution.ps1') $root
if (-not $?) {
    exit 1
}

Write-Output "PASS: finalized clean package; removed $($removed.Count) stale/private/runtime file(s)"
foreach ($path in $removed) {
    Write-Output "removed: $path"
}
