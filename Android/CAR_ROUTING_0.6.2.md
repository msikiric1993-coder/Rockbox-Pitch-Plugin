# Car Lab 0.6.2 — local routing handoff experiment

This extends the 0.6.1 focus fix. It is a separate installation from the working
phone app. No source, reports or APKs are uploaded to GitHub.

## Behavior

With the car capture switch enabled, Start listening launches a separate shell
route guardian alongside the existing 1 GiB varispeed pipeline. It discovers
Android Auto registrations by the projection package's actual UID in AudioService
events, then verifies the native mix has exactly the reported MEDIA/GAME rules,
pure loopback flags, player type and remote-submix output device.

For active third-party MEDIA/UNKNOWN players, it temporarily adds UID exclusions
to that projection music mix. This lets our later input mix receive the original
source while the helper's processed output remains eligible for Android Auto.
The navigation/assistant mix is not changed. New players and connections are
checked while listening. Unsupported or unrecognized routes are left alone and
reported in the UI. A status of REROUTED confirms the rule change, not audible
pitch processing in the physical car.

Stop restores the original two-rule music mix. The guardian retains restoration
state separately from the audio helper and restores on parent-pipe EOF or a
six-second heartbeat expiry. It compares the installed rule signature before
restoring, so it does not overwrite an unrelated external rule change. A removed
mix is not resurrected. The guardian is not forcibly killed during normal cleanup.
This does not guarantee recovery if Android kills the guardian itself; reconnecting
Android Auto recreates its routes if normal Stop does not restore playback.

The ordinary app path does not launch this guardian. Its original installation
is not replaced. Output capture permission still permits other authorized capture.

## Why native inspection uses dumps

The Android 16 native-to-Java mix conversion used by getRegisteredPolicyMixes
loses usage/exclusion information on the tested emulator. Official JNI source
also assigns the usage-field handle to mSource and omits excluded UID cases.
Do not use those converted rules as restoration data. Enumeration is additionally
gated by audio_mix_test_api. This build instead verifies bounded native audio
policy dumps and builds a clean update descriptor for the verified address.
It never unregisters/re-registers the car's mix or opens a second recorder on it.

Native API/source references:

- [AudioPolicyInterfaceImpl.cpp](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/android16-release/services/audiopolicy/service/AudioPolicyInterfaceImpl.cpp)
- [AudioPolicyMix.cpp](https://android.googlesource.com/platform/frameworks/av/+/refs/heads/android16-release/services/audiopolicy/common/managerdefinitions/src/AudioPolicyMix.cpp)
- [JNI mix conversion](https://raw.githubusercontent.com/aosp-mirror/platform_frameworks_base/android16-release/core/jni/android_media_AudioSystem.cpp)

## Validation and limits

The emulator routing suite contains twelve cases, including reported-rule
competition, native car-first handoff, guarded handoff with Stop restoration,
and guarded handoff with parent-pipe EOF restoration. Processed peak is about
0.04 for source peak 0.08. Both guarded cases verify the original native rule is
restored, and all synthetic policies unregister without cleanup errors.

The focus callback regression, ten protocol unit tests, app/helper lint and
build checks are also exercised. Receipts and the exact tested APK are in
`artifacts/car-routing-0.6.2/`.

RouteCapabilityProbe verifies native update and native inspection on an owned
mix matching only an unassigned UID, then unregisters it. It never captures real
playback or changes an Android Auto policy. This separates Samsung API support
from physical car qualification. Calls, navigation mixing, reconnections,
long-drive stability and battery use are not qualified by the synthetic tests.

## Parked-car check

1. Stop ordinary Varispeed. Open Varispeed Car Lab and keep its capture switch on.
2. Connect Android Auto and play music. Tap Start listening, wait for the car
   route status, and try -4. Starting listening before connecting is also supported
   by the experiment.
3. Confirm pitch and speed change in the car. Original pitch preserves backlog;
   Re-centre discards the accumulated delay as before.
4. Stop listening and confirm ordinary music resumes. If it fails, share the
   connection report while the symptom is present. If Stop cannot restore sound,
   disconnect and reconnect Android Auto.
