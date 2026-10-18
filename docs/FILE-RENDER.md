# Render slowed file - 0.4.2 preview

1. Open the updated VST and set pitch, effects and output levels.
2. Click **Render slowed file** at the bottom. Choose the original source audio file.
3. Confirm the settings and choose a new output filename. The original extension is suggested automatically.
4. Wait for rendering/encoding. Use **Cancel render** if needed.
5. Drag **Drag file into DAW** onto your DAW track, or click it to show the result in Explorer and import it. Mute the old clip and bypass/remove the original pitch/effects instance to avoid processing twice.

The result has the full changed duration, plus effect decay. It starts at the source file's beginning without the live startup reserve. The source file is never overwritten. This renders the chosen file and this instance's settings, not DAW clip edits, other plugins, automation or other group members. The chosen pitch is shown in the confirmation (including a currently pending group pitch). Plugin bypass does not disable this explicit file render; rack/slot bypass states are respected.

## Format and size

WAV, FLAC and AIFF retain the source sample rate, supported bit depth and mono/stereo channel count. Unsupported bit-depth/format combinations fail explicitly. Floating-point source output is preserved as WAV. Lossy output has no PCM bit-depth setting.

MP3, M4A/AAC, OGG and Opus are re-encoded using the detected source bitrate and source sample rate where the encoder supports them. FFmpeg and ffprobe must be available on PATH or beside the host executable; both are already installed and tested on this PC. They are external tools, not bundled in this preview. Other PCs may need FFmpeg installed. No silent fallback to a large WAV occurs.

At half speed, a compressed song is about twice as long and about twice the size at the same bitrate, plus effect decay/metadata. Variable-rate encoding can differ. Compressed exports use temporary PCM data while encoding; temporary files are removed afterward. Allow temporary disk space for uncompressed audio. Re-encoding lossy audio introduces another encoding generation.

A private renderer uses the existing Hermite interpolation and effects engines, reads source data from disk in small blocks and calculates the full output duration. It does not depend on the DAW export range, live history or its 2 GiB limit. Live playback is not stopped or modified. Missing IRs, unavailable enabled engines, non-finite/runaway effects, infinite freeze, write failures and cancellation report an error rather than committing partial output.

## Preview validation

Five targeted regressions pass (7.00 seconds): processor/history, actual VST3, buffered-tail test, DAW synchronization and file rendering. End-marker tests verify the full ending and slower/faster file lengths. WAV source rate/depth checks cover 44.1/48/96 kHz and 16/24/32-bit PCM. Wet reverb decay, in-progress cancellation and source protection pass. Compressed file tests pass for MP3/M4A/OGG/Opus, including an MP3 job on the actual background worker. MP3 fixture: 25,389 bytes in, 52,461 bytes out at half speed. The expanded editor snapshot was inspected and the button is visible.

Twelve live-audio comparison renders still match the accepted 0.4.1 output exactly (SHA256 D0A855FAB4722FFA5B4CDAFEFAFEF8353A73F56D1F5DA87D6C6CDBA33B53AEA7). Native Sonar/Audacity file chooser and drag/drop acceptance still need a user trial. No new sanitizer pass is claimed for this preview. Stable 0.4.1 downloads remain unchanged.
