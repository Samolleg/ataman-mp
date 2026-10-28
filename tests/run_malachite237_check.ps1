param(
    [Parameter(Mandatory=$true)][string]$NativeExe,
    [Parameter(Mandatory=$true)][string]$HostExe,
    [Parameter(Mandatory=$true)][string]$OutputRoot,
    [switch]$ClassicRegression
)
$ErrorActionPreference='Stop'
if(Test-Path -LiteralPath $OutputRoot){throw 'Use a fresh private output directory.'}
$repo=Split-Path -Parent $PSScriptRoot
foreach($name in @('app','AppData','LocalAppData','User','Temp','settings','automation','backups')){[IO.Directory]::CreateDirectory((Join-Path $OutputRoot $name))|Out-Null}
$run=(Resolve-Path -LiteralPath $OutputRoot).Path
foreach($name in @('data','resource-profiles')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run 'app') -Recurse}
Copy-Item -LiteralPath $NativeExe -Destination (Join-Path $run 'app/check.exe')
foreach($pair in @(@('APPDATA','AppData'),@('LOCALAPPDATA','LocalAppData'),@('USERPROFILE','User'),@('TEMP','Temp'),@('TMP','Temp'))){[Environment]::SetEnvironmentVariable($pair[0],(Join-Path $run $pair[1]),'Process')}
$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings'
$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups'
$env:EI_MALACHITE_HOST=(Resolve-Path -LiteralPath $HostExe).Path
if($ClassicRegression){$env:EI_MALACHITE_HOST=Join-Path $run 'intentionally-unavailable.exe'}
$env:EI_MALACHITE_CHECK_ROOT=$run
$env:QT_QPA_PLATFORM='windows'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$checkCommand=if($ClassicRegression){'--scenario-layer-check'}else{'--assembly-catalog-check'}
$process=Start-Process -FilePath (Join-Path $run 'app/check.exe') -ArgumentList $checkCommand -WindowStyle Hidden -PassThru -WorkingDirectory (Join-Path $run 'app') -RedirectStandardOutput (Join-Path $run 'check.stdout') -RedirectStandardError (Join-Path $run 'check.stderr')
$null=$process.Handle
if(-not $process.WaitForExit(60000)){Stop-Process -Id $process.Id;throw 'Owned integration check exceeded 60 seconds.'}
$process.WaitForExit()
@{exitCode=$process.ExitCode;pid=$process.Id;mode='hidden native Qt plus real child WPF; synthetic private MOB'}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $run 'process.json')
Get-Content -LiteralPath (Join-Path $run 'check.stdout') -Tail 18
if($process.ExitCode -ne 0){Get-Content -LiteralPath (Join-Path $run 'check.stderr') -Tail 10}
exit $process.ExitCode
