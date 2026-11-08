param(
    [string]$ObsPath = 'C:\Program Files\obs-studio',
    [string]$BuildDirectory = 'build/windows',
    [string]$QtRoot = '',
    [switch]$NoRender
)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $projectRoot
if (!$QtRoot) { $QtRoot = Join-Path $projectRoot '.deps/qt6' }
$env:PATH = (Join-Path $ObsPath 'bin/64bit') + ';' + (Join-Path $QtRoot 'bin') + ';' + $env:PATH
$env:QT_PLUGIN_PATH = Join-Path $QtRoot 'plugins'
if ($NoRender) { $env:QT_QPA_PLATFORM = 'offscreen' }
else { $env:QT_QPA_PLATFORM = 'windows' }
$executable = Join-Path $projectRoot "$BuildDirectory/scene-deck-runtime-tests.exe"
if (!(Test-Path -LiteralPath $executable)) { throw 'Build with scripts/build.ps1 -RuntimeTests first.' }
$arguments = @('--obs', $ObsPath)
if (!$NoRender) { $arguments += '--render' }
& $executable @arguments
if ($LASTEXITCODE) { throw "Runtime tests failed: $LASTEXITCODE" }
