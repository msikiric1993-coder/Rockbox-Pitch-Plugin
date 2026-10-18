# 0.4.2-rc.1

- VST source player: load or drag in a song, audition pitch/effects, render from the beginning, then drag the extended waveform into the DAW or save it separately.
- Expandable standalone Song render panel, Restart song, native FLAC and optional AAC at a 320 kbps target through FFmpeg.
- Playback, seeking, waveform zoom/pan and source/rendered comparison. Rendering is independent of the live buffer and DAW export range.
- Retains synchronized DAW groups, automatic whole-window sizing, scaling, racks and Rockbox-style coupled pitch/speed.
- Existing TDSpeedPreview identity retained for session compatibility. Live VST processing still does not extend DAW clips.
- Matching source, runtime libraries, licences and validation report. Trusted application signing requires a publisher certificate; original VB-CABLE fallback remains vendor signed.

# Changelog

## 0.4.1 — 18 October 2026

- DAW transport timing, same-process linked track groups, shared bypass and pitch latched at Play.
- Fresh history on Play/seek/loop, clear warnings for missing track history; Listening held playback preserved.
- Whole-interface Auto/50–200% scaling, automatic expansion and fixed VST3 content sizing.
- Release polish preserves the accepted pitch/effects sound and parameter/plugin identities.
- Hoist invariant DAW recovery-period and rack smoothing calculations; avoid unused DAW ring-position calculation.
- Release stale host-bypass votes on repeated prepare; guard empty bypass callbacks and suppress denormals.
- Fix standalone collapse notification when JUCE applies resize limits synchronously.
- Update installer, portable/VST3/source packaging, runtime version, CI paths and dependency adaptation notes.

## Earlier history

## 0.4.0-rc.1

- Isolated TDSpeed RC application, installer, VST3 identity, settings, racks, startup registration and recovery journal; copy old settings once and reserve the forwarding bridge across older versions.
- Restored true dry/wet Mix, exact endpoints, equal-power ambience/modulation, fixed measured wet calibration, advanced Wet Level and prominent slot/final output controls.
- Added pinned Signalsmith Basics FDN ambience (correct RT20-to-RT60 conversion) and MVerb plate/chamber; fixed MVerb initialization/first-block mixing and cached geometry setters.
- Curated grouped picker with slot-scoped Deep Space, Nebula, Universe and Dark Cloud variants; validated older algorithms retained in Advanced with stable saved indices.
- Locked window and editor sizing, automatic coordinated expansion/collapse, whole-app scaling and compact shared Legacy controls. Removed nested scrolling/resizing and retained repaired rack Save/Load/overwrite behavior; A/B remains removed.
- Explicit IR loading/ready/failure; decode/build/adoption off the audio callback with dry bypass for failed/missing IRs. Unsupported saved engines preserve their identifiers and bypass explicitly.
- Preserved Hermite varispeed, 2 GiB growing history, three-second recovery, dry startup and held playback over Disable/Enable.
- Strengthened raw wet-output/control-response tests, calibration/renders, hardware diagnostics, AddressSanitizer checks, source/licence packaging, hashes and Windows CI.

This is a release candidate. The user accepted the fully wet reverb comparisons. Fresh-machine, DPI/host, app-specific listening and physical-disconnect checks are recorded in validation; no publication was performed. The user removed the two-hour soak requirement; completed short hardware checks are used instead.
