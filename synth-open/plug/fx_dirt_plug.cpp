// Dirt as a runtime master insert plugin (dirt.plg). One source, two builds:
//   - plugin: synth-open/plug/build_plug.py links this file with ../ff_dirt.cpp, ../ff_dirt_ctl.cpp and plug_rt.c into dirt.plg
//   - in-image: synth-open/plug_dirt_inimage.cpp includes it with PLUG_IN_IMAGE (bench baseline, the same code from flash)
// The kernel is the shipped int16 ffdirt::process, wrapped to the float ABI, so the plugin sounds exactly like the built-in
// Dirt entries of the Saturation list (bit identical when ff_dirt_ctl.cpp is built at the firmware's -Os, see README).
// Licence: MIT (OpenTracker DIRT port, see ../ff_dirt.h and licenses/LICENSE-OpenTracker-fx-MIT.txt).
#include "plug_abi.h"
#include "ff_dirt.h"

#ifdef PLUG_IN_IMAGE
#define PLUG_ENTRY_NAME plug_entry_dirt
#else
#define PLUG_ENTRY_NAME plug_entry
extern "C" const plug_host* plug_rt_host;   // plug_rt.c: libm and memcpy forward to the host table
#endif

namespace
{
// Parameters of the original DIRT module (opentracker-fx modules/dirt, ranges and units as there). Defaults = the built-in
// "Dirt Soft" entry (Drive 35, no crush, no tone) with Mix 100, so a fresh Dirt insert sounds like it always did.
enum Param : uint32_t { kType, kDrive, kBits, kRate, kTone, kMix, kParamCount };

struct Inst
{
    ffdirt::Coef coef;
    ffdirt::State st;
    ffdirt::Coef oldCoef;   // the coefficients and state before the last change: faded out over one block
    ffdirt::State oldSt;
    float wet;              // the wet amount the last block ended on (Mix, ramped)
    float wetTarget;
    uint8_t v[kParamCount];   // type drive bits rate tone mix
    uint8_t ran;              // a block has run: a change from now on crossfades
    uint8_t pending;          // oldCoef and oldSt hold the pre-change state, the next block crossfades from them
    int16_t ol[ffdirt::kBlock], orr[ffdirt::kBlock];   // the faded-out copy of the block (instance, not stack)
};

const char* const kNames[4] = {"Soft", "Hard", "Fold", "Asym"};
const plug_param kParams[kParamCount] = {
    {"Type", "", 0.0f, 3.0f, 0.0f, 1.0f, 4, 0, kNames},
    {"Drive", "%", 0.0f, 100.0f, 35.0f, 1.0f, 0, 0, nullptr},
    {"Bits", "", 4.0f, 16.0f, 16.0f, 1.0f, 0, 0, nullptr},     // 16 = no crush
    {"Rate", "fr", 1.0f, 32.0f, 1.0f, 1.0f, 0, 0, nullptr},    // hold every n frames, 1 = off
    {"Tone", "", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, nullptr},   // 0..99 = low-pass 200 Hz .. 20 kHz, 100 = off
    {"Mix", "%", 0.0f, 100.0f, 100.0f, 1.0f, 0, 0, nullptr},
};

// The coefficients of the stored values (powf and tanf once per change, never in the block loop)
void rebuild(Inst* s)
{
    ffdirt::Preset p = {s->v[kType], s->v[kDrive], s->v[kBits], s->v[kRate], s->v[kTone], ""};
    ffdirt::makeCoef(s->coef, p);
}

int32_t init(void* p, const plug_host*)
{
    Inst* s = static_cast<Inst*>(p);
    s->st.reset();
    s->oldSt.reset();
    for (uint32_t i = 0; i < kParamCount; i++) s->v[i] = uint8_t(kParams[i].def);
    s->wet = s->wetTarget = 1.0f;
    s->ran = s->pending = 0;
    rebuild(s);
    return 0;
}

void reset(void* p)
{
    Inst* s = static_cast<Inst*>(p);
    s->st.reset();
    s->ran = s->pending = 0;
    s->wet = s->wetTarget;
}

// Audio interrupt, block start, latest value per parameter. Type, Drive, Bits, Rate and Tone build new coefficients and
// fade from the old ones over the next block (the original ramps Drive and crossfades a Type change), Mix ramps the wet amount.
void setParam(void* p, uint32_t index, float v)
{
    if (index >= kParamCount) return;
    Inst* s = static_cast<Inst*>(p);
    const plug_param& d = kParams[index];
    v = v < d.min ? d.min : (v > d.max ? d.max : v);
    const uint8_t u = uint8_t(v + 0.5f);
    if (s->v[index] == u) return;
    s->v[index] = u;
    if (index == kMix)
    {
        s->wetTarget = float(u) * 0.01f;
        if (!s->ran) s->wet = s->wetTarget;
        return;
    }
    if (s->ran && !s->pending)
    {
        s->oldCoef = s->coef;
        s->oldSt = s->st;
        s->pending = 1;
    }
    if (s->v[kType] != ffdirt::kAsym) s->st.dc[0] = s->st.dc[1] = 0.0f;   // the DC block is Asym's alone
    rebuild(s);
}

inline int16_t toI16(float x)
{
    float y = x * 32768.0f;
    y = y > 32767.0f ? 32767.0f : (y < -32768.0f ? -32768.0f : y);
    return int16_t(y);
}

void process(void* p, float* const* ch, uint32_t channels, uint32_t frames)
{
    if (channels != 2 || frames != uint32_t(ffdirt::kBlock)) return;
    Inst* s = static_cast<Inst*>(p);
    int16_t l[ffdirt::kBlock], r[ffdirt::kBlock];
    float* L = ch[0];
    float* R = ch[1];
    for (int i = 0; i < ffdirt::kBlock; i++)
    {
        l[i] = toI16(L[i]);
        r[i] = toI16(R[i]);
    }
    const float w0 = s->wet, w1 = s->wetTarget;
    if (s->pending)
    {   // a coefficient change: the old setting on a copy, the new one in place, crossfaded over the block
        for (int i = 0; i < ffdirt::kBlock; i++)
        {
            s->ol[i] = l[i];
            s->orr[i] = r[i];
        }
        ffdirt::process(s->oldCoef, s->oldSt, s->ol, s->orr, w0, w1);
        ffdirt::process(s->coef, s->st, l, r, w0, w1);
        for (int i = 0; i < ffdirt::kBlock; i++)
        {
            const int32_t f = i + 1;   // 1..128 of 128
            l[i] = int16_t(s->ol[i] + (((int32_t(l[i]) - s->ol[i]) * f) >> 7));
            r[i] = int16_t(s->orr[i] + (((int32_t(r[i]) - s->orr[i]) * f) >> 7));
        }
        s->pending = 0;
    }
    else
    {
        ffdirt::process(s->coef, s->st, l, r, w0, w1);
    }
    s->wet = w1;
    s->ran = 1;
    for (int i = 0; i < ffdirt::kBlock; i++)
    {
        L[i] = float(l[i]) * (1.0f / 32768.0f);
        R[i] = float(r[i]) * (1.0f / 32768.0f);
    }
}

PLUG_DESCRIPTOR const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Dirt", "FF (OpenTracker DIRT)", kParamCount, kParams, sizeof(Inst),
    init,             reset,                   setParam, process, nullptr,
};
}   // namespace

extern "C" PLUG_ENTRY const plug_desc* PLUG_ENTRY_NAME(const plug_host* host, uint32_t abi)
{
    if ((abi >> 16) != PLUG_ABI_MAJOR || !host) return nullptr;
#ifndef PLUG_IN_IMAGE
    plug_rt_host = host;
#endif
    return &kDesc;
}
