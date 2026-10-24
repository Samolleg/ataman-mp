param(
    [Parameter(Mandatory=$true)][string]$PackageRoot,
    [Parameter(Mandatory=$true)][string]$TargetRoot,
    [Parameter(Mandatory=$true)][string]$RollbackRoot
)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'program_files.ps1')
$package=(Resolve-Path -LiteralPath $PackageRoot).Path.TrimEnd('\')
$target=(Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$backup=[IO.Path]::GetFullPath($RollbackRoot).TrimEnd('\')
if($package -eq $target -or $backup.StartsWith($target+'\',[StringComparison]::OrdinalIgnoreCase) -or $target.StartsWith($package+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Separate staging, target and backup required'}
if(Test-Path -LiteralPath $backup){throw 'Rollback folder must be new; previous backups are preserved'}
$lockPath=Get-ProgramPath $target '.ataman-install.lock'
$lock=[IO.File]::Open($lockPath,'CreateNew','ReadWrite','None')
try {
    Assert-EditorClosed $target
    $old=Read-ProgramManifest $target;$new=Read-ProgramManifest $package
    Test-ProgramFiles $target $old;Test-ProgramFiles $package $new
    foreach($relative in $new.Keys){if(-not $old.ContainsKey($relative) -and (Test-Path -LiteralPath (Get-ProgramPath $target $relative))){throw "Unmanaged collision: $relative"}}
    $changed=@($new.Keys|Where-Object {$old.ContainsKey($_) -and $old[$_] -ne $new[$_]})
    $added=@($new.Keys|Where-Object {-not $old.ContainsKey($_)})
    $removed=@($old.Keys|Where-Object {-not $new.ContainsKey($_)})
    [IO.Directory]::CreateDirectory($backup)|Out-Null
    foreach($relative in @($old.Keys)+@('SHA256SUMS.txt')) {
        $destination=Get-ProgramPath $backup $relative
        [IO.Directory]::CreateDirectory((Split-Path -Parent $destination))|Out-Null
        [IO.File]::Copy((Get-ProgramPath $target $relative),$destination,$false)
    }
    Test-ProgramFiles $backup $old
    $oldManifestHash=Get-ProgramHash (Get-ProgramPath $target 'SHA256SUMS.txt')
    if((Get-ProgramHash (Get-ProgramPath $backup 'SHA256SUMS.txt')) -ne $oldManifestHash){throw 'Backup manifest differs'}
    Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'rollback.ps1'),(Join-Path $PSScriptRoot 'program_files.ps1') -Destination $backup
    $state=@{target=$target;old=$old;new=$new;changed=$changed;added=$added;removed=$removed;oldManifestHash=$oldManifestHash;newManifestHash=(Get-ProgramHash (Get-ProgramPath $package 'SHA256SUMS.txt'))}
    [IO.File]::WriteAllText((Join-Path $backup 'rollback-state.json'),($state|ConvertTo-Json -Depth 6),[Text.UTF8Encoding]::new($false))
    Clear-RetiredFiles $target
    Assert-EditorClosed $target
    Test-ProgramFiles $target $old
    $touched=@()
    try {
        foreach($relative in @($changed)+@($added)) {
            $touched+=$relative
            Set-ProgramFile $target $relative (Get-ProgramPath $package $relative) $new[$relative]
        }
        foreach($relative in $removed){$touched+=$relative;Remove-ProgramFile $target $relative}
        Set-ProgramFile $target 'SHA256SUMS.txt' (Get-ProgramPath $package 'SHA256SUMS.txt') $state.newManifestHash
        Test-ProgramFiles $target $new
    } catch {
        $failure=$_
        try {Restore-ProgramBackup $backup $target $old $new $touched $oldManifestHash}
        catch {throw "Installation failed ($failure); automatic rollback FAILED ($_). Verified backup: $backup"}
        throw "Installation failed; automatic rollback verified. $failure"
    }
    Write-Output "INSTALLED verified=$($new.Count) changed=$($changed.Count) added=$($added.Count) removed=$($removed.Count); backup=$backup"
} finally {$lock.Dispose();[IO.File]::Delete($lockPath)}
