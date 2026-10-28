param([Parameter(Mandatory=$true)][string]$RunRoot,
      [Parameter(Mandatory=$true)][string]$TestExe,
      [Parameter(Mandatory=$true)][string]$ReleaseRoot)
$ErrorActionPreference='Stop'
$repo=Split-Path -Parent $PSScriptRoot
if(Test-Path -LiteralPath $RunRoot){throw 'RunRoot must be new'}
$run=[IO.Path]::GetFullPath($RunRoot)
foreach($folder in @('app','exports','AppData','LocalAppData','User','Temp','settings','backups','game/Starter/MODS/DejaVu/Maps','game/Starter/MODS/DejaVu/res','game/Starter/Engine','game/Res')) {
    [IO.Directory]::CreateDirectory((Join-Path $run $folder))|Out-Null
}
$installation=Join-Path $repo 'artifacts/EI-Mapper-Ataman-2-first-test'
$options=Get-Content -LiteralPath (Join-Path $installation 'endurance-options.json') -Raw|ConvertFrom-Json
$fixture=Join-Path $repo 'build-topdown-238/private-profile'
$maps=Join-Path $fixture 'Starter/MODS/DejaVu/Maps'
foreach($folder in @($maps,(Join-Path $fixture 'Starter/MODS/DejaVu/res'),(Join-Path $fixture 'Res'),(Join-Path $fixture 'Starter/Engine'))){[IO.Directory]::CreateDirectory($folder)|Out-Null}
$sources=@()
foreach($name in @('zone1','zone2')) {
    foreach($extension in @('.mpr','.mob','-atlas.res')) {
        $source=Join-Path $options.Resource.mapRoots[0] ($name+$extension)
        if(Test-Path -LiteralPath $source){$sources+=Get-Item -LiteralPath $source;Copy-Item -LiteralPath $source -Destination (Join-Path $maps ($name+$extension))}
    }
}
foreach($root in $options.Resource.resourceRoots){$sources+=Get-ChildItem -LiteralPath $root -File|Where-Object {$_.Extension -eq '.res' -or $_.Name -eq 'model-catalog.json'}}
$sources+=Get-Item -LiteralPath (Join-Path $installation 'endurance-options.json')
$guards=@($sources|Sort-Object FullName -Unique|ForEach-Object {@{path=$_.FullName;length=$_.Length;mtime=$_.LastWriteTimeUtc.Ticks;sha256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
$guards|ConvertTo-Json -Depth 5|Set-Content -LiteralPath (Join-Path $run 'guard-before.json') -Encoding UTF8
New-Item -ItemType Junction -Path (Join-Path $run 'linked-export') -Target $maps|Out-Null
for($i=0;$i -lt $options.Resource.resourceRoots.Count;$i++) {
    $sourceRoot=$options.Resource.resourceRoots[$i]
    $targetRoot=if($i -eq 0){Join-Path $fixture 'Starter/MODS/DejaVu/res'}else{Join-Path $fixture 'Res'}
    foreach($file in Get-ChildItem -LiteralPath $sourceRoot -File|Where-Object {$_.Extension -eq '.res' -or $_.Name -eq 'model-catalog.json'}) {
        $destination=Join-Path $targetRoot $file.Name
        # Reuse only a verified private byte-identical copy between runs.
        if(-not [IO.File]::Exists($destination) -or (Get-FileHash -LiteralPath $destination).Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash){Copy-Item -LiteralPath $file.FullName -Destination $destination}
    }
    $config=Join-Path (Split-Path -Parent $sourceRoot) 'config.ini'
    if(Test-Path -LiteralPath $config){Copy-Item -LiteralPath $config -Destination (Join-Path (Split-Path -Parent $targetRoot) 'config.ini')}
}
foreach($line in Get-Content -LiteralPath (Join-Path $installation 'SHA256SUMS.txt')) {
    if($line -match '^[0-9a-fA-F]{64} \*(.+)$') {
        $target=Join-Path $run ('app/'+$Matches[1]);[IO.Directory]::CreateDirectory((Split-Path -Parent $target))|Out-Null
        Copy-Item -LiteralPath (Join-Path $installation $Matches[1]) -Destination $target
    }
}
Copy-Item -LiteralPath $TestExe -Destination (Join-Path $run 'app/topdown_export_check.exe')
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'EIMapperEndurance.exe'),(Join-Path $ReleaseRoot 'EIMapperMcp.exe') -Destination (Join-Path $run 'app') -Force
Copy-Item -LiteralPath (Join-Path $ReleaseRoot 'runtime') -Destination (Join-Path $run 'app') -Recurse -Force
[IO.File]::WriteAllText((Join-Path $run 'game/Starter/MODS/DejaVu/main.lua'),'-- isolated profile marker, never executed')
[IO.File]::WriteAllText((Join-Path $run 'game/Starter/Engine/game.exe'),'non-executable profile presence marker')
[IO.File]::WriteAllText((Join-Path $run 'game/game.exe'),'non-executable profile presence marker')
[IO.File]::WriteAllText((Join-Path $fixture 'Starter/MODS/DejaVu/main.lua'),'-- isolated profile marker, never executed')
[IO.File]::WriteAllText((Join-Path $fixture 'Starter/Engine/game.exe'),'non-executable profile presence marker')
[IO.File]::WriteAllText((Join-Path $fixture 'game.exe'),'non-executable profile presence marker')
$empty=[byte[]](0x3c,0xe2,0x9c,0x01,0,0,0,0,16,0,0,0,0,0,0,0)
[IO.File]::WriteAllBytes((Join-Path $run 'game/Res/isolated.res'),$empty)
[IO.File]::WriteAllBytes((Join-Path $run 'game/Starter/MODS/DejaVu/res/isolated.res'),$empty)
$options.General.gameRoot=$fixture.Replace('\','/')
$options.General.openLastSessionOnStartup=$false;$options.General.autosaveEnabled=$false;$options.General.recentFiles=@()
$options.General.lastVisitedFolder=$maps.Replace('\','/')
$options.Resource.mapRoots=@($maps.Replace('\','/'))
$options.Resource.resourceRoots=@((Join-Path $fixture 'Starter/MODS/DejaVu/res').Replace('\','/'),(Join-Path $fixture 'Res').Replace('\','/'))
[IO.File]::WriteAllText((Join-Path $run 'app/endurance-options.json'),($options|ConvertTo-Json -Depth 30),[Text.UTF8Encoding]::new($false))
$env:APPDATA=Join-Path $run 'AppData';$env:LOCALAPPDATA=Join-Path $run 'LocalAppData';$env:USERPROFILE=Join-Path $run 'User'
$env:TEMP=Join-Path $run 'Temp';$env:TMP=$env:TEMP
$env:EI_MCP_TEST_ROOT=$run;$env:EI_MCP_TEST_MAPS=$maps;$env:EI_MAPPER_SETTINGS_DIR=Join-Path $run 'settings';$env:EI_MAPPER_AUTOMATION_DIR=Join-Path $run 'automation'
$env:EI_MAPPER_BACKUP_ROOT=Join-Path $run 'backups';$env:EI_MAPPER_HEADLESS_IDLE_MS='600000'
$env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
$timer=[Diagnostics.Stopwatch]::StartNew()
$p=Start-Process -FilePath (Join-Path $run 'app/topdown_export_check.exe') -ArgumentList '--assembly-catalog-check' -WorkingDirectory (Join-Path $run 'app') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $run 'stdout.txt') -RedirectStandardError (Join-Path $run 'stderr.txt')
$null=$p.Handle
if(-not $p.WaitForExit(600000)){Stop-Process -Id $p.Id;throw 'Owned native test process timeout'}
$p.WaitForExit();$code=$p.ExitCode
@{exitCode=$code;wallSeconds=$timer.Elapsed.TotalSeconds;pid=$p.Id;exe=(Get-FileHash (Join-Path $run 'app/topdown_export_check.exe')).Hash;mapper=(Get-FileHash (Join-Path $run 'app/EIMapperEndurance.exe')).Hash;bridge=(Get-FileHash (Join-Path $run 'app/EIMapperMcp.exe')).Hash}|ConvertTo-Json|Set-Content (Join-Path $run 'process.json')
$drift=@()
foreach($g in $guards){$f=Get-Item -LiteralPath $g.path;if($f.Length -ne $g.length -or $f.LastWriteTimeUtc.Ticks -ne $g.mtime -or (Get-FileHash -LiteralPath $g.path).Hash -ne $g.sha256){$drift+=$g.path}}
@{count=$guards.Count;drift=$drift}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $run 'guard-after.json')
Get-Content -LiteralPath (Join-Path $run 'stdout.txt') -Tail 16
if($drift.Count){throw ('External input drift; never restored: '+($drift -join ', '))}
if($null -eq $code){throw 'Native exit status unavailable'}
exit $code
