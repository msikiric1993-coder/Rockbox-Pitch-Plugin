# Varispeed 0.7.0 — one app for phone and Android Auto

## Use

Open **Varispeed**, start Shizuku if needed, play music in the source app and tap
**Start listening**. The same control handles phone speaker, headphones and
Android Auto. It can be started before or after the car connects; no car mode
button or capture switch is needed. Allow Shizuku access and notifications if
requested after a fresh installation.

Pitch and speed remain coupled. Original pitch preserves accumulated delay;
Re-centre discards delay near the live input. The buffer remains 1 GiB in RAM.
Stop removes our capture route and restores ordinary playback. Reports are in
**Help → Share connection report**. Reports never include recorded music and
are shared only through the user's chooser. Keep debugging enabled for Shizuku
as previously established; a PC connection is not needed during listening.

The installed package remains `com.fyodor.tdspeed.android.debug`, version code
13, label **Varispeed**. This continues the owner's normal development-signed
installation lineage. The Car Lab build type remains available for development
but is not installed as the everyday app. This repository version includes the APK, source and validation receipts.

## First Stop after connecting the car

The user confirms that 0.6.3 worked in the car and survived repeated Stop/Start.
The remaining symptom occurred on the first Stop of a session started before
Android Auto: music position continued but sound remained absent until retrying.

The latest report was exported after stopping and disconnecting Android Auto.
Its retained audio metrics describe the last successful session, not a live
recording of the first mute. Its event history nevertheless corroborates the
transition: after the first Stop, Tidal stays on device 12022 rather than Android
Auto's device 12009. Later Stops return it to 12009. The final device inventory
does not identify the former device, so its physical type is not asserted.

The old cleanup restored the Android Auto rules while our own older primary mix
still existed, then removed our mix. Native rule updates explicitly reevaluate
output selection; unregistering a loopback mix follows a different path. That
ordering is a concrete defect consistent with the first-session-only symptom.
The Samsung-specific audible failure has not been reproduced on the stock emulator.

0.7.0 removes our input policy before asking the guardian to restore the car's
rules. The guardian also waits for capture removal on a normal Stop or parent
disconnect. It checks the restored native rules and active eligible source
routes, and performs one bounded rule refresh if a source has not returned to
the car. A failure is reported rather than silently acknowledged as restored.
Reports retain stopRestoreStatus/stopRestoreDetail after cleanup.

## Integration

The normal app enables automatic car handling unconditionally at session start.
The former Car Lab preference cannot disable it. The large experimental panel
and capture switch are absent; an active car route has a small status line.
Connection report sharing moves into Help. On devices without the projection
package, the guardian remains inactive. On ordinary phone/headphone routes it
avoids repeated policy dumps while no other remote-submix output is present.

Verified connection caching, boot invalidation, recorder-based discovery and
the sibling-listener check are retained. Sibling detection excludes the current
package, so the normal app does not mistake its own UID for another installation.
No native DSP, pitch slider, interpolation, history allocation or re-centre
algorithm was changed. Both packaged native ABIs are byte-identical to 0.5.3.

## Verification

- Fifteen actual Android shell routing cases pass. The two new cases register
  the processor before/after the car respectively, forward transformed tones,
  stop without restarting the source, and measure original audio at the car
  receiver afterward (~0.08 peak versus ~0.04 while processing).
- Fifteen protocol unit tests pass.
- Four scoped UI instrumentation tests pass: existing controls, responsive
  slider, retained lab controls and the unified normal screen/report action.
  The legacy Shizuku integration class requires an independently initialized
  Shizuku emulator and is not included in this scoped UI run.
- App/helper lint and build pass. APK native libraries match the working 0.5.3
  baseline byte for byte.
- Installed over wireless ADB on the S24 Ultra. Device APK SHA256 matches the
  tested local APK. The isolated Samsung native-route capability probe passes
  and unregisters its test policy; it does not capture real music.

0.7.0's corrected first Stop still needs the user's parked-car confirmation.
The field success already established belongs to 0.6.3. Android 13 remains the
minimum build target, not a compatibility claim for every device or head unit.

## Local receipts

APK, routing/GUI receipts, source archive, hashes and VALIDATION.json are under
`artifacts/varispeed-0.7.0/`. The original report is kept in private-reports there
and excluded from source/repository release packaging. The original architecture
documents remain an immutable design baseline; this note records current behavior.
