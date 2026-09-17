param([Parameter(Mandatory=$true)][string]$SourcePackage,
      [Parameter(Mandatory=$true)][string]$TestRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$source=(Resolve-Path -LiteralPath $SourcePackage).Path
$target=[IO.Path]::GetFullPath($TestRoot)
if(!$target.StartsWith($repo+'\build-',[StringComparison]::OrdinalIgnoreCase) -or (Test-Path -LiteralPath $target)){throw 'Tests require a new owned build directory'}
New-Item -ItemType Directory -Path $target | Out-Null
Copy-Item -Path (Join-Path $source '*') -Destination $target -Recurse
$validator=Join-Path $repo 'packaging/validate_distribution.ps1'
function Check([string]$Name,[bool]$ExpectedPass) {
 $log=Join-Path (Split-Path $target -Parent) ($Name+'.txt')
 & "$env:SystemRoot\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File $validator $target *> $log
 $passed=$LASTEXITCODE -eq 0
 if($passed -ne $ExpectedPass){throw "Unexpected validation result: $Name"}
 "PASS: $Name"
}
$manifest=Join-Path $target 'SHA256SUMS.txt'
$manifestBytes=[IO.File]::ReadAllBytes($manifest)
$notes=Join-Path $target 'CHANGELOG.md';$notesBytes=[IO.File]::ReadAllBytes($notes)
Check 'valid-package' $true
Remove-Item -LiteralPath $notes
Check 'reject-missing-changelog' $false
[IO.File]::WriteAllBytes($notes,$notesBytes)
Set-Content -LiteralPath (Join-Path $target 'VERSION') '999'
Check 'reject-version-mismatch' $false
Copy-Item -LiteralPath (Join-Path $source 'VERSION') -Destination $target -Force
Add-Content -LiteralPath $notes 'unexpected change'
Check 'reject-wrong-digest' $false
[IO.File]::WriteAllBytes($notes,$notesBytes)
Set-Content -LiteralPath (Join-Path $target 'unlisted.txt') 'fixture'
Check 'reject-unlisted-file' $false
Remove-Item -LiteralPath (Join-Path $target 'unlisted.txt')
$first=(Get-Content -LiteralPath $manifest)[0]
Add-Content -LiteralPath $manifest $first
Check 'reject-duplicate-path' $false
[IO.File]::WriteAllBytes($manifest,$manifestBytes)
Add-Content -LiteralPath $manifest (('0'*64)+' *../outside.txt')
Check 'reject-escaping-path' $false
[IO.File]::WriteAllBytes($manifest,$manifestBytes)
Set-Content -LiteralPath $manifest 'broken'
Check 'reject-malformed-manifest' $false
[IO.File]::WriteAllBytes($manifest,$manifestBytes)
Check 'restored-package' $true
