param(
    [Parameter(Mandatory = $true)] [string]$MobExplorer,
    [string]$RepositoryRoot = "",
    [string]$OutputDirectory = ""
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedTool = "253CDF9AAEE74640A219283A210C2A026A644161CE06F4DF25A297E9CAD4ECDD"
$expectedMob = "24CA3352EBCC174EDADB9A35A180B9DEA3F2A57B70BD9BBE5F5BFA014A0F113B"
$expectedMpr = "2D4A14374676364FC7E36DE4C93B0B66F7161A9DC3A67B2134E05F116A6B7192"

function Resolve-Guarded([string]$Path, [string]$Hash, [string]$Label) {
    if (-not [IO.File]::Exists($Path)) { throw "$Label not found: $Path" }
    $resolved = (Resolve-Path -LiteralPath $Path).Path
    $actual = (Get-FileHash -LiteralPath $resolved -Algorithm SHA256).Hash
    if ($actual -ne $Hash) { throw "$Label hash mismatch: expected $Hash, got $actual" }
    return $resolved
}

function Get-Field($Node, [string]$Name) {
    $field = $Node.Items | Where-Object { $_.Id.ToString() -eq $Name } |
        Select-Object -First 1
    if ($null -eq $field) { throw "Particle record is missing $Name" }
    return $field
}

if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
    $RepositoryRoot = Split-Path -Parent $PSScriptRoot
}
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
$tool = Resolve-Guarded $MobExplorer $expectedTool "MobExplorer"
$sourceMob = Resolve-Guarded (Join-Path $root `
    "artifacts\particle-catalog-grid\all-particles-grid.mob") $expectedMob "Catalog MOB"
$sourceMpr = Resolve-Guarded (Join-Path $root `
    "artifacts\particle-catalog-collection\test-scenes\particle-catalog-grid\particle-zone1-flat-7x5.mpr") `
    $expectedMpr "Catalog MPR"
$sourceManifestPath = Join-Path $root `
    "artifacts\particle-catalog-grid\all-particles-grid.json"
$sourceManifest = Get-Content -Raw -LiteralPath $sourceManifestPath | ConvertFrom-Json

if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $root "artifacts\particle-runtime-crash-bisection"
}
$outputRoot = [IO.Path]::GetFullPath($OutputDirectory)
$collections = Join-Path $outputRoot "collections"
$scenes = Join-Path $outputRoot "test-scenes\particle-catalog-bisection"
[IO.Directory]::CreateDirectory($collections) | Out-Null
[IO.Directory]::CreateDirectory($scenes) | Out-Null
Copy-Item -LiteralPath $sourceMpr -Destination (
    Join-Path $scenes "particle-zone1-flat-7x5.mpr") -Force

$assembly = [Reflection.Assembly]::LoadFile($tool)
$mobType = $assembly.GetType("EILib.MobFile", $true)
$constructor = $mobType.GetConstructor([Type[]]@([string]))
if ($null -eq $constructor) { throw "Expected MobFile(string) constructor was not found" }

$variants = [ordered]@{
    "00-empty-control" = @()
    "10-half-000-041" = @(0..41)
    "11-half-042-082" = @(42..82)
    "20-quarter-000-020" = @(0..20)
    "21-quarter-021-041" = @(21..41)
    "22-quarter-042-062" = @(42..62)
    "23-quarter-063-082" = @(63..82)
}
for ($row = 0; $row -le 8; $row++) {
    $variants["3$row-row-$row"] = @($sourceManifest.records |
        Where-Object { [int]$_.row -eq $row } |
        ForEach-Object { [int]$_.index })
}

$bundle = [ordered]@{
    format = "ei-particle-runtime-bisection-v2"
    sourceMobSha256 = $expectedMob
    sourceMprSha256 = $expectedMpr
    borrowedZone = "gz1g"
    borrowedExit = 1
    variants = @()
}

foreach ($variant in $variants.GetEnumerator()) {
    $name = [string]$variant.Key
    $selected = @($variant.Value)
    $selectedIds = @{}
    foreach ($index in $selected) { $selectedIds[900000 + [int]$index] = $true }

    $mob = $constructor.Invoke([object[]]@([string]$sourceMob))
    $mob.OpenObjectsSection()
    for ($index = $mob.CurrentSection.Items.Count - 1; $index -ge 0; $index--) {
        $node = $mob.CurrentSection.Items[$index]
        if ($node.Id.ToString() -ne "PARTICL") { continue }
        $id = [int](Get-Field $node "PARTICL_ID").GetNumber()
        if (-not $selectedIds.ContainsKey($id)) { $mob.CurrentSection.Items.RemoveAt($index) }
    }

    $mobName = "$name.mob"
    $mobPath = Join-Path $scenes $mobName
    $mob.SaveMobFile($mobPath)
    $reopened = $constructor.Invoke([object[]]@([string]$mobPath))
    $reopened.OpenObjectsSection()
    $actualIds = @($reopened.CurrentSection.Items |
        Where-Object { $_.Id.ToString() -eq "PARTICL" } |
        ForEach-Object { [int](Get-Field $_ "PARTICL_ID").GetNumber() })
    $expectedIds = @($selected | ForEach-Object { 900000 + [int]$_ })
    if (($actualIds -join ",") -ne ($expectedIds -join ",")) {
        throw "Reopened variant $name does not match its selection"
    }

    $collectionName = "$name.collection"
    $collection = @(
        "# EI Mapper Endurance Collection", "[collection]", "version=1",
        "name=`"Particle runtime bisection - $name`"", "profile=`"jamais-vu`"",
        "map=`"gz1g`"",
        "mpr=`"../test-scenes/particle-catalog-bisection/particle-zone1-flat-7x5.mpr`"",
        "mob=`"../test-scenes/particle-catalog-bisection/$mobName`""
    ) -join [Environment]::NewLine
    [IO.File]::WriteAllText((Join-Path $collections $collectionName),
        $collection + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))

    $records = @($sourceManifest.records |
        Where-Object { $selected -contains [int]$_.index })
    $bundle.variants += [ordered]@{
        name = $name
        indices = $selected
        count = $records.Count
        records = $records
        mob = [ordered]@{
            fileName = $mobName
            size = (Get-Item -LiteralPath $mobPath).Length
            sha256 = (Get-FileHash -LiteralPath $mobPath -Algorithm SHA256).Hash
        }
        collection = $collectionName
    }
}

[IO.File]::WriteAllText((Join-Path $outputRoot "BISECTION_MANIFEST.json"),
    ($bundle | ConvertTo-Json -Depth 10) + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))
$readme = @'
# Particle runtime crash bisection

All controls use the canonical 7x5 flat MPR and the catalog positions centered
around the valid borrowed Zone 1 entrance. Records preserve their canonical ID,
type, position, scale, order, and serialized field data.

Run `00-empty-control` first and wait at least 30 seconds. Then run both halves,
the quarters in a crashing half, and the rows intersecting a crashing quarter.
Record whether waiting or camera movement triggers the crash and retain the
test-run directory. Do not attribute a type until an isolated control is tested
both standalone and with its expected carrier/lifecycle where applicable.
'@
[IO.File]::WriteAllText((Join-Path $outputRoot "README.md"),
    $readme + [Environment]::NewLine, [Text.UTF8Encoding]::new($false))
Write-Output "Built bisection bundle: $outputRoot"
Write-Output "Variants: $($variants.Count)"
