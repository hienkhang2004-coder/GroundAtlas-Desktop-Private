param(
 [Parameter(Mandatory=$true)][string]$QtRoot,
 [Parameter(Mandatory=$true)][string]$CompilerBin,
 [string]$CMake='cmake',
 [string]$MapLibreRoot='',
 [switch]$SkipTests,
 [switch]$Package
)
$ErrorActionPreference='Stop'
$nativeRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$nativeQt=[IO.Path]::GetFullPath($QtRoot)
$nativeCompiler=[IO.Path]::GetFullPath($CompilerBin)
if(!$MapLibreRoot){$MapLibreRoot=Join-Path ([Environment]::GetFolderPath('MyDocuments')) 'Codex/ga-sdk/maplibre-install'}
$nativeMapLibre=[IO.Path]::GetFullPath($MapLibreRoot)
if(!(Test-Path -LiteralPath "$nativeQt/bin/qtpaths.exe")){throw 'QtRoot must contain bin/qtpaths.exe'}
if(!(Test-Path -LiteralPath "$nativeCompiler/g++.exe")){throw 'CompilerBin must contain g++.exe'}
if(!(Test-Path -LiteralPath "$nativeMapLibre/lib/cmake/QMapLibre/QMapLibreConfig.cmake")){throw 'Build the pinned MapLibre Native for Qt SDK with scripts/build-maplibre.ps1 and pass -MapLibreRoot'}
$env:PATH="$nativeCompiler;$nativeQt/bin;$nativeMapLibre/bin;$env:PATH"
& $CMake -S $nativeRoot -B "$nativeRoot/build" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=$nativeQt" "-DQMapLibre_DIR=$nativeMapLibre/lib/cmake/QMapLibre" "-DCMAKE_CXX_COMPILER=$nativeCompiler/g++.exe" -DBUILD_TESTING=ON
if($LASTEXITCODE -ne 0){throw 'CMake configuration failed'}
& $CMake --build "$nativeRoot/build" -j 4
if($LASTEXITCODE -ne 0){throw 'Native build failed'}
if(!$SkipTests){
 New-Item -ItemType Directory -Path "$nativeRoot/validation" -Force | Out-Null
 & "$nativeRoot/build/native_core_tests.exe" -o "$nativeRoot/validation/core-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native core tests failed'}
 & "$nativeRoot/build/native_data_tests.exe" -o "$nativeRoot/validation/data-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native data tests failed'}
 & "$nativeRoot/build/native_ui_tests.exe" -platform offscreen -o "$nativeRoot/validation/ui-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native UI tests failed'}
 & "$nativeRoot/build/native_import_tests.exe" -platform offscreen -o "$nativeRoot/validation/import-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native import tests failed'}
 & "$nativeRoot/build/native_session_tests.exe" -o "$nativeRoot/validation/session-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native session tests failed'}
 & "$nativeRoot/build/native_presentation_tests.exe" -platform offscreen -o "$nativeRoot/validation/presentation-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native presentation tests failed'}
 & "$nativeRoot/build/native_vector_context_tests.exe" -o "$nativeRoot/validation/vector-context-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native vector context tests failed'}
 & "$nativeRoot/build/native_v012_tests.exe" -platform offscreen -o "$nativeRoot/validation/v012-tests.txt,txt"
 if($LASTEXITCODE -ne 0){throw 'Native v012 tests failed'}
}
if($Package){
 $nativePackage=Join-Path $nativeRoot 'portable-1.3.0'
 New-Item -ItemType Directory -Path $nativePackage -Force | Out-Null
 Copy-Item -LiteralPath "$nativeRoot/build/GroundAtlas.exe" -Destination "$nativePackage/GroundAtlas.exe" -Force
 Copy-Item -LiteralPath "$nativeRoot/data" -Destination $nativePackage -Recurse -Force
 & "$nativeQt/bin/windeployqt.exe" --release --no-translations --no-opengl-sw --compiler-runtime --qtpaths "$nativeQt/bin/qtpaths.exe" --dir $nativePackage "$nativePackage/GroundAtlas.exe"
 if($LASTEXITCODE -ne 0){throw 'Qt deployment failed'}
 Copy-Item -LiteralPath "$nativeMapLibre/bin/QMapLibre.dll" -Destination "$nativePackage/QMapLibre.dll" -Force
 Copy-Item -LiteralPath "$nativeRoot/README.txt" -Destination "$nativePackage/README.txt" -Force
 if(Test-Path -LiteralPath "$nativeRoot/licenses"){Copy-Item -LiteralPath "$nativeRoot/licenses" -Destination $nativePackage -Recurse -Force}
 Get-FileHash -LiteralPath "$nativePackage/GroundAtlas.exe" | Format-List
 Write-Output "Packaged native application: $nativePackage"
}
