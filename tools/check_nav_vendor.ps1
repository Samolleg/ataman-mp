param([string]$Root=(Join-Path (Split-Path -Parent $PSScriptRoot) 'third_party/eirebuild64-nav'))
$ErrorActionPreference='Stop'
$lock=Get-Content -LiteralPath (Join-Path $Root 'source.lock.json') -Encoding UTF8 -Raw|ConvertFrom-Json
$actual=@(Get-ChildItem -LiteralPath $Root -Recurse -File|Where-Object Name -ne 'source.lock.json')
if($actual.Count -ne $lock.files.Count){throw 'Nav vendor file count differs from lock'}
foreach($entry in $lock.files){
 $path=[IO.Path]::GetFullPath((Join-Path $Root $entry.path))
 if(-not $path.StartsWith([IO.Path]::GetFullPath($Root)+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe vendor path'}
 if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $entry.sha256){throw "Nav vendor mismatch: $($entry.path)"}
}
Write-Output "Nav vendor lock PASS: $($actual.Count) files, $($lock.sourceCommit)"
