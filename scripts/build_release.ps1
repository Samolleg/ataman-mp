[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-release",
    [string]$QtBin = $env:EI_MAPPER_QT_BIN,
    [string]$MinGwBin = $env:EI_MAPPER_MINGW_BIN,
    [switch]$Msvc,
    [string]$VcVars64,
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'build_repository_identity.ps1')

# The shared Endurance checkout keeps its supported production kit here.  Use
# it automatically so fresh agent shells and linked worktrees do not depend on
# undocumented machine PATH state. Explicit arguments/environment still win.
if (-not $QtBin) {
    $bundledQt = Join-Path $repositoryRoot '.tooling\Qt\5.12.12\mingw73_64\bin'
    if (Test-Path -LiteralPath (Join-Path $bundledQt 'qmake.exe') -PathType Leaf) {
        $QtBin = $bundledQt
    }
}
if (-not $MinGwBin) {
    $bundledMinGw = Join-Path $repositoryRoot '.tooling\Qt\Tools\mingw730_64\bin'
    if (Test-Path -LiteralPath (Join-Path $bundledMinGw 'mingw32-make.exe') -PathType Leaf) {
        $MinGwBin = $bundledMinGw
    }
}

function Import-VcEnvironment([string]$ScriptPath) {
    if (-not $ScriptPath) {
        throw 'MSVC mode requires -VcVars64 <path-to-vcvars64.bat>.'
    }
    $resolved = (Resolve-Path -LiteralPath $ScriptPath).Path
    $command = 'call "' + $resolved + '" >nul && set'
    $environment = & cmd.exe /d /s /c $command
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed with exit code $LASTEXITCODE." }
    foreach ($line in $environment) {
        $separator = $line.IndexOf('=')
        if ($separator -gt 0) {
            [Environment]::SetEnvironmentVariable(
                $line.Substring(0, $separator), $line.Substring($separator + 1), 'Process')
        }
    }
}

function Resolve-ToolDirectory {
    param(
        [string]$ConfiguredDirectory,
        [string]$Executable,
        [string]$Description
    )

    if ($ConfiguredDirectory) {
        $resolved = (Resolve-Path -LiteralPath $ConfiguredDirectory).Path
        if (-not (Test-Path -LiteralPath (Join-Path $resolved $Executable) -PathType Leaf)) {
            throw "$Description was not found in '$resolved'."
        }
        return $resolved
    }

    $command = Get-Command $Executable -ErrorAction SilentlyContinue
    if ($command) {
        return Split-Path -Parent $command.Source
    }

    throw "Cannot find $Description. Pass the directory explicitly or set the documented environment variable."
}

$qtDirectory = Resolve-ToolDirectory $QtBin "qmake.exe" "qmake.exe"
if (-not (Test-Path -LiteralPath (Join-Path $qtDirectory "uic.exe") -PathType Leaf)) {
    throw "uic.exe was not found in Qt bin directory '$qtDirectory'."
}
if (-not (Test-Path -LiteralPath (Join-Path $qtDirectory "lrelease.exe") -PathType Leaf)) {
    throw "lrelease.exe was not found in Qt bin directory '$qtDirectory'."
}
if ($Msvc) {
    Import-VcEnvironment $VcVars64
    $compilerDirectory = Join-Path $env:VCToolsInstallDir 'bin\HostX64\x64'
    $compilerDirectory = Resolve-ToolDirectory $compilerDirectory "nmake.exe" "nmake.exe"
    $makeExecutable = "nmake.exe"
} else {
    $compilerDirectory = Resolve-ToolDirectory $MinGwBin "mingw32-make.exe" "mingw32-make.exe"
    $makeExecutable = "mingw32-make.exe"
}

# qmake, uic, moc and rcc are dynamically linked Windows programs. Merely
# invoking qmake by its full path is insufficient: Qt and MinGW runtime DLLs
# must also be discoverable by every child process spawned by make.
$env:PATH = "$qtDirectory;$compilerDirectory;$env:PATH"

$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$buildPath = (Resolve-Path -LiteralPath $buildPath).Path

$qmake = Join-Path $qtDirectory "qmake.exe"
$lrelease = Join-Path $qtDirectory "lrelease.exe"
$make = Join-Path $compilerDirectory $makeExecutable
$project = Join-Path $repositoryRoot "ei_maper.pro"

if (-not $Msvc) {
    $qmakeSpec = (& $qmake -query QMAKE_SPEC).Trim()
    if ($LASTEXITCODE -ne 0 -or $qmakeSpec -ne 'win32-g++') {
        throw "The selected Qt kit is '$qmakeSpec', not the required MinGW win32-g++ kit. Do not substitute the MSVC Qt kit for production builds. See docs/BUILDING_ENDURANCE.md."
    }
}

$executionRoot = $repositoryRoot
$executionBuildPath = $buildPath
$temporaryDrive = $null
if (($repositoryRoot -match '[^\x00-\x7F]') -or ($buildPath -match '[^\x00-\x7F]')) {
    if (-not $buildPath.StartsWith($repositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "For a Unicode repository path, BuildDirectory must be located inside the repository."
    }

    # Never reuse a pre-existing mapping. Different worktrees share Git
    # identity and often the same project-file hash, so identity-based reuse
    # can silently build the wrong checkout. Claim a free drive for this build
    # and remove only that mapping in the finally block below.
    $usedDrives = @(Get-PSDrive -PSProvider FileSystem | ForEach-Object { $_.Name.ToUpperInvariant() })
    $driveLetter = @('Z','Y','X','W','V','U','T','S','R','Q','P') |
        Where-Object { $usedDrives -notcontains $_ } |
        Select-Object -First 1
    if (-not $driveLetter) {
        throw "Qt 5.12 requires an ASCII build path, and no free temporary drive letter is available."
    }
    $temporaryDrive = "${driveLetter}:"
    & subst.exe $temporaryDrive $repositoryRoot
    if ($LASTEXITCODE -ne 0) { throw "Could not create temporary Qt build drive $temporaryDrive." }
    $mappedDrive = $temporaryDrive
    $relativeBuild = $buildPath.Substring($repositoryRoot.Length).TrimStart('\')
    $executionRoot = "$mappedDrive\"
    $executionBuildPath = Join-Path $executionRoot $relativeBuild
    $project = Join-Path $executionRoot "ei_maper.pro"
}

Write-Host "Qt bin:    $qtDirectory"
Write-Host "Compiler:  $compilerDirectory"
Write-Host "Build:     $buildPath"
if ($temporaryDrive) { Write-Host "ASCII root: $executionRoot" }

Push-Location $executionBuildPath
try {
    $translationSource = Join-Path $executionRoot 'translations\ei_mapper_ru.ts'
    $translationBinary = Join-Path $executionRoot 'translations\ei_mapper_ru.qm'
    & $lrelease $translationSource -qm $translationBinary
    if ($LASTEXITCODE -ne 0) { throw "lrelease failed with exit code $LASTEXITCODE." }

    # Qt 5.12's Windows qmake misdecodes non-ASCII absolute command-line
    # paths. Resolve the project relative to the shadow directory so builds
    # work when the repository lives under a Cyrillic (or other Unicode) path.
    $projectArgument = Resolve-Path -Relative $project
    Write-Host "qmake project: $projectArgument"
    & $qmake $projectArgument "CONFIG+=release"
    if ($LASTEXITCODE -ne 0) { throw "qmake failed with exit code $LASTEXITCODE." }

    if ($Msvc) {
        & $make /NOLOGO release
    } else {
        & $make "-j$Jobs" release
    }
    if ($LASTEXITCODE -ne 0) { throw "Release build failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
    if ($temporaryDrive) {
        & subst.exe $temporaryDrive /D
    }
}

Write-Host "Built: $(Join-Path $buildPath 'release\EIMapperEndurance.exe')"

# Test Current Map is delivered by two independently compiled 32-bit runtime
# components. Building only the Qt executable can otherwise leave an older
# bridge in source/runtime/*/bin, producing a package whose UI and protocol no
# longer agree (the graphless launch flag was one such failure). Rebuild both
# components for every release build and copy only distributable binaries into
# the release tree.
$runtimeComponentBuild = Join-Path $buildPath 'runtime-components'
$runtimeRelease = Join-Path $buildPath 'release\runtime'
New-Item -ItemType Directory -Force -Path $runtimeComponentBuild | Out-Null
New-Item -ItemType Directory -Force -Path $runtimeRelease | Out-Null

$bridgeScript = Join-Path $repositoryRoot 'source\runtime\jmv_bridge\build-x86.cmd'
$hostScript = Join-Path $repositoryRoot 'source\runtime\base_runtime_host\build-x86.cmd'
$previousBridgeOut = $env:EI_BRIDGE_OUT_DIR
$previousHostOut = $env:EI_RUNTIME_HOST_OUT_DIR
try {
    $env:EI_BRIDGE_OUT_DIR = $runtimeComponentBuild
    & cmd.exe /d /c ('"{0}"' -f $bridgeScript)
    if ($LASTEXITCODE -ne 0) {
        throw "32-bit runtime bridge build failed with exit code $LASTEXITCODE."
    }
    $env:EI_RUNTIME_HOST_OUT_DIR = $runtimeComponentBuild
    & cmd.exe /d /c ('"{0}"' -f $hostScript)
    if ($LASTEXITCODE -ne 0) {
        throw "32-bit runtime host build failed with exit code $LASTEXITCODE."
    }
} finally {
    $env:EI_BRIDGE_OUT_DIR = $previousBridgeOut
    $env:EI_RUNTIME_HOST_OUT_DIR = $previousHostOut
}

foreach ($name in @('EIMapperEnduranceJmvBridge.plg',
                     'EIMapperEnduranceRuntimeHost.exe')) {
    $source = Join-Path $runtimeComponentBuild $name
    if (-not (Test-Path -LiteralPath $source -PathType Leaf)) {
        throw "Runtime component was not produced: $source"
    }
    Copy-Item -LiteralPath $source -Destination (Join-Path $runtimeRelease $name) -Force
}
Write-Host "Built runtime components: $runtimeRelease"
