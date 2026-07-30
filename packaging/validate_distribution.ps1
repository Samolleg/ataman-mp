param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$failures = [System.Collections.Generic.List[string]]::new()

$requiredFiles = @(
    'EIMapperEndurance.exe',
    'VERSION',
    'Readme.md',
    'LICENSE.txt',
    'NOTICE.md',
    'Qt5Core.dll',
    'Qt5Gui.dll',
    'Qt5Widgets.dll',
    'Qt5OpenGL.dll',
    'Qt5Concurrent.dll',
    'platforms\qwindows.dll'
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
foreach ($relative in $requiredIcons) {
    if (-not (Test-Path -LiteralPath (Join-Path $root $relative) -PathType Leaf)) {
        $failures.Add("required UI icon is missing: $relative")
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
}

$checksums = Join-Path $root 'SHA256SUMS.txt'
if(Test-Path -LiteralPath $checksums)
{
    $forbiddenManifestEntries = Select-String -LiteralPath $checksums -Pattern '(endurance-options\.json|options\.json)$|\.log$|\.(dmp|mdmp|sav|o|obj|a|lib|exp|ilk|pdb|zip)$|(^|/)(Makefile[^/]*|\.qmake\.stash|moc_[^/]*\.cpp|qrc_[^/]*\.cpp|ui_[^/]*\.h)$'
    foreach($entry in $forbiddenManifestEntries)
    {
        $failures.Add("checksum manifest references a private/runtime artifact: $($entry.Line)")
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
