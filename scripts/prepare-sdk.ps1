param([string]$ObsVersion = '33.0.1')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$dependencyRoot = Join-Path $projectRoot '.deps'
New-Item -ItemType Directory -Path $dependencyRoot -Force | Out-Null
$sourceRoot = Join-Path $dependencyRoot 'obs-studio'
if (!(Test-Path -LiteralPath (Join-Path $sourceRoot 'libobs/obs.h'))) {
    & git clone --depth 1 --branch $ObsVersion https://github.com/obsproject/obs-studio.git $sourceRoot
    if ($LASTEXITCODE) { throw 'Could not download official OBS sources' }
}
$actualVersion = (& git -C $sourceRoot describe --tags --exact-match HEAD).Trim()
if ($LASTEXITCODE -or $actualVersion -ne $ObsVersion) {
    throw "Existing OBS headers are $actualVersion, requested $ObsVersion. Use a matching checkout."
}
$presets = Get-Content -LiteralPath (Join-Path $sourceRoot 'CMakePresets.json') -Raw | ConvertFrom-Json
$dependency = ($presets.configurePresets | Where-Object name -eq 'dependencies').vendor.'obsproject.com/obs-studio'.dependencies.qt6
$qtRoot = Join-Path $dependencyRoot 'qt6'
$marker = Join-Path $qtRoot '.scene-deck-sdk.sha256'
if ((Test-Path -LiteralPath $marker) -and
    (Get-Content -LiteralPath $marker -Raw).Trim() -eq $dependency.hashes.'windows-x64') {
    Write-Output 'Matching official Qt SDK already prepared.'
    return
}
$archiveName = "windows-deps-qt6-$($dependency.version)-x64.zip"
$archive = Join-Path $dependencyRoot $archiveName
if (!(Test-Path -LiteralPath $archive)) {
    Invoke-WebRequest "$($dependency.baseUrl)/$($dependency.version)/$archiveName" -OutFile $archive
}
if ((Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant() -ne $dependency.hashes.'windows-x64') {
    throw 'Official Qt SDK SHA256 verification failed. No files were extracted.'
}
Expand-Archive -LiteralPath $archive -DestinationPath $qtRoot -Force
$dependency.hashes.'windows-x64' | Set-Content -LiteralPath $marker -Encoding ascii
Write-Output "Official OBS $ObsVersion headers and matching Qt SDK prepared in .deps."
