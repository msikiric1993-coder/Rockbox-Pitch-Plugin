# Effects

## Curated picker

Reverb: Smooth Hall (Default/Warm/Wide), Small Room, MVerb, Dragonfly NRev Plate, Spring, PSX, Convolution.

Space: Signalsmith Ambient, CloudSeed cloud, Airwindows Galactic. Slot variants: Deep Space (Signalsmith), Nebula (Galactic), Universe (large, long Signalsmith), Dark Cloud (CloudSeed). They default to audible Mix and do not enable freeze.

Modulation: Stereo Chorus, Ensemble, Stereo Flanger, 6-stage Phaser. Delay: Stereo, Ping-pong, Tape-style. Tone/Colour/Dynamics: bell EQ, low/high-pass, soft saturation, CHOW magnetic tape and compressor. Advanced includes the other validated original algorithms without changing their saved indices.

## Reverb characters

Signalsmith uses an eight-channel feedback delay network with internal detuning and early reflections. Its Decay control is **RT60 in seconds**; the upstream RT20 value is RT60 / 3. Room size is milliseconds, pre-delay is separate. High/low cut, detune, early reflections and width have independent measurable effects. Width 0 intentionally folds the wet stereo return to mono. Its built-in dry return is disabled.

MVerb is a Dattorro-style plate/chamber. Decay shape is a normalized feedback control, not a claimed RT60. Damping and bandwidth control its damping/input filters. Size changes retime and clear its tank once; unchanged settings never clear it every block. Pre-delay is displayed in milliseconds (0-200 ms) and converted to the engine’s normalized control. Late balance selects the early/late return; the internal mixer is fully wet, including the initial block.

Smooth Hall/Small Room are original bounded delay networks. These are listening effects inspired by the user's preferred Poweramp character, not proprietary Poweramp code or an exact clone. Dragonfly entries use Freeverb3 algorithm families with TDSpeed controls; they are not copies of complete vendor plugins or presets. CloudSeed uses the original MIT CloudSeedCore, not its proprietary plugin UI. Spring/PSX preserve original TDSpeed characters. Convolution uses an original deterministic chamber or your IR.

## Balance

Mix uses cosine dry / sine wet weights for reverbs, spaces, delays and modulation; exact endpoint branches guarantee no residual dry at 100%. Insert effects use linear dry/wet blending to retain their tonal/dynamic changes. Fixed wet gains are in Source/WetCalibration.h and the measured baseline is included with validation. The calibration uses deterministic pink noise at engine defaults, not runtime loudness adaptation or a guarantee of identical loudness for every preset. Convolution compensates JUCE's 0.125 IR normalization target with a fixed 8x return gain.

Wet Level in Advanced is an additional -24 to +12 dB reverb/delay return control, default 0. Slot Output and Final Output are independent. Decay, filtering, feedback, nonlinear colour and correlated dry/wet audio can legitimately change level. Use the meter and trims.

## Faults and compatibility

An effect output that is non-finite or exceeds +/-32 before ordinary peak protection latches dry bypass. Reset effect clears that effect fault, not song history. Missing engines keep their identifiers/unsupported controls and show a bypassed state; choosing another engine explicitly replaces them. Missing IRs bypass until a valid file is loaded. Effect replacement retains a slot's On/bypass state; adding to an empty slot enables it. Replacing an active engine retains the existing short 10 ms change mute. No new shimmer DSP is included.
