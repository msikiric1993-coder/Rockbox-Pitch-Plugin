param([switch]$DeviceTests, [switch]$Package,
      [string]$ReferenceBuild = '', [string]$CMake = '')
$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path -Parent $PSScriptRoot
if (-not $env:ANDROID_HOME) { $env:ANDROID_HOME = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
if (-not $env:JAVA_HOME) { $env:JAVA_HOME = Join-Path $env:ProgramFiles 'Android\Android Studio\jbr' }
if (-not $CMake) { $CMake = Join-Path $env:ANDROID_HOME 'cmake\3.22.1\bin\cmake.exe' }
$ctest = Join-Path (Split-Path -Parent $CMake) 'ctest.exe'
if (-not $ReferenceBuild) { $ReferenceBuild = Join-Path $projectRoot '..\RC build\0.4.2-rc.1\build' }
Push-Location -LiteralPath $projectRoot
try {
    New-Item -ItemType Directory -Force artifacts\p1-work | Out-Null
    # Rebuild only when needed; do not edit reference source or project settings.
    & $CMake --build $ReferenceBuild --config Release --target TDSpeedVST --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Reference Release build failed.' }
    & python tools/prepare_reference.py $ReferenceBuild dsp-native/host/build
    if ($LASTEXITCODE -ne 0) { throw 'Pinned reference input verification failed.' }
    $referenceConfig = (Join-Path $projectRoot 'dsp-native\host\build\reference.cmake').Replace('\','/')
    & $CMake -S dsp-native/host -B dsp-native/host/build -G 'Visual Studio 17 2022' -A x64 "-DREFERENCE_CONFIG=$referenceConfig"
    if ($LASTEXITCODE -ne 0) { throw 'Host configure failed.' }
    & $CMake --build dsp-native/host/build --config Release --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Host build failed.' }
    & $ctest --test-dir dsp-native/host/build -C Release --output-on-failure -V |
        Tee-Object -FilePath artifacts\p1-work\host-tests.txt
    if ($LASTEXITCODE -ne 0) { throw 'Portable or reference tests failed.' }
    & python tools/stage_reference_fixtures.py
    if ($LASTEXITCODE -ne 0) { throw 'Reference fixture staging failed.' }
    $tasks = @(':protocol:testDebugUnitTest', ':dsp-native:lintDebug', ':dsp-native:assembleDebug', ':app:lintDebug', ':test-source:lintDebug',
               ':app:assembleDebug', ':app:assembleDebugAndroidTest', ':test-source:assembleDebug', ':dsp-native:assembleDebugAndroidTest')
    if ($DeviceTests) { $tasks += ':dsp-native:connectedDebugAndroidTest' }
    & .\gradlew.bat @tasks --console=plain
    if ($LASTEXITCODE -ne 0) { throw 'Android checks failed.' }
    & python tools/audit_apk.py app/build/outputs/apk/debug/app-debug.apk
    if ($LASTEXITCODE -ne 0) { throw 'APK audit failed.' }
    if ($Package) {
        & python tools/package_p1.py
        if ($LASTEXITCODE -ne 0) { throw 'P1 package verification failed.' }
    }
} finally { Pop-Location }
