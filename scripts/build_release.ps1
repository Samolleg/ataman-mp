[CmdletBinding()]
param(
    [string]$BuildDirectory = "build-release",
    [string]$QtBin = $env:EI_MAPPER_QT_BIN,
    [string]$MinGwBin = $env:EI_MAPPER_MINGW_BIN,
    [ValidateRange(1, 64)]
    [int]$Jobs = 4
)

$ErrorActionPreference = "Stop"
$repositoryRoot = Split-Path -Parent $PSScriptRoot

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
$mingwDirectory = Resolve-ToolDirectory $MinGwBin "mingw32-make.exe" "mingw32-make.exe"

# qmake, uic, moc and rcc are dynamically linked Windows programs. Merely
# invoking qmake by its full path is insufficient: Qt and MinGW runtime DLLs
# must also be discoverable by every child process spawned by make.
$env:PATH = "$qtDirectory;$mingwDirectory;$env:PATH"

$buildPath = if ([IO.Path]::IsPathRooted($BuildDirectory)) {
    $BuildDirectory
} else {
    Join-Path $repositoryRoot $BuildDirectory
}
New-Item -ItemType Directory -Force -Path $buildPath | Out-Null
$buildPath = (Resolve-Path -LiteralPath $buildPath).Path

$qmake = Join-Path $qtDirectory "qmake.exe"
$make = Join-Path $mingwDirectory "mingw32-make.exe"
$project = Join-Path $repositoryRoot "ei_maper.pro"

$executionRoot = $repositoryRoot
$executionBuildPath = $buildPath
$temporaryDrive = $null
if (($repositoryRoot -match '[^\x00-\x7F]') -or ($buildPath -match '[^\x00-\x7F]')) {
    $existingDrive = $null
    foreach ($line in (& subst.exe)) {
        if ($line -match '^([A-Z]):\\: => (.+)$' -and
            $Matches[2].TrimEnd('\').Equals($repositoryRoot.TrimEnd('\'), [StringComparison]::OrdinalIgnoreCase)) {
            $existingDrive = "$($Matches[1]):"
            break
        }
    }
    # Windows PowerShell may decode `subst` output through an OEM code page,
    # making a Unicode target impossible to compare. Recognize an existing
    # repository mapping by its project and Git identity as a fallback.
    if (-not $existingDrive) {
        $projectHash = (Get-FileHash -LiteralPath (Join-Path $repositoryRoot 'ei_maper.pro') -Algorithm SHA256).Hash
        $gitHead = (Get-Content -Raw -LiteralPath (Join-Path $repositoryRoot '.git\HEAD')).Trim()
        foreach ($drive in (Get-PSDrive -PSProvider FileSystem)) {
            $candidateProject = Join-Path $drive.Root 'ei_maper.pro'
            $candidateHead = Join-Path $drive.Root '.git\HEAD'
            if ((Test-Path -LiteralPath $candidateProject -PathType Leaf) -and
                (Test-Path -LiteralPath $candidateHead -PathType Leaf) -and
                ((Get-FileHash -LiteralPath $candidateProject -Algorithm SHA256).Hash -eq $projectHash) -and
                ((Get-Content -Raw -LiteralPath $candidateHead).Trim() -eq $gitHead)) {
                $existingDrive = "$($drive.Name):"
                break
            }
        }
    }
    if (-not $buildPath.StartsWith($repositoryRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "For a Unicode repository path, BuildDirectory must be located inside the repository."
    }

    if ($existingDrive) {
        $mappedDrive = $existingDrive
    } else {
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
    }
    $relativeBuild = $buildPath.Substring($repositoryRoot.Length).TrimStart('\')
    $executionRoot = "$mappedDrive\"
    $executionBuildPath = Join-Path $executionRoot $relativeBuild
    $project = Join-Path $executionRoot "ei_maper.pro"
}

Write-Host "Qt bin:    $qtDirectory"
Write-Host "MinGW bin: $mingwDirectory"
Write-Host "Build:     $buildPath"

Push-Location $executionBuildPath
try {
    # Qt 5.12's Windows qmake misdecodes non-ASCII absolute command-line
    # paths. Resolve the project relative to the shadow directory so builds
    # work when the repository lives under a Cyrillic (or other Unicode) path.
    $projectArgument = Resolve-Path -Relative $project
    & $qmake $projectArgument "CONFIG+=release"
    if ($LASTEXITCODE -ne 0) { throw "qmake failed with exit code $LASTEXITCODE." }

    & $make "-j$Jobs" release
    if ($LASTEXITCODE -ne 0) { throw "mingw32-make failed with exit code $LASTEXITCODE." }
} finally {
    Pop-Location
    if ($temporaryDrive) {
        & subst.exe $temporaryDrive /D
    }
}

Write-Host "Built: $(Join-Path $buildPath 'release\EIMapperEndurance.exe')"
