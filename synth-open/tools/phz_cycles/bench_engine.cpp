// Generic engine cycle bench, target side (see run_bench.sh ENGINE argument): bare-metal Cortex-M4F program that renders
// blocks of the engine named by -DBENCH_DEF (an EngineDef, e.g. kChipDef) the way the shim does: prepare() once per block,
// then every voice of the slot, then slotPost() for an engine with a slot effect. NOT firmware.
// Voices: kBenchVoices (BENCH_VOICES, default 4) voice records playing a chord (57 61 64 68), or for an engine with maxVoices 1
// (PERC, T-Type: one voice, multi-hit inside it) one record that gets 5 note-ons on top of each other (kit hits 60 62 64 69 52)
// and re-triggers them every 8 blocks so the measured window always holds the full hit load.
// Scenario 0 is the engine's default patch (CHIP: still in its 0.35 s decay), for CHIP scenario 1 + m is model m (Model param 0)
// on top of the defaults and scenario 9 is the default patch held in sustain (Decay 0.01 s). Scenario 20: every parameter at
// the top of its range, 21: every parameter at the middle of its range, 22: every parameter at the bottom (stress cases for any
// engine; a case that goes silent is flagged by the host side and not counted).
// Synth automation (open_mod.h): scenario 30 ramps every parameter a slot may drive (triangle over its range, 64 blocks, Volume
// kept) with serial++ and moving every block: the upper bound of the engine side, more than 4 slots can do. 31 ramps the first 4
// of the picker order (wave like params first) the same way: the realistic 4 slot case. 32 is the default patch plus the
// modulator alone (4 slots: 2 LFOs, 2 envelopes, stepped and added per slot as OpenPatch::effective adds them, amount 0 so the
// engine sees the same values): 32 minus 0 is what the automation itself costs per part and block.
#include "open_engine.h"
#include "open_mod.h"

using namespace ot::audio;

#ifndef BENCH_VOICES
#define BENCH_VOICES 4
#endif

namespace polysynth::open
{
extern const EngineDef BENCH_DEF;
}

namespace
{
constexpr int kBenchVoices = BENCH_VOICES;
SynthSlotScratch gScratch;
SynthVoice gVoice[kBenchVoices];
SynthParams gP;
float gL[kBlock], gR[kBlock];
ISynthEngine* gEng;
bool gKit;
int gBlock;
int gActive;
bool gStarted; // the kit voice has had its first note-on (the shim reuses it for every further note)
int gScen;
int gTick;
int16_t gRamp[polysynth::open::kMaxParams];
int gRampN;
polysynth::open::ModSlot gMod[polysynth::open::kModSlots];
polysynth::open::ModState gModSt[polysynth::open::kModSlots];

const polysynth::open::EngineDef& def() { return polysynth::open::BENCH_DEF; }

void startNotes()
{
    static const uint8_t chord[4] = { 57, 61, 64, 68 };
    static const uint8_t kit[5] = { 60, 62, 64, 69, 52 };
    if (gKit)
    {
        for (int i = 0; i < 5; i++)
        {
            SynthNote n;
            n.note = kit[i];
            n.pitch = float(kit[i]);
            n.reused = gStarted || i > 0;
            n.overlap = false;
            gVoice[0].note = n.note;
            gVoice[0].gate = true;
            gEng->noteOn(gVoice[0], gP, n);
        }
        gStarted = true;
        return;
    }
    for (int i = 0; i < gActive; i++)
    {
        gVoice[i] = SynthVoice();
        gVoice[i].slotScratch = &gScratch;
        gVoice[i].note = chord[i & 3] + 12 * (i >> 2);
        gVoice[i].gate = true;
        SynthNote n;
        n.note = gVoice[i].note;
        n.pitch = float(n.note);
        gEng->noteOn(gVoice[i], gP, n);
    }
}

// scenarios 30 / 31: the ramped parameters move, as the shim's refreshParams makes it with a modulated destination
void ramp()
{
    const auto& d = def();
    const int t = gTick++ & 63;
    const float tri = t < 32 ? float(t) / 32.0f : float(64 - t) / 32.0f;
    for (int i = 0; i < d.paramCount; i++)
    {
        gP.start[i] = gP.end[i];
    }
    for (int k = 0; k < gRampN; k++)
    {
        const auto& pd = d.params[gRamp[k]];
        gP.end[gRamp[k]] = pd.min + (pd.max - pd.min) * tri;
    }
    gP.moving = true;
    gP.serial++;
}

// scenario 32: the modulator alone, what the automation adds per part and block: the 4 slot steps (modAdvance) and the per slot add
// and clamp of OpenPatch::effective. The add[] / hit[] zeroing and the clamp loop of effective run without automation too (macros),
// so they are not counted here. Amount 0: the engine sees the same values as in scenario 0.
__attribute__((noinline)) void modulate()
{
    using namespace polysynth::open;
    const auto& d = def();
    for (int s = 0; s < kModSlots; s++)
    {
        modStep(gMod[s], gModSt[s], 120.0f);
    }
    for (int s = 0; s < kModSlots; s++)
    {
        const int i = gMod[s].dest;
        const float v = gP.end[i] + gModSt[s].out * (d.params[i].max - d.params[i].min);
        gP.end[i] = v < d.params[i].min ? d.params[i].min : (v > d.params[i].max ? d.params[i].max : v);
    }
}

void block()
{
    for (uint32_t i = 0; i < kBlock; i++) gL[i] = gR[i] = 0.0f;
    if (gScen == 30 || gScen == 31)
    {
        ramp();
    }
    else if (gScen == 32)
    {
        modulate();
    }
    gEng->prepare(gScratch, gP);
    for (int i = 0; i < gActive; i++)
    {
        gEng->render(gVoice[i], gP, 1.0f, 0.0f, gL, gR);
    }
    if (def().slotFx)
    {
        gEng->slotPost(0, gScratch, gP, gL, gR);
    }
    if (gKit && (++gBlock & 7) == 0)
    {
        startNotes();
    }
}
} // namespace

extern "C" void bench_setup(int sc)
{
    const auto& d = def();
    for (int i = 0; i < d.paramCount; i++)
    {
        gP.start[i] = gP.end[i] = d.params[i].def;
    }
    gP.count = d.paramCount;
    gP.serial = 1;
    gP.moving = false;
    if (sc >= 1 && sc <= 8)
    {
        gP.start[0] = gP.end[0] = float(sc - 1); // CHIP: Model
    }
    else if (sc == 9)
    {
        gP.start[14] = gP.end[14] = 0.01f; // CHIP: Decay
    }
    else if (sc >= 20 && sc <= 22)
    {
        for (int i = 0; i < d.paramCount; i++)
        {
            const float lo = d.params[i].min, hi = d.params[i].max;
            gP.start[i] = gP.end[i] = sc == 20 ? hi : (sc == 21 ? 0.5f * (lo + hi) : lo);
        }
        if (d.volumeParam >= 0)
        {
            gP.start[d.volumeParam] = gP.end[d.volumeParam] = d.params[d.volumeParam].def;
        }
    }
    gScen = sc;
    gTick = 0;
    gRampN = 0;
    if (sc == 30 || sc == 31)
    {
        int16_t pick[polysynth::open::kModPickMax];
        const int n = polysynth::open::modPicker(&d, pick);
        for (int k = 0; k < n && (sc == 30 || gRampN < polysynth::open::kModSlots); k++)
        {
            if (pick[k] < polysynth::open::kModPickMacro && pick[k] != d.volumeParam)
            {
                gRamp[gRampN++] = pick[k];
            }
        }
    }
    if (sc == 32)
    {
        int16_t pick[polysynth::open::kModPickMax];
        const int n = polysynth::open::modPicker(&d, pick);
        int got = 0;
        for (int k = 0; k < n && got < polysynth::open::kModSlots; k++)
        {
            if (pick[k] < polysynth::open::kModPickMacro)
            {
                polysynth::open::ModSlot& m = gMod[got];
                m = polysynth::open::ModSlot{};
                m.type = got < 2 ? polysynth::open::ModType::kLfo : polysynth::open::ModType::kEnv;
                m.shape = uint8_t(got == 0 ? 2 : 4);
                m.speed = 14;
                m.dest = pick[k];
                m.amount = 0;
                polysynth::open::modReset(gModSt[got], got);
                polysynth::open::modNoteOn(m, gModSt[got], true);
                got++;
            }
        }
        for (; got < polysynth::open::kModSlots; got++)
        {
            gMod[got] = polysynth::open::ModSlot{};
            gMod[got].type = polysynth::open::ModType::kEnv;
            gMod[got].dest = 0;
            gMod[got].amount = 0;
        }
    }
    gKit = d.maxVoices == 1;
    gActive = gKit ? 1 : kBenchVoices;
    gStarted = false;
    gEng = d.engine();
    gEng->slotReset(gScratch);
    for (int i = 0; i < kBenchVoices; i++)
    {
        gVoice[i] = SynthVoice();
        gVoice[i].slotScratch = &gScratch;
    }
    gBlock = 0;
    gEng->prepare(gScratch, gP);
    startNotes();
    for (int b = 0; b < 6; b++)
    {
        block();
    }
}

extern "C" void bench_run(int blocks)
{
    for (int b = 0; b < blocks; b++)
    {
        block();
    }
}

extern "C" float bench_peak()
{
    float p = 0;
    for (uint32_t i = 0; i < kBlock; i++) p = gL[i] > p ? gL[i] : (-gL[i] > p ? -gL[i] : p);
    return p;
}

// Bare metal: a bump allocator for the engine's one-off objects (CHIP creates its voices with new), no exit.
// libm (newlib, the firmware's toolchain) needs errno.
namespace
{
alignas(8) unsigned char gHeap[64 * 1024];
unsigned gHeapUsed;
} // namespace
void* operator new(unsigned n)
{
    void* p = gHeap + gHeapUsed;
    gHeapUsed += (n + 7u) & ~7u;
    return p;
}
void operator delete(void*) {}
void operator delete(void*, unsigned) {}
extern "C" void* memcpy(void* d, const void* s, unsigned n)
{
    auto* dd = static_cast<unsigned char*>(d);
    auto* ss = static_cast<const unsigned char*>(s);
    while (n--) *dd++ = *ss++;
    return d;
}
extern "C" void* memset(void* d, int c, unsigned n)
{
    auto* dd = static_cast<unsigned char*>(d);
    while (n--) *dd++ = static_cast<unsigned char>(c);
    return d;
}
extern "C" void __cxa_pure_virtual() { for (;;) {} }
// libc's abort() (reached from libm's error path only) wants these
extern "C" void _exit(int) { for (;;) {} }
extern "C" int _kill(int, int) { return -1; }
extern "C" int _getpid() { return 1; }
