param([Parameter(Mandatory=$true)][string]$RunRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
. (Join-Path $repo 'packaging/program_files.ps1')
if(Test-Path -LiteralPath $RunRoot){throw 'Fresh owned test root required'}
$run=[IO.Path]::GetFullPath($RunRoot)
foreach($name in @('target','package')){[IO.Directory]::CreateDirectory((Join-Path $run $name))|Out-Null}
$target=Join-Path $run 'target';$package=Join-Path $run 'package'
foreach($root in @($target,$package)) {
    $version=if($root -eq $target){'1'}else{'2'}
    foreach($name in @('EIMapperEndurance.exe','EIMapperMcp.exe','VERSION')){[IO.File]::WriteAllText((Join-Path $root $name),"synthetic non-executable $version $name")}
    $lines=@(@('EIMapperEndurance.exe','EIMapperMcp.exe','VERSION')|ForEach-Object {(Get-ProgramHash (Join-Path $root $_)).ToLowerInvariant()+' *'+$_})
    [IO.File]::WriteAllLines((Join-Path $root 'SHA256SUMS.txt'),$lines,[Text.UTF8Encoding]::new($false))
}
$old=Read-ProgramManifest $target
$sentinel=Join-Path $target 'notes.old-owner';[IO.File]::WriteAllText($sentinel,'preserve unrelated old files')
$sentinelHash=Get-ProgramHash $sentinel
# Unlike an image mapping, this deliberately denies rename/delete sharing.
$handle=[IO.File]::Open((Join-Path $target 'VERSION'),'Open','Read','Read')
try {
    $script=Join-Path $repo 'packaging/install_program.ps1'
    $start=[Diagnostics.ProcessStartInfo]::new('powershell.exe')
    $start.Arguments='-NoProfile -ExecutionPolicy Bypass -File "'+$script+'" -PackageRoot "'+$package+'" -TargetRoot "'+$target+'" -RollbackRoot "'+(Join-Path $run 'rollback')+'"'
    $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
    $p=[Diagnostics.Process]::Start($start)
    $stdout=$p.StandardOutput.ReadToEnd();$stderr=$p.StandardError.ReadToEnd();$p.WaitForExit()
    [IO.File]::WriteAllText((Join-Path $run 'stdout.txt'),$stdout)
    [IO.File]::WriteAllText((Join-Path $run 'stderr.txt'),$stderr)
    if($p.ExitCode -eq 0 -or $stderr -notmatch 'automatic rollback verified'){throw 'Expected explicit failure with successful automatic rollback'}
    Test-ProgramFiles $target $old
    if((Get-ProgramHash $sentinel) -ne $sentinelHash){throw 'Unrelated old file changed'}
    if([IO.File]::Exists((Join-Path $target '.ataman-install.lock'))){throw 'Installer lock leaked'}
    Write-Output 'PASS rename-denied failure: automatic rollback hash-verified, unchanged locked file preserved, unrelated .old-owner retained, install lock released'
}finally{$handle.Dispose()}
