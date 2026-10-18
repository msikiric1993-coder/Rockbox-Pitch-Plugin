param([string]$BuildDir,[string]$Destination)
$ErrorActionPreference='Stop'
$project=Split-Path -Parent $PSScriptRoot
if(-not $BuildDir){$BuildDir=Join-Path (Split-Path -Parent $project) 'build'}
if(-not $Destination){$Destination=Split-Path -Parent $project}
$portable=Join-Path $Destination 'Portable';$vst=Join-Path $Destination 'VST3'
New-Item -ItemType Directory -Force -Path $portable,$vst | Out-Null
Copy-Item -LiteralPath (Join-Path $BuildDir 'TDSpeedApp_artefacts/Release/TDSpeedPreview.exe') -Destination $portable -Force
Copy-Item -LiteralPath (Join-Path $BuildDir 'TDSpeedVST_artefacts/Release/VST3/TDSpeedPreview.vst3') -Destination $vst -Force -Recurse
$vswhere=Join-Path ([Environment]::GetFolderPath('ProgramFilesX86')) 'Microsoft Visual Studio/Installer/vswhere.exe'
$vs= & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
$crt=Get-ChildItem -LiteralPath (Join-Path $vs 'VC/Redist/MSVC') -Directory | Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'x64/Microsoft.VC143.CRT'} | Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
if(-not $crt){throw 'Microsoft runtime not found'}
Copy-Item -Path (Join-Path $crt '*.dll') -Destination $portable -Force
foreach($folder in @($portable,$vst)){
 Copy-Item -LiteralPath (Join-Path $project 'LICENSE'),(Join-Path $project 'README.md') -Destination $folder -Force
 $licences=Join-Path $folder 'Licences';New-Item -ItemType Directory -Force $licences | Out-Null
 $notices=@{'JUCE.txt'='JUCE/LICENSE.md';'Airwindows.txt'='Source/ThirdParty/Airwindows/LICENSE.md';'CloudSeed.txt'='Source/ThirdParty/CloudSeed/LICENSE';'Dragonfly.txt'='Source/ThirdParty/Dragonfly/LICENSE';'Freeverb3.txt'='Source/ThirdParty/Dragonfly/common/freeverb/COPYING';'ChowMatrix.txt'='Source/ThirdParty/ChowMatrix/LICENSE';'ChowTape.txt'='Source/ThirdParty/ChowTape/LICENSE';'SignalsmithBasics.txt'='Source/ThirdParty/SignalsmithBasics/LICENSE.txt';'SignalsmithDSP.txt'='Source/ThirdParty/SignalsmithDSP/LICENSE.txt';'MVerb.txt'='Source/ThirdParty/MVerb/gpl-3.0.txt';'MVerb-notice.txt'='Source/ThirdParty/MVerb/LICENSE';'Adaptations.md'='Source/ThirdParty/VERSIONS.md'}
 foreach($n in $notices.GetEnumerator()){Copy-Item -LiteralPath (Join-Path $project $n.Value) -Destination (Join-Path $licences $n.Key) -Force}
}
$hashes=Get-ChildItem -LiteralPath $portable,$vst -File -Recurse | ForEach-Object {@{path=$_.FullName.Substring($Destination.Length+1).Replace('\','/');sha256=(Get-FileHash -LiteralPath $_.FullName).Hash.ToLower()}}
$hashes | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $Destination 'Preview-SHA256.json')
Write-Output 'Staged VST3 and portable files only; no installer or archives.'
