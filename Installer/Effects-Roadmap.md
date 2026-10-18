# TDSpeed effects expansion plan

Status: the rack and curated engine integration are implemented in 0.3.0. Researched 13 October 2026.

The build supplies 48 choices, including original hall/room/space/freeze, all three phaser stage counts, chorus/flanger/delay families, movement/tone/colour/dynamics, eleven selected Airwindows algorithms, CloudSeedCore, Freeverb3 families from Dragonfly, ChowMatrix diffusion, CHOW magnetic modelling and convolution with user IR import. It includes dynamic controls, factory presets, named favourites and A/B comparison. Source revisions/adaptations are recorded in `Source/ThirdParty/VERSIONS.md`.

Engine selection is deliberate: Freeverb3 STRev showed unstable output during the sample-rate/control-sweep tests in this wrapper, so the plate uses the Dragonfly NRev family, which passed. The rack embeds selected DSP, not the vendors' complete plugins or GUIs. Advanced through-zero, spectral frequency shifting, shimmer and reverse processing remain future modes requiring separate latency and stability work; they are not offered as unfinished menu entries.

## Listening experience

Keep the existing pitch interface. Add three serial effect slots after varispeed, each with a category, effect selector, preset selector, bypass, and mix control. Clicking a slot opens its controls; show only controls relevant to that algorithm. Start with all new slots bypassed. Save favourites and complete rack presets, with A/B comparison and reset for the selected effect.

The controls must have descriptive names and units. A chorus gets Rate (Hz), Depth (ms), Delay (ms), Feedback and Width; a hall gets Decay (s), Size, Pre-delay (ms), Damping (Hz), Diffusion and Width. An Advanced drawer holds less common controls. Mix and output level remain consistently placed. Preserve the existing window sizing and scrolling so nothing is clipped on a laptop.

## Candidate catalogue

These are proposed modes, not claims that every named source implements every mode.

| Group | Modes to offer | Main controls | Proposed implementation |
|---|---|---|---|
| Natural reverb | Room, hall, chamber, plate, early reflections | Decay, size, pre-delay, damping, diffusion, width | Original stereo feedback-delay-network hall first; evaluate Dragonfly engines later |
| Character reverb | Existing spring/PSX sound, dark vintage hall, bright plate | Decay, tone, modulation, pre-delay | Preserve current engine; evaluate selected Airwindows algorithms |
| Ambient reverb | Space, cloud, modulated wash, freeze | Decay, modulation, diffusion, tone, freeze | Airwindows Galactic/Verbity2; CloudSeed evaluation |
| Convolution | Rooms, venues, hardware impulses | IR choice, pre-delay, length, low/high cut | JUCE convolution, with licensed or user-supplied impulse responses |
| Chorus | Classic stereo, ensemble, subtle thickener | Rate, depth, delay, voices, width | JUCE building blocks and selected Airwindows chorus code |
| Flanger | Classic, negative feedback, stereo; through-zero later | Rate, depth, delay, feedback, phase | Original bounded modulated-delay engine |
| Phaser | 4/6/12-stage, stereo | Rate, depth, centre frequency, feedback, stereo phase | JUCE phaser first; extend stage selection after validation |
| Delay | Stereo, ping-pong, tape-style, multi-tap, diffuse | Time, feedback, tone, modulation, spread | Original delay lines; ChowMatrix DSP evaluation for complex routing |
| Movement | Tremolo, autopan, vibrato, rotary-style | Rate, depth, waveform, stereo phase | Original LFO engines; selected Airwindows candidates |
| Tone | Parametric EQ, low/high pass, auto-wah | Frequency, Q, gain, sensitivity | Existing JUCE filters plus original envelope follower |
| Colour | Soft saturation, tube-style drive, tape, bit reduction | Drive, tone, output, bits, downsample | Selected Airwindows engines; CHOW Tape evaluation |
| Dynamics | Compressor, gentle limiter, gate | Threshold, ratio, attack, release, makeup | JUCE DSP plus verified original control logic |
| Experimental | Ring modulation, frequency shifting, shimmer, reverse ambience | Frequency, depth, shift, feedback, duration | Later original engines; explicitly show any additional latency |

Manual delay time is the default for the standalone: Tidal provides no DAW tempo clock. A tap-tempo button can supplement it. The VST may use host tempo when available.

## Sources worth evaluating

1. [Airwindows](https://github.com/airwindows/airwindows), MIT. Broad DSP collection suitable for selective embedding. [Galactic](https://www.airwindows.com/galactic/) is a wide ambient reverb; [Verbity2](https://www.airwindows.com/verbity2/) adds crossmodulation to its reverb topology. Audition a small curated set rather than expose hundreds of similarly named choices. Preserve upstream attribution and parameter meanings.
2. [CloudSeed](https://github.com/ValdemarOrn/CloudSeed), MIT at the linked repository. Candidate for lush algorithmic ambience. Inspect the actual DSP repository and dependencies before integration; do not import its separate UI.
3. [Dragonfly Reverb](https://github.com/michaelwillis/dragonfly-reverb), GPL-3.0. Candidate family for room/hall/plate and reflections. Extract and adapt DSP rather than embed a second plugin interface. Review bundled Freeverb3 dependencies and comply with source-distribution requirements before distributing an integrated build.
4. [ChowMatrix](https://github.com/Chowdhury-DSP/ChowMatrix), BSD-3-Clause. Its tree of delay lines supports individual feedback, panning and distortion. A simplified fixed-node version would fit this listening app better than its full routing editor.
5. [CHOW Tape / AnalogTapeModel](https://github.com/jatinchowdhury18/AnalogTapeModel), GPLv3. Candidate for tape colour and modulation; defer until basic effects are stable.
6. JUCE DSP already in this project provides reusable chorus, phaser, delay, filtering and convolution components. Its existing licensing remains applicable; adding an MIT effect does not change JUCE licensing.

Pin the exact source revision of each imported engine and audit its files and dependencies. Include attribution and required source/licence material. These licence observations concern the linked projects, not a blanket clearance for all assets or dependencies.

## Poweramp-inspired reverb

Build an original smooth stereo hall aimed at the qualities you enjoy in Poweramp: dense reflections, a smooth tail, adjustable warmth and width, and useful low-mix presets for listening to full songs. Start with “Small room”, “Warm hall”, “Wide hall” and “Deep space”. Use a decorrelated stereo feedback network, input diffusion, pre-delay, and frequency-dependent decay; keep dry audio unaltered when the effect is bypassed.

A faithful Poweramp clone is not established: we do not have its algorithm or a reference recording/settings comparison. Treat it as a listening target. Your favourite Poweramp preset and knob values would let us tune the first hall much more closely. No Poweramp code or preset assets are included.

## Execution order

1. Rack foundation: effect interface with prepare/process/reset, metadata-driven controls, three slots, presets and state migration. Demonstrate bypass sample identity and unchanged legacy pitch/reverb settings.
2. First useful release: original smooth hall plus room preset, stereo chorus, flanger, phaser, stereo/ping-pong delay. Add favourite presets and effect-specific controls. Listen-test each with full songs and quiet material.
3. Character release: audition and integrate a few Airwindows reverbs/choruses; add ambient reverb, saturation, tremolo/autopan and EQ. Choose algorithms by listening comparisons and CPU cost.
4. Advanced release: CloudSeed/Dragonfly candidates, convolution, complex delay, tape and experimental effects only after their dependencies and performance are validated.

## Implementation constraints and checks

- Share rack DSP between standalone and VST, keeping existing parameter IDs and saved settings readable. Use stable effect IDs and a versioned slot state; do not repurpose an automated parameter ID for a different meaning when changing the selected effect.
- Prepare engines, delay buffers and impulse responses away from the audio threads. Use bounded processing and no settings writes or allocation in callbacks. The intentional pitch-history buffer and transport-clock correction stay independent of the rack.
- Smooth continuous parameter changes. No overlapping fades in the varispeed/history path. If an algorithm change cannot preserve its internal tail, apply that change at an explicit short mute boundary and explain that behaviour in the UI; do not silently reset the song backlog.
- Feedback must be bounded and denormals/non-finite output handled. Add output metering and an optional protective limiter, off by default so the existing sound remains unchanged.
- Verify bypass identity, preset round trips, old-state loading, bounded feedback, silence/tail behaviour, channel isolation and parameter sweeps. Exercise both standalone and VST at 44.1/48/96 kHz; test long listening, CPU spikes, device recovery and the existing three-second history recovery.
- Report added latency for effects that require lookahead, through-zero delay or spectral processing. Effects cannot eliminate the fundamental backlog of slowed live audio.
