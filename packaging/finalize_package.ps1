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

Write-Output "PASS: finalized clean package; removed $($removed.Count) private/runtime file(s)"
foreach ($path in $removed) {
    Write-Output "removed: $path"
}
