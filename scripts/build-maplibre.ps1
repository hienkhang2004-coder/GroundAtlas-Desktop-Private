param(
 [Parameter(Mandatory=$true)][string]$SourceRoot,
 [Parameter(Mandatory=$true)][string]$QtRoot,
 [Parameter(Mandatory=$true)][string]$CompilerBin,
 [Parameter(Mandatory=$true)][string]$CMake,
 [Parameter(Mandatory=$true)][string]$Ninja,
 [Parameter(Mandatory=$true)][string]$BuildRoot,
 [Parameter(Mandatory=$true)][string]$InstallRoot,
 [string]$Python='python',
 [int]$Jobs=4,
 [switch]$SkipConfigure
)
$ErrorActionPreference='Stop'
$nativeSource=[IO.Path]::GetFullPath($SourceRoot)
$nativeBuild=[IO.Path]::GetFullPath($BuildRoot)
$nativeInstall=[IO.Path]::GetFullPath($InstallRoot)
$nativeQt=[IO.Path]::GetFullPath($QtRoot)
$nativeCompiler=[IO.Path]::GetFullPath($CompilerBin)
if((Get-Content -LiteralPath "$nativeSource/VERSION.txt" -Raw).Trim() -ne '3.0.0'){throw 'This integration is pinned to MapLibre Native for Qt 3.0.0'}
if(!(Test-Path -LiteralPath "$nativeSource/vendor/maplibre-native/src/mbgl/map/map.cpp")){throw 'Initialize the pinned MapLibre Native submodule first'}
& $Python (Join-Path $PSScriptRoot 'patch-maplibre.py') $nativeSource
if($LASTEXITCODE -ne 0){throw 'Pinned MapLibre compatibility patch failed'}
$env:PATH="$nativeCompiler;$nativeQt/bin;$env:PATH"
if(!$SkipConfigure){
 $nativeArguments=@('-S',$nativeSource,'-B',$nativeBuild,'-G','Ninja','-DCMAKE_BUILD_TYPE=Release',"-DCMAKE_CXX_COMPILER=$nativeCompiler/g++.exe","-DCMAKE_C_COMPILER=$nativeCompiler/gcc.exe","-DCMAKE_MAKE_PROGRAM=$Ninja","-DCMAKE_PREFIX_PATH=$nativeQt","-DCMAKE_INSTALL_PREFIX=$nativeInstall",'-DQT_VERSION_MAJOR=6','-DMLN_QT_WITH_LOCATION=OFF','-DMLN_QT_WITH_WIDGETS=ON','-DMLN_QT_WITH_INTERNAL_SQLITE=ON','-DMLN_QT_STATIC=OFF','-DMLN_WITH_WERROR=OFF','-DBUILD_TESTING=OFF','-DCMAKE_POLICY_VERSION_MINIMUM=3.5')
 & $CMake @nativeArguments
 if($LASTEXITCODE -ne 0){throw 'MapLibre configuration failed'}
}
& $CMake --build $nativeBuild --target MLNQtCore MLNQtWidgets -j $Jobs
if($LASTEXITCODE -ne 0){throw 'MapLibre build failed'}
& $CMake --install $nativeBuild
if($LASTEXITCODE -ne 0){throw 'MapLibre SDK installation failed'}
Write-Output "MapLibre Native for Qt SDK: $nativeInstall"
