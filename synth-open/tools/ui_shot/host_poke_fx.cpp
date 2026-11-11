// host_poke.cpp helper: the delay and reverb the audio engine holds after audioEngine::init and after a project load. Host only, NOT firmware.
// The private members of the engine, the delay and the reverb are read through #define private public.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#define private public
#include "mtAudioEngine.h"
#undef private
#include "mtStructs.h"

// "masterfx FEEDBACK DAMP": put the project values FEEDBACK (percent, 0..100) and DAMP (0..1) in mtProject, let the project load path set the
// delay and the reverb (fProjEdit.cpp: engine.setDelayFeedback, engine.setReverbDamp), read the feedback byte of the delay and the low pass
// coefficient of the reverb, scramble both, run the init path (engine.beginDelay, engine.beginReverb, what audioEngine::init calls) and
// read them again. The line ends with "same" when the two states are equal, "DIFFER" when not. The load state is put back at the end.
extern "C" void hostPokeMasterFx(unsigned feedback, float damp, std::string* out)
{
    audioEngine& e = engine;
    mtProject.values.delayFeedback = (uint8_t)feedback;
    mtProject.values.reverb.damp = damp;

    e.setDelayFeedback(mtProject.values.delayFeedback);
    e.setReverbDamp(mtProject.values.reverb.damp);
    const unsigned loadFb = e.delay->feedback;
    const float loadLp = e.polyverb->reverb.lp_;

    e.delay->setFeedback(0.0f);
    e.polyverb->setDamp(0.123f);
    e.beginDelay();
    e.beginReverb();
    const unsigned initFb = e.delay->feedback;
    const float initLp = e.polyverb->reverb.lp_;

    e.setDelayFeedback(mtProject.values.delayFeedback);
    e.setReverbDamp(mtProject.values.reverb.damp);

    char b[256];
    snprintf(b, sizeof b, "masterfx feedback=%u damp=%.3f load: delay byte %u reverb lp %.6f, init: delay byte %u reverb lp %.6f, %s", feedback,
             (double)damp, loadFb, (double)loadLp, initFb, (double)initLp,
             (loadFb == initFb && loadLp == initLp) ? "same" : "DIFFER");
    *out = b;
}
