# Varispeed 0.5.3 — responsive slider

The old UI sent pitch only in onValueChangeFinished, deferring the audible change
until finger release. It also reverted to the last acknowledged snapshot during
the interval before the new value arrived. This revision sends rounded 0.1-step
pitch updates during onValueChange and retains local requested state until a
matching acknowledgement. Presets and fine-adjustment buttons share this state.
No additional delay, smoothing or buffer reset was added to the DSP.

The slider has a 36 dp round thumb, 64 dp touch height and inset endpoints so zero
is easier to grab. Range stays -12 to zero. Readout groups occupy equal columns;
labels are centred under their own values on a common baseline. The prior moving
wave is replaced by two illustrations of the same segment on a common time
scale: the slowed segment extends to the right. This is not live PCM.

Build, lint and static package audit passed. Both native libraries are byte-
identical to the first successful baseline; DSP, routing helper and protocol
sources are unchanged. Two emulator UI tests passed, including actual Compose
touch gestures held down across updates and an assertion that stale snapshots
do not snap the display back. Test-only AndroidX dependencies were updated to
stable versions compatible with the Android 16 emulator.

Installed and version verified on the user's S24 Ultra. The real service was
observed RUNNING with nonzero music input and changing pitch. The user confirmed:
"Yes, it follows smoothly now" when asked about fast drags and fine adjustment.

## Android Auto status

No Android Auto fix has been implemented or tested. The user reports wireless
Android Auto continues playing unchanged music. The ordered retrofit family is
Motivesupplies MMI for BMW, EVO 6; exact hardware/firmware revision unverified.
Competing dynamic audio routes and processed-output capture restrictions are
hypotheses, not established diagnoses. Next work is a separate diagnostic build
to measure input signal and actual route ownership with Android Auto connected,
followed by an isolated output-capture experiment. No email/order personal data
is included in the artifacts. The successful phone build stays preserved.

APK, screenshot (synthetic UI state), hashes and receipts:
`releases/0.5.3-responsive-slider/`.
