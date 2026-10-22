# TDSpeed Android coding and release plan

Implement the [architecture](ARCHITECTURE.md) in the order below. Every milestone produces a runnable or inspectable deliverable with recorded results. An APK compiling successfully is not an audio-routing pass.

## P0 Prove the external audio route

Deliver a diagnostic APK and a debug-only stereo source app. Use no musical DSP and no long-term storage. The diagnostic UI needs start, stop, input/output meters, current route, negotiated format and an exportable result record.

1. Create the Kotlin project with minSdk 33, compileSdk 36, targetSdk 36, ARM64 JNI and a compatible pinned stable toolchain. Record the actual JDK, Android Gradle Plugin, Gradle, Kotlin, NDK, CMake, Shizuku API and Media3 versions in the build manifest. Use no dynamic dependency versions.
2. Bind a non-daemon Shizuku UserService. Verify shell identity, caller validation, context attribution and loading an APK-shipped native library.
3. Implement hidden API probes and the pure loopback policy. Validate the UID exclusion with processed output playing while capture runs.
4. Forward 48 kHz stereo through AudioRecord and AudioTrack. Check channels using different left/right tones. A diagnostic-only attenuation or channel swap makes replacement audible; it must never silently remain enabled in a listening build.
5. Implement normal stop, failed-start rollback, Binder death and watchdog cleanup.
6. Probe source-session access and non-governing focus observation. Measure what happens to source and helper gains during navigation, calls and focus changes.
7. Test on the actual S24 Ultra: phone output, Bluetooth and the available Android Auto connection. Record Android and One UI builds rather than inferring them from the handset model.

**Pass:** deterministic captured audio, no audible original in parallel, no feedback, correct stereo, no source pause caused by our focus request, clean stop and recovery after helper/app termination. Complete at least 30 start/stop cycles and a 30-minute dry run. Phone-output restoration after process death should occur within 2 seconds as an initial target; measure car recovery separately. Unreliable interruption handling blocks car support even if ordinary forwarding works.

If hidden APIs or commercial playback fail, retain the diagnostic APK and document the exact failed capability. Do not mask failure with public playback capture, which has a different copy-versus-replacement contract. A standalone file player is a possible separate product, not completion of this milestone.

## P1 Extract and preserve the listening engine

Deliver a portable C++ core, a desktop reference harness and an Android native build. Start from the hashed 0.4.2-rc.1 reference inputs in [SOURCES.md](SOURCES.md#local-engine-baseline).

Separate the live listening algorithm and selected effects from JUCE editor creation, DAW synchronization, file rendering, Windows routing and the VST entry point. Preserve the reference algorithms and parameter interpretation. Retain JUCE DSP components where needed under the project's applicable licence; a full replacement of dependencies is not a prerequisite.

Generate reference fixtures with synthetic tones, impulses and deterministic stereo signals. Test unity, minus 12, minus 4 and plus 4 semitones, changing pitch, re-centre, zero-pitch return, effects-off identity and recovery. Include 44.1, 48 and 96 kHz for the portable core; the first phone route remains 48 kHz.

**Pass:** the extracted desktop core matches the original with an initial dry maximum absolute sample-error target of 1e-6 on normalized test signals, adjusted only with a recorded floating-point explanation. Establish ARM64 tolerances separately. Tone frequency and long-run cursor advance must match the intended rate within the chosen measurement tolerance. Prove no callback allocation and no non-finite output. Keep existing Windows checks passing after any future shared-code integration.

## P2 Deliver a usable listening alpha

Deliver a signed alpha APK using RAM history, without claiming long-session support.

- Add pitch from minus 12 to plus 12 semitones, initially in 0.1-semitone UI increments, with original pitch as a separate action.
- Implement 128 MiB history, visible backlog/capacity and the three-second full/empty recovery policy.
- Implement acknowledged pause/resume, immediate re-centre and stop. Distinguish requested source pause from confirmed source pause, and track independent hold causes.
- Add the foreground notification, Quick Settings tile, preferences and named presets.
- Port one reference reverb after the dry engine passes. Add further effects only after CPU, channel and reset checks.
- Complete source selection and two-app isolation tests where the adapter supports filtering.

**Pass:** listening position survives pause/resume; returning to zero pitch retains delay; repeated re-centre IDs act once; recovery silence is exact at the engine boundary; no sample drop at ordinary RAM wrap; no unexpected reset when the Activity closes, a track changes or settings are recalled.

## P3 Deliver long listening with bounded storage

Deliver the storage-backed history provider and an updated APK. The private file is opened by the normal app and passed to the helper as a descriptor. Use fixed sequence-addressed chunks, preallocation, bounded caches and independent storage I/O.

Test file wrap with accelerated small capacities as well as a real 1 GiB allocation. Exercise read/write contention, full storage, allocation failure, stalled I/O, process death and stale files after restart. Confirm that the render thread never faults in file-backed pages or performs direct filesystem work.

**Pass:** bit-identical history reads across RAM, disk and wrap boundaries; no overwrite of unread audio; no render stall caused by a storage lock; explicit recovery before ingress loss. Complete a two-hour run at minus 4 semitones with the default 1 GiB history, and a capacity-limit test at minus 12. Record memory, storage writes, temperature, CPU, battery and transport counters. A two-hour test does not establish unlimited runtime.

## P4 Deliver Android Auto controls

Deliver MediaLibraryService, the custom Player adapter, discovery metadata and custom commands. The car controller must not create a second source player or take permanent focus from the upstream app.

Validate with Desktop Head Unit, then the physical car. Test both wired and wireless connections when available; an untested mode stays marked not tested. Include car startup before/after source playback, switching from Tidal or YouTube Music to TDSpeed controls, steering-wheel pause, navigation prompts, assistant activation, calls and disconnect/reconnect.

**Pass:** controls affect the acknowledged helper state; re-centre is discoverable; no competing focus loop; no delayed guidance; no automatic loudspeaker playback on car disconnect. Generic metadata is acceptable initially. Accurate delayed song titles are a later enhancement with their own tests.

## P5 Produce the installation package

Deliver:

```text
TDSpeed-Android-<version>-arm64.apk
TDSpeed-Android-<version>-source.zip
INSTALL.md
COMPATIBILITY.json
BUILD-MANIFEST.json
SHA256SUMS.txt
LICENSES/
```

Sign production APKs using a stable privately held release key. Keep keys outside source control and source archives. Preserve application ID and signing identity across updates. AAB generation is optional distribution work; it does not replace the standalone installable APK for this workflow.

Use a supported Android Gradle Plugin and NDK with 16 KiB page-size compatibility. Audit every packaged native library, not only our JNI library. Make a 16 KiB emulator/device check part of packaging. [Native packaging requirements](SOURCES.md#native-packaging).

Choose the proposed application ID com.fyodor.tdspeed.android at project creation and verify it is available for the intended distribution. Build types use distinct IDs where needed. Record public licence notices and corresponding source for the actual dependency set. Do not assume that removing the JUCE GUI removes obligations from retained DSP dependencies.

**Pass:** fresh install, update, reboot, reauthorization, stop, uninstall and failure recovery work according to [INSTALLATION_AND_VALIDATION.md](INSTALLATION_AND_VALIDATION.md). Checksums match and the source archive can reproduce the recorded build with documented external tooling.

## Proposed interface contracts

These are protocol requirements, not generated AIDL signatures. Use typed parcelables in implementation.

| Request | Required inputs | Acknowledged result |
|---|---|---|
| probe | protocol version | backend UID, adapter availability, supported formats and observations |
| start | session ID, command ID, selected scope, history descriptor/config | generation, epoch, accepted configuration or error |
| setPitch | session ID, command ID, semitones | actual applied pitch and source boundary |
| setPaused | session ID, command ID, paused | held/running state and upstream pause status |
| recenter | session ID, command ID | new epoch/cursor, discard reason and applied boundary |
| setEffects | session ID, command ID, validated preset | applied preset revision or refusal |
| stop | session ID, command ID | cleanup outcome, released policy and stopped state |
| heartbeat | session ID, controller sequence | lease acknowledgement |
| snapshot | session ID | authoritative state and counters |

Snapshots must include protocol/engine versions, state/reason, monotonic timestamp, epoch, W/R, presented position estimate, requested/effective rate, backlog/capacity, negotiated input/output format, transport queue depth, xruns, disk queue depth, recovery countdown, source-control availability and last acknowledged command ID. Reports must separate measured quantities from estimates.

Define stable errors including SHIZUKU_UNAVAILABLE, PERMISSION_DENIED, WRONG_BACKEND_UID, PROTOCOL_MISMATCH, ATTRIBUTION_FAILED, NATIVE_LOAD_FAILED, POLICY_UNAVAILABLE, POLICY_REGISTRATION_FAILED, FORMAT_UNSUPPORTED, SOURCE_IDLE, SOURCE_ROUTE_UNVERIFIED, OUTPUT_LOST, HISTORY_FULL, STORAGE_STALLED, STORAGE_FAILED, FOCUS_OBSERVER_UNAVAILABLE and CLEANUP_INCOMPLETE. A missing signal alone maps to waiting/unverified, not a definitive DRM diagnosis.

## Initial engineering budgets

These targets are subject to measurement in P0 and P2. Changing them requires an explicit engineering decision, not an invisible fallback.

| Metric | Initial target |
|---|---|
| DSP quantum | 256 frames at 48 kHz |
| P99 DSP time per quantum | Below 25 percent of the 5.33 ms quantum on the target device for the released effect preset |
| Source history startup offset | Approximately 250 ms, excluding other queues and transport |
| UI command acknowledgement | Within 250 ms under normal load |
| Status publication | At most 4 Hz |
| Undocumented discontinuities | Zero |
| NaN, infinity or runaway output | Zero; contain faults and expose status |
| Cache RAM in storage mode | At most 64 MiB, with other engine/framework memory reported separately |
| Controller heartbeat and lease | Initially 2 seconds and 10 seconds respectively |

Measure battery and thermal behaviour before setting a supported runtime claim. Do not retain the full desktop rack merely because the device can compile it.
