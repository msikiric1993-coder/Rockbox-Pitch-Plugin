# Android Auto routing investigation — 0.6.1 focus lab

Local experiment only. The working phone installation and published 0.6.0
artifacts have not been replaced. Nothing has been uploaded.

## Evidence

The user reports silence while playback advanced with listening started before
connecting the car. Stopping listening and restarting the streaming app restored
sound. Starting listening again left music audible with no pitch effect.

The supplied 0.6.0 report confirms the later bypass: Tidal and the shell output
route to Android Auto's media submix, while our separate input receives silence.
The pitch command was applied. The user explicitly stopped the earlier session;
its unregister event is not evidence of an automatic service interruption.

The AudioService history shows projection taking focus, Tidal taking it back,
then projection receiving focus loss. The original FocusObserver treats a media
focus loss from *any* client as a global mute. An old client's loss delivered
after a replacement client's grant can therefore leave output muted.

This is a demonstrated code defect and a candidate explanation for the initial
silence. The historical report does not contain the earlier session's own focus
trace or signal peaks, so it does not prove this defect caused that car symptom.

## Changes

- With the lab capture switch enabled, track the latest granted focus client by
  UID and client ID. Ignore displaced clients' loss events. Preserve current
  client ducking/muting and non-media grant protection. Ordinary mode retains
  its previous behavior; the separate audio-mode call protection is unchanged.
- Include focus attenuation in snapshots to distinguish intentional muting from
  silent capture.
- Reproduce the reported MEDIA/GAME PCM16 stereo mix and the separate
  ASSISTANT/NAVIGATION PCM16 mono 16 kHz mix in the shell routing harness. Our
  reported input matches MEDIA/UNKNOWN and excludes helper/controller UIDs.
- Version 0.6.1-focus-lab, code 10, prevents reusing the previous helper build.

## Verification

Android 16 emulator, shell UID 2000, independent quiet stereo tone source.
The actual framework AudioFocusInfo and FocusObserver callbacks reproduce old
attenuation 0 versus corrected attenuation 1 after a displaced projection loss.
Current-owner ducking and muting also pass. Three reducer regression tests and
the four existing lease tests pass. Build and shell lint pass.

Nine routing cases pass their expected assertions, including the three new
reported-rule cases. With projection first, processor input is zero and the car
receives the original ~0.08 peak. With processor first, including projection
joining ongoing playback, processor input is ~0.08 and car output ~0.04.
All test policies unregister and report no cleanup errors.

These are policy and focus tests, not real Android Auto or native varispeed
qualification. The exact usage rules alone do not reproduce the initial silence
on the emulator. The focus fix does not fix the projection-first routing bypass.
No Android Auto-owned policy is modified or removed.

## Local artifacts

`artifacts/routing-investigation/receipt.json` contains the emulator results.
`artifacts/routing-investigation/Varispeed-Car-Lab-0.6.1.apk` is the tested build.
SHA256: `417b29248b985264293443990c1fda91e8048d610bc44ea7ef65b7719200bcc1`.

Next physical validation is whether the focus correction restores the
processor-first chain. Already-connected automatic interception remains a
separate unresolved routing problem; no working implementation is claimed.
