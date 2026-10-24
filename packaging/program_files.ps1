# Shared guarded program-file replacement for installation and rollback.
# Never dot-source from the target installation; use the reviewed source/backup.
$ErrorActionPreference='Stop'
function Get-ProgramPath([string]$Root,[string]$Relative) {
    if($Relative -match '[:*?]' -or [IO.Path]::IsPathRooted($Relative)){throw 'Unsafe managed path'}
    $path=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
    if(-not $path.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Managed path escapes root'}
    $check=$path
    while($check -and $check.Length -ge $Root.Length) {
        if((Test-Path -LiteralPath $check) -and ((Get-Item -LiteralPath $check -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw "Linked program path: $check"}
        $check=Split-Path -Parent $check
    }
    return $path
}
function Read-ProgramManifest([string]$Root) {
    $result=@{}
    foreach($line in [IO.File]::ReadAllLines((Get-ProgramPath $Root 'SHA256SUMS.txt'))) {
        if($line -notmatch '^([a-fA-F0-9]{64}) \*(.+)$'){throw 'Malformed program manifest'}
        $sha=$Matches[1];$relative=$Matches[2].Replace('\','/')
        $null=Get-ProgramPath $Root $relative
        if($result.ContainsKey($relative) -or $relative -eq 'SHA256SUMS.txt'){throw 'Duplicate/reserved manifest path'}
        # A manifest cannot turn user state into managed program data.
        if($relative -match '(?i)(^|/)(endurance-options\.json|options\.json|world-map-layouts\.json|\.ataman[^/]*|logs?|saves?|autosaves?|cache|maps)(/|$)' -or $relative -match '(?i)(^|/)rollback[^/]*/' -or $relative -match '(?i)\.(mpr|mob|res|grp|dmp|log)$'){throw "Protected user path in manifest: $relative"}
        $result[$relative]=$sha.ToUpperInvariant()
    }
    if(-not $result.ContainsKey('EIMapperEndurance.exe') -or -not $result.ContainsKey('EIMapperMcp.exe') -or -not $result.ContainsKey('VERSION')){throw 'Incomplete program manifest'}
    return $result
}
function Get-ProgramHash([string]$Path) {return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash}
function Test-ProgramFiles([string]$Root,$Manifest) {
    foreach($relative in $Manifest.Keys) {
        $path=Get-ProgramPath $Root $relative
        if(-not [IO.File]::Exists($path) -or (Get-ProgramHash $path) -ne $Manifest[$relative]){throw "Managed file differs: $relative"}
    }
}
function Assert-EditorClosed([string]$Root) {
    $running=Get-CimInstance Win32_Process | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($Root+'\',[StringComparison]::OrdinalIgnoreCase) -and
        ($_.Name -like 'EIMapperEndurance.exe*' -or $_.Name -like 'EIMapperEnduranceRuntimeHost.exe*')
    }
    if($running){throw 'Close this installation editor/runtime before replacement. MCP bridges may keep running; no process was stopped.'}
}
function Save-RetiredFiles([string]$Root,$Entries) {
    $path=Get-ProgramPath $Root '.ataman-retired.json'
    $temp=$path+'.tmp-'+[guid]::NewGuid().ToString('N')
    [IO.File]::WriteAllText($temp,(ConvertTo-Json -InputObject @($Entries) -Depth 5),[Text.UTF8Encoding]::new($false))
    try {
        if([IO.File]::Exists($path)){[IO.File]::Replace($temp,$path,[System.Management.Automation.Language.NullString]::Value)}else{[IO.File]::Move($temp,$path)}
    } finally {if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Read-RetiredFiles([string]$Root) {
    $path=Get-ProgramPath $Root '.ataman-retired.json'
    if([IO.File]::Exists($path)) {
        # PS5 ConvertFrom-Json emits a JSON array as one pipeline object.
        # Enumerate explicitly so multiple retired files never become one entry.
        foreach($item in ([IO.File]::ReadAllText($path)|ConvertFrom-Json)){Write-Output $item}
    }
}
function Clear-RetiredFiles([string]$Root) {
    $retained=@();$removed=0
    foreach($entry in @(Read-RetiredFiles $Root)) {
        $path=Get-ProgramPath $Root $entry.path
        if($entry.path -notmatch '\.old-ataman-[a-f0-9]{32}$'){throw 'Invalid retired-file receipt'}
        if(-not [IO.File]::Exists($path)){continue}
        if((Get-ProgramHash $path) -ne $entry.sha256){throw "Retired file externally changed: $($entry.path)"}
        # A mapped image cannot be opened for writing. Delete can succeed with
        # pending-delete semantics, so explicitly retain images still in use.
        try {$handle=[IO.File]::Open($path,'Open','ReadWrite','None');$handle.Dispose();[IO.File]::Delete($path);$removed++}
        catch [IO.IOException] {$retained+=$entry}
        catch [UnauthorizedAccessException] {$retained+=$entry}
    }
    Save-RetiredFiles $Root $retained
    Write-Output "Retired files removed=$removed retained=$($retained.Count)"
}
function Move-ProgramAside([string]$Root,[string]$Relative) {
    $path=Get-ProgramPath $Root $Relative
    if(-not [IO.File]::Exists($path)){return}
    $retired=$Relative+'.old-ataman-'+[guid]::NewGuid().ToString('N')
    $entries=@(Read-RetiredFiles $Root)
    $entry=@{path=$retired;sha256=(Get-ProgramHash $path)}
    # Record before rename: interrupted updates retain exact owned cleanup data.
    Save-RetiredFiles $Root @($entries+$entry)
    [IO.File]::Move($path,(Get-ProgramPath $Root $retired))
}
function Set-ProgramFile([string]$Root,[string]$Relative,[string]$Source,[string]$Hash) {
    $path=Get-ProgramPath $Root $Relative
    [IO.Directory]::CreateDirectory((Split-Path -Parent $path))|Out-Null
    $temp=$path+'.new-ataman-'+[guid]::NewGuid().ToString('N')
    try {
        [IO.File]::Copy($Source,$temp,$false)
        if((Get-ProgramHash $temp) -ne $Hash){throw "Incoming file differs: $Relative"}
        if([IO.File]::Exists($path)) {
            # File.Replace may succeed even for a mapped image on newer Windows.
            # Explicitly preserve busy images by name so cleanup is observable
            # and works identically on Windows versions with stricter sharing.
            $busy=$false
            try {$handle=[IO.File]::Open($path,'Open','ReadWrite','None');$handle.Dispose()}
            catch [IO.IOException] {$busy=$true}
            catch [UnauthorizedAccessException] {$busy=$true}
            if($busy){Move-ProgramAside $Root $Relative;[IO.File]::Move($temp,$path)}
            else {
                try {[IO.File]::Replace($temp,$path,[System.Management.Automation.Language.NullString]::Value)}
                catch [IO.IOException] {Move-ProgramAside $Root $Relative;[IO.File]::Move($temp,$path)}
                catch [UnauthorizedAccessException] {Move-ProgramAside $Root $Relative;[IO.File]::Move($temp,$path)}
            }
        }else{[IO.File]::Move($temp,$path)}
        if((Get-ProgramHash $path) -ne $Hash){throw "Replacement readback differs: $Relative"}
    }finally{if([IO.File]::Exists($temp)){[IO.File]::Delete($temp)}}
}
function Remove-ProgramFile([string]$Root,[string]$Relative) {
    $path=Get-ProgramPath $Root $Relative
    try {[IO.File]::Delete($path)}
    catch [IO.IOException] {Move-ProgramAside $Root $Relative}
    catch [UnauthorizedAccessException] {Move-ProgramAside $Root $Relative}
}
function Restore-ProgramBackup([string]$Backup,[string]$Target,$Old,$New,$Touched,[string]$OldManifestHash) {
    foreach($relative in $Touched) {
        $path=Get-ProgramPath $Target $relative
        if([IO.File]::Exists($path)) {
            $hash=Get-ProgramHash $path
            if($hash -ne $Old[$relative] -and $hash -ne $New[$relative]){throw "Rollback conflict: $relative"}
            if($Old.ContainsKey($relative) -and $hash -eq $Old[$relative]){continue}
        }
        if($Old.ContainsKey($relative)){Set-ProgramFile $Target $relative (Get-ProgramPath $Backup $relative) $Old[$relative]}
        else{Remove-ProgramFile $Target $relative}
    }
    $manifest=Get-ProgramPath $Target 'SHA256SUMS.txt'
    if(-not [IO.File]::Exists($manifest) -or (Get-ProgramHash $manifest) -ne $OldManifestHash) {
        Set-ProgramFile $Target 'SHA256SUMS.txt' (Get-ProgramPath $Backup 'SHA256SUMS.txt') $OldManifestHash
    }
    Test-ProgramFiles $Target $Old
}
