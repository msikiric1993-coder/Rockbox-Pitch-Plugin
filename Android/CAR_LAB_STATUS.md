# Android Auto experiment — 0.6.0-car-lab

Prepared 2026-10-24. This is a separate **Varispeed Car Lab** app
(`com.fyodor.tdspeed.android.carlab`), not an update to the working phone app.
The original 0.5.3 app, its data, permissions and first successful baseline remain
preserved. Do not run listening sessions in both apps together.

## Test in the parked car

1. Install the Car Lab APK alongside Varispeed. Keep the ordinary app stopped.
2. Confirm Shizuku is running. Authorize **Varispeed Car Lab** when it asks and
   allow notifications. Keep Developer options/USB debugging enabled as before.
3. Disconnect Android Auto first. Leave the lab's capture switch enabled.
4. Start music on the phone, open Car Lab and tap **Start listening**. Wait for
   Listening, then connect wireless Android Auto. This order is deliberate.
5. Select -4 or -6. Confirm the car itself plays the lower/slower music. Try
   Original pitch and Re-centre. Re-centre should discard accumulated delay.
6. While the symptom is present, tap **Share connection report**. Choose where
   to save/share it; the app never sends it automatically. The report contains
   package names, device addresses, audio-route details and signal peaks, not
   recorded audio. Collect a report even if music remains unchanged or silent.
7. Tap Stop when finished. For ordinary listening, return to the existing app.

If connection order matters, record that. A later parked comparison can connect
Android Auto before starting Car Lab, or turn its capture switch off while
stopped. Change one variable at a time. If music is already routed before the
lab starts, disconnect Android Auto and repeat steps 3–5. This is a test procedure,
not a claim that car support has been established.

## Implemented

- Car Lab permits capture of its processed output (`ALLOW_CAPTURE_BY_ALL`) by
  default. This is not exclusive to Android Auto: other otherwise-authorized
  playback capture can receive it too. The stopped-only switch restores the old
  `ALLOW_CAPTURE_BY_NONE` behaviour for comparison. Normal builds force the
  override off; the DSP, 1 GiB buffer and pure-loopback input remain unchanged.
- The input still excludes the helper UID and controlling app UID, preventing
  the processed output from feeding back into our capture.
- Input/output device IDs and addresses, our mix address, output capture policy
  and signal metrics are available in explicit reports. Device queries run on
  a 250 ms cadence rather than adding a Binder call to every audio block.
- Reports collect bounded audio-policy, AudioFlinger and AudioService dumps in
  the shell helper. Collection runs separately from the heartbeat executor.
  Each dump is capped at 48 Ki characters; the complete Binder parcel is checked
  below 512 KiB. Reports use a narrow, non-exported cache FileProvider and a user
  chooser. No PCM is written. Cache retains at most four reports per app.
- AIDL protocol 3 appends configured-start and report methods with explicit IDs.
  Helper build version changes prevent accidentally reusing an older helper.
- No Android Auto-owned policy is edited, unregistered or muted; no root,
  third-party patch or app injection is used. There is no car-screen UI yet.

## Evidence and limits

The Android 16 emulator passed six real audio-policy tests using a separate
quiet stereo-source app and a simulated car receiver. These tests use a simple
0.5 gain transformation, not the varispeed DSP; they isolate route chaining.

| Case | Observed result |
| --- | --- |
| Simulated car primary route registered first | Original ~0.08 peak at car; processor input silent |
| Processor first, output capture blocked | Pure-loopback car receives processed ~0.04 peak |
| Processor first, output capture allowed | Pure-loopback car receives processed ~0.04 peak |
| Processor already playing; car connects later | Existing output reroutes; processed ~0.04 peak reaches car |
| Secondary capture, output capture blocked | Receiver silent |
| Secondary capture, output capture allowed | Receiver receives processed ~0.04 peak |

All six unregister their test policies with no reported cleanup errors. Route
report generation also passed its Binder-size check. Three emulator UI tests
passed: continuous dragging/stale snapshots, standard listening controls, and
the lab toggle/report controls. Build, lint and four protocol unit tests passed.
APK static audit verifies framework stubs are absent and native ELF alignment;
the native libraries are byte-identical to the successful 0.5.3 build.

This has **not** been tested with the physical Samsung, real Android Auto,
Motivesupplies BMW retrofit, calls, navigation or car reconnection. Explicit
source-device routing and Samsung/Android Auto policy differences may defeat
the early-start approach. The lab UI's Listening state does not prove processed
audio is audible in the car. The user's parked-car observation and route report
are the next qualification step, as requested for later testing.

## Build and reproduce

Use the installed Android SDK/NDK and JDK 17+ as in the existing build scripts:

```text
gradlew :app:assembleCarLab :app:lintCarLab :protocol:testDebugUnitTest :shell-audio:lintDebug
```

For the synthetic shell test, install the lab APK and `test-source` APK in the
emulator, start its quiet tones and verify Playing. Use explicit display 0 for
headless emulator taps (`input -d 0 tap ...`). Determine the source UID with
`cmd package list packages --user 0 -U com.fyodor.tdspeed.testsource`. Run as
ordinary ADB shell UID 2000, never root:

```text
CLASSPATH=<installed-lab-base.apk> app_process /system/bin com.fyodor.tdspeed.shell.PolicyChainSelfTest com.fyodor.tdspeed.android.carlab <sourceUid>
```

It prints `POLICY_CHAIN_RESULT` JSON and exits; raw audio stays in RAM.
`tools/package_car_lab.py` verifies and packages the exact tested APK/evidence.

## Source basis

First-match primary mix selection and secondary-copy capture restrictions are
described in [AOSP AudioPolicyMix.cpp](https://android.googlesource.com/platform/frameworks/av/+/ffede69924118c5454484164fd590b3474cc716a/services/audiopolicy/common/managerdefinitions/src/AudioPolicyMix.cpp).
The emulator tests above independently exercise these mechanisms. A more
specific UID match is not assumed to override an existing primary route.
We do not use UID affinity as a shortcut: the Java device-affinity check requires
the caller's mix to render to the target device, which our pure-loopback mix
does not do. See [Android 16 AudioMix](https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android16-release/media/java/android/media/audiopolicy/AudioMix.java)
and [AudioService](https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android16-release/services/core/java/com/android/server/audio/AudioService.java).
