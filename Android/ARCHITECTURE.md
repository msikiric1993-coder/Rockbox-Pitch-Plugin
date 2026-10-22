# TDSpeed Android implementation architecture

Baseline: 20 October 2026. Read the [manifesto](README.md) first. In this specification, **must** defines a required behaviour; numeric performance budgets are proposed acceptance targets, not measured Android results.

## Platform and product boundary

| Decision | Initial implementation |
|---|---|
| Minimum OS | Android 13, API 33 |
| First physical device | Samsung Galaxy S24 Ultra, installed firmware to be recorded |
| Production ABI | arm64-v8a; x86_64 is permitted for emulator diagnostics |
| Compile and target SDK baseline | API 36; pin a compatible stable build toolchain at project creation |
| Privileged backend | Shizuku UserService running as ADB shell UID 2000 |
| UI | Kotlin and Jetpack Compose |
| DSP | C++17 through JNI, extracted from the existing engine |
| Audio transport | AudioRecord and AudioTrack in the helper; PCM stereo |
| Initial delivery | Signed APK, source archive, checksums and installation guide |
| First car platform | Android Auto projected from the phone |

Android Automotive OS running in a head unit is a separate later deployment. Remote playback through Chromecast, Spotify Connect or a service-specific handoff can bypass the phone's local audio completely; those paths are outside this processor. Ordinary Bluetooth media and Android Auto audio are test targets, not assumed equivalents.

Android 13 is chosen because the examined AOSP 13 shell manifest includes MODIFY_AUDIO_ROUTING, while the examined AOSP 12 manifest does not. An OEM can change permissions or policy behaviour, so runtime capability checks remain mandatory. [Version evidence](SOURCES.md#minimum-android-version).

The app does not obtain raw music from Spotify or Tidal APIs. It processes an eligible local playback route. No guarantee is made about protected, offloaded or tunneled playback until that exact mode passes a device test.

## Process and module boundaries

### Normal application process

Own the Compose interface, preferences, setup flow, foreground notification, Android lifecycle and optional Media3 session. Keep the application responsive when the helper is absent. Persist pitch, effect presets and user preferences; do not persist an active session flag as permission to resume automatically after a crash.

Run an explicitly started foreground service while forwarding audio. Use the mediaPlayback type for continuous audible output, with the corresponding manifest permission on applicable Android versions. Do not select mediaProcessing, which describes bounded asset processing, or declare a microphone service for an internal shell loopback. Validate the helper and foreground-service lifetime together on the target build. [Service requirements](SOURCES.md#foreground-lifetime).

The notification must expose pause/resume, re-centre and stop. A Quick Settings tile may toggle forwarding. Opening or closing the Activity must not start or reset playback. When Android prevents a background start, show a notification leading to a user-initiated start; do not repeatedly retry restricted launches.

### Privileged helper process

Use Shizuku's supported UserService mechanism, not an ad hoc command runner. Confirm the backend identity is UID 2000 for this product profile. A root backend is outside the acceptance baseline. The helper owns:

- Capability probes and the version-specific hidden API adapter.
- Audio policy registration, input capture and output playback.
- Native processing, history storage and audio threads.
- Source-session and audio-focus observation where the platform permits them.
- Route cleanup, controller-death detection and its own watchdog.

The helper loads only code and native libraries shipped in the installed APK. Prove native loading and shell attribution in the first milestone. A helper surviving an APK update must stop and restart against the new version before processing resumes.

Keep compile-only stubs or generated bindings for hidden Android classes separate from shipped classes. Reflection cannot by itself create a subclass of an abstract hidden callback such as AudioPolicyFocusListener. The adapter must supply a compiled callback implementation through the chosen binding approach, and must never package replacement definitions of Android framework classes. Probe each supported API family instead of treating all future Android versions as one implementation.

### Proposed source layout

```text
Android/
  app/                  Compose UI, onboarding, foreground service
  protocol/             AIDL, parcelables, errors and version handshake
  shell-audio/          Shizuku UserService and Android audio adapters
  dsp-native/           JNI and portable C++ audio engine
  car/                  Optional Media3 controller integration
  test-source/          Debug-only deterministic stereo source application
  tools/                Packaging and device evidence collection
  docs/                 Implementation-specific additions to this baseline
```

These modules are a proposed layout, not existing application code. Keep the specification files at the Android root as the entry point.

## Routing contract

### Select media without delaying the rest of the phone

The default scope includes USAGE_MEDIA and USAGE_UNKNOWN. USAGE_GAME is an explicit optional scope, off by default. Exclude the helper UID from the policy regardless of usage. Exclude the normal application UID too so future UI sounds or diagnostics cannot feed back.

Do not match navigation guidance, voice communication, alarms, assistant speech, notifications or sonification. Usage attributes are supplied by the source; mislabelled audio can still enter the route. Validate the actual navigation and calling apps rather than relying on their declared category alone.

The first prototype can match the media usage scope broadly. The listening alpha should add a selected-source mode using UID matching if the platform adapter supports the required rule combination. Verify Android's mixing-rule semantics with two simultaneous test apps. Do not assume that adding usage and UID criteria produces the intended intersection. A source switch is a route transition, never an in-place mutation of a policy from the audio thread.

### Register a replacement route

The adapter builds an AudioMixingRule, a stereo AudioMix with ROUTE_FLAG_LOOP_BACK, and an AudioPolicy. The helper registers that policy and creates its AudioRecord sink. Pure loopback is intentional: a route that also renders the original would produce doubled playback.

Use an attribution context consistent with the actual shell UID and com.android.shell. Keep all private APIs and context adaptation in shell-audio. Probe method availability and policy registration; handle reflection, permission, attribution and native-loader failures as separate errors. Do not attempt to fix them by declaring signature permissions in the ordinary APK. [Implementation precedent](SOURCES.md#routing-and-privileges).

A successful registration is not sufficient evidence. Verify source capture, suppression of the original and independent output. A quiet music passage does not prove capture is blocked; use the debug test-source app to establish the route separately from commercial content.

### Prepare and commit a session

1. Establish the foreground service and helper connection. Negotiate protocol and engine versions.
2. Check privileges, API availability, source selection, output route and configured storage.
3. Allocate all DSP, queue and cache memory. Prepare the native engine and muted output first.
4. Register the policy. Open and start AudioRecord. Keep rollback handlers for every acquired resource.
5. Prime the live history to approximately 250 ms, then start processed output. System, transport and car buffering add to that history offset.
6. Report RUNNING only after input and output are operational. Distinguish "waiting for source" from a route error.

Registration and original-route suppression are not an atomic Android transaction with starting our output. A short startup gap is acceptable; indefinite silence or an undocumented mixture of original and processed audio is not.

### Stop and restore

Serialize stop on the helper's control executor. Gate processed output, stop and release capture, unregister the owned policy, release AudioTrack, close history and remove temporary files. Complete all steps even when one fails. Publish cleanup results and clear the session lease.

Releasing the policy restores normal framework routing, but does not force a paused source to play. Display "processing stopped" rather than promising that music resumes. A sudden process death relies on framework cleanup of the policy owner; kill tests must verify this on the actual firmware.

## Audio data plane

### Format

Use 48 kHz stereo float32 as the engine format. Negotiate PCM_FLOAT capture and playback first. If a device only supports PCM16 at the boundary, convert once to float and report the negotiated format. Do not silently use mono or the 16 kHz speech format of the reference application. Reject unsupported multichannel layouts in the first release instead of inventing a downmix.

This processing path is not a claim of bit-perfect or high-resolution playback. The Android mix, chosen format and Bluetooth/car transport can change representation. Do not change unrelated Samsung sound settings automatically; test Dolby, equalization and other device effects and record their state.

### Thread ownership

| Thread or executor | Ownership and restrictions |
|---|---|
| Control executor | Serializes start, stop, routes, commands and state changes |
| Capture thread | Blocking AudioRecord reads into preallocated chunks; publishes source frames |
| Render thread | Produces output quanta from history and effects; writes AudioTrack |
| Storage worker | Reads and writes the backing file and prepares history pages |
| Watchdog | Observes thread progress and controller liveness independently of audio processing |

Use bounded single-producer/single-consumer queues with documented owners. JNI calls operate on blocks through reusable direct buffers, never per sample. Binder does not carry PCM. Audio-priority threads perform no file operations, blocking control locks, allocation, logging or Binder transactions. Blocking AudioRecord/AudioTrack I/O belongs only on their designated transport threads.

Begin with 256-frame DSP quanta at 48 kHz, adapting to actual input and output block sizes. Output queueing must be bounded and measured. Keep meter and status publication at no more than 4 Hz; UI rendering cannot back-pressure audio.

### Clocks

Keep capture transport occupancy separate from intentional song backlog. Track input and output timestamps where reliable. If route clocks drift, compensate in a separate transport resampler with a recorded ratio and initially bounded correction of plus or minus 0.1 percent, following the Windows transport model. Never regulate the intentional song backlog with that correction.

The effective source advance is the requested varispeed multiplied by any measured transport correction. Report both. A correction reaching its bound is a diagnostic fault to investigate, not permission to alter pitch further. Re-prime the transport if necessary; do not re-centre song history to fix transport queue occupancy.

## DSP extraction and fidelity

Reference the local 0.4.2-rc.1 project, whose relevant inputs are recorded in [SOURCES.md](SOURCES.md#local-engine-baseline). Its processor currently combines DSP, JUCE parameters, DAW synchronization, file audition and editor creation. Extract the listening DSP behind a portable interface rather than bringing the whole VST and Windows application into the APK.

Retain the interpolation polynomial, channel behaviour, semitone-to-rate mapping, approximately 1 ms rate smoothing, effect order and calibrated gains. Test the extraction first on the desktop against the reference using identical buffers. Compile the new native core for ARM64 separately; audit third-party code for x86-specific assumptions and release only the effects that pass mobile checks.

The engine contract is:

```text
prepare(format, maxQuantum, historyProvider, immutableConfig)
appendSource(interleavedFrames, absoluteFirstFrame)
render(outputFrames, outputFrameCount) -> RenderResult
applyCommand(command) -> CommandAcknowledgement
snapshot() -> EngineSnapshot
release()
```

The C++ implementation uses explicit ownership and no exceptions crossing JNI. Commands are applied at an identified block boundary. Each acknowledgement returns the command ID, applied generation and source position. A prepared-state failure must leave the prior running configuration intact or stop cleanly; it must not partially replace an active rack.

The history provider serves four interpolation neighbours across page and ring boundaries. Missing or stale pages must never look like valid audio. Keep channel samples and their sequence metadata consistent.

## History and storage

### Time model

Use an epoch ID, a 64-bit absolute write cursor W and a fractional read cursor R. Within an active captured timeline:

```text
requested rate r = 2^(semitones / 12)
backlog seconds = (W - R) / sampleRate
live backlog growth per wall-clock second ≈ 1 - r
```

At zero semitones, backlog remains. At negative semitones it grows while the source advances. At positive semitones it drains and can run out. Source inactivity and explicit holds must be represented as states, not confused with recorded musical silence.

At 48 kHz, stereo float32 history uses 384,000 bytes per captured second. These capacities exclude caches, output queues and metadata:

| History capacity | Stored source audio | Approximate running time at minus 4 semitones | At minus 12 semitones |
|---|---|---|---|
| 128 MiB | 5.83 minutes | 28.2 minutes | 11.7 minutes |
| 256 MiB | 11.65 minutes | 56.5 minutes | 23.3 minutes |
| 1 GiB | 46.60 minutes | 225.9 minutes | 93.2 minutes |

The running-time estimates assume continuous live input and near-live startup, with no pauses. They are storage arithmetic, not battery or thermal benchmarks.

### Memory alpha and storage-backed release

The routing prototype uses a small bounded RAM history. The listening alpha uses 128 MiB by default and reports that limit. It is not the long-listening release.

The long-listening release adds a 1 GiB backing-file ring by default, with a combined target of at most 64 MiB for history RAM caches. The user can select a larger bounded storage budget before starting. Reserve at least 1 GiB of free filesystem space beyond the configured history allocation, or refuse that allocation. These are chosen product limits, adjustable after device measurement.

The normal application opens a session file under its private no-backup files directory, preallocates it before routing and passes a ParcelFileDescriptor to the helper. Do not make the private directory world-readable or assume shell can open it by path. The helper duplicates the descriptor; the storage worker owns I/O. Keep session files out of backup and delete them on stop and on the next startup after a crash. Deletion is logical removal, not a secure-erasure claim.

Write sequential chunks, starting at 4,096 frames per chunk. Associate each with epoch, absolute first frame, count and generation. Publish a chunk only when complete. Use preallocated RAM for capture ingress, the recent live window and read-ahead. A near-live reader can use committed RAM while disk catches up. A delayed reader uses prefetched pages. Limit all caches and queues explicitly.

Track accepted source frames separately from frames committed to storage. The writer may overwrite only chunks entirely behind the protected reader boundary, including interpolation neighbours. Test physical-file wrap and generation changes. Do not rely on memory mapping alone to make disk access safe on the render thread.

When data cannot be supplied in time, mute without advancing R, report a storage stall and resume from the same position when available. If capture ingress fills or the oldest unread chunk would be overwritten, enter recovery before accepting destructive writes. Never drop unread music as a queue optimization.

### Full, empty and manual recovery

Retain the Windows listening policy for full history or catching live input: exactly three seconds of engine output silence at the engine sample clock, then re-centre to approximately 250 ms behind new input. Clear effects so tails do not fill the silent interval. External alarms or navigation can still sound independently; the three-second guarantee concerns our engine output.

During full-history recovery, explicitly abandon the old backlog, start a fresh capture epoch and accept new input for the restart. Show the reason and countdown. Manual re-centre acts immediately at a block boundary, cancels recovery, flushes queued output and clears effect history while retaining parameter settings. Storage I/O failure cannot use recovery to claim healthy processing; stop the route if the new epoch cannot be stored.

## Controls and observable states

The helper is the authority for session state. UI and car controllers issue commands and render acknowledged snapshots. All commands carry protocol version, session ID and monotonically increasing command ID. Absolute setters are idempotent; do not use a network-style toggle for pitch or pause. Repeated re-centre IDs apply once.

| Control | Required behaviour |
|---|---|
| Start | Prepare and commit a new route, retaining settings |
| Pause listening | Freeze the audible position and mute output; request upstream pause only through a supported source session |
| Resume listening | Continue from the held position; do not re-centre |
| Re-centre | Discard old backlog, return near live, retain pitch and effect settings |
| Original pitch | Set zero semitones; retain the listening position |
| Dry at current position | Bypass the effects rack at the same source cursor; retain varispeed |
| Stop processing | Tear down the route and discard this session history |

There is no ambiguous master "bypass" in the first UI. Original pitch, effects bypass and stopping the external route are separate actions. A later immediate-live bypass must identify that it changes listening position.

Pause is immediate for our output. Source control is best effort and asynchronous: record whether pause was acknowledged. If the upstream app continues, capture continues and history can fill. If all selected sources are confirmed paused and the capture tail is committed, stop appending idle mix silence while retaining the route. Preserve musical silence when a source is actually playing. On upstream resume, append new captured frames to the same held timeline with an explicit wall-clock discontinuity marker.

Track generated and presented output positions separately using AudioTrack timestamps when available. On pause, flush queued, unpresented output and restore the reader to the corresponding source position; storing only the last generated cursor would skip queued music. Where presentation timing is uncertain, report and test a bounded pause-position error rather than claiming sample-accurate Bluetooth resume.

The rendering ledger maps each queued output span to its source-cursor interval, epoch and parameter revision. Protect the oldest still-queued span from history overwrite. Restoring the source cursor after an output flush does not automatically rewind a reverb's internal state. Define and test the released effect's interruption behaviour separately; retain parameters and do not claim sample-identical effect-tail continuation across a flushed route.

Lifecycle states are STOPPED, CHECKING, PREPARING, RUNNING, HELD, INTERRUPTED, RECOVERING, STOPPING and ERROR. Shizuku absence, permission denial, source inactivity and unsupported routes are distinct reason codes, not additional hidden modes. A configuration change never implies a reset unless its acknowledgement says a new epoch was necessary.

## Source control and audio focus

Source apps remain responsible for their streams and ordinary focus requests. The processor must not request a competing permanent AUDIOFOCUS_GAIN just to play a transformed copy: Android can fade or pause the very source being captured. [Focus behaviour](SOURCES.md#audio-focus-and-source-control).

The proposed external-route adapter observes source media sessions and audio-policy focus events from the shell process. Use AudioPolicyFocusListener with setIsAudioFocusPolicy(false) as a candidate observation mechanism; do not take over system focus arbitration or replace the car's policy. Hidden callback delivery, auto-ducking visibility and shell AudioTrack behaviour must pass P0. If reliable interruption handling cannot be established on the target build, that build cannot pass the car release gate.

Mirror source transient loss to immediate output hold or attenuation as appropriate. Calls and communication-mode changes require immediate output silence and an INTERRUPTED state. A prompt must not wait behind buffered music. Observe ducking near playback time; a gain change captured at W will otherwise be heard much later at R. Avoid applying the same attenuation twice when Android has already changed the output or capture gain. Record traces and establish one gain owner per tested route.

Upstream pause, resume and skip use a selected MediaController only when advertised by that session. The helper can probe MEDIA_CONTENT_CONTROL; optional user-granted notification access is an alternative for source-session discovery if needed. It is not required to capture PCM. No accessibility automation or service-login credentials are introduced as a fallback.

When reliable source control is absent, disable forwarded transport actions and retain our own listening controls. Do not impersonate a source session or show a successful upstream pause before confirmation. A new source app or seek may create a metadata boundary without deleting old history. An explicit "skip to current source" action can request an upstream skip and then re-centre after new audio arrives.

For a controllable selected source, a confirmed external pause also holds local listening, and a confirmed external resume can resume a hold caused by that source. A user hold in TDSpeed has higher priority and cannot be cancelled by an unrelated source event. Distinguish end-of-stream or a stopped source from a deliberate pause: offer to finish buffered music when the source ends. Keep separate hold causes for the user, source, interruption and output loss; clear a hold only when its cause has ended.

## Android Auto integration

Implement Android Auto as a controller of the phone session. The helper stays on the phone. Use MediaLibraryService and a small SimpleBasePlayer adapter backed by the acknowledged engine state, not an extra ExoPlayer that requests focus and produces another audio stream. Expose a truthful "Current phone audio" library item. Authenticate permitted media controllers before accepting commands. [Media integration](SOURCES.md#android-auto).

Standard play/pause applies to buffered listening. Standard stop releases processing. Custom actions expose re-centre, pitch down, pitch up and original pitch, with command availability derived from state. Use compact pitch presets if the host limits actions. The car controls are in TDSpeed's surface; they do not alter the source app's screen.

Initially show the processor name, pitch and backlog. Do not show the source's current title as though it matches delayed audio. Later, timestamp metadata against captured source frames and select it using the presented cursor. Seek and track-boundary uncertainty must be reflected in the display.

Playback routing and dashboard discovery are separate tests. A signed APK may play through the car while remaining absent from the launcher. Personal sideload testing may require Android Auto's own developer option for unknown-source media apps. Public distribution and acceptance of the companion media experience remain separate release work; do not promise store approval.

## Failure containment and privacy

Bind one controller lease to one helper session. Use Binder death notification plus an initial 2-second heartbeat and 10-second lease timeout as redundant cleanup mechanisms. The normal foreground service sends heartbeats off the UI thread. The helper watchdog also stops on sustained capture/render failure or a dead output route. Tune watchdog thresholds with sleep and load tests; a blocked Binder or storage worker must not block cleanup.

Validate Binder callers against the bound application UID, package and signing identity. Handshake protocol and native ABI versions. Reject stale commands and arbitrary paths. Accept an already-open bounded file descriptor for history rather than a shell command. Keep components non-exported except the narrowly required Shizuku and car interfaces. Do not add a generic privileged command endpoint.

The base processing APK needs no internet, location, microphone, contacts or broad-storage permission. Declare foreground-service, notification, wake-lock and Shizuku integration requirements explicitly. Source-session permission alternatives are optional features. Hold a partial wake lock only for active audio work and release it on stop; confirm long idle/held behaviour on the device.

Persist settings, capabilities and counters. Keep raw audio and source titles out of ordinary logs. An explicit diagnostic export contains versions, formats, states, errors and timing counters; audio inclusion is a separate user action. Retain the project's licence and all applicable notices in the source and binary packages.

## Acceptance boundary

The design becomes an accepted implementation only when the [milestones](IMPLEMENTATION_PLAN.md) and [device matrix](INSTALLATION_AND_VALIDATION.md) pass. Android 13+, Shizuku availability and a successful policy registration are prerequisites; none alone proves a working music processor or car integration.
