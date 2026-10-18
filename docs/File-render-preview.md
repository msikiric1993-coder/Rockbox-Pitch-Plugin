# Render source file - waveform preview

1. Set pitch, effects and output level. Click the yellow **Render source file** button near the top and choose the original song.
2. Confirm **Render**. The complete slowed/faster song appears below as a yellow waveform, including effect decay. No save dialog or DAW bounce is needed.
3. Use **Play / Pause**, **Stop**, click to seek, **Ctrl + mouse wheel** to zoom, and the horizontal scrollbar/wheel to move around. **Fit** shows the full duration.
4. Drag the waveform onto a DAW track. The dropped object is the rendered WAV. Mute the original and remove/bypass the original processing to avoid applying pitch/effects twice.
5. **Save As...** is optional. Choose WAV for an exact copy, or a supported compressed/lossless format. Saving does not render or apply the effects again.

**UI size** is visible beside the title: Auto or 50-200%. The waveform and its rulers scale with the rest of the interface. Effects expansion remains automatic. The render and waveform survive closing/reopening this editor while its plugin instance stays loaded; closing the editor stops audition playback.

## Sound and files

The renderer uses the existing Hermite pitch processing, current effect rack and final output level. It renders from the selected file's beginning and calculates the full changed duration, without the live startup reserve or live-history recovery. It includes this instance's settings at confirmation time, including pending group pitch; changing knobs afterward does not change the already-rendered preview. Plugin bypass does not cancel explicit file rendering; rack/slot bypass is respected. Other plugins, DAW clip edits, automation and other group members are not included.

The intermediate WAV retains the source sample rate, channel count and supported decoded bit depth. Preview audition uses a separate immutable PCM buffer (maximum 1 GiB) and adapts playback to the host's sample rate without changing speed. The already-rendered output is auditioned through the plugin's host output; live effects/output gain are not applied twice. The host audio engine must process this plugin to hear it. Audition is excluded from offline exports. Upstream/downstream host routing, track mute and downstream plugins still apply.

Temporary renders are cached under `%LOCALAPPDATA%\TDSpeed Preview\Rendered audio`. Undragged files are removed when replaced or their instance is destroyed (an active save keeps its file alive). A file used for an external drag is retained because some hosts reference it instead of copying it. Use the DAW's copy/import-to-project option or Save As for long-term project storage. Do not delete a referenced cache file. No cached audio is embedded into plugin project state.

Save As suggests the original extension. WAV copies the existing rendered samples exactly. FLAC/AIFF preserve supported PCM depth/rate; incompatible formats fail explicitly. MP3, M4A/AAC, OGG and Opus use external FFmpeg/ffprobe (already installed on this PC, not bundled) and detected source bitrate, bounded to 32-320 kbit/s. With a PCM source, compressed saving defaults to 192 kbit/s. Codec constraints can impose their own sample rate. Slower songs are proportionally longer/larger; compressed saving avoids keeping a large final WAV. Lossy encoding adds an encoding generation. Cancellation/failure preserves existing target files. The original source cannot be overwritten.

The earlier Arm track render / Fast Bounce workflow is removed from this build. Ordinary live VST processing still cannot extend the DAW's clip or export boundary; use this independent renderer when a complete slowed song is needed.

## Validation boundary

Processor/history, actual VST3, buffered-tail, DAW synchronization and file-render tests pass. Coverage includes 44.1/48/96 kHz; 16/24/32-bit PCM; full ending; wet decay; cancellation; audition play/pause/seek/stop/end/rate conversion; offline isolation; concurrent buffer replacement with zero callback allocations; waveform zoom bounds; expansion/scaling/reopening; Save As WAV/FLAC/overwrite/failure/source protection; and compressed export. The UI snapshot was visually reviewed. Twelve normal live renders remain byte-identical to the accepted build.

Native Sonar/Audacity drag/drop, save-dialog and stopped-host audition behavior still need a user check. No new sanitizer pass, hardware soak or stable release publication is claimed.
