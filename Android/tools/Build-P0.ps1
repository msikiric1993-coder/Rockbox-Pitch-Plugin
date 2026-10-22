param([switch]$Package)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
$sdkRoot = $env:ANDROID_HOME
if (-not $sdkRoot) { $sdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not (Test-Path -LiteralPath $sdkRoot)) { throw 'Set ANDROID_HOME to an installed Android SDK.' }
$javaRoot = $env:JAVA_HOME
if (-not $javaRoot) { $javaRoot = Join-Path $env:ProgramFiles 'Android\Android Studio\jbr' }
if (-not (Test-Path -LiteralPath (Join-Path $javaRoot 'bin\javac.exe'))) { throw 'Set JAVA_HOME to JDK 17 or newer.' }
$env:ANDROID_HOME = $sdkRoot
$env:JAVA_HOME = $javaRoot
Push-Location -LiteralPath $projectRoot
try {
    & .\gradlew.bat :protocol:testDebugUnitTest :app:lintDebug :test-source:lintDebug :app:assembleDebug :test-source:assembleDebug --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Gradle validation or build failed.' }
    if ($Package) {
        & python tools/package_p0.py
        if ($LASTEXITCODE -ne 0) { throw 'Package validation failed.' }
    } else {
        & python tools/audit_apk.py app/build/outputs/apk/debug/app-debug.apk
        if ($LASTEXITCODE -ne 0) { throw 'APK audit failed.' }
    }
} finally { Pop-Location }
