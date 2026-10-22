# TDSpeed Android P0 routing prototype

Implemented on 20 October 2026. This is the first coding stage of the approved [architecture](ARCHITECTURE.md), not the finished listening app.

## What is implemented

- Kotlin/Compose Routing Lab with capability inspection, native identity, input/output levels, negotiated format, route, frame count, underruns and focus-event count.
- Foreground forwarding service with visible stop action, bounded wake lock and Shizuku connection management.
- Shizuku UserService under shell UID 2000, with caller checks, version handshake, command ordering, controller death and heartbeat lease.
- Pure loopback routing for media/unknown usage, excluding both the helper UID and normal app UID.
- 48 kHz stereo float32 forwarding, with an explicit PCM16-boundary fallback if float initialization fails.
- A small preallocated startup queue that retains captured audio while the previous output route drains. This is transport priming, not the varispeed history.
- Native C++ diagnostic gain and channel swapping, independent of the future music engine.
- A separate low-level stereo-tone source: 440 Hz left, 660 Hz right.
- Hidden focus callback bindings supplied only at compile time, candidate interruption attenuation, cleanup and diagnostic JSON export.

Pitch/speed, long history, re-centre, effects, Android Auto dashboard integration and source transport controls are not implemented yet. The UI says so. The focus observer and communication-mode guard are diagnostic candidates; calls and car behaviour remain unqualified.

## Validation boundary

The first runtime target is the available Android 16/API 36.1 x86_64 emulator, using ADB-started Shizuku 13.6.0 as UID 2000. It is not a rooted test path. Production-target ARM64 code also compiles, but has not run on the S24 Ultra.

The following checks have passed during development:

| Check | Result and scope |
|---|---|
| Gradle build | Diagnostic app and independent stereo-source APKs build |
| Session unit tests | Four tests covering duplicate/reordered commands, lease expiry and stale sessions |
| Native instrumentation | Three tests covering stereo identity/swap/gain, invalid buffers and non-finite containment |
| Replacement routing | AudioFlinger shows the source on remote submix, helper on speakers, and no original source on the committed speaker route |
| Gain diagnostic | Synthetic input peak about 0.08, output about 0.04 at gain 0.5 |
| Lifecycle integration | Thirty start/stop cycles, source speaker-route restoration and controller lease-expiry cleanup |
| Android lint | No errors; dependency-version suggestions and any remaining prototype warnings are documented in the reports |

The complete P0 milestone remains open: the physical S24 Ultra, commercial music apps, Bluetooth, wired/wireless Android Auto, call/navigation cases, injected storage/native/policy failures, 16 KiB runtime testing and a 30-minute dry soak still need their specified checks. An emulator route pass does not establish those combinations.

## Build

Open this Android directory in Android Studio, or run from PowerShell:

```powershell
.\tools\Build-P0.ps1 -Package
```

The script uses ANDROID_HOME and JAVA_HOME when supplied, otherwise the usual Windows Android Studio/SDK locations. It requires a JDK 17+ installation and Python 3 for package auditing. Gradle installs the pinned native SDK components when their already-accepted SDK licences permit it; otherwise complete that setup in Android Studio.

Pinned baseline: Gradle 8.13 with distribution and wrapper checksums, Android Gradle Plugin 8.13.2, Kotlin 2.2.21, compile/target API 36, min API 33, NDK 28.2.13676358 and CMake 3.22.1. The debug APK includes ARM64 and x86_64 native libraries. It uses an Android debug signing key and is not a production-signed release.

## Run on a phone

1. Start Shizuku through its ADB/wireless-debugging setup and install TDSpeed Routing Lab.
2. Open the lab, select Connect and inspect, and authorize it in Shizuku.
3. For a deterministic first check, install the separate Stereo Source APK and press Play stereo tones. Keep the listening volume low.
4. Return to the lab and press Start stereo forwarding. Check helper UID 2000, the native identity, negotiated stereo format and signal levels.
5. Use Unity, minus 6 dB and Swap L/R to check replacement output. Each new session resets these diagnostics to unity and normal channels.
6. Press Stop and restore route. The source remains a separate app; stop its tones separately when finished.

Use Export diagnostic JSON to save a result without recorded audio. For current Shizuku versions, use the startup command shown by the installed Shizuku app; older /sdcard/start.sh examples may not exist.

## Developer integration tests

Build the app and its test APK with `:app:assembleDebug :app:assembleDebugAndroidTest`. Install both using `adb -s <serial> install -r ...`, retaining the same app installation and Shizuku grant. Start the separate stereo source and leave it playing. Then run:

```text
adb -s <serial> shell am instrument -w -r -e class com.fyodor.tdspeed.android.RoutingIntegrationTest com.fyodor.tdspeed.android.debug.test/androidx.test.runner.AndroidJUnitRunner
```

The test requires the source and authorized shell backend; missing setup is a failure, not an automatic skip. It checks the emulator's speaker route specifically. Pull private test evidence with `adb shell run-as com.fyodor.tdspeed.android.debug cat files/p0-routing-cycles.json` and the corresponding p0-active-audioflinger.txt. Standard connected-test tooling may uninstall the app and revoke its UID-based Shizuku grant, so direct instrumentation is the documented routing-test workflow.

Run native-only instrumentation through `:dsp-native:connectedDebugAndroidTest`; it needs an emulator/device but does not require Shizuku. The app uses no microphone permission, internet permission, accessibility service or music-service credentials.
