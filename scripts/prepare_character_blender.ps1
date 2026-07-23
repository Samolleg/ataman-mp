param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('item', 'humanoid')]
    [string] $Lane,

    [Parameter(Mandatory = $true)] [string] $Workspace,
    [Parameter(Mandatory = $true)] [string] $AddonSource,
    [Parameter(Mandatory = $true)] [string] $DownloadCache,
    [switch] $RefreshAddon
)

$ErrorActionPreference = 'Stop'
$manifestPath = Join-Path $PSScriptRoot 'character_blender_lanes.json'
$manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
$laneConfig = $manifest.lanes.$Lane
if (-not $laneConfig) {
    throw "Unknown character Blender lane: $Lane"
}

$workspacePath = [IO.Path]::GetFullPath($Workspace)
$addonPath = [IO.Path]::GetFullPath($AddonSource)
$downloadPath = [IO.Path]::GetFullPath($DownloadCache)
if (Test-Path -LiteralPath (Join-Path $workspacePath 'game.exe')) {
    throw 'Refusing to prepare a Blender environment in a game installation.'
}
if (-not (Test-Path -LiteralPath (Join-Path $addonPath '__init__.py') -PathType Leaf)) {
    throw "AddonSource must be an extracted ei_figer package: $addonPath"
}

$archivePath = Join-Path $downloadPath $laneConfig.archive
if (-not (Test-Path -LiteralPath $archivePath -PathType Leaf)) {
    throw "Missing portable Blender archive: $archivePath`nExpected source: $($laneConfig.download_url)"
}
$archiveHash = (Get-FileHash -LiteralPath $archivePath -Algorithm SHA256).Hash.ToLowerInvariant()
if ($archiveHash -ne $laneConfig.sha256) {
    throw "Portable Blender archive hash mismatch: $archiveHash (expected $($laneConfig.sha256))"
}

$addonText = Get-Content -LiteralPath (Join-Path $addonPath '__init__.py') -Raw
$versionPattern = "'version'\s*:\s*\(\s*(\d+)\s*,\s*(\d+)"
$versionMatch = [regex]::Match($addonText, $versionPattern)
if (-not $versionMatch.Success) {
    throw 'Could not identify ei_figer version from bl_info.'
}
$addonVersion = "{0}.{1}" -f $versionMatch.Groups[1].Value, $versionMatch.Groups[2].Value
if ($addonVersion -ne $laneConfig.addon.version) {
    throw "Wrong ei_figer for lane '$Lane': found $addonVersion, expected $($laneConfig.addon.version)."
}

$laneRoot = Join-Path $workspacePath "$Lane-lane"
$runtimeRoot = Join-Path $laneRoot 'runtime'
$blenderRoot = Join-Path $runtimeRoot $laneConfig.runtime_directory
$blenderExecutable = Join-Path $blenderRoot 'blender.exe'
New-Item -ItemType Directory -Force -Path $runtimeRoot | Out-Null
if (-not (Test-Path -LiteralPath $blenderExecutable -PathType Leaf)) {
    $tar = Join-Path $env:SystemRoot 'System32\tar.exe'
    if (-not (Test-Path -LiteralPath $tar -PathType Leaf)) {
        throw "Windows tar.exe is required to extract the portable Blender archive: $tar"
    }
    & $tar -xf $archivePath -C $runtimeRoot
    if ($LASTEXITCODE -ne 0) {
        throw "Portable Blender archive extraction failed with exit code $LASTEXITCODE"
    }
}
if (-not (Test-Path -LiteralPath $blenderExecutable -PathType Leaf)) {
    throw "Portable Blender extraction did not produce: $blenderExecutable"
}

$configRoot = Join-Path $laneRoot 'portable-config'
$addonRoot = Join-Path $configRoot 'scripts\addons'
$installedAddon = Join-Path $addonRoot 'ei_figer'
New-Item -ItemType Directory -Force -Path $addonRoot | Out-Null
if (Test-Path -LiteralPath $installedAddon) {
    if (-not $RefreshAddon) {
        throw "Addon is already staged. Archive it or pass -RefreshAddon: $installedAddon"
    }
    $archiveName = 'ei_figer.previous.{0}' -f (Get-Date -Format 'yyyyMMdd-HHmmss')
    Move-Item -LiteralPath $installedAddon -Destination (Join-Path $addonRoot $archiveName)
}
Copy-Item -LiteralPath $addonPath -Destination $installedAddon -Recurse

$addonFiles = Get-ChildItem -LiteralPath $installedAddon -File -Recurse | Sort-Object FullName
$addonHashes = foreach ($file in $addonFiles) {
    [ordered]@{
        path = $file.FullName.Substring($installedAddon.Length + 1).Replace('\', '/')
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        size = $file.Length
    }
}
$provenance = [ordered]@{
    schema = 1
    lane = $Lane
    generated_utc = (Get-Date).ToUniversalTime().ToString('o')
    blender = [ordered]@{
        version = $laneConfig.blender_version
        executable = $blenderExecutable
        archive = $archivePath
        archive_sha256 = $archiveHash
    }
    addon = [ordered]@{
        project = $laneConfig.addon.project
        declared_version = $addonVersion
        copied_from = $addonPath
        files = $addonHashes
    }
}
$provenancePath = Join-Path $laneRoot 'tool-provenance.json'
$provenance | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $provenancePath -Encoding UTF8

Write-Host "Prepared $Lane lane"
Write-Host "Blender: $blenderExecutable"
Write-Host "ei_figer: $installedAddon"
Write-Host "Provenance: $provenancePath"
