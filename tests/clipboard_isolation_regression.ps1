param(
    [string]$AppBuild = 'build-release-134-clipiso',
    [string]$TestBuild = 'build-tests-134-clipiso',
    [switch]$PrepareReader
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$app = Join-Path $root "$AppBuild/release/EIMapperEndurance.exe"
$tests = Join-Path $root $TestBuild
$lock = Join-Path $root 'build-locks/ui-tests.lock'
if (!(Test-Path -LiteralPath $app)) { throw 'Build the application first.' }
if($PrepareReader) {
    # Keep the concurrently owned reader suite untouched. Generate only its
    # isolated entry point under the owned build tree; all cases stay identical.
    $dir=Join-Path $tests 'reader-drain'
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
    $original=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'res_reader_drain_tests.cpp'))
    $marker='    QApplication app(argc,argv);'
    if(!$original.Contains($marker)) { throw 'Reader entry point changed; re-audit its isolation injection.' }
    $isolated='#include "verified_clipboard.h"'+"`n"+$original.Replace($marker,
        '    ei::TestClipboardSession clipboard(true);'+"`n"+'    if(!clipboard.prepared()) return 2;'+"`n"+$marker+"`n"+'    if(!clipboard.install()) return 2;')
    $utf8=New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $dir 'res_reader_drain_tests.cpp'),$isolated,$utf8)
    $repo=$root.Replace('\','/')
    $pro='include("'+$repo+'/tests/res_reader_drain_tests.pro")'+"`n"+'SOURCES = $$PWD/res_reader_drain_tests.cpp'+"`n"+'INCLUDEPATH += "'+$repo+'/tests"'
    [IO.File]::WriteAllText((Join-Path $dir 'reader-drain.pro'),$pro,$utf8)
    $env:PATH="$env:EI_MAPPER_QT_BIN;$env:EI_MAPPER_MINGW_BIN;$env:PATH"
    & "$env:EI_MAPPER_QT_BIN/qmake.exe" (Join-Path $dir 'reader-drain.pro') "RESDRAIN_APP_BUILD=$repo/$AppBuild" -o (Join-Path $dir 'Makefile')
    if($LASTEXITCODE -ne 0) { throw 'Reader qmake failed.' }
    & "$env:EI_MAPPER_MINGW_BIN/mingw32-make.exe" -C $dir -f Makefile.Release -j6 *> (Join-Path $dir 'build.log')
    if($LASTEXITCODE -ne 0) { throw 'Reader build failed.' }
    & "$env:EI_MAPPER_QT_BIN/windeployqt.exe" --release --compiler-runtime (Join-Path $dir 'release/res_reader_drain_tests.exe') *> (Join-Path $dir 'deploy.log')
    if($LASTEXITCODE -ne 0) { throw 'Reader deploy failed.' }
}
New-Item -ItemType Directory -Path $lock | Out-Null # Atomic acquisition; no -Force.
Set-Content -Encoding UTF8 -LiteralPath (Join-Path $lock 'owner.txt') "EI-TEST-CLIPBOARD-ISOLATION-134 PID=$PID root=$root"
try {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ClipboardMetadata134 {
    [DllImport("user32.dll")] public static extern uint GetClipboardSequenceNumber();
    [DllImport("user32.dll", SetLastError=true)]
    public static extern bool GetUpdatedClipboardFormats([Out] uint[] ids, uint capacity, out uint count);
    public static uint[] Formats() {
        uint count;
        bool ok=GetUpdatedClipboardFormats(null,0,out count);
        if(!ok && Marshal.GetLastWin32Error()!=122) throw new Exception("Format inventory failed");
        var ids=new uint[count];
        if(count!=0 && !GetUpdatedClipboardFormats(ids,count,out count)) throw new Exception("Format inventory failed");
        Array.Sort(ids); return ids;
    }
}
'@
    $before = [ClipboardMetadata134]::GetClipboardSequenceNumber()
    $formats = [ClipboardMetadata134]::Formats() -join ','
    if($before -ne [ClipboardMetadata134]::GetClipboardSequenceNumber()) { throw 'Clipboard changed during metadata snapshot.' }
    function Run-Check([string]$Exe,[string]$Arguments,[string]$Name,[int]$Timeout=120000) {
        $si = New-Object Diagnostics.ProcessStartInfo
        $si.FileName=$Exe; $si.Arguments=$Arguments
        $si.WorkingDirectory=Split-Path -Parent $Exe
        $si.UseShellExecute=$false; $si.CreateNoWindow=$true
        $si.RedirectStandardOutput=$true; $si.RedirectStandardError=$true
        $p=[Diagnostics.Process]::Start($si)
        $output=$p.StandardOutput.ReadToEndAsync(); $errors=$p.StandardError.ReadToEndAsync()
        if(!$p.WaitForExit($Timeout)) { $p.Kill(); throw "$Name TIMEOUT (FAIL)" }
        $outPath=Join-Path $tests "$Name.stdout.log"; $errPath=Join-Path $tests "$Name.stderr.log"
        [IO.File]::WriteAllText($outPath,$output.Result)
        [IO.File]::WriteAllText($errPath,$errors.Result)
        $receipt="$Name exit=$($p.ExitCode)"
        Write-Output $receipt
        if($p.ExitCode -ne 0) { throw "$receipt (FAIL); see $outPath and $errPath" }
        if($Arguments -ne '--version' -and !$output.Result.Contains('PASS: clipboard isolation')) {
            throw "$Name missing isolation receipt (FAIL)"
        }
        if($Name -eq 'clipboard' -or $Name -eq 'reader-drain') {
            $resultPath = if($Name -eq 'clipboard') { Join-Path $si.WorkingDirectory 'clipboard-results.txt' }
                else { Join-Path $si.WorkingDirectory '../fixtures-resdrain/reader-drain-results.txt' }
            $summary=[regex]::Match([IO.File]::ReadAllText($resultPath),'Totals: (\d+) passed, (\d+) failed, (\d+) skipped')
            if(!$summary.Success -or [int]$summary.Groups[1].Value -eq 0 -or
                [int]$summary.Groups[2].Value -ne 0 -or [int]$summary.Groups[3].Value -ne 0) {
                throw "$Name missing/failed/skipped QtTest cases (FAIL)"
            }
            Write-Output $summary.Value
        }
    }
    Run-Check (Join-Path $tests 'clipboard/release/verified_clipboard_tests.exe') '-o clipboard-results.txt,txt' 'clipboard'
    Run-Check $app '--version' 'version'
    Run-Check $app '--ui-widget-check' 'ui-widget' 300000
    Run-Check $app '--scenario-layer-check' 'scenario'
    Run-Check $app '--dark-theme-check' 'dark-theme'
    Run-Check $app '--model-gallery-check' 'gallery'
    Run-Check (Join-Path $tests 'reader-drain/release/res_reader_drain_tests.exe') '-o reader-drain-results.txt,txt' 'reader-drain'
    $after=[ClipboardMetadata134]::GetClipboardSequenceNumber()
    $afterFormats=[ClipboardMetadata134]::Formats() -join ','
    if($before -ne $after -or $formats -ne $afterFormats -or $after -ne [ClipboardMetadata134]::GetClipboardSequenceNumber()) {
        throw "FAIL: clipboard metadata changed sequence=$before->$after formats=$formats->$afterFormats"
    }
    $receipt="PASS: harness clipboard metadata sequence=$before->$after formats=$formats->$afterFormats unchanged=1"
    Set-Content -Encoding UTF8 -LiteralPath (Join-Path $tests 'harness-receipt.txt') $receipt
    Write-Output $receipt
} finally {
    Remove-Item -LiteralPath (Join-Path $lock 'owner.txt')
    Remove-Item -LiteralPath $lock
}
