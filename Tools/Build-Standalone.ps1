param([switch]$SkipBuild,[switch]$SkipTests,[string]$BuildDir,[string]$DependencyDir,[string]$ReleaseDir)
$ErrorActionPreference = 'Stop'
$project = Split-Path -Parent $PSScriptRoot
if (-not $BuildDir) { $BuildDir = Join-Path $project 'out/build/release' }
if (-not $DependencyDir) { $DependencyDir = Join-Path $project 'out/dependencies' }
if (-not $ReleaseDir) { $ReleaseDir = Join-Path (Split-Path -Parent $project) 'release' }
$ReleaseDir = [IO.Path]::GetFullPath($ReleaseDir)
$cmakePath = 'C:/Program Files/Microsoft Visual Studio/2022/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
if (-not (Test-Path -LiteralPath $cmakePath)) { $cmakePath=(Get-Command cmake).Source }
$ctestPath = Join-Path (Split-Path -Parent $cmakePath) 'ctest.exe'
if (-not $SkipBuild) {
    & $cmakePath -S $project -B $BuildDir -G 'Visual Studio 17 2022' -A x64 -DTDSPEED_BUILD_TESTS=ON '-DCMAKE_CXX_FLAGS=/MP4 /EHsc'
    if ($LASTEXITCODE -ne 0) { throw 'Configuration failed' }
    & $cmakePath --build $BuildDir --config Release --target TDSpeedApp TDSpeedTests TDSpeedEffectsTests TDSpeedDelayTests TDSpeedVST_VST3 TDSpeedEngineAudit TDSpeedPluginHostSmoke TDSpeedDawTests -j 4
    if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
}
if (-not $SkipTests) { & $ctestPath --test-dir $BuildDir -C Release --output-on-failure; if ($LASTEXITCODE -ne 0) { throw 'Release tests failed' } }
New-Item -ItemType Directory -Force $ReleaseDir | Out-Null
$stage = Join-Path $ReleaseDir 'portable/TDSpeed Preview'
New-Item -ItemType Directory -Force $stage | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'TDSpeedApp_artefacts/Release/TDSpeedPreview.exe') -Destination $stage -Force
$vswhere = Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vsInstall = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$redistRoot = Join-Path $vsInstall 'VC/Redist/MSVC'
$crt = Get-ChildItem -LiteralPath $redistRoot -Directory | Sort-Object Name -Descending | ForEach-Object { Join-Path $_.FullName 'x64/Microsoft.VC143.CRT' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $crt) { throw 'MSVC x64 runtime directory not found' }
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $project 'Installer/Read me.txt'),(Join-Path $project 'Installer/ThirdPartyNotice.txt'),(Join-Path $project 'README.md'),(Join-Path $project 'CHANGELOG.md'),(Join-Path $project 'LICENSE') -Destination $stage -Force
Copy-Item -LiteralPath (Join-Path $project 'docs') -Destination $stage -Force -Recurse
$licences = Join-Path $stage 'Licences';New-Item -ItemType Directory -Force $licences | Out-Null
$notices = @{
 'JUCE.txt'='JUCE/LICENSE.md';'AGPL-3.0.txt'='LICENSE';'Airwindows.txt'='Source/ThirdParty/Airwindows/LICENSE.md';'CloudSeed.txt'='Source/ThirdParty/CloudSeed/LICENSE';
 'Dragonfly.txt'='Source/ThirdParty/Dragonfly/LICENSE';'Freeverb3.txt'='Source/ThirdParty/Dragonfly/common/freeverb/COPYING';
 'ChowMatrix.txt'='Source/ThirdParty/ChowMatrix/LICENSE';'ChowTape.txt'='Source/ThirdParty/ChowTape/LICENSE';
 'SignalsmithBasics.txt'='Source/ThirdParty/SignalsmithBasics/LICENSE.txt';'SignalsmithDSP.txt'='Source/ThirdParty/SignalsmithDSP/LICENSE.txt';
 'MVerb-GPL-3.0.txt'='Source/ThirdParty/MVerb/gpl-3.0.txt';'MVerb-notice.txt'='Source/ThirdParty/MVerb/LICENSE';'Dependency-revisions.md'='Source/ThirdParty/VERSIONS.md'
}
foreach ($entry in $notices.GetEnumerator()) { Copy-Item -LiteralPath (Join-Path $project $entry.Value) -Destination (Join-Path $licences $entry.Key) -Force }
$sourceZip = Join-Path $stage 'TDSpeed-Source-0.4.1.zip'
python (Join-Path $PSScriptRoot 'Archive-Source.py') $project $sourceZip
if ($LASTEXITCODE -ne 0) { throw 'Source archive verification failed' }
Copy-Item -LiteralPath $sourceZip -Destination $ReleaseDir -Force
$pluginDir = Join-Path $ReleaseDir 'VST3';New-Item -ItemType Directory -Force $pluginDir | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'TDSpeedVST_artefacts/Release/VST3/TDSpeedPreview.vst3') -Destination $pluginDir -Force -Recurse
# App-local CRT also accompanies the VST3 module for hosts without a system runtime.
Copy-Item -Path (Join-Path $crt '*.dll') -Destination (Join-Path $pluginDir 'TDSpeedPreview.vst3/Contents/x86_64-win') -Force
Copy-Item -LiteralPath $licences -Destination $pluginDir -Force -Recurse
Copy-Item -LiteralPath $sourceZip -Destination $pluginDir -Force
Copy-Item -LiteralPath (Join-Path $project 'README.md') -Destination $pluginDir -Force
Compress-Archive -Path (Join-Path $pluginDir '*') -DestinationPath (Join-Path $ReleaseDir 'TDSpeed-VST3-0.4.1.zip') -Force
$driverDir = Join-Path $DependencyDir 'VBCABLE';$driver = Join-Path $driverDir 'VBCABLE_Setup_x64.exe';$inno = Join-Path $DependencyDir 'Inno/ISCC.exe'
if ((Get-AuthenticodeSignature -LiteralPath $driver).Status -ne 'Valid') { throw 'Invalid VB-Audio signature' }
if ((Get-FileHash -LiteralPath $driver).Hash -ne '734C35DFA6D98F48782A451633CEB471166EC70D60482FD89A1123D0EE3C4F41') { throw 'Unexpected VB-CABLE package revision' }
& $inno "/DAppStage=$stage" "/DDriverDir=$driverDir" "/O$ReleaseDir" (Join-Path $project 'Installer/TDSpeed.iss')
if ($LASTEXITCODE -ne 0) { throw 'Installer compilation failed' }
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath (Join-Path $ReleaseDir 'TDSpeed-Portable-0.4.1.zip') -Force
$manifest = Get-ChildItem -LiteralPath $ReleaseDir -File -Recurse | Where-Object { $_.Name -ne 'SHA256.json' } | ForEach-Object { @{path=$_.FullName.Substring($ReleaseDir.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLower()} }
$manifest | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $ReleaseDir 'SHA256.json')
python (Join-Path $PSScriptRoot 'Verify-Release.py') $project $BuildDir $ReleaseDir
if ($LASTEXITCODE -ne 0) { throw 'Final package/source verification failed' }
Write-Output "Release packages built in $ReleaseDir"
