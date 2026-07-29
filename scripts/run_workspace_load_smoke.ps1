param(
    [Parameter(Mandatory=$true)][string]$EditorRoot,
    [Parameter(Mandatory=$true)][string]$Mpr,
    [Parameter(Mandatory=$true)][string]$Mob,
    [int]$MaxMobLoadMs = 5000,
    [int]$TimeoutSeconds = 90,
    [int]$InterruptAfterCompleted = 0
)

$ErrorActionPreference = 'Stop'
$editorRoot = (Resolve-Path $EditorRoot).Path
$exe = Join-Path $editorRoot 'EIMapperEndurance.exe'
$log = Join-Path $editorRoot 'logs\workflow.log'
foreach ($path in @($exe, $Mpr, $Mob)) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Missing smoke-test input: $path" }
}

function Archive-WorkflowLog {
    if (Test-Path -LiteralPath $log) {
        $stamp = Get-Date -Format 'yyyyMMdd-HHmmss-fff'
        Move-Item -LiteralPath $log -Destination (Join-Path (Split-Path $log) "workflow-smoke-$stamp.log")
    }
}

function Invoke-Smoke([int]$interruptAt) {
    Archive-WorkflowLog
    $arguments = "--workspace-visual-smoke `"$Mpr`" `"$Mob`""
    $process = Start-Process -FilePath $exe -WorkingDirectory $editorRoot `
        -ArgumentList $arguments -WindowStyle Hidden -PassThru
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $interrupted = $false
    try {
        while ((Get-Date) -lt $deadline) {
            Start-Sleep -Milliseconds 250
            if (-not (Test-Path -LiteralPath $log)) { continue }
            $text = Get-Content -LiteralPath $log -Raw
            if ($interruptAt -gt 0 -and $text -match 'Full character visual progress completed:(\d+)') {
                $matches = [regex]::Matches($text, 'Full character visual progress completed:(\d+)')
                if ([int]$matches[$matches.Count - 1].Groups[1].Value -ge $interruptAt) {
                    Stop-Process -Id $process.Id -Force
                    $interrupted = $true
                    break
                }
            }
            if ($text.Contains('Workspace visual smoke complete')) { break }
            if ($process.HasExited) { throw "Editor exited before the smoke-test marker (exit $($process.ExitCode))." }
        }
        if (-not $interrupted -and (-not (Test-Path -LiteralPath $log) -or
                -not (Get-Content -LiteralPath $log -Raw).Contains('Workspace visual smoke complete'))) {
            throw "Workspace smoke test timed out after $TimeoutSeconds seconds."
        }
    }
    finally {
        if (-not $process.HasExited) { Stop-Process -Id $process.Id -Force -ErrorAction SilentlyContinue }
    }
    $text = Get-Content -LiteralPath $log -Raw
    $loadMatches = [regex]::Matches($text, 'MOB load complete[^\r\n]*elapsedMs:(\d+)')
    if ($loadMatches.Count -eq 0) { throw 'MOB load timing was not recorded.' }
    $loadMs = [int]$loadMatches[$loadMatches.Count - 1].Groups[1].Value
    if ($loadMs -gt $MaxMobLoadMs) { throw "MOB load took ${loadMs}ms (limit ${MaxMobLoadMs}ms)." }
    [pscustomobject]@{ LoadMs=$loadMs; Interrupted=$interrupted }
}

$first = Invoke-Smoke $InterruptAfterCompleted
if ($InterruptAfterCompleted -gt 0) {
    $cache = Join-Path $editorRoot 'cache\character-composites'
    $before = @(Get-ChildItem -LiteralPath $cache -Filter '*.cache' -File -ErrorAction SilentlyContinue).Count
    if ($before -eq 0) { throw 'Interrupted run persisted no completed character composites.' }
    $second = Invoke-Smoke 0
    $after = @(Get-ChildItem -LiteralPath $cache -Filter '*.cache' -File -ErrorAction SilentlyContinue).Count
    [pscustomobject]@{ FirstLoadMs=$first.LoadMs; PersistedAfterInterrupt=$before;
        RestartLoadMs=$second.LoadMs; PersistedAfterRestart=$after }
} else {
    $first
}
