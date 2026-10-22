# Varispeed interface — 0.5.1

Display name: Varispeed. Existing application ID and development signing setup
are retained so this installs over TDSpeed without uninstalling it.

- Compact lowercase wordmark; removed the subtitle and redundant range label.
- Negative-only slider, -12 through 0 semitones, with -6/-4/-2/-1 presets.
  The + fine-adjustment button moves toward original pitch and stops at zero.
- Bold pitch value, paired speed percentage and large touch controls.
- Green Listening (Pitch effect on); red off/preparation/error states. The
  indicator describes processing-session state, not proof of an audible source.
- Compact delay value and actual buffer occupancy: backlog / full source-history
  capacity, not the previous 30-second display scale. The buffer remains 1 GiB.
- Yellow synthesized pitch visualization. Lower pitch stretches its wavelength
  and slows motion; this is explicitly an illustration, not the music waveform.
  It stops off-screen, while processing is off, and when system animations are
  disabled. It does not read PCM or add work to the audio processing thread.
- Start/Stop stays at the bottom; content scrolls on smaller screens.

The native libraries are byte-identical to the first successful baseline; DSP,
helper routing and protocol sources are unchanged. Only notification branding
changed in the controller service. Positive varispeed remains in the core; the
listening interface intentionally exposes slowdown and original speed only.
Faster playback can consume existing backlog but cannot stay ahead of live input.

Build, lint, static APK audit and Android 16.1 emulator UI test passed. UI test
checks all presets, Original pitch, Re-centre, Stop, minimum button height, Help,
on/off status and 10% occupancy for a known delay. Screenshots show synthetic
UI test state, not physical-phone playback. Physical phone and Android Auto were
not connected or tested for this revision. Audio startup requirements remain the
same: start Shizuku after reboot and keep Developer options/USB debugging enabled.

APK, screenshots, checksums and exact evidence: `releases/0.5.1-varispeed-ui/`.
First successful build remains preserved at `android-v0.4.0-simple-1g`.
