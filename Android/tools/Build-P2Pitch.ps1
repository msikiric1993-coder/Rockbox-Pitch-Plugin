param([switch]$Package)
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = Join-Path $env:ProgramFiles 'Android\Android Studio\jbr' }
Push-Location -LiteralPath $projectRoot
try {
    & .\gradlew.bat :protocol:testDebugUnitTest :dsp-native:assembleDebug :dsp-native:assembleDebugAndroidTest `
        :app:assembleDebug :app:assembleDebugAndroidTest :app:lintDebug :dsp-native:lintDebug :shell-audio:lintDebug --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Android build or checks failed.' }
    & python tools/audit_apk.py app/build/outputs/apk/debug/app-debug.apk
    if ($LASTEXITCODE -ne 0) { throw 'APK audit failed.' }
    if ($Package) {
        & python tools/package_p2_pitch.py
        if ($LASTEXITCODE -ne 0) { throw 'P2 pitch package verification failed.' }
    }
} finally { Pop-Location }
