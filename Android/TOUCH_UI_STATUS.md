# Android touch interface — 0.5.0-touch-ui

PC-inspired charcoal panels, mint pitch controls and yellow buffer-delay display.
Large semitone and speed readouts; 0.1-semitone adjustment buttons, presets,
Original pitch and Re-centre. Start/Stop stays anchored at the bottom while the
control panel scrolls when space is limited. Main buttons are 52–64 dp high.
Setup and explanatory text moved into Help.

The yellow graphic represents measured buffer delay on a 0–30-second scale,
with an exact numerical value beyond that scale. It is not an audio waveform.
Live waveform work remains separate. No Android Auto routing changes are included.

The DSP, shell audio helper and IPC sources match the first successful baseline.
Both packaged native libraries are byte-identical to that baseline. Build, lint,
static APK audit and emulator UI test passed. The test checks button callbacks,
minimum touch target height and Help; its screenshot uses synthetic listening
state and does not demonstrate live audio. No physical phone was connected for
this revision. Install the APK over the existing debug app, without uninstalling,
to retain its app data and permissions. Shizuku startup requirements are unchanged.

Artifacts and exact validation: `releases/0.5.0-touch-ui/`.
Previous baseline: tag `android-v0.4.0-simple-1g`, preserved unchanged.
