param([Parameter(Mandatory=$true)][string]$TestExe,
 [Parameter(Mandatory=$true)][string]$RunRoot,
 [Parameter(Mandatory=$true)][string]$SourceRoot,
 [string]$SettingsSnapshot,[string]$Mpr,[string]$OptionsTemplate)
$ErrorActionPreference='Stop'
$run=[IO.Path]::GetFullPath($RunRoot)
if(Test-Path -LiteralPath $run){throw 'Run root must be new'}
foreach($name in @('app','settings/EI Mapper Endurance','AppData','LocalAppData','User','Temp','backups','automation','machine-locks')){New-Item -ItemType Directory -Path (Join-Path $run $name)|Out-Null}
Copy-Item -LiteralPath $TestExe -Destination "$run/app/dock_layout_restart_check.exe"
foreach($name in @('data','assets')){Copy-Item -LiteralPath (Join-Path $SourceRoot $name) -Destination "$run/app/$name" -Recurse}
if($SettingsSnapshot){Copy-Item -LiteralPath $SettingsSnapshot -Destination "$run/settings/EI Mapper Endurance/EI Mapper Endurance Edition.ini"}
if($OptionsTemplate){Copy-Item -LiteralPath $OptionsTemplate -Destination "$run/app/endurance-options.json"}
$env:APPDATA="$run/AppData";$env:LOCALAPPDATA="$run/LocalAppData";$env:USERPROFILE="$run/User";$env:TEMP="$run/Temp";$env:TMP=$env:TEMP
$env:EI_MAPPER_TEST_LOCK_ROOT="$run/machine-locks";$env:EI_MAPPER_AUTOMATION_DIR="$run/automation";$env:EI_MAPPER_BACKUP_ROOT="$run/backups"
$env:EI_MAPPER_SETTINGS_DIR="$run/settings";$env:EI_DOCK_LAYOUT_ROOT=$run
$env:EI_DOCK_LAYOUT_MPR=$Mpr
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$failed=@()
$processes=@()
foreach($phase in @('save','restore','restore')) {
 $env:EI_DOCK_LAYOUT_PHASE=$phase
 $tag=$phase;if(Test-Path "$run/$tag.stdout"){$tag='restore-second'}
 $p=Start-Process -FilePath "$run/app/dock_layout_restart_check.exe" -ArgumentList '--assembly-catalog-check' -WorkingDirectory "$run/app" -WindowStyle Hidden -PassThru -RedirectStandardOutput "$run/$tag.stdout" -RedirectStandardError "$run/$tag.stderr"
 $null=$p.Handle
 if(-not $p.WaitForExit(180000)){Stop-Process -Id $p.Id;throw 'Owned test timeout'}
 $p.WaitForExit()
 $processes+=@{phase=$tag;pid=$p.Id;exitCode=$p.ExitCode}
 if($null -eq $p.ExitCode -or $p.ExitCode -ne 0){$failed+=$tag}
}
@{failed=$failed;processes=$processes;testExeSha256=(Get-FileHash -LiteralPath $TestExe).Hash}|ConvertTo-Json -Depth 4|Set-Content "$run/result.json" -Encoding UTF8
if($failed.Count){throw "Test failed: $failed"}
