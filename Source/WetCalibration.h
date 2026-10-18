#pragma once
// Fixed 48 kHz pink-noise calibration: last eight seconds of a 16-second render.
// No runtime gain adaptation. Convolution retains its IR normalization compensation.
inline float tdWetCalibration(int engine) noexcept { switch(engine) {
case 1: return 2.723f; // hall
case 2: return 3.2614f; // room
case 3: return 1.981f; // space
case 300: return 0.7594f; // dragon_hall
case 301: return 1.378f; // dragon_room
case 302: return 0.89f; // dragon_plate
case 303: return 0.6673f; // dragon_early
case 200: return 2.0618f; // cloudseed
case 10: return 1.0187f; // chorus
case 11: return 1.2736f; // ensemble
case 12: return 0.5815f; // flanger
case 13: return 0.9647f; // negative_flanger
case 14: return 1.0316f; // vibrato
case 15: return 0.8671f; // phaser6
case 16: return 0.8932f; // phaser12
case 20: return 0.9406f; // delay
case 21: return 0.9415f; // pingpong
case 22: return 0.971f; // tape_delay
case 23: return 1.3725f; // multitap
case 24: return 0.9446f; // diffuse_delay
case 25: return 1.3295f; // matrix_delay
case 100: return 2.1496f; // air_Galactic
case 101: return 0.367f; // air_Verbity2
case 102: return 0.967f; // air_StereoChorus
case 103: return 1.4572f; // air_ChorusEnsemble
case 104: return 1.026f; // air_GalacticVibe
case 105: return 0.7107f; // air_Chamber2
case 106: return 0.636f; // air_kPlateA
case 4: return 2.723f; // freeze
case 17: return 0.8503f; // phaser4
case 70: return 1.4537f; // spring
case 71: return 1.5108f; // psx
case 400: return 0.5611f; // signalsmith
case 401: return 1.0843f; // mverb
default: return 1.0f;
}}
