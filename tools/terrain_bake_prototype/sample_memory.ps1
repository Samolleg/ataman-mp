param(
    [Parameter(Mandatory=$true)][int]$ProcessId,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [ValidateRange(1,3600)][int]$DurationSeconds=180,
    [ValidateRange(100,10000)][int]$IntervalMilliseconds=500
)
# Owner-run read-only monitor. Never starts, stops or modifies the game.
$ErrorActionPreference='Stop'
$terrainOutput=[IO.Path]::GetFullPath($OutputDirectory)
$terrainAllowed=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../build-tests-terrain-bake-stage-1'))
if (-not $terrainOutput.StartsWith($terrainAllowed+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)) {
    throw 'Measurements must be inside build-tests-terrain-bake-stage-1.'
}
New-Item -ItemType Directory -Force -Path $terrainOutput | Out-Null
$terrainSamples=Join-Path $terrainOutput 'memory.jsonl'
if (Test-Path -LiteralPath $terrainSamples) { throw 'Use a fresh measurement directory; previous samples are preserved.' }
Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class TerrainCapacityMemory {
    [StructLayout(LayoutKind.Sequential)] struct MBI {
        public IntPtr BaseAddress, AllocationBase;
        public uint AllocationProtect;
        public UIntPtr RegionSize;
        public uint State, Protect, Type;
    }
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenProcess(uint access,bool inherit,int pid);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);
    [DllImport("kernel32.dll",SetLastError=true)] static extern UIntPtr VirtualQueryEx(IntPtr process,IntPtr address,out MBI info,UIntPtr size);
    [DllImport("kernel32.dll",SetLastError=true)] static extern bool IsWow64Process(IntPtr process,out bool wow);
    public static long[] ReadVA(int pid) {
        IntPtr h=OpenProcess(0x400,false,pid);
        if(h==IntPtr.Zero) throw new Exception("OpenProcess: "+Marshal.GetLastWin32Error());
        try {
            bool wow;
            if(!IsWow64Process(h,out wow)) throw new Exception("IsWow64Process failed");
            if(Environment.Is64BitOperatingSystem && !wow) throw new Exception("This capacity protocol expects a 32-bit game process");
            ulong address=0,limit=0x100000000UL;
            long committed=0,reserved=0,free=0,largest=0,regions=0;
            while(address<limit) {
                MBI b;
                if(VirtualQueryEx(h,new IntPtr((long)address),out b,(UIntPtr)Marshal.SizeOf(typeof(MBI)))==UIntPtr.Zero) {
                    // A non-LAA 32-bit process can have a lower query ceiling.
                    int error=Marshal.GetLastWin32Error();
                    if(error==87 && address>=0x7ffe0000UL) break;
                    throw new Exception("VirtualQueryEx at "+address+": "+error);
                }
                ulong size=b.RegionSize.ToUInt64();
                ulong end=unchecked((ulong)b.BaseAddress.ToInt64())+size;
                if(size==0||end<=address) throw new Exception("Invalid VA region");
                long n=(long)Math.Min(size,limit-address);
                if(b.State==0x1000) committed+=n;
                else if(b.State==0x2000) reserved+=n;
                else if(b.State==0x10000) {free+=n;largest=Math.Max(largest,n);}
                address=end;regions++;
            }
            return new long[]{committed,reserved,free,largest,regions,(long)address};
        } finally {CloseHandle(h);}
    }
}
'@
$terrainProcess=Get-Process -Id $ProcessId
$terrainStart=$terrainProcess.StartTime.ToUniversalTime().ToString('o')
$terrainExe=$terrainProcess.Path
@{pid=$ProcessId;path=$terrainExe;startUtc=$terrainStart;sha256=(Get-FileHash -LiteralPath $terrainExe -Algorithm SHA256).Hash;
    intervalMs=$IntervalMilliseconds;durationSeconds=$DurationSeconds;readOnly=$true} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $terrainOutput 'process.json') -Encoding UTF8
$terrainWatch=[Diagnostics.Stopwatch]::StartNew()
$terrainPeakPrivate=0L;$terrainPeakWorking=0L;$terrainPeakCommit=0L;$terrainMinLargest=[long]::MaxValue;$terrainCount=0
$terrainWriter=[IO.StreamWriter]::new($terrainSamples,$false,[Text.UTF8Encoding]::new($false))
$terrainEnd='duration'
try {
    while ($terrainWatch.Elapsed.TotalSeconds -lt $DurationSeconds) {
        try {
            $terrainProcess.Refresh()
            if ($terrainProcess.HasExited) { $terrainEnd='process-exited';break }
            $terrainVA=[TerrainCapacityMemory]::ReadVA($ProcessId)
            $terrainSample=@{utc=[DateTime]::UtcNow.ToString('o');seconds=$terrainWatch.Elapsed.TotalSeconds;
                workingSet=$terrainProcess.WorkingSet64;privateBytes=$terrainProcess.PrivateMemorySize64;
                virtualMemorySize=$terrainProcess.VirtualMemorySize64;committedVA=$terrainVA[0];reservedVA=$terrainVA[1];
                freeVA=$terrainVA[2];largestFreeRegion=$terrainVA[3];regions=$terrainVA[4];scannedAddressLimit=$terrainVA[5]}
            $terrainWriter.WriteLine(($terrainSample | ConvertTo-Json -Compress));$terrainWriter.Flush()
            $terrainPeakPrivate=[Math]::Max($terrainPeakPrivate,$terrainProcess.PrivateMemorySize64)
            $terrainPeakWorking=[Math]::Max($terrainPeakWorking,$terrainProcess.WorkingSet64)
            $terrainPeakCommit=[Math]::Max($terrainPeakCommit,$terrainVA[0])
            $terrainMinLargest=[Math]::Min($terrainMinLargest,$terrainVA[3]);$terrainCount++
        } catch { $terrainEnd='sample-error: '+$_.Exception.Message;break }
        Start-Sleep -Milliseconds $IntervalMilliseconds
    }
} finally { $terrainWriter.Dispose() }
@{samples=$terrainCount;end=$terrainEnd;peakPrivateBytes=$terrainPeakPrivate;peakWorkingSet=$terrainPeakWorking;
    peakCommittedVA=$terrainPeakCommit;minimumLargestFreeRegion=$(if($terrainCount){$terrainMinLargest}else{$null});
    runtimePass=$false;note='Memory samples alone do not prove map load, full page residency, gameplay, overlay or mip acceptance.'} |
    ConvertTo-Json | Set-Content -LiteralPath (Join-Path $terrainOutput 'summary.json') -Encoding UTF8
if ($terrainCount -eq 0 -or $terrainEnd.StartsWith('sample-error')) { throw $terrainEnd }
