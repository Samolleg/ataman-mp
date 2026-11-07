param(
    [Parameter(Mandatory=$true)][string]$SourceRepositoryPath,
    [Parameter(Mandatory=$true)][string]$RendererRepositoryPath,
    [Parameter(Mandatory=$true)][string]$BuildRoot,
    [string]$ShaderStore,
    [string]$PrebuiltShaderRoot,
    [string]$MSBuild='C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/MSBuild/Current/Bin/MSBuild.exe'
)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$lock=Get-Content -LiteralPath (Join-Path $repo 'third_party/eirebuild64-renderer/source.lock.json') -Raw -Encoding UTF8|ConvertFrom-Json
$target=[IO.Path]::GetFullPath($BuildRoot)
if(Test-Path -LiteralPath $target){throw 'BuildRoot must be a new owned directory.'}
if(-not $target.StartsWith(([IO.Path]::GetFullPath((Join-Path $repo 'work'))+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'BuildRoot must be below this checkout work/.'}
foreach($patch in $lock.patches){
    $path=Join-Path $PSScriptRoot ('patches/'+$patch.path)
    if((Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash -ne $patch.sha256){throw ('Patch lock mismatch: '+$patch.path)}
}
& git -C $SourceRepositoryPath cat-file -e ($lock.sourceCommit+'^{commit}')
if($LASTEXITCODE){throw 'Pinned adapter commit unavailable.'}
& git -C $RendererRepositoryPath cat-file -e ($lock.dataCommit+'^{commit}')
if($LASTEXITCODE){throw 'Pinned material data commit unavailable.'}
New-Item -ItemType Directory -Path $target|Out-Null
foreach($folder in @('vendor','host','bin','tmp','shader-store','logs')){New-Item -ItemType Directory -Path (Join-Path $target $folder)|Out-Null}
$vendor=Join-Path $target 'vendor'
$tar=Join-Path $target 'vendor.tar'
& git -C $SourceRepositoryPath archive --format=tar -o $tar $lock.sourceCommit @($lock.archivePaths)
if($LASTEXITCODE){throw 'Pinned adapter archive failed.'}
Push-Location $target
try { & tar -xf vendor.tar -C vendor;if($LASTEXITCODE){throw 'Vendor extraction failed.'} }
finally {Pop-Location}
$previousCeiling=$env:GIT_CEILING_DIRECTORIES
try {
    # An archive may be nested below the mapper checkout. Do not let Git's
    # repository-relative prefix silently skip paths outside that prefix.
    $env:GIT_CEILING_DIRECTORIES=$target
    foreach($patch in $lock.patches){
        & git -C $vendor apply --no-index --ignore-space-change (Join-Path $PSScriptRoot ('patches/'+$patch.path))
        if($LASTEXITCODE){throw ('Patch failed: '+$patch.path)}
    }
} finally { $env:GIT_CEILING_DIRECTORIES=$previousCeiling }
if([IO.File]::ReadAllText((Join-Path $vendor 'OwnedRenderer/OwnedRenderer.vcxproj')) -match 'Import Project="DlssSdk.props"|Source\\dlss_backend.cpp'){
    throw 'No-NGX patch was not applied to the extracted renderer.'
}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'host') -Destination $target -Recurse -Force
Copy-Item -LiteralPath (Join-Path $repo 'source/render/rebuild64_abi.h') -Destination (Join-Path $target 'host/rebuild64_abi.h')
foreach($name in @('CorePerf.h','GameJournal.h')){Copy-Item -LiteralPath (Join-Path $PSScriptRoot ('host/'+$name)) -Destination (Join-Path $vendor ('Game/'+$name))}
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'no_ngx.cpp'),(Join-Path $PSScriptRoot 'backend.vcxproj') -Destination $target
$dataTar=Join-Path $target 'materials.tar'
& git -C $RendererRepositoryPath archive --format=tar -o $dataTar $lock.dataCommit $lock.dataArchivePath
if($LASTEXITCODE){throw 'Pinned material archive failed.'}
Push-Location $target
try { & tar -xf materials.tar;if($LASTEXITCODE){throw 'Material extraction failed.'} }
finally {Pop-Location}
$env:TEMP=Join-Path $target 'tmp';$env:TMP=$env:TEMP
$env:EID3D11_SHADER_STORE=Join-Path $target 'shader-store'
if($ShaderStore){
    $resolvedStore=[IO.Path]::GetFullPath($ShaderStore)
    if(-not $resolvedStore.StartsWith(([IO.Path]::GetFullPath((Join-Path $repo 'work'))+'\'),[StringComparison]::OrdinalIgnoreCase)){throw 'ShaderStore must be below this checkout work/.'}
    $env:EID3D11_SHADER_STORE=$resolvedStore
}
$hostRoot=$target.Replace('\','/')+'/'
$projects=@('vendor/OwnedRenderer/RendererShaders.vcxproj','vendor/OwnedRenderer/OwnedRenderer.vcxproj','backend.vcxproj')
if($PrebuiltShaderRoot) {
    # Explicit reuse of a previously validated table; the caller must verify
    # unchanged shader producers and retain their hashes in the build receipt.
    # Only the table resources are copied. Runtime vendor objects are rebuilt.
    $prebuilt=(Resolve-Path -LiteralPath $PrebuiltShaderRoot).Path
    $shaderOutput=Join-Path $vendor 'out/x64/Release/RendererShaders'
    New-Item -ItemType Directory -Path $shaderOutput -Force|Out-Null
    foreach($name in @('eid3d11_shaders.bin','eid3d11_shaders.res','eid3d11_camp_art.res')) {
        $file=Join-Path $prebuilt $name
        if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Prebuilt shader resource missing: $name"}
        Copy-Item -LiteralPath $file -Destination $shaderOutput
        Get-FileHash -LiteralPath $file -Algorithm SHA256
    }
    $projects=@('vendor/OwnedRenderer/OwnedRenderer.vcxproj','backend.vcxproj')
}
foreach($project in $projects) {
    $log=Join-Path $target ('logs/'+[IO.Path]::GetFileNameWithoutExtension($project)+'.log')
    & $MSBuild (Join-Path $target $project) /p:Configuration=Release /p:Platform=x64 "/p:AtamanHostRoot=$hostRoot" /m:1 /v:minimal *> $log
    if($LASTEXITCODE){throw ('Build failed: '+$project+'; '+$log)}
}
Copy-Item -LiteralPath (Join-Path $target 'Data/eid3d_materials') -Destination (Join-Path $target 'bin') -Recurse
$licenses=Join-Path $target 'bin/licenses/rebuild64'
New-Item -ItemType Directory -Path $licenses -Force|Out-Null
$renderer=Join-Path $vendor 'external/owned-renderer'
foreach($name in @('LICENSE.md','NOTICE.md','PROVENANCE.md')) {Copy-Item -LiteralPath (Join-Path $renderer $name) -Destination $licenses}
foreach($item in @(@('imgui','LICENSE.txt'),@('meshoptimizer','LICENSE.md'),@('smaa','LICENSE.txt'),@('fsr2','LICENSE.txt'))) {
    Copy-Item -LiteralPath (Join-Path $renderer ('ThirdParty/'+$item[0]+'/'+$item[1])) -Destination (Join-Path $licenses ($item[0]+'-'+$item[1]))
}
Copy-Item -LiteralPath (Join-Path $renderer 'Assets/terrain2/PROVENANCE.txt') -Destination (Join-Path $licenses 'terrain2-PROVENANCE.txt')
if(Get-ChildItem -LiteralPath (Join-Path $target 'bin') -File -Recurse|Where-Object {$_.Name -match '^(nvngx|nvsdk|sl\.|streamline|nvapi)'}){throw 'NVIDIA files are forbidden.'}
Get-FileHash -LiteralPath (Join-Path $target 'bin/ataman_rebuild64.dll') -Algorithm SHA256
Write-Output ('Backend and renderer data ready in '+(Join-Path $target 'bin'))
