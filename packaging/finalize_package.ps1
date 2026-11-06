param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot,
    [string]$PreviousPackageRoot,
    [string[]]$AllowedRemovedPath = @(),
    [string]$CompositionDecision,
    [switch]$InitialPackage
)

$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'program_files.ps1')
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$removed = [System.Collections.Generic.List[string]]::new()

# A complete manifest can still describe an incomplete delivery. Always compare
# with the accepted installed program manifest, even in a git-archive build.
if([string]::IsNullOrWhiteSpace($PreviousPackageRoot)) {
    $installed = Join-Path (Split-Path -Parent $PSScriptRoot) 'artifacts\EI-Mapper-Ataman-2-first-test'
    if(Test-Path -LiteralPath (Join-Path $installed 'SHA256SUMS.txt') -PathType Leaf) {
        $PreviousPackageRoot = $installed
    } elseif(-not $InitialPackage) {
        throw 'An accepted program manifest is required. Pass -PreviousPackageRoot; a genuinely initial package needs -InitialPackage and -CompositionDecision.'
    }
}
if(($InitialPackage -or $AllowedRemovedPath.Count -gt 0) -and [string]::IsNullOrWhiteSpace($CompositionDecision)) {
    throw 'Initial packaging or removal requires an explicit -CompositionDecision and exact -AllowedRemovedPath entries.'
}
if(-not [string]::IsNullOrWhiteSpace($PreviousPackageRoot)) {
    $previous = (Resolve-Path -LiteralPath $PreviousPackageRoot).Path
    if($previous -eq $root){throw 'The accepted installation is not a staging folder.'}
    $manifest = Join-Path $previous 'SHA256SUMS.txt'
    if(-not(Test-Path -LiteralPath $manifest -PathType Leaf)){throw 'Accepted manifest is missing.'}
    $null = Read-ProgramManifest $previous
    # Fail before cleanup: a missing managed file must not be hidden by a new
    # self-consistent manifest. The full validator repeats this after cleanup.
    foreach($line in Get-Content -LiteralPath $manifest) {
        if($line -notmatch '^([0-9a-fA-F]{64}) \*(.+)$'){throw 'Malformed accepted manifest.'}
        $relative=$Matches[2].Replace('\','/')
        $destination=[IO.Path]::GetFullPath((Join-Path $root $relative))
        if([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or $relative.Split('/') -contains '..' -or -not $destination.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe accepted manifest path.'}
        if(-not(Test-Path -LiteralPath $destination -PathType Leaf) -and $relative -notin $AllowedRemovedPath){throw "Managed program file would be lost: $relative"}
    }
    Write-Output "Accepted program manifest: $previous"
}

function Remove-PackageFile([System.IO.FileInfo]$File) {
    $relative = $File.FullName.Substring($root.Length + 1)
    [System.IO.File]::Delete($File.FullName)
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

foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.log', '.dmp', '.mdmp', '.sav')
}) {
    Remove-PackageFile $file
}

# Runtime-generated performance caches belong to the tester's own installation
# and must never be carried forward from a package used during development.
foreach ($cacheDirectory in Get-ChildItem -LiteralPath $root -Recurse -Directory -Filter 'cache') {
    $relative = $cacheDirectory.FullName.Substring($root.Length + 1)
    Remove-Item -LiteralPath $cacheDirectory.FullName -Recurse -Force
    $removed.Add($relative + '\')
}

# A tester folder is a runtime package, never a qmake/Make shadow directory.
# Remove common compiler intermediates defensively; the strict validator below
# will still reject anything this list does not recognize.
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.o', '.obj', '.a', '.lib', '.exp', '.ilk', '.pdb', '.zip') -or
    $_.Name -eq '.qmake.stash' -or
    $_.Name -like 'Makefile*' -or
    $_.Name -like 'moc_*.cpp' -or
    $_.Name -like 'qrc_*.cpp' -or
    $_.Name -like 'ui_*.h'
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

# This user manual is part of every package, including git-archive builds.
# Resolve it from the source tree owning this finalizer, never a previous install.
$travelSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'docs\TRAVEL_EDITOR.md'
if (-not (Test-Path -LiteralPath $travelSource -PathType Leaf)) {
    throw "Required package manual source is missing: $travelSource"
}
Copy-Item -LiteralPath $travelSource -Destination (Join-Path $root 'TRAVEL_EDITOR.md') -Force

# Preserve the complete color-icon payload carried by historical Ataman
# packages. The current source tree has no direct references to this directory;
# keep it as part of the existing distribution composition contract.
$colorIconSource = Join-Path (Split-Path -Parent $PSScriptRoot) 'packaging\assets\icons_color'
$colorIconFiles = @(Get-ChildItem -LiteralPath $colorIconSource -File)
if($colorIconFiles.Count -ne 23)
{
    throw "Expected 23 archived icons_color assets, found $($colorIconFiles.Count): $colorIconSource"
}
$colorIconDestination = Join-Path $root 'icons_color'
$null = New-Item -ItemType Directory -Force -Path $colorIconDestination
foreach($file in $colorIconFiles)
{
    Copy-Item -LiteralPath $file.FullName `
        -Destination (Join-Path $colorIconDestination $file.Name) -Force
}

# Owner's finalization-133 policy: UTF-8 without BOM, CRLF package texts.
# Normalize package copies only; source files and Git blobs stay untouched.
$utf8 = New-Object System.Text.UTF8Encoding($false, $true)
foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.md', '.txt', '.json') -or $_.Name -eq 'VERSION'
}) {
    if ($file.Name -eq 'SHA256SUMS.txt') { continue }
    $text = [System.IO.File]::ReadAllText($file.FullName, $utf8)
    $text = $text.TrimStart([char]0xFEFF).Replace("`r`n", "`n").Replace("`r", "`n").Replace("`n", "`r`n")
    [System.IO.File]::WriteAllText($file.FullName, $text, $utf8)
}

$checksums = Join-Path $root 'SHA256SUMS.txt'
if (Test-Path -LiteralPath $checksums) {
    [System.IO.File]::Delete($checksums)
}
Get-ChildItem -LiteralPath $root -Recurse -File |
    Sort-Object FullName |
    ForEach-Object {
        $relative = $_.FullName.Substring($root.Length + 1).Replace('\', '/')
        $hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        "$hash *$relative"
    } | Set-Content -LiteralPath $checksums -Encoding ascii

# Reject paths with the same policy used by installation and rollback.
$null = Read-ProgramManifest $root

$validatorArguments = @{ PackageRoot = $root }
if(-not [string]::IsNullOrWhiteSpace($PreviousPackageRoot))
{
    $validatorArguments.PreviousPackageRoot = $PreviousPackageRoot
}
if($AllowedRemovedPath.Count -gt 0)
{
    $validatorArguments.AllowedRemovedPath = $AllowedRemovedPath
}
& (Join-Path $PSScriptRoot 'validate_distribution.ps1') @validatorArguments
if (-not $?) {
    exit 1
}

Write-Output "PASS: finalized clean package; removed $($removed.Count) stale/private/runtime file(s)"
foreach ($path in $removed) {
    Write-Output "removed: $path"
}
