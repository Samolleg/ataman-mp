param(
    [Parameter(Mandatory=$true)][string]$ModuleSdk,
    [Parameter(Mandatory=$true)][string]$MalachitePackage,
    [Parameter(Mandatory=$true)][string]$OutputDirectory,
    [Parameter(Mandatory=$true)][string]$BuildDirectory
)
$ErrorActionPreference='Stop'
$sdk=(Resolve-Path -LiteralPath $ModuleSdk).Path
$module=(Resolve-Path -LiteralPath $MalachitePackage).Path
if(Test-Path -LiteralPath $OutputDirectory){throw 'Use a fresh owned output directory.'}
$repo=Split-Path -Parent $PSScriptRoot
$env:DOTNET_CLI_HOME=Join-Path $BuildDirectory 'dotnet-home'
$env:DOTNET_SKIP_FIRST_TIME_EXPERIENCE='1'
$env:DOTNET_CLI_TELEMETRY_OPTOUT='1'
$inputs=@('Malachite.Core.dll','Malachite.UI.Wpf.dll','module.json','manifest.json')
$fingerprints=@{}
foreach($name in $inputs){$fingerprints[$name]=(Get-FileHash -LiteralPath (Join-Path $module $name)).Hash}
& dotnet publish (Join-Path $repo 'tools/malachite_host/Ataman.MalachiteHost.csproj') -c Release --self-contained false -o $OutputDirectory "-p:ModuleSdk=$sdk" "-p:MalachitePackage=$module" "-p:BaseIntermediateOutputPath=$BuildDirectory/obj/" "-p:BaseOutputPath=$BuildDirectory/bin/"
if($LASTEXITCODE -ne 0){throw 'Malachite companion build failed.'}
foreach($name in $inputs){if((Get-FileHash -LiteralPath (Join-Path $module $name)).Hash -ne $fingerprints[$name]){throw 'Module changed during build.'}}
$provenance=@{schema=1;module=@{};sdk=@{}}
foreach($name in @('Malachite.Core.dll','Malachite.UI.Wpf.dll')){$provenance.module[$name]=$fingerprints[$name]}
foreach($name in @('ModuleContracts.V1.dll','ModuleContracts.Wpf.V1.dll')){$provenance.sdk[$name]=(Get-FileHash -LiteralPath (Join-Path $sdk $name)).Hash}
$provenance.moduleManifest=$fingerprints['manifest.json']
Copy-Item -LiteralPath (Join-Path $repo 'tools/malachite_host/README.md') -Destination $OutputDirectory
$provenance|ConvertTo-Json -Depth 6|Set-Content -LiteralPath (Join-Path $OutputDirectory 'module-provenance.json') -Encoding utf8
Get-ChildItem -LiteralPath $OutputDirectory -Filter '*.pdb'|Remove-Item
Write-Output 'Malachite host built from the existing binary module; source/DB installation unchanged.'
