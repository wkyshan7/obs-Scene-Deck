param(
    [string]$ObsPath = 'C:\Program Files\obs-studio',
    [string]$QtRoot = '',
    [string]$ObsSource = '',
    [string]$BuildDirectory = 'build/windows',
    [string]$Configuration = 'RelWithDebInfo',
    [string]$Ninja = '',
    [switch]$RuntimeTests
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
if (!$ObsSource) { $ObsSource = Join-Path $projectRoot '.deps/obs-studio' }
if (!$QtRoot) { $QtRoot = Join-Path $projectRoot '.deps/qt6' }

# Import a project-local toolchain if Visual Studio's developer shell isn't active.
if (!(Get-Command cl.exe -ErrorAction SilentlyContinue)) {
    $portableSetup = Join-Path $projectRoot '.deps/tools/msvc/setup_x64.bat'
    if (!(Test-Path -LiteralPath $portableSetup)) {
        throw 'Run from an x64 Visual Studio developer PowerShell, or prepare .deps/tools/msvc/setup_x64.bat.'
    }
    $environmentLines = & $env:ComSpec /d /s /c "`"`"$portableSetup`" >nul && set`""
    foreach ($line in $environmentLines) {
        if ($line -match '^([^=]+)=(.*)$') {
            [Environment]::SetEnvironmentVariable($Matches[1], $Matches[2], 'Process')
        }
    }
}
if (!(Test-Path -LiteralPath (Join-Path $ObsSource 'libobs/obs.h'))) {
    throw 'Matching OBS headers are missing. See scripts/prepare-sdk.ps1 and README.md.'
}
if (!(Test-Path -LiteralPath (Join-Path $QtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw 'Qt development files are missing. Set -QtRoot or run scripts/prepare-sdk.ps1.'
}
$importDirectory = Join-Path $projectRoot '.deps/obs-imports'
New-Item -ItemType Directory -Path $importDirectory -Force | Out-Null
foreach ($library in @('obs', 'obs-frontend-api')) {
    $dllPath = Join-Path $ObsPath "bin/64bit/$library.dll"
    if (!(Test-Path -LiteralPath $dllPath)) { throw "Missing OBS library: $dllPath" }
    $exports = & dumpbin.exe /nologo /exports $dllPath
    if ($LASTEXITCODE) { throw "dumpbin failed: $dllPath" }
    $names = @($exports | ForEach-Object {
        if ($_ -match '^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(\S+)') { $Matches[1] }
    })
    if (!$names.Count) { throw "No exports found: $dllPath" }
    $definition = Join-Path $importDirectory "$library.def"
    @("LIBRARY $library.dll", 'EXPORTS') + $names | Set-Content -LiteralPath $definition -Encoding ascii
    & lib.exe /nologo /machine:x64 "/def:$definition" "/out:$(Join-Path $importDirectory "$library.lib")"
    if ($LASTEXITCODE) { throw "Import library generation failed: $library" }
}
$arguments = @('-S', $projectRoot, '-B', $BuildDirectory, '-G', 'Ninja',
    "-DCMAKE_BUILD_TYPE=$Configuration", "-DCMAKE_PREFIX_PATH=$QtRoot",
    "-DSCENE_DECK_OBS_SOURCE=$ObsSource", "-DSCENE_DECK_OBS_LIBS=$importDirectory")
if ($Ninja) { $arguments += "-DCMAKE_MAKE_PROGRAM=$Ninja" }
if ($RuntimeTests) { $arguments += '-DSCENE_DECK_RUNTIME_TESTS=ON' }
& cmake @arguments
if ($LASTEXITCODE) { throw 'CMake configure failed' }
& cmake --build $BuildDirectory --config $Configuration --parallel
if ($LASTEXITCODE) { throw 'Plugin build failed' }
& ctest --test-dir $BuildDirectory -C $Configuration --output-on-failure
if ($LASTEXITCODE) { throw 'Tests failed' }
& cmake --install $BuildDirectory --config $Configuration --prefix (Join-Path $projectRoot 'dist')
if ($LASTEXITCODE) { throw 'Packaging failed' }
$versionLine = Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') | Where-Object { $_ -match '^CMAKE_PROJECT_VERSION:STATIC=' }
if (!$versionLine) { throw 'Could not determine the package version from CMakeCache.txt' }
$packageVersion = ($versionLine -split '=', 2)[1]
$archivePath = Join-Path $projectRoot "dist/obs-scene-deck-$packageVersion-windows-x64.zip"
Compress-Archive -Path (Join-Path $projectRoot 'dist/obs-scene-deck') -DestinationPath $archivePath -Force
Write-Output "Built package: $archivePath"
