# TDSpeed 0.4.2-rc.1 validation

The project owner accepted the source player and standalone Song render revision. RC-specific results, signature status and package hashes are in the accompanying Validation-report.txt. No two-hour soak is requested or performed.

New tests cover load without autoplay/render, source audition, complete changed-length rendering, effect tails, preview lifetime, waveform controls, cancellation, failed saves, native FLAC and optional FFmpeg/AAC. Sources and rendered previews are session-only. Keep permanent audio in the DAW project or use Save As. Live VST processing does not extend host clips or export ranges; Render from start creates a complete new file instead.

## Historical 0.4.1 baseline

The user accepted the 0.4.1 preview as working well on 18 October 2026. This release review preserves its plugin identity, parameters, effects and sound. No two-hour soak is required or performed.

## Review scope

Reviewed maintained processor/editor, DAW group coordination, rack/engine wrappers, fractional delays, scaling, standalone routing/transport/recovery, persistence, tests, build scripts, packaging and dependency notices. Vendored DSP algorithms are preserved; wrapper adaptations are recorded in Source/ThirdParty/VERSIONS.md. This is a targeted manual review with regressions, not a claim that every vendor line is defect-free.

## Safe changes

Invariant DAW recovery-period and rack smoothing calculations move outside inner loops. DAW rendering no longer calculates an unused listening ring position. Unused interpolation arguments/dead limiter branch are removed. Re-prepare releases an existing host-bypass vote before clearing ownership; empty bypass callbacks return safely and use denormal protection. Runtime metadata and all packaging/CI paths now match 0.4.1.

## Results

Baseline accepted preview: 5/5 short Release tests passed, 14.83 seconds. Full Release suite: 6/6 passed in 182.52 seconds, including the 52-engine / three-rate wet audit and the re-prepare host-bypass regression. The final UI notification fix is included in the actual hosted-editor and DAW tests.

Deterministic audio comparison loads the accepted and release VST3 modules through the same independent host. Twelve cases cover 44.1/48/96 kHz, DAW/Listening, -12/+7 semitones and MVerb/Signalsmith rack paths at 73% Mix. Both raw-float renders (12,056,384 bytes) have SHA-256 D0A855FAB4722FFA5B4CDAFEFAFEF8353A73F56D1F5DA87D6C6CDBA33B53AEA7: byte-identical. This covers the sampled steady processing paths, not every possible automation/control sequence.

Standalone native UI: locked frame, expansion/collapse and requested 100/125/150/200% layouts pass on the available 96 DPI monitor. Requested larger scales fit down to the monitor automatically. Standalone self-test passes. Hardware loopback measured dry 440.13 Hz and -12 semitones 219.80 Hz, zero transport gaps/drops in both short checks. No Windows routing change was needed for those tone checks.

Release-specific sanitizer results, package hashes and isolated installer checks are recorded in the Validation-report.txt release asset. This source document records the checks completed before packaging.

Startup dry forwarding, held Enable/Disable, missing saved-output fallback, streams remaining open, Stop/restore, forced-termination watchdog and manual routing ownership checks passed on this PC. Normal playback routing was restored. The final short Release recheck passed 5/5 in 16.72 seconds; the earlier full six-test run includes the wet audit.

## Practical limits

Variable-speed backlog is intentional; fixed DAW compensation cannot cancel it. Groups must run in one host process with all members processed. Windows default routing uses an isolated undocumented desktop COM interface. App/installer are unsigned; the original bundled cable driver is vendor-signed. Fresh-machine driver installation, physical USB/Bluetooth disconnects and every external DAW/DPI combination are not established by automated tests. The user's acceptance supplements, but does not replace, those checks.
