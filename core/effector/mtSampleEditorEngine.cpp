#include "mtSampleEditorEngine.h"
#include "mtEffect.h"
#include "mtEffectCut.h"
#include "mtEffectFadeIn.h"
#include "mtEffectFadeOut.h"
#include "mtEffectNormalizer.h"
#include "mtEffectReverse.h"
#include "mtEffectTimestretch.h"
#include "mtEffectWavetableSmoother.h"
#include "mtEffectModular.h"

#ifndef FW_SE_PTFX
#include "mtEffectAmplifier.h"
#include "mtEffectBitcrusher.h"
#include "mtEffectChorus.h"
#include "mtEffectCompresor.h"
#include "mtEffectDelay.h"
#include "mtEffectEQ.h"
#include "mtEffectFlanger.h"
#include "mtEffectLimiter.h"
#include "mtEffectOverdrive.h"
#endif

// A slot whose effect moved to a card file (FW_SE_PTFX) never points at a class: the list row goes through sampleEditorModular
// (cSampleEditor::selectListedEffect), the entry only keeps the table dense
#ifdef FW_SE_PTFX
#define SE_MOVED_EFFECT(name) &sampleEditorModular
#else
#define SE_MOVED_EFFECT(name) &sampleEditor##name
#endif

mtEffect* sampleEditorEffect[editorEffectMax] = { &sampleEditorNormalizer,
                                                  &sampleEditorCut,
                                                  &sampleEditorReverse,
                                                  SE_MOVED_EFFECT(Amplifier),
                                                  SE_MOVED_EFFECT(Overdrive),
                                                  SE_MOVED_EFFECT(Delay),
                                                  SE_MOVED_EFFECT(Bitcrusher),
                                                  SE_MOVED_EFFECT(Chorus),
                                                  SE_MOVED_EFFECT(Flanger),
                                                  SE_MOVED_EFFECT(EQ),
                                                  SE_MOVED_EFFECT(Limiter),
                                                  SE_MOVED_EFFECT(Compresor),
                                                  &sampleEditorWavetableSmoother,
                                                  &sampleEditorTimestretchBeat,
                                                  &sampleEditorTimestretchNote,
                                                  &sampleEditorFadeIn,
                                                  &sampleEditorFadeOut,
                                                  &sampleEditorModular };

//******************************DELAY

#ifndef FW_SE_PTFX
namespace delayParams
{
const char* const labelText[2] = { (const char*)"Feedback", (const char*)"Time" };
float upConstrain[2] = { 0.95f, 5000.0f };
float downConstrain[2] = { 0.01f, 1.0f };
float changeStep[2] = { 0.01f, 5.0f };
float displayMult[2] = { 100.0f, 1.0f };
const char* const afterValueText[2] = { (const char*)" %", (const char*)" ms" };
const char displayType[2] = { 'd', 'd' };
}
#endif

#ifndef FW_SE_PTFX
namespace bitcrusherParams
{
const char* const labelText[2] = { (const char*)"Bits", (const char*)"Rate" };
float upConstrain[2] = { 16.0f, 44100.0f };
float downConstrain[2] = { 1.0f, 500.0f };
float changeStep[2] = { 1.0f, 100.0f };
float displayMult[2] = { 1.0f, 1.0f };
const char* const afterValueText[2] = { nullptr, (const char*)" Hz" };
const char displayType[2] = { 'd', 'd' };
}
#endif

#ifndef FW_SE_PTFX
namespace amplifierParams
{
const char* const labelText[1] = { (const char*)"Amplifier" };
float upConstrain[1] = { 5.0f };
float downConstrain[1] = { 0.01f };
float changeStep[1] = { 0.01f };
float displayMult[1] = { 1.0f };
const char* const afterValueText[1] = { nullptr };
const char displayType[1] = { 'f' };
}
#endif

#ifndef FW_SE_PTFX
namespace overdriveParams
{
const char* const labelText[2] = { (const char*)"Gain", (const char*)"Type" };
float upConstrain[2] = { 5.0f, 2.0f };
float downConstrain[2] = { 0.01f, 0.0f };
float changeStep[2] = { 0.01f, 1.0f };
float displayMult[2] = { 1.0f, 1.0f };
const char* const afterValueText[2] = { nullptr, nullptr };
const char displayType[2] = { 'f', 'd' };
}
#endif

namespace timestretchBeatParams
{
const char* const labelText[4] = { (const char*)"Tempo",
                                   (const char*)"Steps",
                                   (const char*)"Micro-step",
                                   (const char*)"Granule" };
float upConstrain[4] = { 800.0f, 64.0f, 1.0f, 10.0f };
float downConstrain[4] = { 40.0f, 1.0f, 0.0f, 1.0f };
float changeStep[4] = { 1.0f, 1.0f, 0.01f, 1.0f };
float displayMult[4] = { 1.0f, 1.0f, 1.0f, 0.25f };
const char* const afterValueText[4] = { " BPM", nullptr, nullptr, nullptr };
const char displayType[4] = { 'd', 'd', 'f', 'f' };
}

namespace timestretchNoteParams
{
const char* const labelText[2] = { (const char*)"Length", (const char*)"Granule" };
float upConstrain[2] = { 8.0f, 1000.0f };
float downConstrain[2] = { 0.1f, 10.0f };
float changeStep[2] = { 0.01f, 1.0f };
float displayMult[2] = { 1.0f, 1.0f };
const char* const afterValueText[2] = { " s", " ms" };
const char displayType[2] = { 'f', 'd' };
}

#ifndef FW_SE_PTFX
namespace chorusParams
{
const char* const labelText[2] = { (const char*)"Length", (const char*)"Voices" };
float upConstrain[2] = { 4096.0f, 8.0f };
float downConstrain[2] = { 128.0f, 1.0f };
float changeStep[2] = { 128.0f, 1.0f };
float displayMult[2] = { 1.0f / 44.1f, 1.0f };
const char* const afterValueText[2] = { " ms", nullptr };
const char displayType[2] = { 'd', 'd' };
}
#endif

#ifndef FW_SE_PTFX
namespace flangerParams
{
const char* const labelText[4] = { (const char*)"Length",
                                   (const char*)"Offset",
                                   (const char*)"Depth",
                                   (const char*)"Rate" };
float upConstrain[4] = { 4096.0f, 100.0f, 8.0f, 5.0f };
float downConstrain[4] = { 128.0f, 1.0f, 1.0f, 0.1f };
float changeStep[4] = { 128.0f, 1.0f, 1.0f, 0.1f };
float displayMult[4] = { 1.0f / 44.1f, 1.0f, 1.0f, 1.0f };

const char* const afterValueText[4] = { (const char*)" ms",
                                        (const char*)" %",
                                        nullptr,
                                        (const char*)" Hz" };
const char displayType[4] = { 'd', 'd', 'd', 'f' };
}
#endif

#ifndef FW_SE_PTFX
namespace eqParams
{
const char* const labelText[9] = { (const char*)"Low",       (const char*)"Low freq",
                                   (const char*)"Low Width", (const char*)"Mid",
                                   (const char*)"Mid freq",  (const char*)"Mid Width",
                                   (const char*)"High",      (const char*)"High freq",
                                   (const char*)"High Width" };
float upConstrain[9] = { 12.0f, 200.0f, 5.0f, 12.0f, 8000.0f, 5.0f, 12.0f, 20000.0f, 5.0f };
float downConstrain[9] = { -12.0f, 10.0f, 0.5f, -12.0f, 100.0f, 0.5f, -12.0f, 8000.0f, 0.5f };
float changeStep[9] = { 0.05f, 1.0f, 0.05f, 0.05f, 10.0f, 0.05f, 0.05f, 100.0f, 0.05f };
float displayMult[9] = { 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f };

const char* const afterValueText[9] = { " dB", " Hz", "", " dB", " Hz", "", " dB", " Hz", "" };
const char displayType[9] = { 'f', 'd', 'f', 'f', 'd', 'f', 'f', 'd', 'f' };
}
#endif

#ifndef FW_SE_PTFX
namespace limiterParams
{
const char* const labelText[3] = { (const char*)"Threshold",
                                   (const char*)"Attack",
                                   (const char*)"Release" };

float upConstrain[3] = { 0.0f, 10.0f, 2000.0f };
float downConstrain[3] = { -60.0f, 0.3f, 1.0f };

float changeStep[3] = { 1, 0.1f, 1 };
float displayMult[3] = { 1, 1, 1 };

const char* const afterValueText[3] = { (const char*)" dB",
                                        (const char*)" ms",
                                        (const char*)" ms" };

const char displayType[3] = { 'd', 'f', 'd' };
}
#endif

#ifndef FW_SE_PTFX
namespace compressorParams
{
const char* const labelText[4] = {
    (const char*)"Threshold",
    (const char*)"Ratio",
    (const char*)"Attack",
    (const char*)"Release",
};
float upConstrain[4] = { 0.0f, 50.0f, 100.0f, 2000.0f };
float downConstrain[4] = { -60.0f, 1.0f, 0.3f, 1.0f };

float changeStep[4] = { 1, 0.5f, 0.1f, 1 };
float displayMult[4] = { 1, 1, 1, 1 };

const char* const afterValueText[4] = { (const char*)" dB",
                                        nullptr,
                                        (const char*)" ms",
                                        (const char*)" ms" };

const char displayType[4] = { 'd', 'f', 'f', 'd' };
}
#endif

namespace wavetableSmootherParams
{
const char* const labelText[2] = {
    (const char*)"Window Num",
    (const char*)"Window Len",
};
float upConstrain[2] = { 256.0f, 2048.0f };
float downConstrain[2] = { 1.0f, 1.0f };
float changeStep[2] = { 1.0f, 1.0f };
float displayMult[2] = { 1.0f, 1.0f };
const char* const afterValueText[2] = { nullptr, nullptr };
const char displayType[2] = { 'd', 'd' };
}

namespace normalizerParams
{
const char* const labelText[1] = { (const char*)"Level" };
float upConstrain[1] = { 100.0f };
float downConstrain[1] = { 0.0f };
float changeStep[1] = { 1.0f };
float displayMult[1] = { 1.0f };

const char* const afterValueText[1] = {
    "%",
};
const char displayType[1] = { 'd' };
}
