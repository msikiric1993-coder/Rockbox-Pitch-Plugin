# TDSpeed simple Android edition — 0.4.0-simple-1g

This edition provides Start, Stop, coupled pitch/speed from -12 to +12 semitones,
Original pitch and Re-centre buffer. Diagnostic controls and diagnostic readouts
are removed from the listening screen. Internal test instrumentation remains.
There is no reverb, recording, audio-file saving or export control.

At the user's request this edition replaces the earlier 128 MiB RAM limit with
1 GiB (1,073,741,824 bytes) of RAM history. The original approved architecture
documents remain unchanged; this is a separate resource configuration, not the
planned storage-backed history phase. At 48 kHz stereo float32, capacity is
134,217,728 frames, or 46 minutes 36 seconds of source audio. Allocation happens
before audio routing changes. Start requires at least 1 GiB plus 256 MiB available
memory; it refuses instead of silently selecting a smaller buffer. Stop frees it.
Android may still terminate the process under memory pressure.

## Everyday use

1. Open Shizuku and check that it is running. After a phone reboot, start it
   again using its Wireless debugging option. The first phone-only setup may
   require Shizuku's own pairing step, even if a computer was already paired.
   See https://shizuku.rikka.app/guide/setup/ . Root is not needed.
2. Play music in Tidal or another source app.
3. Open TDSpeed and tap Start. Allow Shizuku access and notifications if asked.
   Wait for Listening. If memory is insufficient, close other apps and retry.
4. Adjust the semitone slider or a preset. Negative values lower pitch and slow
   playback together; -4 gives approximately 79.4% speed.
5. Original pitch restores normal speed while retaining the accumulated delay.
   Re-centre buffer discards that delay and jumps near live (about 0.25 seconds).
6. You can return to the music app or the home screen. The TDSpeed notification
   opens the controls and offers Stop. Stop clears memory and restores ordinary
   source playback. A new session starts at original pitch with an empty history.

No computer or USB cable is needed for listening once Shizuku is running.
Keep Developer options and USB debugging enabled on the phone, even with no
cable attached. Disconnecting the PC is different from disabling these settings.
The user identified disabling USB debugging / Developer options as the cause of
the interrupted session. Shizuku must be started again after reboot; pairing is
normally retained and is not a connection that must remain active. Continuous
operation across Wi-Fi loss and screen locking still needs a recorded device test.
Pitch changes do not control the source
app's playback position. If the finite history fills or playback catches live
input, the engine uses the existing three-second recovery behaviour.

## Validation boundary

Built with Android SDK 36, minimum Android 13/API 33. Tested on the connected
Samsung S24 Ultra, Android 16, arm64. The packaged evidence records eight native
tests, actual 1 GiB allocation and release, Windows-reference DSP comparisons,
live pitch/history/re-centre checks, and the repeated-routing test result.
Build, protocol unit tests and app/native/helper lint passed. See VALIDATION.json
for exact artifact hashes and test qualification; do not infer results for other
builds. Earlier listening checks confirmed Tidal and ReVanced YouTube on this
phone. This edition does not establish compatibility with every music service.

Bluetooth, Android Auto, calls/navigation interruptions, other phones and a long
listening soak remain unqualified. The APK uses a development signing key.

## First successful Android build

Preserved on 2026-10-21 as the first successful simple Android baseline.
APK SHA-256: `932d2c59a34d37401e16a769f02b59b5cd8351d98728eb225c0f0f7bcfda610a`.
Documentation updates do not change this installed binary.

## Later interface revision — recorded, not implemented

- Follow the established PC application's visual style.
- Larger, touch-friendly controls for semitones, Original pitch and Re-centre.
- Yellow live waveform with a clear representation of stretching during slowdown.
  A genuine waveform is feasible using a decimated peak envelope from the audio
  engine; rendering must stay off the audio thread. Clearly distinguish source
  time, processed playback and buffer delay instead of implying time preservation.
- Remove explanatory footer text such as the no-file-saving statement from the
  main listening screen; keep setup information in a separate help area.
- Preserve coupled pitch/speed behaviour and buffer semantics.

Keep this baseline intact while gathering everyday-use feedback before redesign.
