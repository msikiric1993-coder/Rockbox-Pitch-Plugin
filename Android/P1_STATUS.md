# TDSpeed Android P1 dry engine

Implemented on 21 October 2026. This advances the approved plan through the portable dry listening engine. P2 will connect it to live playback and add listening controls. The APK remains a Routing Lab: its ordinary playback still uses the P0 dry forwarding path. It does not yet offer pitch, pause or re-centre controls.

## Physical S24 Ultra update

Later on 21 October, the connected SM-S928B running Android 16 passed all six native tests, including all 24 Windows reference cases and 315,750 checkpoint samples with zero sample error on ARM64. The phone also passed 30 synthetic routing start/stop cycles and controller-heartbeat expiry restoration. Shizuku ran as shell UID 2000, without root.

Physical testing exposed two shutdown races missed by the emulator run. Status snapshots now read a cached underrun count instead of querying a released AudioTrack, and the app stops sending heartbeats once stop is requested while waiting for cleanup acknowledgement. The routing test follows that same stop contract. The fixed APK's SHA-256 is `27e571dd611f6fb60d548e37298c67dd7e67aab3c20f5cf32cc1da5459ce8963`. The original P0 and P1 archives remain historical snapshots; the fixed phone APK and evidence are under `artifacts/s24-ultra-2026-10-21/`.

Initial phone-speaker listening checks passed with Tidal 2.215.0 and YouTube ReVanced 20.14.43. The user confirmed audible playback and a clear -6 dB gain change for both, and normal-volume restoration for YouTube ReVanced. AudioFlinger showed each source on remote submix and the helper alone on the speaker during forwarding, followed by the source's normal speaker route after stop. Tidal's restored volume was uncertain to the user because unity gain had already been restored before stopping; its restored route was verified in Android. Zero underruns were observed in the recorded live snapshots. These are short checks of the selected streams, not qualification of all content, stock YouTube, other output routes or long sessions. Forwarding was stopped at the end.

## What is implemented

- Portable C++ stereo varispeed engine extracted from the pinned Windows 0.4.2-rc.1 processor, without JUCE, Windows, Android or filesystem dependencies.
- The reference's coupled rate `2^(semitones/12)`, four-point Hermite interpolation, 1 ms smoothing, 250 ms initial/re-centre offset and exact three-second full/empty recovery.
- Planar and interleaved in-place processing, explicit preparation outside processing, block-boundary re-centre, state inspection, finite output and no C++ heap allocation during processing.
- Returning to zero pitch retains accumulated delay. Ordinary ring wrap retains the timeline. Recovery keeps capturing and then recentres on fresh history.
- A Kotlin/JNI owner with serialized process/close, direct native-order writable buffers, bounds/alignment checks, idempotent close, allocation failure handling and a 128 MiB Android history ceiling. The core's host ceiling remains the Windows reference's 2 GiB; P1 tests use smaller histories.
- A desktop harness links the actual existing Windows shared-code library, compares every output sample and recovery/backlog state, and generates deterministic fixtures. The reference source is unchanged and all nine pinned input hashes are checked.
- Android instrumentation runs the JNI build against those Windows fixtures. Fixtures and provenance are test assets only; they are not bundled into the ordinary app APK.

## Verified on this machine

P0 was rebuilt and rechecked before starting this phase. Its APK SHA-256 remained `7579edfeddf23880407d6fc3b8ed34cacb76e27aa2cc1642ff1c55e58fcd0a8c`. Thirty live routing start/stop cycles, restoration after the controller heartbeat expires, four session tests and three diagnostic native tests passed again on the Android 16 x86_64 emulator using ADB-started Shizuku as shell UID 2000.

The new desktop core matched the real Windows processor across 24 cases and **26,334,000 channel samples**, with maximum absolute sample error **zero** against the initial `1e-6` target. The cases cover 44.1/48/96 kHz, unity/-12/-4/+4, changing pitch, re-centre, return to zero, full history and catching live input, with tones, ramps, independent impulses and deterministic stereo noise. Variable blocks range from 1 to 1024 frames.

Separate core invariants check measured tone frequency, cursor advance, exact recovery silence at all three sample rates, 180 seconds of continuous slowed listening, zero-pitch backlog retention, re-centre, ordinary wrap, planar/interleaved equivalence, callback partition independence, invalid PCM/configuration and repeat preparation. A processing allocation counter observes zero C++ heap allocations.

The same invariant suite also passed with MSVC AddressSanitizer enabled.

Android x86_64 compared **315,750 checkpoint samples** from the same 24 reference cases, with maximum absolute sample error **zero** at `1e-6` tolerance. Every variable-sized block's backlog and recovery state also passed. Six Android native tests pass, including the previous three diagnostic tests and new fixture/lifecycle/invalid-PCM checks. The subsequent physical S24 Ultra run also passed these tests and the same tolerance on ARM64, as recorded above.

The package records exact APK/native-library hashes and the runtime evidence available for those bytes. Static APK checks cover both ABIs, 16 KiB ELF segment alignment and absence of framework stub definitions. A static alignment check does not establish operation on a 16 KiB-page device.

## Build and test

`tools/Build-P1.ps1 -DeviceTests -Package` builds/checks the Windows reference, runs the host oracle and invariants, stages fixtures, builds both Android ABIs, runs unit/lint checks and native instrumentation, audits the APK and packages P1 separately from P0. It requires Visual Studio 2022, Python, the existing reference Release build, the configured Android SDK/NDK and JDK 17+; defaults use Android Studio's JDK and SDK CMake. `-DeviceTests` requires a connected emulator or phone. Do not use Gradle's connected app test task for the Shizuku routing test: its reinstall can revoke the app UID authorization.

For routing regression, install the new app and its app-test APK with replacement (preserving authorization), run the separate stereo test source, start Shizuku through ADB and authorize the Routing Lab. Run `com.fyodor.tdspeed.android.RoutingIntegrationTest` through the Android test runner. Evidence must name the exact tested APK hash. Never root the emulator or phone to make the test pass.

The host core can also be configured alone from `dsp-native/host` without `REFERENCE_CONFIG`; this runs the independent invariants without JUCE/reference linking. `TDSPEED_CORE_ASAN=ON` enables the memory-safety check build.

## Remaining qualification and next implementation

Android 13/14/15, stock YouTube, the broader DRM/offload/content matrix, Bluetooth, navigation/calls, wired/wireless Android Auto, 16 KiB runtime and a 30-minute routing soak remain untested. The short S24 Ultra speaker checks above do not complete P0's full qualification gate or establish universal capture of YouTube, Tidal or any other app. ARM64 numerical comparison has passed on this S24 Ultra firmware; other hardware/firmware remains unqualified.

P2 is next: wire this tested dry engine into the helper's capture/output pipeline; avoid stacking the P0 startup delay on top of the engine's 250 ms offset; add pitch, original pitch, acknowledged pause/resume, one-shot re-centre, visible backlog/capacity and the 128 MiB history allocation. Port the first reference reverb only after that live path passes. Storage history and car controls remain later phases.
