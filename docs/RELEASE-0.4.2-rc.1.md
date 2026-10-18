# Rockbox Pitch Plugin 0.4.2-rc.1

This release candidate adds a complete-song workflow alongside the existing live effect.

## VST3: drag in, tweak, render, drag out

Drop a song/DAW audio file into the waveform, or use Load song. Loading does not start rendering. Press Play in Song + effects, adjust pitch and effects, then Render from start. Drag the yellow rendered waveform into the DAW as a complete new clip, or Save As. Mute the original and avoid applying the processing twice. This avoids cutting slowed songs at their original clip length.

The host must supply an audio file when dragging in. Loaded source and preview audio are session-only; copy rendered media into your DAW project or save it permanently.

## Standalone: render and save a song

Open Song render, load/drop a file, audition and tweak, then Render from start. Restart song rewinds audition and clears its effect tails. Save as FLAC for lossless PCM compression or AAC (.m4a) at a 320 kbps target. Float/32-bit audio converts to 24-bit PCM for FLAC. FLAC works out of the box. AAC and additional compressed formats need external FFmpeg on PATH (`winget install --id Gyan.FFmpeg -e`), followed by an app restart. FFmpeg is not bundled.

## Download choices

- Setup EXE: installs the standalone app and includes the original vendor-signed standard VB-CABLE fallback. Existing usable supported cables are reused; cable drivers are never removed on uninstall.
- Portable ZIP: standalone app, runtime libraries, licences and corresponding source. A supported cable is needed for live listening, but not for file rendering itself.
- VST3 ZIP: complete plugin bundle, runtime libraries, licences and corresponding source. No virtual cable needed.
- Source ZIP: the matching working source and build/test instructions, including vendored DSP dependencies.

The internal TDSpeedPreview app/plugin names and identities are preserved for existing sessions. Installing updates that Preview application. RC packages do not overwrite archived older releases.

## Boundaries

Live varispeed still accumulates backlog when slowed. Re-centre buffer returns to current incoming audio. Live VST processing cannot resize the host clip/export selection; use the new renderer or select a longer export range. DAW synchronization groups, effect racks, scaling and normal live sound are preserved.

See Validation-report.txt and SHA256SUMS.txt for checks and exact artifacts. The bundled VB-CABLE driver is vendor signed. The RC application and installer remain unsigned until a trusted publisher certificate is supplied; hashes do not replace a publisher signature. This is a release candidate, not a claim of fresh-machine/physical-disconnect or all-host acceptance. No two-hour soak is performed.
