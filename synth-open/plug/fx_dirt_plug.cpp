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
struct Inst
{
    ffdirt::Coef coef;
    ffdirt::State st;
    uint32_t preset;
};

const char* const kNames[ffdirt::kPresetCount] = {"Soft", "Hard", "Fold", "Asym", "Crush"};
const plug_param kParams[1] = {{"Preset", "", 0.0f, 4.0f, 0.0f, 1.0f, ffdirt::kPresetCount, 0, kNames}};

int32_t init(void* p, const plug_host*)
{
    Inst* s = static_cast<Inst*>(p);
    s->st.reset();
    s->preset = 0;
    ffdirt::makeCoef(s->coef, ffdirt::kPresets[0]);
    return 0;
}

void reset(void* p)
{
    static_cast<Inst*>(p)->st.reset();
}

void setParam(void* p, uint32_t index, float v)
{
    if (index != 0) return;
    Inst* s = static_cast<Inst*>(p);
    int k = int(v + 0.5f);
    k = k < 0 ? 0 : (k >= ffdirt::kPresetCount ? ffdirt::kPresetCount - 1 : k);
    s->preset = uint32_t(k);
    ffdirt::makeCoef(s->coef, ffdirt::kPresets[k]);
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
    ffdirt::process(s->coef, s->st, l, r, 1.0f, 1.0f);
    for (int i = 0; i < ffdirt::kBlock; i++)
    {
        L[i] = float(l[i]) * (1.0f / 32768.0f);
        R[i] = float(r[i]) * (1.0f / 32768.0f);
    }
}

PLUG_DESCRIPTOR const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_MASTER_INSERT, "Dirt", "FF (OpenTracker DIRT)", 1, kParams, sizeof(Inst),
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
