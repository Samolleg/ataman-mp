param(
    [Parameter(Mandatory = $true)] [string] $Workspace,
    [Parameter(Mandatory = $true)] [string] $EiFigerSource,
    [string] $BackgroundScript,
    [switch] $PrepareAddon
)

$ErrorActionPreference = 'Stop'
$workspacePath = [System.IO.Path]::GetFullPath($Workspace)
$addonPath = [System.IO.Path]::GetFullPath($EiFigerSource)
if (-not (Test-Path -LiteralPath $workspacePath -PathType Container)) {
    throw "Workspace does not exist: $workspacePath"
}
if (Test-Path -LiteralPath (Join-Path $workspacePath 'game.exe')) {
    throw 'Refusing to use a game installation as the Blender workspace.'
}
if (-not (Test-Path -LiteralPath (Join-Path $addonPath '__init__.py') -PathType Leaf)) {
    throw "EiFigerSource must be the extracted ei_figer package directory: $addonPath"
}

$blender = Get-ChildItem -LiteralPath (Join-Path $workspacePath 'runtime') -Filter blender.exe -File -Recurse |
    Sort-Object FullName | Select-Object -First 1
if (-not $blender) {
    throw "No portable blender.exe found below $workspacePath\runtime"
}

$configRoot = Join-Path $workspacePath 'portable-config'
$scriptRoot = Join-Path $configRoot 'scripts'
$addonRoot = Join-Path $scriptRoot 'addons'
New-Item -ItemType Directory -Force -Path $addonRoot | Out-Null
if ($PrepareAddon) {
    $destination = Join-Path $addonRoot 'ei_figer'
    if (Test-Path -LiteralPath $destination) {
        throw "Addon destination already exists; remove or archive it explicitly first: $destination"
    }
    Copy-Item -LiteralPath $addonPath -Destination $destination -Recurse
}

$env:BLENDER_USER_CONFIG = Join-Path $configRoot 'config'
$env:BLENDER_USER_SCRIPTS = $scriptRoot
$env:BLENDER_USER_DATAFILES = Join-Path $configRoot 'datafiles'
$env:PYTHONPATH = (Split-Path -Parent $addonPath)

$arguments = @('--factory-startup')
if ($BackgroundScript) {
    $scriptPath = [System.IO.Path]::GetFullPath($BackgroundScript)
    if (-not (Test-Path -LiteralPath $scriptPath -PathType Leaf)) {
        throw "Background script does not exist: $scriptPath"
    }
    $arguments += @('--background', '--python', $scriptPath)
}

Write-Host "Blender: $($blender.FullName)"
Write-Host "Workspace: $workspacePath"
Write-Host "ei_figer: $addonPath"
& $blender.FullName @arguments
exit $LASTEXITCODE
