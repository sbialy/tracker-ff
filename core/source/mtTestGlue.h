#pragma once
// The image's side of the factory self test (core/source/mtTest.cpp, cTest). Every build calls these. In the release image of the original
// Tracker (FEATURE_SELFTEST_MODULE) the test itself is the self test tenant of /Plugins/Ext/extras.plg: the image keeps the pad combination
// and forwards through ext_hooks.selftest, the module is loaded when the combination is complete. Elsewhere they call mtTest directly.
#include <stdint.h>

class Platform;
class InputProcessor;

void selftestSetup(Platform* platform);           // interface start: the codec, radio, buttons, encoder, timer, battery and card
void selftestLoop();                              // every main loop pass
bool selftestCombo(uint8_t pad, bool mini);       // one pad (or the Mini's button) of the start code: true when the code is complete
bool selftestReady();                             // the test can run: loads extras.plg, else a popup ("Self test needs extras.plg")
void selftestStart(InputProcessor* ip);           // eventActivateTestingProcedure

// The RAM test writes the whole SDRAM. Run from /Plugins/Ext/extras.plg (FEATURE_SELFTEST_MODULE) the test's own code and data sit in the
// release region (extras.ld: [__ext_base, __noinit_start)), so those words are neither written nor checked.
#if defined(FEATURE_SELFTEST_MODULE) && !defined(UI_SHOT_HOST)
extern "C" uint32_t __ext_base[], __noinit_start[];
static inline bool ramTestSkips(const uint32_t* p) { return p >= __ext_base && p < __noinit_start; }
#else
static inline bool ramTestSkips(const uint32_t*) { return false; }
#endif
