param(
    [Parameter(Mandatory=$true)][string]$ReleaseRoot,
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$PreviousPackageRoot,
    [string]$QtBin=$env:EI_MAPPER_QT_BIN,
    [string]$MinGwBin=$env:EI_MAPPER_MINGW_BIN
)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $PackageRoot){throw 'Staging must be a new owned folder.'}
$previous=(Resolve-Path -LiteralPath $PreviousPackageRoot).Path
$release=(Resolve-Path -LiteralPath $ReleaseRoot).Path
$manifest=Join-Path $previous 'SHA256SUMS.txt'
if(-not(Test-Path -LiteralPath $manifest)){throw 'Accepted manifest required.'}
New-Item -ItemType Directory -Path $PackageRoot|Out-Null
$stage=(Resolve-Path -LiteralPath $PackageRoot).Path
$env:PATH="$QtBin;$MinGwBin;$env:PATH"
# Retain every accepted managed path. Prefer current tracked sources and the
# exact retained Qt kit; binaries below always come from the new Release.
foreach($line in Get-Content -LiteralPath $manifest) {
    if($line -notmatch '^([0-9a-fA-F]{64}) \*(.+)$'){throw 'Malformed accepted manifest.'}
    $hash=$Matches[1];$relative=$Matches[2].Replace('\','/')
    $source=Join-Path $previous $relative
    if((Get-FileHash -LiteralPath $source).Hash -ne $hash){throw "Accepted managed file changed: $relative"}
    $destination=[IO.Path]::GetFullPath((Join-Path $stage $relative))
    if(-not $destination.StartsWith($stage+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe manifest path.'}
    $current=Join-Path $repo $relative
    if($relative -like 'translations/qt_*.qm'){$current=Join-Path (Split-Path -Parent $QtBin) $relative}
    if(Test-Path -LiteralPath $current -PathType Leaf){$source=$current}
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force|Out-Null
    Copy-Item -LiteralPath $source -Destination $destination
}
Copy-Item -LiteralPath (Join-Path $release 'EIMapperEndurance.exe'),(Join-Path $release 'EIMapperMcp.exe') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $release 'runtime') -Destination $stage -Recurse -Force
& (Join-Path $QtBin 'windeployqt.exe') --release --compiler-runtime (Join-Path $stage 'EIMapperEndurance.exe')
if($LASTEXITCODE -ne 0){throw 'Qt deployment failed.'}
foreach($name in @('Qt5Concurrent.dll','Qt5Network.dll')){Copy-Item -LiteralPath (Join-Path $QtBin $name) -Destination $stage -Force}
foreach($name in @('libgcc_s_seh-1.dll','libstdc++-6.dll','libwinpthread-1.dll')){Copy-Item -LiteralPath (Join-Path $MinGwBin $name) -Destination $stage -Force}
$notes=[IO.File]::ReadAllText((Join-Path $repo 'CHANGELOG.md'))
$currentVersion=[IO.File]::ReadAllText((Join-Path $repo 'VERSION')).Trim()
$heading='## Ataman '+$currentVersion+' '
$start=$notes.IndexOf($heading)
if($start -lt 0){throw 'Current changelog entry missing.'}
$next=$notes.IndexOf('## Ataman ', $start+$heading.Length)
if($next -lt 0){throw 'Current changelog boundary missing.'}
[IO.File]::WriteAllText((Join-Path $stage 'CHANGELOG.md'),$notes.Substring($start,$next-$start),[Text.UTF8Encoding]::new($false))
Copy-Item -LiteralPath (Join-Path $repo 'docs/AI_AGENT_MCP.md') -Destination (Join-Path $stage 'docs') -Force
Copy-Item -LiteralPath (Join-Path $repo 'docs/NAVMESH.md') -Destination (Join-Path $stage 'docs') -Force
New-Item -ItemType Directory -Path (Join-Path $stage 'licenses') -Force|Out-Null
Copy-Item -LiteralPath (Join-Path $repo 'third_party/eirebuild64-nav/external/recastnavigation/License.txt') -Destination (Join-Path $stage 'licenses/Detour-zlib.txt')
New-Item -ItemType Directory -Path (Join-Path $stage 'tools') -Force|Out-Null
foreach($name in @('install_program.ps1','program_files.ps1','rollback.ps1')) {
    Copy-Item -LiteralPath (Join-Path $repo ('packaging/'+$name)) -Destination (Join-Path $stage 'tools') -Force
}
Write-Output "Prepared staging; validate versions/hashes and finalize with -PreviousPackageRoot $previous"
