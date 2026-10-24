param([switch]$VerifyOnly)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'program_files.ps1')
$backup=(Resolve-Path -LiteralPath $PSScriptRoot).Path.TrimEnd('\')
$state=Get-Content -LiteralPath (Join-Path $backup 'rollback-state.json') -Raw|ConvertFrom-Json
$old=@{};$new=@{}
foreach($p in $state.old.PSObject.Properties){$old[$p.Name]=$p.Value}
foreach($p in $state.new.PSObject.Properties){$new[$p.Name]=$p.Value}
Test-ProgramFiles $backup $old
if((Get-ProgramHash (Get-ProgramPath $backup 'SHA256SUMS.txt')) -ne $state.oldManifestHash){throw 'Backup manifest differs'}
if($VerifyOnly){Write-Output 'ROLLBACK_VERIFIED';return}
$target=(Resolve-Path -LiteralPath $state.target).Path.TrimEnd('\')
$lockPath=Get-ProgramPath $target '.ataman-install.lock'
$lock=[IO.File]::Open($lockPath,'CreateNew','ReadWrite','None')
try {
    Assert-EditorClosed $target
    Test-ProgramFiles $target $new
    if((Get-ProgramHash (Get-ProgramPath $target 'SHA256SUMS.txt')) -ne $state.newManifestHash){throw 'Installed manifest changed'}
    Clear-RetiredFiles $target
    Restore-ProgramBackup $backup $target $old $new (@($state.changed)+@($state.added)+@($state.removed)) $state.oldManifestHash
    Write-Output 'ROLLBACK_RESTORED: all program hashes verified; user files preserved; running MCP bridges retained.'
} finally {$lock.Dispose();[IO.File]::Delete($lockPath)}
