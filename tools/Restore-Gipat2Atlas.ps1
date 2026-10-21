[CmdletBinding()]
param(
    [string]$ModRoot = 'C:\GOG Games\PZ\MODS\DejaVu',
    [string]$Python,
    [switch]$Apply
)
$ErrorActionPreference = 'Stop'
if ($Apply -and (Get-Process -Name EIMapperEndurance,game -ErrorAction SilentlyContinue)) {
    throw 'Close the mapper and game before applying the atlas publication.'
}
if (-not $Python) {
    $candidates = @(
        (Join-Path $env:USERPROFILE '.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python313\python.exe'),
        (Join-Path $env:LOCALAPPDATA 'Programs\Python\Python312\python.exe')
    )
    $Python = $candidates | Where-Object { Test-Path -LiteralPath $_ -PathType Leaf } | Select-Object -First 1
}
if (-not $Python) { throw 'Python 3.9+ is required; specify its executable with -Python.' }
$taskArgs = @(
    (Join-Path $PSScriptRoot 'publish_map_local_atlas.py'),
    '--map', (Join-Path $ModRoot 'Maps\gipat2.mpr'),
    '--atlas', (Join-Path $ModRoot 'Maps\gipat2-atlas.res'),
    '--pack', (Join-Path $ModRoot 'res\HDPack_Terrain.res')
)
if ($Apply) { $taskArgs += '--apply' }
& $Python @taskArgs
if ($LASTEXITCODE -ne 0) { throw "Atlas publication failed (exit $LASTEXITCODE). See output above; retain any backup." }
