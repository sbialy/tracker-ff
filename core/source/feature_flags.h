#pragma once

// FEATURE_FEATURES: the Features registry, the Config > Features page and the features that live in it (MIDI remote, Play stop lock,
// Chord names). On at every FEATURE_EXT_HOOKS build (the MK66 release, as before) and on Tracker+ and Mini with -DFEATURE_FF_FEATURES
// (core/makefile, PLATFORM=IMX): there the settings live in /Features/<id>.cfg of the card, no extension module, no restart mode,
// no developer mode, no USB audio. FEATURE_FEATURES_FULL is the MK66 set: those four and the USB entry that follows them.
#if defined(FEATURE_EXT_HOOKS) || defined(FEATURE_FF_FEATURES)
#define FEATURE_FEATURES 1
#endif
#if defined(FEATURE_EXT_HOOKS) && !defined(PLATFORM_IMX) && !defined(TRACKER_MINI)
#define FEATURE_FEATURES_FULL 1
#endif
