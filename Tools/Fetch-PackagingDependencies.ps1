param([string]$DependencyDir = (Join-Path (Split-Path -Parent $PSScriptRoot) 'out/dependencies'))
$ErrorActionPreference = 'Stop'
New-Item -ItemType Directory -Force $DependencyDir | Out-Null
$driverZip = Join-Path $DependencyDir 'VBCABLE_Driver_Pack45.zip'
$driverDir = Join-Path $DependencyDir 'VBCABLE'
if (-not (Test-Path -LiteralPath (Join-Path $driverDir 'VBCABLE_Setup_x64.exe'))) {
    Invoke-WebRequest 'https://download.vb-audio.com/Download_CABLE/VBCABLE_Driver_Pack45.zip' -OutFile $driverZip
    Expand-Archive -LiteralPath $driverZip -DestinationPath $driverDir -Force
}
$driver = Join-Path $driverDir 'VBCABLE_Setup_x64.exe'
if ((Get-FileHash -LiteralPath $driver).Hash -ne '734C35DFA6D98F48782A451633CEB471166EC70D60482FD89A1123D0EE3C4F41') { throw 'Unexpected VB-CABLE revision; review before packaging' }
if ((Get-AuthenticodeSignature -LiteralPath $driver).Status -ne 'Valid') { throw 'Invalid VB-Audio setup signature' }
$innoDir = Join-Path $DependencyDir 'Inno'
if (-not (Test-Path -LiteralPath (Join-Path $innoDir 'ISCC.exe'))) {
    $setup = Join-Path $DependencyDir 'innosetup-6.7.3.exe'
    Invoke-WebRequest 'https://github.com/jrsoftware/issrc/releases/download/is-6_7_3/innosetup-6.7.3.exe' -OutFile $setup
    if ((Get-AuthenticodeSignature -LiteralPath $setup).Status -ne 'Valid') { throw 'Invalid Inno Setup installer signature' }
    $compilerSetup = Start-Process -FilePath $setup -ArgumentList '/VERYSILENT','/SUPPRESSMSGBOXES','/NORESTART',('/DIR="'+$innoDir+'"') -WindowStyle Hidden -PassThru -Wait
    if ($compilerSetup.ExitCode -ne 0) { throw 'Inno compiler setup failed' }
}
Write-Output "Packaging dependencies ready in $DependencyDir"
