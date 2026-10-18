# Standalone validation — 2026-10-13

## Passed on the development PC

- Windows 11 x64 Release app, existing VST3 target and DSP regression executable build successfully.
- Existing processor tests pass: 44.1/48/96 kHz, 2 GiB history, song-position continuity, stereo/mono, reverb, reset/state recall, and exact three-second recovery.
- Standalone self-checks pass: SPSC queue stereo mapping, wrap and overflow protection; metadata cable pairing; disabled/mismatched Hi-Fi detection; identical unity impulse through the linked DSP core.
- Native discovery finds both existing standard VB-CABLE and Hi-Fi Cable. Hi-Fi/Eversolo report busy while VSTHost owns them; both open after VSTHost closes. The additional 16-channel cable endpoint is excluded from stereo pairing.
- Windows policy COM default-output changes work on this Windows build. Communications routing is untouched.
- Actual shared-mode capture/render opens using Hi-Fi Cable (Windows mix 44.1 kHz) and Eversolo DAC (Windows mix 192 kHz), with the app processing at 48 kHz. Monitor and Yeti output smoke checks also pass.
- End-to-end signal test: a quiet 440 Hz tone rendered into Hi-Fi Cable is captured, processed at -12 semitones and measured via Eversolo output loopback at approximately 220.09 Hz. No transport underruns or overruns were reported.
- A 60-second Hi-Fi Cable → Eversolo stream check completed with zero queue underruns/overruns. This is a short stability check, not a multi-hour listening soak.
- Watchdog test terminates the monitored helper without cleanup: both playback roles restore to the physical device and the recovery journal is cleared. A manually changed role is preserved. Original defaults are restored after the test.
- Complete standalone UI was rendered and visually reviewed; small windows use scrolling so the original editor remains accessible.
- The packaged existing-driver install/uninstall test and installed-binary checks are recorded in `out/QA/installer-test.log`, `uninstaller-test.log`, and `installed-checks.json`.
- Original VB-Audio setup and Inno compiler installer Authenticode signatures verified valid before packaging. Third-party runtime DLLs are packaged app-locally.

## Still requires another machine or physical/user checks

- Fresh Windows 11 installation without any virtual cable: driver elevation, reboot, endpoint availability, and first-run routing. No fresh Windows VM/laptop was available in this session; do not label this acceptance case as passed.
- Tidal/browser music audition and comparison against the VST build. The actual Windows audio path was measured using a test tone; no Tidal account/session was operated.
- Physical USB/Bluetooth unplug/replug and automatic reconnection.
- Multi-hour playback under normal Windows load; native hardware at 96 kHz.
- Startup at sign-in and Windows shutdown behaviour across a real reboot.

This is a working first standalone release with verified hardware transport, signal processing and crash restoration, plus a packaged installer. It is not yet a fully qualified cross-device release. Both application and installer are unsigned; the bundled virtual driver is vendor signed.
# 0.2.1 startup routing fix — 13 October 2026

- Release app, VST3 and installer build succeeded.
- Existing TDSpeedRegression passed; standalone self-test passed including orphaned cable routing, preservation of manual output changes, unavailable destination rejection and feedback prevention.
- Preview generation passed. No live routing test or Tidal listening test was performed for this update while the existing app was running.
- Normal output is repaired on idle startup and output selection even when no recovery journal exists. Processing failure/disable also attempts idle repair. A running VSTHost prevents this repair to respect the existing host setup.
- Portable deliverable: `Standalone build/0.2.1/TDSpeed/TDSpeed.exe`; installer: `Standalone build/TDSpeedSetup-0.2.1.exe`.

# 0.3.0 effects rack — 13 October 2026

- A separate clean Release build of standalone, VST3 and both test executables succeeded, including the vendored DSP sources without a previously generated JUCE header.
- Existing TDSpeedRegression and TDSpeedEffectsRegression passed. All 48 choices were processed at 44.1/48/96 kHz at defaults and exposed control limits. Checks include dry sample identity, bounded finite output, audio-thread `new` allocation detection, preset/state migration, live user-IR handoff/adoption, exact 144,000-sample recovery silence at 48 kHz and recursive visible UI bounds.
- Choice indices are rounded consistently in DSP and UI, including slightly sub-integer recalled float values; a regression covers all 48 indices to prevent recalling the preceding engine.
- Worst measured single-engine wall-time/audio-duration ratio was approximately 8% on this development PC in the diagnostic sweep. This is a smoke benchmark, not a guarantee for every machine or combination of three slots.
- The Freeverb3 STRev plate wrapper showed unstable output during validation and was replaced with the NRev family. Underlying source remains in the corresponding source archive; STRev is not available at runtime.
- Corresponding adapted source, JUCE source, build scripts/tests and licences are bundled. The original signed standard VB-CABLE package remains unchanged.
- Release standalone self-test, endpoint probe and full UI preview passed. Hi-Fi Cable (native 44.1 kHz) to LG ULTRAGEAR (48 kHz) hardware signal test measured 220.0445 Hz from a 440 Hz source at -12 semitones, with zero capture overruns/render underruns. Eversolo was not active during this run.
- Forced-termination watchdog recovery, preservation of manual output changes and restoration of the original default roles passed on this PC.
- The final installer was smoke-tested in an isolated workspace install directory. It detected the existing usable cable and skipped driver installation; the installed executable passed its self-test and test uninstall succeeded. Packaged executable/source hashes matched the tested build/current source.
- Exact Poweramp matching has not been established. Full-song listening comparisons, multi-hour playback and the previously documented fresh-machine/device-disconnect acceptance checks remain manual validation.


# 0.3.1 reliability, UI and startup revision - 13 October 2026

- Final Release standalone, VST3 and three regression targets build successfully. `out/reliability/tests.log` records all three CTest suites passing. Pitch, 2 GiB history and exact three-second recovery remain unchanged.
- Exact fractional-delay boundary regressions reproduce the 20 ms float wrap at 44.1/48/96 kHz and check every write position, end-of-ring interpolation, excessive delays and non-finite delay inputs. Both interpolation indices are bounded and positions use doubles.
- Every one of the 50 engines is exercised at defaults and minimum/maximum controls at all three rates, including zero-Amount/Mix exact dry identity and allocation detection. Warm/Wide hall each run 20-second tone/impulse/silence sequences per rate, include maximum controls, produce nonzero decay tails, and stay finite/bounded without fault fallback concealing instability.
- Three additive delay slots preserve the initial dry transient exactly. Real runaway compressor output latches a slot fault and falls back to its dry input; ordinary DSP reset does not clear the fault. Reset effect clears it. Non-finite controls are sanitized. The peak guard remains separate and pre-guard rack peaks drive clipping indication.
- A separately instrumented MSVC AddressSanitizer build passes the delay and effects suites (`out/reliability/asan-tests.log`). DSP and imported engines are instrumented. Legacy reverb control sanitization, latched fault bypass and independent reset are covered as well.
- Picker regressions verify single-slot Warm/Wide variants, automatic enable for empty slots and preserved bypass on replacement. Panel expansion preserves rack parameters. Old states and user IR paths restore; legacy reverb retains its position before the rack. Expanded content uses scrolling at small window sizes. PNG snapshots were visually inspected.
- Hardware loopback checks use existing Hi-Fi Cable at native 44.1 kHz and Eversolo DAC at native 192 kHz, with transport/DSP at 48 kHz. Processed 440 Hz measures approximately 220 Hz at -12; dry pass-through measures approximately 440 Hz. Both report zero overruns and underruns (`out/QA/signal-check.json`, `dry-signal-check.json`). Measurement excludes startup and the test tone's trailing silence.
- Actual standalone controller startup in isolated settings selects an available physical fallback for a deliberately missing saved endpoint, starts dry, and retains the missing preference. First Enable recentres to 250 ms. Disable/Enable preserves buffered delay and does not reopen streams: the start counter remains one. Stop restores routing, and communications remains unchanged (`out/QA/bridge-check.json`).
- Deliberately invalid capture/render IDs fail with no running transport (`out/QA/stream-failure.json`). Forced-termination watchdog restoration, manual-role ownership and original-default restoration pass (`out/QA/watchdog-check.json`).
- The original TDSpeed icon is included as SVG, PNG, multi-resolution ICO, embedded executable/window/tray resources and installer icon. Optional regeneration uses Tools/Generate-Icon.py and Pillow; authoring dependencies are not runtime requirements.
- Installer and portable app, VST3 bundle, corresponding source and existing third-party licences are packaged as 0.3.1. Existing vendor-signed cable drivers remain unchanged.

Still unverified: actual full-song Tidal/browser listening, fresh Windows driver installation/reboot, physical USB/Bluetooth disconnects, sign-in startup and multi-hour/laptop playback. The measured hardware checks do not establish those acceptance cases or exact Poweramp algorithm identity.

# 0.3.2 rack saving and automatic fit - 13 October 2026

- Standalone and VST3 Release targets build; all three CTest suites pass (`out/rack-save-fix/tests.log`). DSP and growing song history are unchanged.
- The manually owned save dialog is explicitly shown, focused and themed. A regression invokes the actual Save rack button, verifies that the modal name window is visible, completes its callback and checks the XML saved to an isolated temporary path.
- Save/reopen/recall, same-name replacement, empty-name rejection, invalid-folder failure and reloading an already selected rack all pass. Persistence uses an atomic file replacement; in-memory favourites are committed only after successful disk writing. Tests leave personal favourites untouched.
- A/B is removed. Saved racks offers explicit Save rack and Load controls. Existing favourites remain compatible.
- Automatic window-fit diagnostic on the current 3440 x 1400 working area grows from 660 to 1220 pixels including a legacy reverb card, fits both the entire editor and effects content without scrolling, and collapses to 580 pixels (`out/rack-save-fix/ui-check.json`). A PNG is rendered without starting audio or writing personal settings. Expansion uses the window's current monitor and repositions it on screen. Physical screens too small to contain the whole interface retain scrolling.
- Matching source and licences are packaged with 0.3.2 installer/portable/VST3 deliverables. The icon and cable package are unchanged.
