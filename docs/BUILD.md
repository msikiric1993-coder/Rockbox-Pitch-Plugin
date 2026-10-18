# Reproducible Windows build

Install Visual Studio 2022 with Desktop development with C++, Windows SDK, CMake >=3.22 and Python >=3.9. All audio-engine sources and JUCE are vendored; rebuilding them needs no network download. Their revisions/adaptations are documented in Source/ThirdParty/VERSIONS.md.

From this project folder:

```powershell
cmake --preset windows-release
cmake --build --preset release --parallel 4
ctest --preset release
```

The app links the same processor/editor library used by the VST3 wrapper. It never loads a VST file at runtime. Output names are TDSpeedPreview.exe and TDSpeedPreview.vst3. Plugin code TdP1 and bundle ID com.fyodor.tdspeedpreview distinguish older versions.

AddressSanitizer:

```powershell
cmake --preset windows-asan
cmake --build --preset asan --parallel 4
# Add your MSVC bin/Hostx64/x64 directory to PATH for the ASan runtime.
ctest --preset asan
```

Tests cover history/pitch/recovery, exact delay boundaries, effect faults/control limits/allocation checks, concurrent IR replacement, rack persistence, UI scaling and actual wet engine output. TDSpeedEngineAudit writes measured impulse/music WAVs and engine-audit.json. Its fixture is an original synthesized chord pattern; comparison files include the archived v0.3.0 Spring/PSX sources, identified under Tests/Reference. TDSpeedPluginHostSmoke loads the actual VST3 bundle through an independent JUCE host and checks parameter IDs, three-rate dry forwarding, wet-only Signalsmith, state recall and the locked editor.

For installer/portable/source packaging, run Tools/Fetch-PackagingDependencies.ps1 once, then Tools/Build-Standalone.ps1. That script accepts BuildDir, DependencyDir and ReleaseDir, runs release tests unless SkipTests is specified, includes hidden CI files in the source archive, copies local Microsoft runtime DLLs, verifies the VB-CABLE signature/hash and creates independent RC installer/portable/VST3 packages and SHA-256 manifests. It then runs Verify-Release.py to check every package hash, archived source against the current project, and ZIP binary/source contents against the build. SkipBuild only packages an existing build.

Dependencies: the original standard VB-CABLE Pack45 and Inno Setup 6.7.3 are downloaded from their vendors. The unpacked driver is never modified. Its signed x64 setup executable is pinned by SHA-256; the compiler setup signature must be valid. Do not put vendor installers or build output into the Git repository.

Hardware diagnostics are optional local commands; CI cannot provide real audio endpoints. Use --probe, --self-test, --route-check, --bridge-check, --dry-signal-check, --signal-check, --routing-test and --ui-check. --soak-check takes capture ID, cable playback ID, physical output ID, report.json and seconds (up to 7200); it opens real streams, injects a very quiet stereo signal, processes -3 semitones plus three effects, and logs gaps, drops, peaks, faults and intentional backlog. It does not change Windows defaults. Do not run bridge diagnostics during a soak: forwarding is deliberately exclusive. Endpoint IDs and paths must be quoted.

Runtime: WASAPI shared-mode stereo float 48 kHz; Windows converts native formats at each stream boundary. A preallocated queue and bounded clock resampling track only transport occupancy, not intentional pitch backlog. Buffers/history/engines are prepared before streaming. The undocumented desktop default-output setter is isolated in WindowsAudio.cpp and validated separately. Routing uses console/multimedia roles, a recovery journal, ownership checks and a hidden watchdog process.

GitHub Actions builds/tests Windows Release and ASan configurations. Hardware/subjective/fresh-machine checks remain manual gates. No signing certificate is supplied; release app/installer files are unsigned. Keep the root licence, vendor notices and corresponding source with redistributed packages.

The VST3 reports zero fixed algorithmic latency for the effect engines; musical pre-delay/echo and the intentionally variable varispeed backlog are not a fixed compensation delay. Tail metadata sums enabled engine tails, uses exact IR duration/Signalsmith RT60 and feedback-based delay estimates; normalized legacy/vendor decay shapes use conservative estimates. Freeze can sustain indefinitely. Host compensation for variable varispeed timing and conservative vendor tails should be confirmed in the intended DAW before publication.

## RC packaging and signing

`Tools/Build-Standalone.ps1 -Version 0.4.2-rc.1` creates matching installer, portable, VST3 and source packages. Supply `-SigningThumbprint` and `-SignTool` to sign the app, plugin module and installer with an installed trusted publisher certificate and SHA-256 timestamp. Without those inputs packages are unsigned and must be labelled accordingly. Never put private keys or credentials in Git. Source/ZIP verification runs after signing.

FFmpeg is optional and not redistributed. Native FLAC works without it; AAC uses the encoder found beside the executable or on PATH.
