param(
    [Parameter(Mandatory = $true)]
    [string]$MobExplorer,

    [Parameter(Mandatory = $true)]
    [string]$BaseMob,

    [Parameter(Mandatory = $true)]
    [string]$DonorMob,

    [Parameter(Mandatory = $true)]
    [string]$OutputMob,

    [double]$SpacingWorldUnits = 3.0,
    [int]$Columns = 10,
    [double]$OriginX = 16.0,
    [double]$OriginY = 16.0,
    [double]$OriginZ = 0.0,
    [double]$Scale = 1.0
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$expectedMobExplorerSha256 =
    "253CDF9AAEE74640A219283A210C2A026A644161CE06F4DF25A297E9CAD4ECDD"
$catalogFirstType = 8192
$catalogLastType = 8274
$catalogIdBase = 900000
$terrainTileWidthWorldUnits = 2.0

$particleNames = [ordered]@{
    8192 = "Fireball"
    8193 = "Campfire"
    8194 = "Fireblast"
    8195 = "Fire"
    8196 = "Smoke"
    8197 = "Vulcansmoke"
    8198 = "Healing"
    8199 = "Poisonfog"
    8200 = "Aggressionfog"
    8201 = "Geyser"
    8202 = "Tornado"
    8203 = "Casting"
    8204 = "Nuke"
    8205 = "Bansheecasting"
    8206 = "Mushroom"
    8207 = "Blood1"
    8208 = "Firewall"
    8209 = "Firearrow"
    8210 = "Acidray"
    8211 = "Bluegas"
    8212 = "Link"
    8213 = "Sphereacid"
    8214 = "Sphereelectricity"
    8215 = "Spherefire"
    8216 = "Clayring"
    8217 = "Teleport"
    8218 = "Antimagic"
    8219 = "Modifier1"
    8220 = "Modifier2"
    8221 = "Modifier3"
    8222 = "Modifier4"
    8223 = "Modifier5"
    8224 = "Modifier6"
    8225 = "Modifier7"
    8226 = "Modifier8"
    8227 = "Modifier9"
    8228 = "Modifier10"
    8229 = "Modifier11"
    8230 = "Modifier12"
    8231 = "Castingfire"
    8232 = "Castingelectricity"
    8233 = "Castingacid"
    8234 = "Castingdivination"
    8235 = "Castingillusion"
    8236 = "Castingdomination"
    8237 = "Castingenchantment"
    8238 = "Castinghealing"
    8239 = "Castingfailed"
    8240 = "Lightningblast"
    8241 = "Blood2"
    8242 = "Blood3"
    8243 = "Blood4"
    8244 = "A4bloodred"
    8245 = "A4bloodgreen"
    8246 = "A4bloodblue"
    8247 = "A4bloodblack"
    8248 = "Zoneexit"
    8249 = "Path"
    8250 = "Pathdestination"
    8251 = "Pathfailed"
    8252 = "Moshka"
    8253 = "Portalstar"
    8254 = "Portal"
    8255 = "Cylinder1"
    8256 = "Cylinder2"
    8257 = "Firestar"
    8258 = "Acidstar"
    8259 = "Sparks"
    8260 = "Visionstar1"
    8261 = "Visionstar2"
    8262 = "Visionstar3"
    8263 = "Regeneration"
    8264 = "Silence"
    8265 = "Feeblemind"
    8266 = "Feetcloud1"
    8267 = "Feetcloud2"
    8268 = "Visionstar4"
    8269 = "Ballofstars"
    8270 = "Rickarrow"
    8271 = "Cursestars"
    8272 = "Curseholder"
    8273 = "Starttrans"
    8274 = "Transform"
}

function Resolve-ExistingFile([string]$Path, [string]$Label) {
    if (-not [IO.File]::Exists($Path)) {
        throw "$Label does not exist: $Path"
    }
    return (Resolve-Path -LiteralPath $Path).Path
}

function Get-Field($Node, [string]$Name) {
    $field = $Node.Items | Where-Object { $_.Id.ToString() -eq $Name } |
        Select-Object -First 1
    if ($null -eq $field) {
        throw "Particle donor is missing required field $Name"
    }
    return $field
}

function Set-PlotField($Field, [double]$X, [double]$Y, [double]$Z, $DataField) {
    [float[]]$values = @([float]$X, [float]$Y, [float]$Z)
    $bytes = New-Object byte[] 12
    [Buffer]::BlockCopy($values, 0, $bytes, 0, 12)
    $DataField.SetValue($Field, $bytes)
}

if ($Columns -lt 1) {
    throw "Columns must be positive"
}
if ($SpacingWorldUnits -le 0.0) {
    throw "SpacingWorldUnits must be positive"
}
if ($Scale -le 0.0) {
    throw "Scale must be positive"
}
if ($particleNames.Count -ne ($catalogLastType - $catalogFirstType + 1)) {
    throw "Particle catalog is not contiguous"
}

$mobExplorerPath = Resolve-ExistingFile $MobExplorer "MobExplorer"
$baseMobPath = Resolve-ExistingFile $BaseMob "Base MOB"
$donorMobPath = Resolve-ExistingFile $DonorMob "Donor MOB"
$toolHash = (Get-FileHash -LiteralPath $mobExplorerPath -Algorithm SHA256).Hash
if ($toolHash -ne $expectedMobExplorerSha256) {
    throw "Unexpected MobExplorer build: $toolHash"
}

$outputFullPath = [IO.Path]::GetFullPath($OutputMob)
if ($outputFullPath -eq $baseMobPath -or $outputFullPath -eq $donorMobPath) {
    throw "Output MOB must not overwrite either input"
}
$outputDirectory = [IO.Path]::GetDirectoryName($outputFullPath)
if ([string]::IsNullOrEmpty($outputDirectory)) {
    throw "Output MOB must have a parent directory"
}
[IO.Directory]::CreateDirectory($outputDirectory) | Out-Null

$assembly = [Reflection.Assembly]::LoadFile($mobExplorerPath)
$mobType = $assembly.GetType("EILib.MobFile", $true)
$sectionType = $assembly.GetType("EILib.MobFileSection", $true)
$constructor = $mobType.GetConstructor([Type[]]@([string]))
if ($null -eq $constructor) {
    throw "Expected MobFile(string) constructor was not found"
}
$dataField = $sectionType.GetField(
    "_data", [Reflection.BindingFlags]"NonPublic,Instance")
if ($null -eq $dataField) {
    throw "Expected MobFileSection payload field was not found"
}

$donor = $constructor.Invoke([object[]]@([string]$donorMobPath))
$donor.OpenObjectsSection()
$template = $donor.CurrentSection.Items |
    Where-Object { $_.Id.ToString() -eq "PARTICL" } |
    Select-Object -First 1
if ($null -eq $template) {
    throw "Donor MOB contains no particle record"
}
foreach ($required in @(
        "PARTICL_ID", "PARTICL_POSITION", "PARTICL_NAME",
        "PARTICL_COMMENTS", "PARTICL_TYPE", "PARTICL_SCALE")) {
    [void](Get-Field $template $required)
}

$catalogMob = $constructor.Invoke([object[]]@([string]$baseMobPath))
$catalogMob.OpenObjectsSection()
if ($catalogMob.CurrentSection.Id.ToString() -ne "OBJECTSECTION") {
    throw "Base MOB did not expose an object section"
}
if ($catalogMob.CurrentSection.Items.Count -ne 0) {
    throw "Base MOB object section must be empty"
}

$manifestRecords = @()
$index = 0
foreach ($entry in $particleNames.GetEnumerator()) {
    $typeId = [int]$entry.Key
    $name = [string]$entry.Value
    if ($typeId -ne $catalogFirstType + $index) {
        throw "Particle catalog ordering is not contiguous at $typeId"
    }
    $row = [Math]::Floor($index / $Columns)
    $column = $index % $Columns
    $x = $OriginX + $column * $SpacingWorldUnits
    $y = $OriginY + $row * $SpacingWorldUnits
    $particleId = $catalogIdBase + $index

    $node = $template.Clone($catalogMob.CurrentSection)
    (Get-Field $node "PARTICL_ID").SetNumber($particleId)
    Set-PlotField (Get-Field $node "PARTICL_POSITION") $x $y $OriginZ $dataField
    (Get-Field $node "PARTICL_NAME").SetString(
        "Particle $typeId $name")
    (Get-Field $node "PARTICL_COMMENTS").SetString(
        "EI Mapper particle catalog row=$row column=$column")
    (Get-Field $node "PARTICL_TYPE").SetNumber($typeId)
    (Get-Field $node "PARTICL_SCALE").SetNumber($Scale)
    $catalogMob.CurrentSection.Items.Add($node)

    $manifestRecords += [ordered]@{
        index = $index
        row = $row
        column = $column
        particleId = $particleId
        typeId = $typeId
        typeHex = "0x{0:X4}" -f $typeId
        name = $name
        position = @($x, $y, $OriginZ)
        scale = $Scale
    }
    $index++
}

$catalogMob.SaveMobFile($outputFullPath)

$reopened = $constructor.Invoke([object[]]@([string]$outputFullPath))
$reopened.OpenObjectsSection()
$reopenedParticles = @($reopened.CurrentSection.Items |
    Where-Object { $_.Id.ToString() -eq "PARTICL" })
if ($reopenedParticles.Count -ne $particleNames.Count) {
    throw "Reopened MOB has $($reopenedParticles.Count) particles, expected $($particleNames.Count)"
}
for ($checkIndex = 0; $checkIndex -lt $reopenedParticles.Count; $checkIndex++) {
    $node = $reopenedParticles[$checkIndex]
    $actualId = [int](Get-Field $node "PARTICL_ID").GetNumber()
    $actualType = [int](Get-Field $node "PARTICL_TYPE").GetNumber()
    if ($actualId -ne $catalogIdBase + $checkIndex -or
            $actualType -ne $catalogFirstType + $checkIndex) {
        throw "Reopened particle $checkIndex does not match the generation plan"
    }
}

$outputItem = Get-Item -LiteralPath $outputFullPath
$outputHash = (Get-FileHash -LiteralPath $outputFullPath -Algorithm SHA256).Hash
$manifestPath = [IO.Path]::ChangeExtension($outputFullPath, ".json")
$manifest = [ordered]@{
    format = "ei-particle-catalog-grid-v1"
    purpose = "Deterministic EI Mapper visual catalog; not proof of native standalone behavior"
    output = [ordered]@{
        fileName = $outputItem.Name
        size = $outputItem.Length
        sha256 = $outputHash
    }
    inputs = [ordered]@{
        mobExplorer = [ordered]@{
            fileName = [IO.Path]::GetFileName($mobExplorerPath)
            sha256 = $toolHash
        }
        baseMob = [ordered]@{
            fileName = [IO.Path]::GetFileName($baseMobPath)
            sha256 = (Get-FileHash -LiteralPath $baseMobPath -Algorithm SHA256).Hash
        }
        donorMob = [ordered]@{
            fileName = [IO.Path]::GetFileName($donorMobPath)
            sha256 = (Get-FileHash -LiteralPath $donorMobPath -Algorithm SHA256).Hash
        }
    }
    layout = [ordered]@{
        count = $particleNames.Count
        columns = $Columns
        rows = [int][Math]::Ceiling($particleNames.Count / [double]$Columns)
        terrainTileWidthWorldUnits = $terrainTileWidthWorldUnits
        spacingWorldUnits = $SpacingWorldUnits
        spacingTiles = $SpacingWorldUnits / $terrainTileWidthWorldUnits
        origin = @($OriginX, $OriginY, $OriginZ)
    }
    limitations = @(
        "The MOB intentionally contains standalone PARTICL records only.",
        "Carrier-dependent and transient effects need separate native owner fixtures.",
        "Large effects can overlap; regenerate with a larger SpacingWorldUnits value."
    )
    records = $manifestRecords
}
$json = $manifest | ConvertTo-Json -Depth 8
[IO.File]::WriteAllText($manifestPath, $json + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Write-Output "Generated $outputFullPath"
Write-Output "Generated $manifestPath"
Write-Output "Particles: $($particleNames.Count)"
Write-Output "SHA256: $outputHash"
