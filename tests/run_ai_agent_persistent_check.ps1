param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$ReleaseRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $RunRoot){throw 'RunRoot must be new'}
$run=[IO.Path]::GetFullPath($RunRoot)
foreach($folder in @('app','AppData','LocalAppData','User','Temp','Codex','Claude')){[IO.Directory]::CreateDirectory((Join-Path $run $folder))|Out-Null}
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/ai_agent_persistent_check.exe')
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperMcp.exe') -Destination (Join-Path $run 'app/EIMapperMcp.exe')
foreach($name in @('data','assets')){Copy-Item -LiteralPath (Join-Path $repo $name) -Destination (Join-Path $run ('app/'+$name)) -Recurse}
$options=@{General=@{gameRoot=$run.Replace('\','/');openLastSessionOnStartup=$false;autosaveEnabled=$false;recentFiles=@()};Resource=@{resourceProfile='base-game';mapRoots=@();resourceRoots=@();terrainAuthoringArchive=''}}
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User'
$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP;$env:CODEX_HOME=Join-Path $run 'Codex';$env:CLAUDE_CONFIG_DIR=Join-Path $run 'Claude'
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_BRIDGE=Join-Path $run 'app/EIMapperMcp.exe';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/ai_agent_persistent_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(240000)){Stop-Process -Id $p.Id;throw 'Owned test process timeout'}
$p.WaitForExit();$code=$p.ExitCode
if($null -eq $code){throw 'Native exit code unavailable'}
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$p.Id;exe=(Get-FileHash (Join-Path $run 'app/ai_agent_persistent_check.exe')).Hash;bridge=(Get-FileHash (Join-Path $run 'app/EIMapperMcp.exe')).Hash}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
Get-Content (Join-Path $run 'stdout.txt') -Tail 14
exit $code
