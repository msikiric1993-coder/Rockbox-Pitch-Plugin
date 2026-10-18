# Track render / Fast Bounce preview

## Use it

1. Close the DAW and replace the existing complete TDSpeedPreview.vst3 bundle with this preview. Keep only one copy in the DAW's scan paths.
2. Set your pitch and effects. Click the prominent orange **Arm track render** button near the top. Confirm and choose the FINAL slowed WAV filename.
3. In the DAW, select the original track/time range. Keep Track FX enabled and run a silent Fast Bounce/offline export. No source-file picker or real-time listening is needed. Use a DIFFERENT temporary filename for the DAW's own export; that dry bounce is only a capture pass and can be discarded.
4. The plugin finalizes the captured input and renders its own complete slowed WAV with the pitch/effects snapshot from arming. If capture remains active after the bounce finishes, click **Finish capture**. Keep/reopen the plugin window so its interface can finalize/start the render.
5. Drag **Drag file into DAW** onto the destination track, or click it to show the file in Explorer and import it. Mute the original and avoid applying the same pitch/effects twice.

The original track range is captured, including silent passages, clip edits and upstream plugins within that range. TDSpeed's own automation, downstream plugins, other tracks and group members are not included. Use a continuous range; a detected seek/loop jump fails rather than joining unrelated audio. Do not click Finish while the bounce is still running.

## What it does

Audio reaching this instance is recorded BEFORE TDSpeed processing. During the armed offline pass the DAW receives dry audio, not the shortened live-varispeed result. The separate disk-based renderer then calculates the full slowed/faster duration and includes the selected effect decay. It avoids the live history limit, startup reserve and original DAW clip boundary.

Captured host audio is saved as a 32-bit float WAV at the host's capture sample rate and mono/stereo layout. The host audio does not carry the original compressed file format/bitrate. WAV size therefore follows uncompressed sample count. The existing **Render source file** fallback remains available, including compressed-format preservation where supported.

Ordinary real-time playback is never recorded. File writes happen only during host-marked offline processing, where faster-than-real-time work can wait for disk I/O without blocking real-time playback. Capture storage is disk-based with bounded processing buffers. Cancellation/failure does not commit a partial final output. Capture preparation survives host re-prepare; reset/release/leaving offline mode can finish a nonempty capture, with manual finishing as fallback.

## Validation and limits

Five targeted regressions passed in 7.22 seconds. Integrated capture test covers no recording during real-time playback, re-prepare from 48 to 44.1 kHz, raw input identity through normal and host-bypass callbacks, all 44,100 frames captured, no live-history advance, automatic finalization, 88,200-frame half-speed output, retained final marker, empty-capture rejection and discontinuous timeline rejection. Existing file rendering/compressed format, DAW synchronization and actual VST3 smoke checks also pass.

Twelve unarmed live-audio renders remain byte-identical to accepted 0.4.1 (SHA256 D0A855FAB4722FFA5B4CDAFEFAFEF8353A73F56D1F5DA87D6C6CDBA33B53AEA7). The expanded editor screenshot was reviewed: the orange button is visible near the top and controls fit.

Native Sonar/Audacity fast-bounce acceptance is NOT yet established by these synthetic-host tests. The host must process this armed instance in offline mode. A host that skips disabled effects, clones a different instance for export, uses another process, or never marks offline processing may not send capture data. The interface reports no offline input instead of silently claiming success. Normal VST export still cannot resize the original DAW clip; the independently saved WAV is the intended result. No new sanitizer run or stable release package is claimed here.
