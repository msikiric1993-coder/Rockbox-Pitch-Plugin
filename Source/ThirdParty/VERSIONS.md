# Embedded engine revisions and adaptations

The sources below were retrieved from the authors' repositories on 13 October 2026. No vendor plugin GUI, installer, preset art or fonts are embedded.

| Component | Source | Pinned revision | Licence |
|---|---|---|---|
| Airwindows consolidated DSP | https://github.com/baconpaul/airwin2rack | 8a67b80e04230a5cf40e84d6e8b9a2fd70b13a75 | MIT |
| CloudSeedCore | https://github.com/GhostNoteAudio/CloudSeedCore | See `CloudSeed/REVISION.txt` | MIT |
| Freeverb3 from Dragonfly | https://github.com/michaelwillis/dragonfly-reverb | 440ec7b3b3db2fec34b9f80cf4890102ca14d1c3 | GPL-2.0-or-later DSP; Dragonfly repository GPL-3.0 |
| ChowMatrix diffusion | https://github.com/Chowdhury-DSP/ChowMatrix | 40d8e0ef1f752a6843099ff3dfc3d99132b332eb | BSD-3-Clause |
| CHOW Tape magnetic model | https://github.com/jatinchowdhury18/AnalogTapeModel | 604372e4ffd9690c3e283362e4598cb43edbb475 | GPL-3.0 |

Airwindows: selected algorithm files only. Added `tdReset()` using the existing DSP constructor initialization, excluding allocation-owning plugin metadata. Reset seeds are deterministic; default parameter values are restored and then the rack reapplies its controls. Original processing loops remain intact.

CloudSeed: use the original dark-plate base configuration with selected live controls. Fixed uninitialized diffuser/flag fields, initialized scaled parameter defaults before sample-rate setup, and replaced the temporary seed vectors in `UpdateLines` with a fixed-size stack array using the same LCG and blend formula. Seed-changing controls are not exposed in the audio callback. Its fixed-capacity delay storage is allocated before streaming.

Dragonfly: embed Freeverb3 ZRev2, Progenitor2, NRev and EarlyRef algorithm families, with TDSpeed's own wrapper and controls. STRev was evaluated and showed unstable output in the wrapper's control-sweep/sample-rate tests; it is not selected at runtime. These are the underlying DSP families, not exact copies of Dragonfly plugin presets or complete plugin signal chains. Room geometry is set during prepare; live pre-delay uses a separate preallocated line. Separate output buffers are required for EarlyRef.

ChowMatrix: namespace the original diffusion unit and include JUCE types explicitly. TDSpeed supplies the bounded four-node echo network; this is not the full ChowMatrix tree editor or processing graph.

CHOW Tape: extract the scalar magnetic hysteresis processing and mathematical operations. Select the original scalar path, remove the neural STN dependency and use the numerical NR4 solver. This is the magnetic model, not the complete oversampled tape plugin with all its additional stages.

The downloadable source archive contains these adapted files, licence texts, TDSpeed source, tests, build scripts and JUCE source. Rebuild instructions are in `Installer/BUILD.md`. Retain all corresponding notices when redistributing.

## RC additions (14 October 2026)

| Component | Repository | Pinned revision | Licence |
|---|---|---|---|
| Signalsmith Basics | https://github.com/Signalsmith-Audio/basics | 369e906e03760ec219d07b06fa06dc4fd099f377 | MIT |
| Signalsmith DSP | https://github.com/Signalsmith-Audio/dsp | ca390d28c41ea5108a528e2e81faa4088f2f8ac8 | MIT |
| MVerb | https://github.com/martineastwood/mverb | 9f9512e9d990fcf265e00fc7c2ee47ad20c8019f | GPL-3.0-or-later |

Signalsmith: added the omitted delay4 reset and initialized geometry-offset arrays before the first parameter fade. TDSpeed uses separate input/output buffers, sets internal dry=0/wet=1, converts displayed RT60 to RT20 by dividing by three, supplies external pre-delay/width, and configures storage before audio processing.

MVerb: normalized initial PREDELAY to 0.1 (20 ms) instead of an already converted sample count; set MixSmooth to requested Mix before each process block so a fully wet wrapper cannot leak dry input on first prepare/reset. MVerb reset also clears previous feedback samples, initializes all control smoothers and converts its normalized pre-delay to samples consistently. The wrapper sets internal MIX/GAIN=1, uses separate output buffers, applies cached changes only, and passes unsmoothed SIZE values because upstream SIZE clears its tank. The header is included in tdMVerb to avoid collisions with the original SpringVerb Allpass type. Vendor UI/assets are archived as source but not compiled or used by TDSpeed.

CloudSeed pin: deb21ded9eb7dad9b3ff94ce1ba96a963716594e. JUCE is a complete vendored source snapshot; its tree hash inventory accompanies the source package, and the version is recorded in JUCE/CMakeLists.txt. No dependency fetch occurs at runtime or during engine builds. Fixed wet calibration is a TDSpeed wrapper adaptation, recorded in Source/WetCalibration.h and the included measured baseline. Tone/dynamics preserve intentional gain changes.

Tests/Reference/SpringVerb-0.3.0.h is copied from the preserved v0.3.0 corresponding-source package solely to generate listening comparisons. All original third-party notices are retained. Microsoft app-local runtime components and standard VB-CABLE Pack45 are separate aggregated vendor packages under their own redistribution terms.

Freeverb3 EarlyRef: mute() now clears the four output-filter memories as well as delay/allpass storage. These reset fixes are covered by noise-then-reset-silence and impulse onset tests.

## VST3 editor constraint adaptation

JUCE 8.0.10 VST3 wrapper: checkSizeConstraint uses an editor's exact min/max constraints when resizability is disabled. This lets Effects/Advanced request the new required size instead of retaining the host's previous viewport. Host-forced viewports still scale the complete interface. No host native window styles are changed.

Vendored JUCE base revision: 1b460fe0895635a2ab8ac5c00cb5575e33e5dc1e (8.0.10), plus the documented TDSpeed VST3 constraint adaptation. Build-generated JUCE example x64 output is excluded from Git and source packages.
