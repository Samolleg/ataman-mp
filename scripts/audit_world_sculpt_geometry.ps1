param(
    [Parameter(Mandatory=$true)][string]$LayoutPath,
    [Parameter(Mandatory=$true)][string]$MapsDirectory,
    [string]$ProfileId = 'jamais-vu',
    [string]$AllodId = 'gipat',
    [string]$AnchorZoneId = 'gz1g'
)
# Read-only diagnostic. No extraction, copying, editing or layout persistence.
$ErrorActionPreference = 'Stop'
$layout = Get-Content -LiteralPath $LayoutPath -Raw | ConvertFrom-Json
$zones = @($layout.profiles | Where-Object profileId -eq $ProfileId |
    ForEach-Object allods | Where-Object allodId -eq $AllodId |
    ForEach-Object zones | Where-Object componentAnchorZoneId -eq $AnchorZoneId)
$maps = @()
foreach ($zone in $zones) {
    $path = Join-Path $MapsDirectory ($zone.mprIdentity + '.mpr')
    $bytes = [IO.File]::ReadAllBytes($path)
    if ([BitConverter]::ToUInt32($bytes,0) -ne 0x019CE23C) { throw 'Invalid RES signature' }
    $count = [BitConverter]::ToUInt32($bytes,4)
    $table = [BitConverter]::ToUInt32($bytes,8)
    $names = [long]$table + [long]$count * 22
    $members = @{}
    for ($i=0; $i -lt $count; $i++) {
        $entry = $table + $i*22
        $length = [BitConverter]::ToUInt16($bytes,$entry+16)
        $nameOffset = [BitConverter]::ToUInt32($bytes,$entry+18)
        $name = [Text.Encoding]::GetEncoding(1251).GetString($bytes,$names+$nameOffset,$length)
        $members[$name] = [BitConverter]::ToUInt32($bytes,$entry+8)
    }
    $mpNames = @($members.Keys | Where-Object { $_ -match '\.mp$' })
    if ($mpNames.Count -ne 1) { throw 'Ambiguous MP namespace' }
    $mp = $members[$mpNames[0]]
    if ([BitConverter]::ToUInt32($bytes,$mp) -ne 0xCE4AF672L) { throw 'Invalid MP signature' }
    $maxZ = [BitConverter]::ToSingle($bytes,$mp+4)
    $width = [BitConverter]::ToUInt32($bytes,$mp+8)*32
    $height = [BitConverter]::ToUInt32($bytes,$mp+12)*32
    $namespace = $mpNames[0].Substring(0,$mpNames[0].Length-3)
    $boundary = @{}
    foreach ($name in $members.Keys) {
        if ($name -notmatch ('^'+[regex]::Escape($namespace)+'(\d{3})(\d{3})\.sec$')) { continue }
        $sx = [int]$Matches[1]; $sy = [int]$Matches[2]
        $payload = $members[$name]
        for ($ly=0; $ly -le 32; $ly++) {
            for ($lx=0; $lx -le 32; $lx++) {
                $gx=$sx*32+$lx; $gy=$sy*32+$ly
                if ($gx -ne 0 -and $gx -ne $width -and $gy -ne 0 -and $gy -ne $height) { continue }
                $v = $payload+5+($ly*33+$lx)*8
                $dx=[int]$bytes[$v]; if ($dx -ge 128) { $dx-=256 }
                $dy=[int]$bytes[$v+1]; if ($dy -ge 128) { $dy-=256 }
                # Decimal coordinates avoid float-key epsilon choices. All source
                # offsets are exact integer multiples of 1/254 world units.
                $x=([decimal]$zone.origin.x+$gx)*254+$dx
                $y=([decimal]$zone.origin.y+$gy)*254+$dy
                $key=$x.ToString([Globalization.CultureInfo]::InvariantCulture)+','+$y.ToString([Globalization.CultureInfo]::InvariantCulture)
                $z=[BitConverter]::ToUInt16($bytes,$v+2)*[double]$maxZ/65535
                if ($boundary.ContainsKey($key) -and $boundary[$key] -ne $z) { throw 'Inconsistent intra-map boundary ownership' }
                $boundary[$key]=$z
            }
        }
    }
    $hash=(Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash
    $maps += [pscustomobject]@{Id=$zone.zoneId; Mpr=$zone.mprIdentity; X=[double]$zone.origin.x; Y=[double]$zone.origin.y; Width=$width; Height=$height; MaxZ=$maxZ; Boundary=$boundary; SHA256=$hash}
}
$mapReport = @($maps | Select-Object Id,Mpr,X,Y,Width,Height,MaxZ,SHA256)
$pairs=@()
for ($i=0; $i -lt $maps.Count; $i++) {
    for ($j=$i+1; $j -lt $maps.Count; $j++) {
        $a=$maps[$i]; $b=$maps[$j]
        $ix=[Math]::Min($a.X+$a.Width,$b.X+$b.Width)-[Math]::Max($a.X,$b.X)
        $iy=[Math]::Min($a.Y+$a.Height,$b.Y+$b.Height)-[Math]::Max($a.Y,$b.Y)
        $shared=0; $different=0; $maxDifference=0.0
        foreach ($key in $a.Boundary.Keys) {
            if (!$b.Boundary.ContainsKey($key)) { continue }
            $shared++
            $difference=[Math]::Abs($a.Boundary[$key]-$b.Boundary[$key])
            if ($difference -gt 0.000001) { $different++ }
            $maxDifference=[Math]::Max($maxDifference,$difference)
        }
        $pairs += [pscustomobject]@{A=$a.Id; B=$b.Id; NominalIntersectionX=$ix; NominalIntersectionY=$iy; ExactBoundaryXYMatches=$shared; DifferentHeights=$different; MaxWorldHeightDifference=$maxDifference}
    }
}
[pscustomobject]@{Maps=$mapReport; Pairs=$pairs} | ConvertTo-Json -Depth 5
