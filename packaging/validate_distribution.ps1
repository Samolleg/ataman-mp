param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot,
    [string]$PreviousPackageRoot,
    [string[]]$AllowedRemovedPath = @()
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$failures = [System.Collections.Generic.List[string]]::new()

$requiredFiles = @(
    'EIMapperEndurance.exe',
    'VERSION',
    'Readme.md',
    'TRAVEL_EDITOR.md',
    'LICENSE.txt',
    'NOTICE.md',
    'Qt5Core.dll',
    'Qt5Gui.dll',
    'Qt5Widgets.dll',
    'Qt5Multimedia.dll',
    'Qt5OpenGL.dll',
    'Qt5Concurrent.dll',
    'libgcc_s_seh-1.dll',
    'libstdc++-6.dll',
    'libwinpthread-1.dll',
    'platforms\qwindows.dll',
    'audio\qtaudio_windows.dll',
    'mediaservice\dsengine.dll'
)
foreach ($relative in $requiredFiles) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        $failures.Add("required runtime file is missing: $relative")
    }
}

$requiredIcons = @(
    'icons_fluent\copy.ico',
    'icons_fluent\move.ico',
    'icons_fluent\landscape.ico',
    'icons_fluent\undo.ico'
)
$requiredHeightmaps = @(
    'heightmaps\README.md',
    'heightmaps\diagonal_ramp_257.png',
    'heightmaps\mixed_features_heightmap_1254.png',
    'heightmaps\orientation_corners_257.png',
    'heightmaps\radial_hill_257.png',
    'heightmaps\seam_cross_257.png',
    'heightmaps\terraced_basin_257.png'
)
$requiredColorIcons = @(
    'icons_color\copy.ico', 'icons_color\copy.png',
    'icons_color\cut.ico', 'icons_color\cut.png',
    'icons_color\delete.ico', 'icons_color\delete.png',
    'icons_color\erase.ico', 'icons_color\erase.png',
    'icons_color\move.ico', 'icons_color\move.png',
    'icons_color\new.ico', 'icons_color\new.png',
    'icons_color\paste.ico', 'icons_color\paste.png',
    'icons_color\redo.ico', 'icons_color\redo.png',
    'icons_color\rotate.png',
    'icons_color\scale.ico', 'icons_color\scale.png',
    'icons_color\select.ico', 'icons_color\select.png',
    'icons_color\undo.ico', 'icons_color\undo.png'
)
foreach ($relative in @($requiredIcons) + @($requiredHeightmaps) + @($requiredColorIcons)) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        $failures.Add("required package asset is missing: $relative")
    }
}

$bridge = Join-Path $root 'runtime\EIMapperEnduranceJmvBridge.plg'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) {
    $failures.Add('packaged JMV launch bridge is missing: runtime\EIMapperEnduranceJmvBridge.plg')
}
$runtimeHost = Join-Path $root 'runtime\EIMapperEnduranceRuntimeHost.exe'
if(-not (Test-Path -LiteralPath $runtimeHost -PathType Leaf))
{
    $failures.Add('packaged Endurance runtime host is missing: runtime\EIMapperEnduranceRuntimeHost.exe')
}

foreach ($name in @('endurance-options.json', 'options.json')) {
    $matches = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name
    foreach ($match in $matches) {
        $failures.Add("user settings included: $($match.FullName.Substring($root.Length + 1))")
    }
}

foreach ($artifact in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.log', '.dmp', '.mdmp', '.sav')
}) {
    $failures.Add("private runtime artifact included: $($artifact.FullName.Substring($root.Length + 1))")
}

foreach ($artifact in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.o', '.obj', '.a', '.lib', '.exp', '.ilk', '.pdb', '.zip') -or
    $_.Name -eq '.qmake.stash' -or
    $_.Name -like 'Makefile*' -or
    $_.Name -like 'moc_*.cpp' -or
    $_.Name -like 'qrc_*.cpp' -or
    $_.Name -like 'ui_*.h'
}) {
    $failures.Add("compiler/archive debris included: $($artifact.FullName.Substring($root.Length + 1))")
}

# Collection paths are relative to the descriptor, not to an arbitrary
# staging source. A package can contain the MPR bytes yet still present the
# editor with a missing map if the descriptor was moved without its relative
# layout (the modern-atlas R34 package exposed exactly this failure).
foreach ($collection in Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.collection') {
    $relativeCollection = $collection.FullName.Substring($root.Length + 1)
    $text = Get-Content -LiteralPath $collection.FullName -Raw
    $match = [regex]::Match($text, '(?m)^\s*mpr\s*=\s*"([^"]*)"\s*$')
    if (-not $match.Success -or [string]::IsNullOrWhiteSpace($match.Groups[1].Value)) {
        $failures.Add("collection has no MPR reference: $relativeCollection")
        continue
    }
    $reference = $match.Groups[1].Value.Replace('/', '\')
    $resolved = [IO.Path]::GetFullPath((Join-Path $collection.DirectoryName $reference))
    $insidePackage = $resolved.StartsWith(
        $root + '\', [System.StringComparison]::OrdinalIgnoreCase)
    if (-not $insidePackage) {
        $failures.Add("collection MPR escapes the package: $relativeCollection -> $reference")
    }
    elseif (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
        $failures.Add("collection MPR is missing: $relativeCollection -> $reference")
    }
}

foreach ($profile in Get-ChildItem -LiteralPath $root -Recurse -File -Filter '*.semantic-atlas.json') {
    $relative = $profile.FullName.Substring($root.Length + 1)
    try {
        $document = Get-Content -LiteralPath $profile.FullName -Raw | ConvertFrom-Json
        $portableClone = $document.provenance -eq 'portable-exact-stock-atlas-clone-v1'
        $portableModern = $document.provenance -eq 'portable-finalized-modern-atlas-v1'
        $portableRootPath = $relative.StartsWith(
            'terrain-atlas-profiles\', [System.StringComparison]::OrdinalIgnoreCase)
        $portableCollectionPath = $relative.StartsWith(
            'collections\', [System.StringComparison]::OrdinalIgnoreCase) -and
            $relative.Contains('\terrain-atlas-profiles\')
        $slotCount = [int]$document.atlasSlotCount
        $complete = $slotCount -gt 0 -and $document.approved -eq $true -and
            $document.atlasPixelSha256 -match '^[0-9a-fA-F]{64}$' -and
            @($document.slots).Count -eq $slotCount
        $allAssigned = @($document.slots | Where-Object { -not $_.assigned }).Count -eq 0
        $hasAssigned = @($document.slots | Where-Object { $_.assigned }).Count -gt 0
        $allowedStockClone = $portableClone -and $portableRootPath -and
            $complete -and $allAssigned
        # Modern finalized pages are physical 8x8 sheets. Their last page may
        # contain deliberately unassigned padding, so requiring every physical
        # slot to be assigned would reject the exact sidecar needed to reopen a
        # packaged visual-test MPR. Keep this exception collection-local.
        $allowedModernCollection = $portableModern -and
            $portableCollectionPath -and $complete -and $hasAssigned
        if (-not ($allowedStockClone -or $allowedModernCollection)) {
            $failures.Add("pre-shipped local Material atlas override included: $relative")
        }
    }
    catch {
        $failures.Add("invalid packaged Material atlas profile: $relative ($($_.Exception.Message))")
    }
}

$versionPath = Join-Path $root 'VERSION'
if (-not (Test-Path -LiteralPath $versionPath -PathType Leaf)) {
    $failures.Add('VERSION is missing')
}
else {
    $version = (Get-Content -LiteralPath $versionPath -Raw).Trim()
    if ($version -match 'alpha\.(\d+)') {
        $alphaNumber = [int]$Matches[1]
        $expectedPattern = "^ALPHA$alphaNumber(?:_|$).*\.md$"
        $alphaChangelists = @(Get-ChildItem -LiteralPath $root -File -Filter 'ALPHA*.md')
        $currentChangelists = @($alphaChangelists |
            Where-Object { $_.Name -match $expectedPattern })
        if ($currentChangelists.Count -ne 1) {
            $failures.Add("expected exactly one alpha.$alphaNumber changelist; found $($currentChangelists.Count)")
        }
        foreach ($file in $alphaChangelists |
            Where-Object { $_.Name -notmatch $expectedPattern }) {
            $failures.Add("stale alpha changelist included: $($file.Name)")
        }
    }
    elseif ($version -match '^[1-9][0-9]*$') {
        $changelogs = @(Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
            $_.Name -match '(?i)changelog|changelist|^ALPHA[0-9].*\.md$'
        })
        $currentNotes = Join-Path $root 'CHANGELOG.md'
        if ($changelogs.Count -ne 1 -or -not (Test-Path -LiteralPath $currentNotes -PathType Leaf)) {
            $failures.Add('integer releases require exactly one root CHANGELOG.md')
        } else {
            $notes = Get-Content -LiteralPath $currentNotes -Raw -Encoding UTF8
            $headings = [regex]::Matches($notes, '(?m)^#{1,2}\s+(?:Ataman\s+)?([0-9]+)(?=\s|$|[—–-])')
            if ($headings.Count -ne 1 -or $headings[0].Groups[1].Value -ne $version) {
                $failures.Add("CHANGELOG.md must describe only current version $version")
            }
        }
    }
    else { $failures.Add("unsupported package VERSION: $version") }
}

$checksums = Join-Path $root 'SHA256SUMS.txt'
if(Test-Path -LiteralPath $checksums)
{
    $manifestPaths = @{}
    foreach ($line in Get-Content -LiteralPath $checksums) {
        if ($line -notmatch '^([0-9a-fA-F]{64}) \*(.+)$') {
            $failures.Add("malformed checksum entry: $line"); continue
        }
        $digest = $Matches[1]; $relative = $Matches[2].Replace('\','/')
        try { $full = [IO.Path]::GetFullPath((Join-Path $root $relative)) }
        catch { $failures.Add("invalid checksum path: $relative"); continue }
        if ([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or
            $relative.Split('/') -contains '..' -or $relative.Split('/') -contains '.' -or
            -not $full.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase) -or
            $relative -eq 'SHA256SUMS.txt' -or $manifestPaths.ContainsKey($relative)) {
            $failures.Add("unsafe, self-referencing or duplicate checksum path: $relative"); continue
        }
        $manifestPaths[$relative] = $true
        if (-not (Test-Path -LiteralPath $full -PathType Leaf)) {
            $failures.Add("checksum file is missing: $relative"); continue
        }
        $linked = $false; $ancestor = Get-Item -LiteralPath $full
        while ($ancestor -and $ancestor.FullName -ne $root) {
            if ($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint) { $linked=$true; break }
            $ancestor = Get-Item -LiteralPath (Split-Path $ancestor.FullName -Parent)
        }
        if ($linked) { $failures.Add("linked checksum path is not portable: $relative"); continue }
        if ((Get-FileHash -LiteralPath $full -Algorithm SHA256).Hash -ne $digest) {
            $failures.Add("checksum mismatch: $relative")
        }
    }
    foreach ($file in Get-ChildItem -LiteralPath $root -Recurse -File) {
        $relative=$file.FullName.Substring($root.Length+1).Replace('\','/')
        if ($relative -ne 'SHA256SUMS.txt' -and -not $manifestPaths.ContainsKey($relative)) {
            $failures.Add("file is not covered by checksum manifest: $relative")
        }
    }
    $forbiddenManifestEntries = Select-String -LiteralPath $checksums -Pattern '(endurance-options\.json|options\.json)$|\.log$|\.(dmp|mdmp|sav|o|obj|a|lib|exp|ilk|pdb|zip)$|(^|/)(Makefile[^/]*|\.qmake\.stash|moc_[^/]*\.cpp|qrc_[^/]*\.cpp|ui_[^/]*\.h)$'
    foreach($entry in $forbiddenManifestEntries)
    {
        $failures.Add("checksum manifest references a private/runtime artifact: $($entry.Line)")
    }

    if(-not [string]::IsNullOrWhiteSpace($PreviousPackageRoot))
    {
        $previousRoot = (Resolve-Path -LiteralPath $PreviousPackageRoot).Path
        if($previousRoot -eq $root)
        {
            $failures.Add('previous package root must be a different staging directory')
        }
        else
        {
            $previousChecksums = Join-Path $previousRoot 'SHA256SUMS.txt'
            if(-not (Test-Path -LiteralPath $previousChecksums -PathType Leaf))
            {
                $failures.Add("previous package manifest is missing: $previousChecksums")
            }
            else
            {
                $previousPaths = [System.Collections.Generic.HashSet[string]]::new(
                    [System.StringComparer]::OrdinalIgnoreCase)
                $previousVersionPath = Join-Path $previousRoot 'VERSION'
                if(-not (Test-Path -LiteralPath $previousVersionPath -PathType Leaf))
                {
                    $failures.Add('previous package VERSION is missing')
                }
                foreach($line in Get-Content -LiteralPath $previousChecksums)
                {
                    if($line -notmatch '^([0-9a-fA-F]{64}) \*(.+)$')
                    {
                        $failures.Add("malformed previous checksum entry: $line")
                        continue
                    }
                    $relative = $Matches[2].Replace('\','/')
                    if([IO.Path]::IsPathRooted($relative) -or $relative.Contains(':') -or
                       $relative.Split('/') -contains '..' -or $relative.Split('/') -contains '.' -or
                       $relative -eq 'SHA256SUMS.txt')
                    {
                        $failures.Add("unsafe path in previous package manifest: $relative")
                        continue
                    }
                    if(-not $previousPaths.Add($relative))
                    {
                        $failures.Add("duplicate path in previous package manifest: $relative")
                    }
                }
                $allowed = [System.Collections.Generic.HashSet[string]]::new(
                    [System.StringComparer]::OrdinalIgnoreCase)
                foreach($path in $AllowedRemovedPath)
                {
                    $null = $allowed.Add($path.Replace('\','/'))
                }
                foreach($path in $allowed)
                {
                    if(-not $previousPaths.Contains($path))
                    {
                        $failures.Add("allowed removal is absent from previous package manifest: $path")
                    }
                }
                $removedPaths = @($previousPaths | Where-Object {
                    -not $manifestPaths.ContainsKey($_)
                } | Sort-Object)
                $unexplained = @($removedPaths | Where-Object { -not $allowed.Contains($_) })
                foreach($path in $unexplained)
                {
                    $failures.Add("package path removed without an explicit explanation: $path")
                }
                Write-Output "Previous package path comparison: $($previousPaths.Count) prior, $($manifestPaths.Count) current, $($removedPaths.Count) removed"
                if($removedPaths.Count -eq 0)
                {
                    Write-Output 'Removed paths: none'
                }
                else
                {
                    foreach($path in $removedPaths)
                    {
                        $classification = if($allowed.Contains($path)) { 'allowed' } else { 'unexplained' }
                        Write-Output "Removed path ($classification): $path"
                    }
                }
            }
        }
    }
}
else
{
    $failures.Add('SHA256SUMS.txt is missing; the package was not finalized')
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "PASS: complete runnable package contains one current changelist and no private/runtime or compiler debris"
