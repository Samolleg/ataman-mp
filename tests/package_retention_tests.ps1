param([Parameter(Mandatory=$true)][string]$RunRoot,[Parameter(Mandatory=$true)][string]$AcceptedRoot)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $RunRoot){throw 'Use a new owned test root.'}
New-Item -ItemType Directory -Path $RunRoot|Out-Null
$root=(Resolve-Path -LiteralPath $RunRoot).Path
$accepted=(Resolve-Path -LiteralPath $AcceptedRoot).Path
$finalizer=Join-Path (Split-Path -Parent $PSScriptRoot) 'packaging/finalize_package.ps1'
foreach($line in Get-Content (Join-Path $accepted 'SHA256SUMS.txt')){
    if($line -notmatch '^[0-9a-fA-F]{64} \*(.+)$'){throw 'Bad baseline.'}
    $relative=$Matches[1];$destination=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $destination.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Unsafe fixture path.'}
    New-Item -ItemType Directory -Path (Split-Path -Parent $destination) -Force|Out-Null
    Copy-Item -LiteralPath (Join-Path $accepted $relative) -Destination $destination
}
foreach($relative in @('translations/qt_ru.qm','tools/Restore-Gipat2Atlas.ps1')){
    $target=[IO.Path]::GetFullPath((Join-Path $root $relative))
    if(-not $target.StartsWith($root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Invalid deletion target.'}
    [IO.File]::Delete($target)
    $ErrorActionPreference='Continue'
    $output=& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $finalizer $root -PreviousPackageRoot $accepted 2>&1
    $exitCode=$LASTEXITCODE;$ErrorActionPreference='Stop'
    if($exitCode -eq 0 -or ($output|Out-String) -notmatch [regex]::Escape("Managed program file would be lost: $relative")){throw "Missing-file regression did not fail: $relative"}
    $output|Out-String|Set-Content (Join-Path $root (($relative -replace '[^A-Za-z0-9]','_')+'.failure.txt'))
    Copy-Item -LiteralPath (Join-Path $accepted $relative) -Destination $target
    Write-Output "PASS: finalizer rejects missing $relative before cleanup"
}
Write-Output 'RESULT 2 PASS, 0 FAIL; fixture EXEs never launched'
