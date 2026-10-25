param([Parameter(Mandatory=$true)][string]$BuildRoot,
      [Parameter(Mandatory=$true)][string]$OutputRoot,
      [Parameter(Mandatory=$true)][string]$BaselineRef,
      [Parameter(Mandatory=$true)][string]$QtBin,
      [Parameter(Mandatory=$true)][string]$MinGwBin,
      [switch]$BaselineKeys)
# BaselineRef is the accepted225 source in this clone's history. Only the old
# panel and key handler are used inside a freshly compiled native test host;
# no old mapper executable is launched.
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $OutputRoot){throw 'OutputRoot must be a new owned directory'}
$output=[IO.Path]::GetFullPath($OutputRoot);$fixture=Join-Path $output 'baseline'
[IO.Directory]::CreateDirectory($fixture)|Out-Null
git -C $repo archive --format=tar -o (Join-Path $output 'baseline.tar') $BaselineRef -- source/ui/travel_panel.cpp source/ui/travel_panel.h source/app/view_keybinding.cpp
if($LASTEXITCODE){throw 'Cannot extract accepted baseline source'}
tar -xf (Join-Path $output 'baseline.tar') -C $fixture
if($LASTEXITCODE){throw 'Cannot unpack baseline'}
foreach($extension in @('h','cpp')) {
 $source=[IO.File]::ReadAllText((Join-Path $fixture ('source/ui/travel_panel.'+$extension)))
 $source=$source.Replace('CTravelPanel','CTravelPanel225').Replace('"travel_panel.h"','"travel_panel225.h"')
 [IO.File]::WriteAllText((Join-Path $fixture ('travel_panel225.'+$extension)),$source,[Text.UTF8Encoding]::new($false))
}
Copy-Item (Join-Path $fixture 'source/app/view_keybinding.cpp') (Join-Path $fixture 'view_keybinding.cpp')
$env:PATH="$QtBin;$MinGwBin;$env:PATH"
$qmakeArgs=@((Join-Path $repo 'tests/travel_ux_check.pro'),('BUILD_ROOT='+[IO.Path]::GetFullPath($BuildRoot)),('BASELINE_ROOT='+$fixture))
if($BaselineKeys){$qmakeArgs+='CONFIG+=baseline_keys'}
Push-Location $output
try {
 $ErrorActionPreference='Continue'
 & (Join-Path $QtBin 'qmake.exe') @qmakeArgs *> (Join-Path $output 'qmake.txt')
 if($LASTEXITCODE){throw 'qmake failed; see qmake.txt'}
 & (Join-Path $MinGwBin 'mingw32-make.exe') -f Makefile.Release -j2 *> (Join-Path $output 'build.txt')
 if($LASTEXITCODE){throw 'Native build failed; see build.txt'}
} finally {Pop-Location}
