$ErrorActionPreference = 'Stop'
$repositoryRoot = Split-Path -Parent $PSScriptRoot
. (Join-Path $repositoryRoot 'scripts\build_repository_identity.ps1')

$tempRoot = Join-Path ([IO.Path]::GetTempPath()) (
    'EIME-build-worktree-' + [guid]::NewGuid().ToString('N') + '-Ж')
$primary = Join-Path $tempRoot 'primary'
$linked = Join-Path $tempRoot 'linked-Ж'
$mappedDrive = $null

try {
    New-Item -ItemType Directory -Path $primary -Force | Out-Null
    & git -C $primary init --quiet
    if ($LASTEXITCODE -ne 0) { throw 'git init failed' }
    & git -C $primary config user.name 'Endurance Build Test'
    & git -C $primary config user.email 'build-test@example.invalid'
    Set-Content -LiteralPath (Join-Path $primary 'ei_maper.pro') -Value 'TEMPLATE = app' -Encoding ascii
    & git -C $primary add -- ei_maper.pro
    & git -C $primary commit --quiet -m 'fixture'
    if ($LASTEXITCODE -ne 0) { throw 'fixture commit failed' }
    & git -C $primary worktree add --quiet --detach $linked HEAD
    if ($LASTEXITCODE -ne 0) { throw 'linked worktree creation failed' }

    $primaryIdentity = Get-EnduranceGitRepositoryIdentity -Root $primary
    $linkedIdentity = Get-EnduranceGitRepositoryIdentity -Root $linked
    if (-not $primaryIdentity -or -not $linkedIdentity) {
        throw 'normal repository or linked worktree identity was not resolved'
    }
    if ($primaryIdentity -eq $linkedIdentity) {
        throw 'linked worktree identity was not distinguished from the primary repository'
    }

    $usedDrives = @(Get-PSDrive -PSProvider FileSystem |
        ForEach-Object { $_.Name.ToUpperInvariant() })
    $driveLetter = @('Z','Y','X','W','V','U','T','S','R','Q','P') |
        Where-Object { $usedDrives -notcontains $_ } |
        Select-Object -First 1
    if (-not $driveLetter) { throw 'no free drive letter for linked-worktree identity test' }
    $mappedDrive = "${driveLetter}:"
    & subst.exe $mappedDrive $linked
    if ($LASTEXITCODE -ne 0) { throw 'subst failed' }
    $mappedIdentity = Get-EnduranceGitRepositoryIdentity -Root "$mappedDrive\"
    if ($mappedIdentity -ne $linkedIdentity) {
        throw 'ASCII mapping did not preserve linked-worktree identity'
    }

    Write-Output 'PASS: normal repository, Unicode linked worktree, and ASCII mapping identities resolved'
}
finally {
    if ($mappedDrive) {
        & subst.exe $mappedDrive /D | Out-Null
    }
    if (Test-Path -LiteralPath $linked) {
        & git -C $primary worktree remove --force $linked 2>$null
    }
    $resolvedTemp = Resolve-Path -LiteralPath $tempRoot -ErrorAction SilentlyContinue
    if ($resolvedTemp) {
        $systemTemp = [IO.Path]::GetFullPath([IO.Path]::GetTempPath())
        if (-not $resolvedTemp.Path.StartsWith($systemTemp,
                [StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove test path outside TEMP: $($resolvedTemp.Path)"
        }
        Remove-Item -LiteralPath $resolvedTemp.Path -Recurse -Force
    }
}
