# P2 first slice: live pitch and speed

Version 0.3.0-p2-pitch, protocol 2, implemented and tested on 21 October 2026.

This connects the existing verified dry core to the live rootless phone route. It is the pitch portion of P2, not the complete P2 milestone. The approved architecture documents and archived P0/P1 packages remain unchanged.

## Implemented

- Coupled pitch/speed from -12 to +12 semitones in 0.1-semitone increments, with -12/-4/+4 presets and an Original pitch action.
- 128 MiB stereo float RAM history: 16,777,216 source frames, approximately 349.5 source seconds at 48 kHz. This is history capacity, not a fixed listening-session duration.
- Visible applied pitch, speed, accumulated delay, capacity and recovery countdown.
- Re-centre retains pitch and returns to the reference's 250 ms engine offset. It is acknowledged at a processing-block boundary, rejects repeated command IDs and survives a subsequent pitch command arriving before the block applies it.
- Returning to zero pitch preserves accumulated delay. Only an explicit re-centre, new session or the reference's capacity/input-limit recovery discards it.
- The native engine's 250 ms starting offset replaces the P0 startup transport queue; they are not stacked. The physical output queue adds its own latency.
- Preallocated JNI state reporting avoids allocating a diagnostic array on the transport thread. Native history allocation happens before registering the replacement route. The underlying dry interpolation/rate/recovery algorithm is unchanged from P1.
- Existing foreground service, controller heartbeat, policy cleanup, diagnostic gain/swap and shutdown-race fixes remain in place. Protocol and helper build versions advance together so an old helper cannot silently use the new control interface.

## Tests

On the physical Samsung SM-S928B, Android 16 firmware recorded in the evidence, connected through paired wireless ADB:

- Seven native instrumentation tests passed. The original Windows fixtures still match all 24 cases and 315,750 checkpoint samples with zero sample error on ARM64. The new preallocated state-buffer API matches the ordinary snapshot and rejects invalid/read-only/unaligned storage.
- Live synthetic stereo through the real capture/output policy passed pitch commands, approximately 0.5 rate at -12, expected backlog growth, backlog retention at zero, one-shot re-centre, rapid re-centre/pitch command ordering and a +4 rate above 1.25.
- Observed backlog grew from about 0.29 s to 1.80 s at half speed, stayed about 1.80 s after returning to original pitch and returned to about 0.25 s after re-centre.
- Thirty routing start/stop cycles, source/helper isolation on the speaker route and controller-heartbeat expiry cleanup passed on the first P2 build (`8b1e0fd441bbbc09604ff916ea70888ac1d072b9d92a972f868f5940397e0532`). The final build adds a narrowly scoped lint annotation explaining the privileged REMOTE_SUBMIX recorder; no microphone permission is requested. Its live-pitch test passed again. The final thirty-cycle rerun overlapped the user's Tidal playback and failed the assertion that the separate tone source remained active on speakers; that full rerun is not counted as passed. The package retains the failed log and does not reuse the earlier APK's passing result for different bytes.
- Both Android ABIs build; session unit tests and app/native lint checks passed. Archived P1 desktop/reference/AddressSanitizer checks remain applicable to the unchanged core algorithm.

The first Wi-Fi endpoint changed after USB was removed. The paired mDNS identity rediscovered the phone without another pairing code. Shizuku required restarting during the connection switch and was started through wireless ADB as shell UID 2000. No root backend was used. Wi-Fi debugging is a development connection; PCM processing stays on the phone.

## Tidal listening check

On the final build, the user confirmed that Tidal at -4 semitones sounded clearly lower/slower and clean, then confirmed clean normal-speed playback after Original pitch. The measured effective rate was approximately 0.793702 at -4; returning to zero retained roughly 6.36 seconds of accumulated delay. Re-centre reduced it to roughly 0.25 seconds, with one application of the command. Zero underruns were observed in these live snapshots. The package's listening receipt records the user's re-centre response and final route state separately; do not infer any unrecorded response.

The user did not notice the initial musical jumps, including a subsequent 26-second jump. Those attempts are not counted as positive listening confirmations. A pause-based check with a 22.6-second buffer was also inconclusive because nearly 30 seconds elapsed during the chat exchange and the buffered music had already drained. The decisive repeat was executed as one timed sequence: with approximately 19.67 seconds of delay at normal speed, pause Tidal, verify zero new input and nonzero buffered output, allow several seconds of continued buffered playback, then apply re-centre and verify output silence with approximately 0.25 seconds of remaining delay. The user confirmed hearing music continue after source pause and then stop. Tidal was resumed afterward, and processing was left running at original pitch near live. Source pause/play in this diagnostic used wireless ADB media commands; app pause/source controls are still not implemented.

## Build and installation

Run `tools/Build-P2Pitch.ps1` with the existing Android Studio JDK/SDK. It builds the app, app test APK, native module/test APK, protocol tests and lint checks and audits the app package. `-Package` also records the available phone evidence and packages this milestone separately. It does not invent missing device results.

For deployment, use replacement installation to preserve Shizuku authorization. With multiple transports present, explicitly select the intended phone serial; do not run an unqualified connected-device task against every transport. Start Shizuku and the separate stereo source before routing instrumentation. Run the live-pitch and thirty-cycle methods as separate instrumentation invocations, allowing helper teardown to complete between them.

## Still pending

Acknowledged pause/resume and source transport, independent interruption holds, source selection/filtering, presets/preferences, notification pitch/re-centre actions, Quick Settings tile, one reference reverb, storage-backed long history, Bluetooth, car safety and Android Auto controls remain later work. P2 is not complete yet.

This slice retains P0's candidate interruption attenuation. It is not qualified for calls/navigation, automatic holds or car use. The single blocking capture/output transport is retained for this phone experiment; the architecture's separate bounded capture/render and storage paths remain to be implemented and qualified.

Source inactivity is not yet interpreted as a pause. A running silent source still advances capture/history. The app therefore labels idle input as unverified; stop processing when not listening. At positive pitch the engine can catch live input and enter its exact three-second silent recovery. Negative pitch accumulates finite history and eventually reaches its capacity. These are the existing reference policies, not unlimited playback claims.
