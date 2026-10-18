# Load, play, tweak and render

1. **Load song...**, or drop one audio file into the waveform. Loading prepares the original song; it does not render or save it.
2. In **Song + effects**, press **Play** and adjust pitch, effects and output. Pause holds position; Stop rewinds; **Restart song** restarts and clears effect tails. Turn off Bypass to hear processing.
3. Click the yellow **Render from start** when ready. It renders the whole loaded song from the beginning, plus effect decay, independently of the playback cursor and live buffer. No buffer reset is needed.
4. The selector switches to **Rendered file**. Listen to the result, then drag its waveform into the DAW. Mute the original and avoid applying pitch/effects twice.
5. **Save As...** saves separately. In Song + effects it renders current settings first if necessary; in Rendered file it saves that result. The original source is preserved.

Editing controls after rendering returns audition to the source at the corresponding position. The earlier render remains available for comparison as **Rendered (earlier)** until replaced. **Live input** returns to host/global audio processing. After file audition, restart DAW playback if a linked group reports missing history.

## Audio from a DAW

The waveform and editor accept supported audio-file drops. A host must supply a real file rather than an internal track object. SONAR documents dragging selected clips outside the application, including selection and clip effects: [Cakewalk documentation](https://legacy.cakewalk.com/Documentation?help=NewFeatures.125.html&language=3&product=SONAR). The user has confirmed the drag-in, render and drag-out workflow works in their DAW. Explorer and **Load song...** work independently of host clip-drag support.

The plugin loads the file supplied by the host. Clip edits and upstream effects are included only if the host bakes them into that file. Loaded PCM remains usable for playback and WAV rendering if the original temporary file disappears. The plugin cannot request arbitrary tracks directly.

## Waveform and interface

In the standalone app, **Song render** opens a compact panel with Load, playback, **Restart song**, a save-format selector and **Save As**. Opening it grows the app automatically; closing it returns to Live input and keeps the loaded song/result available. The main **Re-centre buffer** controls global live audio; **Restart song** inside the panel rewinds the file and clears effect tails.

Standalone saving defaults to **AAC at a 320 kbps target (.m4a)**, roughly up to 2.4 MB/min. Simple audio may use fewer bits, and codec constraints can limit the actual bitrate. **FLAC** compresses integer PCM losslessly at the rendered source depth. Float/32-bit renders are converted to 24-bit PCM first, as shown beside the selector. Out-of-range float audio is rejected rather than silently clipped during conversion. FLAC can still be much larger than the original MP3.

AAC needs FFmpeg. FFmpeg 8.1.1 and its AAC encoder were verified on this PC, so nothing needs installing here. On another PC, use `winget install --id Gyan.FFmpeg -e` and restart the app. Without FFmpeg, the panel defaults to FLAC, which uses the included native codec. FFmpeg is not bundled in this preview.

- Source waveform: click or drag to seek. Rendered waveform: drag to export the processed WAV.
- **Ctrl + wheel** zooms around the cursor. Wheel or scrollbar pans. **Double-click** shows the whole song; Fit has been removed.
- The waveform uses an I-beam cursor and highlights accepted file drags.
- **UI SIZE** offers Auto and 50-200%; all controls and the waveform scale together. Effects expand automatically.

The song/render survive editor reopening while that plugin instance remains loaded. Closing the editor pauses audition. Audio is session-only and is not embedded in DAW project state.

## Sound and files

Source audition uses Hermite varispeed and current effects without accumulating a live-input backlog. Its cursor remains continuous when pitch changes. Rendering snapshots the loaded source and controls and starts at frame zero, without the live startup reserve or history recovery.

Audition uses the host output and requires host callbacks. Source effects run at the host rate; final rendering retains the source rate. Rendered playback converts to the host rate and does not reapply pitch, effects or gain. Audition is excluded from offline host exports. Downstream host plugins, routing and mute still apply.

Source and rendered audition each have a 1 GiB PCM limit, in addition to live history. Decoding, file operations, waveform preparation and buffer retirement happen outside audio callbacks. Source samples are validated before playback.

The temporary render is WAV at the source/decoded rate, compatible depth and channel count. VST Save As suggests the original extension; standalone Save As uses the AAC/FLAC preset. WAV copies the rendered file exactly; FLAC/AIFF preserve compatible PCM depth/rate. MP3/M4A/AAC/OGG/Opus use external FFmpeg/ffprobe, installed on this PC but not bundled. Original-format VST saving uses detected source bitrate bounded to 32-320 kbit/s, or 192 kbit/s for PCM sources. The standalone AAC preset explicitly targets 320 kbit/s. Codec constraints can impose their own rate. Original-format compressed saving needs the original file for metadata. The AAC 320 preset and WAV/FLAC saves work without it. Lossy re-encoding adds an encoding generation. Slower files are proportionally longer/larger.

Cached renders live under `%LOCALAPPDATA%\TDSpeed Preview\Rendered audio`. Undragged files are cleaned up when replaced or the instance is destroyed; saves retain their input. Dragged files remain because some hosts reference them. Import/copy them into the project or Save As for permanent storage. Do not delete files a DAW still references.

Live VST processing still cannot resize a DAW clip or export range. This renderer creates the complete changed-length file separately. Existing live processing, groups, effects and racks remain available.

## Validation

Five targeted suites pass: processor/history, actual VST3, buffered tail, DAW synchronization and file rendering. Coverage includes loading without rendering, file-drop loading, pitch changes, effects, pause/restart/mono, unchanged live history, RAM/disk render identity, vanished temporary sources, full endings, rate/depth, wet decay, allocation-free callbacks including source effects/restarts, waveform zoom/scaling/reopening, saving and compressed formats. Twelve live comparison renders remain byte-identical to the accepted build.

The user confirmed the DAW drag-in/render/drag-out workflow. Standalone native Save As and physical playback after this panel revision remain for user checks; AAC/FLAC files and their decoding have passed automated tests. No new sanitizer run, hardware soak, installer or stable release publication is claimed.
