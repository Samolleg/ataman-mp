param(
    [string]$OutputDirectory = 'build-tests-terrain-memory-synthetic/probe-build',
    [string]$VsDevCmd = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/Tools/VsDevCmd.bat'
)
$ErrorActionPreference = 'Stop'
$probeRoot = (Resolve-Path "$PSScriptRoot/../..").Path
$probeOwnedRoot = [IO.Path]::GetFullPath((Join-Path $probeRoot 'build-tests-terrain-memory-synthetic'))
$probeOutput = [IO.Path]::GetFullPath((Join-Path $probeRoot $OutputDirectory))
if (-not $probeOutput.StartsWith($probeOwnedRoot + [IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Output must be below owned build-tests-terrain-memory-synthetic'
}
New-Item -ItemType Directory -Path $probeOutput -Force | Out-Null
# /LARGEADDRESSAWARE is verified against the installed game PE by the runner.
# The helper is console-only and writes all compiler artifacts to the owned root.
$probeSource = Join-Path $PSScriptRoot 'memory_probe32.cpp'
$probeBatch = Join-Path $probeOutput 'build.cmd'
$probeExe = Join-Path $probeOutput 'memory_probe32.exe'
$probeObj = Join-Path $probeOutput 'memory_probe32.obj'
$probeText = @"
@echo off
set "PATH=$([Environment]::GetFolderPath('ProgramFilesX86'))\Microsoft Visual Studio\Installer;%PATH%"
call "$VsDevCmd" -no_logo -arch=x86 -host_arch=x64
if errorlevel 1 exit /b %errorlevel%
cl /nologo /O2 /EHsc /std:c++14 /MT "$probeSource" /Fo"$probeObj" /Fe"$probeExe" /link /LARGEADDRESSAWARE /SUBSYSTEM:CONSOLE psapi.lib
exit /b %errorlevel%
"@
[IO.File]::WriteAllText($probeBatch,$probeText,[Text.Encoding]::ASCII)
Push-Location $probeOutput
try { & $env:ComSpec /d /c $probeBatch; if($LASTEXITCODE -ne 0) {throw "x86 build failed: $LASTEXITCODE"} }
finally { Pop-Location }
