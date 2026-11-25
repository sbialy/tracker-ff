#ifndef LIBS_AUDIO_STEREO_SENDS_H_
#define LIBS_AUDIO_STEREO_SENDS_H_

// FEATURE_STEREO_SENDS (poly-stereo-io step 3): stereo input for the delay and the reverb on the original (MK66).
// The send mixers stay the mono sum M = (L + R) / 2 (a pair sends both chains at half gain). A second "side" mixer per FX carries
// S = (L - R) / 2 (leader +gain/2, follower -gain/2, line in left +, right -). The reverb gets M on port 0 and S on port 1, the delay
// gets S on port 1. With no pair linked every side gain is 0, the side blocks are missing and both FX run their old code, bit for bit.
// Only where the mute gate and the 13 input send mixers exist (SYNTH_OPEN images, not the Tracker+ graph).
#if defined(FEATURE_STEREO_SENDS) && !defined(PLATFORM_IMX) && defined(FEATURE_SYNTH_OPEN)
#define FW_STEREO_SENDS 1
#endif

#endif
