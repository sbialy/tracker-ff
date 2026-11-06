// Mac test only (emu_synth.py), never shipped: drives a PLUG_EXT_SYNTH engine through a fixed note and parameter script, as the
// shim's OpenInstrument does (open_synth.cpp: fresh voice, slotScratch, gate, gain ramp, prepare before the voices, slot_post
// on a private bus). Built as a .plg so the emulator loads it next to the engine plugin. Struct layouts come from the real
// OpenTracker headers, so they are the firmware's.
#include "plug_abi.h"
#include "audio/synth_engine.h"

#include <string.h>

using namespace ot::audio;

namespace
{
constexpr int kVoices = 4;
// raw storage: the structs have member initializers, a global of their type would need a static constructor
alignas(8) uint8_t g_pMem[sizeof(SynthParams)], g_scMem[sizeof(SynthSlotScratch)], g_vMem[kVoices][sizeof(SynthVoice)];
SynthParams& g_p = *reinterpret_cast<SynthParams*>(g_pMem);
SynthSlotScratch& g_sc = *reinterpret_cast<SynthSlotScratch*>(g_scMem);
SynthVoice* const g_v = reinterpret_cast<SynthVoice*>(g_vMem);
bool g_on[kVoices];
float g_gain[kVoices];
float g_busL[kBlock], g_busR[kBlock];

struct Ev
{
    uint8_t block, voice, note, on;   // on: 1 note on, 0 note off
};
// four voices: a chord built up note by note, then released one by one (release tails render to the end)
const Ev kScript[] = {
    {0, 0, 48, 1}, {3, 1, 55, 1}, {6, 2, 60, 1}, {9, 3, 64, 1}, {24, 0, 48, 0}, {27, 1, 55, 0}, {30, 2, 60, 0},
    {33, 3, 64, 0}, {40, 0, 67, 1}, {52, 0, 67, 0},
};

float pick(const plug_synth_param& d, int scenario, int i)
{   // scenario 1: every parameter away from its default (an enum to its next value), so each code path runs
    if (scenario == 0) return d.def;
    if (d.fmt == PLUG_FMT_ENUM || d.fmt == PLUG_FMT_ON_OFF || d.fmt == PLUG_FMT_INT)
        return d.def + 1.0f <= d.max ? d.def + 1.0f : d.min;
    const float t = (i & 1) ? 0.3f : 0.7f;
    return d.min + (d.max - d.min) * t;
}

// out: blocks x 256 floats (L then R per block)
void run(const plug_ext_synth* x, float* out, uint32_t blocks, uint32_t scenario)
{
    memset(&g_p, 0, sizeof(g_p));
    g_p.engine = x->type_id;
    g_p.count = int(x->param_count);
    for (uint32_t i = 0; i < x->param_count; i++) g_p.start[i] = g_p.end[i] = x->params[i].def;
    if (scenario)
        for (uint32_t i = 0; i < x->param_count; i++) g_p.start[i] = g_p.end[i] = pick(x->params[i], 1, int(i));
    memset(&g_sc, 0, sizeof(g_sc));
    if (x->slot_reset) x->slot_reset(&g_sc);
    if (x->set_tempo) x->set_tempo(120.0f);
    for (uint32_t b = 0; b < blocks; b++)
    {
        g_p.moving = false;
        if (scenario && b == 16)   // a ramp on every continuous parameter over one block (p.moving)
            for (uint32_t i = 0; i < x->param_count; i++)
                if (x->params[i].fmt != PLUG_FMT_ENUM && x->params[i].fmt != PLUG_FMT_ON_OFF)
                {
                    g_p.end[i] = pick(x->params[i], 1, int(i) + 1);
                    g_p.moving = g_p.moving || g_p.end[i] != g_p.start[i];
                }
        g_p.serial++;
        for (const Ev& e : kScript)
        {
            if (e.block != b) continue;
            SynthVoice& v = g_v[e.voice];
            if (e.on)
            {
                SynthNote n;
                n.note = e.note;
                n.pitch = float(e.note);
                n.velocity = x->volume_param < 0 ? 100.0f / 127.0f : 1.0f;
                v = SynthVoice{};
                memset(v.state, 0, sizeof(v.state));
                v.slotScratch = &g_sc;
                v.note = e.note;
                v.gate = true;
                g_on[e.voice] = true;
                g_gain[e.voice] = 0.0f;
                x->note_on(&v, &g_p, &n);
            }
            else if (g_on[e.voice])
            {
                v.gate = false;
                x->note_off(&v, &g_p);
            }
        }
        float* L = out + b * 2 * kBlock;
        float* R = L + kBlock;
        memset(L, 0, 2 * kBlock * sizeof(float));
        const bool fx = (x->flags & PLUG_SYNTH_SLOT_FX) && x->slot_post;
        if (fx)
        {
            memset(g_busL, 0, sizeof(g_busL));
            memset(g_busR, 0, sizeof(g_busR));
        }
        if (x->prepare) x->prepare(&g_sc, &g_p);
        for (int k = 0; k < kVoices; k++)
        {
            if (!g_on[k]) continue;
            const float target = x->volume_param >= 0 ? g_p.end[x->volume_param] : 1.0f;
            const float g0 = g_gain[k];
            g_gain[k] = target;
            if (!x->render(&g_v[k], &g_p, g0, (target - g0) * (1.0f / float(kBlock)), fx ? g_busL : L, fx ? g_busR : R))
                g_on[k] = false;
        }
        if (fx)
        {
            x->slot_post(0, &g_sc, &g_p, g_busL, g_busR);
            for (int i = 0; i < kBlock; i++)
            {
                L[i] += g_busL[i];
                R[i] += g_busR[i];
            }
        }
        memcpy(g_p.start, g_p.end, sizeof(g_p.start));
    }
}

int32_t init(void*, const plug_host*) { return 0; }
void process(void*, float* const*, uint32_t, uint32_t) {}
const void* query(uint32_t id) { return id == 0x7E ? reinterpret_cast<const void*>(&run) : nullptr; }

PLUG_DESCRIPTOR const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_OFFLINE, "SynthHarness", "test", 0, nullptr, 8, init, nullptr, nullptr, process, query,
};
}   // namespace

extern "C" const plug_host* plug_rt_host;
extern "C" PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t)
{
    plug_rt_host = host;
    return &kDesc;
}
