param(
    [Parameter(Mandatory = $true, Position = 0)]
    [string]$PackageRoot
)

$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $PackageRoot).Path
$failures = [System.Collections.Generic.List[string]]::new()

$bridge = Join-Path $root 'runtime\EIMapperEnduranceJmvBridge.plg'
if (-not (Test-Path -LiteralPath $bridge -PathType Leaf)) {
    $failures.Add('packaged JMV launch bridge is missing: runtime\EIMapperEnduranceJmvBridge.plg')
}
$runtimeHost = Join-Path $root 'runtime\EIMapperEnduranceRuntimeHost.exe'
if(-not (Test-Path -LiteralPath $runtimeHost -PathType Leaf))
{
    $failures.Add('packaged Base runtime host is missing: runtime\EIMapperEnduranceRuntimeHost.exe')
}

foreach ($name in @('endurance-options.json', 'options.json')) {
    $matches = Get-ChildItem -LiteralPath $root -Recurse -File -Filter $name
    foreach ($match in $matches) {
        $failures.Add("user settings included: $($match.FullName.Substring($root.Length + 1))")
    }
}

$logsRoot = Join-Path $root 'logs'
if (Test-Path -LiteralPath $logsRoot) {
    foreach ($file in Get-ChildItem -LiteralPath $logsRoot -Recurse -File) {
        $failures.Add("runtime log included: $($file.FullName.Substring($root.Length + 1))")
    }
}

foreach ($dump in Get-ChildItem -LiteralPath $root -Recurse -File | Where-Object {
    $_.Extension -in @('.dmp', '.mdmp')
}) {
    $failures.Add("crash dump included: $($dump.FullName.Substring($root.Length + 1))")
}

if ($failures.Count -ne 0) {
    $failures | ForEach-Object { Write-Error $_ }
    exit 1
}

Write-Output "PASS: distribution contains no user settings, logs, or crash dumps"
