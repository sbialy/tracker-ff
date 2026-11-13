// Mac test only (tools/para/emu_render.py), never shipped. Like sdk/files/synth-open/plug/synth/synth_harness.cpp (one engine plugin
// driven the way the shim's OpenInstrument drives it) but the script comes from a config block in memory: how many voices, which
// notes, when the gate ends and a list of parameter overrides (index, value). Output: blocks x 256 floats (L then R), then blocks
// x uint32 instruction counts of each block, then blocks x uint32 estimated cycles (host query(1)).
#include "plug_abi.h"
#include "audio/synth_engine.h"

#include <stdint.h>
#include <string.h>

using namespace ot::audio;

namespace
{
constexpr int kVoices = 4;
struct Cfg
{
    uint32_t voices;     // 1..4
    uint32_t offBlock;   // gate off at this block
    uint32_t nover;
    uint32_t note[kVoices];
    uint32_t idx[64];
    float val[64];
};
alignas(8) uint8_t g_pMem[sizeof(SynthParams)], g_scMem[sizeof(SynthSlotScratch)], g_vMem[kVoices][sizeof(SynthVoice)];
SynthParams& g_p = *reinterpret_cast<SynthParams*>(g_pMem);
SynthSlotScratch& g_sc = *reinterpret_cast<SynthSlotScratch*>(g_scMem);
SynthVoice* const g_v = reinterpret_cast<SynthVoice*>(g_vMem);
bool g_on[kVoices];
float g_gain[kVoices];
const plug_host* g_host;

void run(const plug_ext_synth* x, float* out, uint32_t blocks, const Cfg* cfg)
{
    memset(&g_p, 0, sizeof(g_p));
    g_p.engine = x->type_id;
    g_p.count = int(x->param_count);
    for (uint32_t i = 0; i < x->param_count; i++) g_p.start[i] = g_p.end[i] = x->params[i].def;
    for (uint32_t k = 0; k < cfg->nover; k++) g_p.start[cfg->idx[k]] = g_p.end[cfg->idx[k]] = cfg->val[k];
    memset(&g_sc, 0, sizeof(g_sc));
    if (x->slot_reset) x->slot_reset(&g_sc);
    if (x->set_tempo) x->set_tempo(120.0f);
    for (uint32_t b = 0; b < blocks; b++)
    {
        const uint32_t t0 = g_host->cycles(), q0 = uint32_t(uintptr_t(g_host->query(1)));
        g_p.moving = false;
        g_p.serial++;
        for (uint32_t k = 0; k < cfg->voices; k++)
        {
            SynthVoice& v = g_v[k];
            if (b == 0)
            {
                SynthNote n;
                n.note = uint8_t(cfg->note[k]);
                n.pitch = float(cfg->note[k]);
                n.velocity = x->volume_param < 0 ? 100.0f / 127.0f : 1.0f;
                v = SynthVoice{};
                memset(v.state, 0, sizeof(v.state));
                v.slotScratch = &g_sc;
                v.note = n.note;
                v.gate = true;
                g_on[k] = true;
                g_gain[k] = 0.0f;
                x->note_on(&v, &g_p, &n);
            }
            else if (b == cfg->offBlock && g_on[k])
            {
                v.gate = false;
                x->note_off(&v, &g_p);
            }
        }
        float* L = out + b * 2 * kBlock;
        float* R = L + kBlock;
        memset(L, 0, 2 * kBlock * sizeof(float));
        if (x->prepare) x->prepare(&g_sc, &g_p);
        for (uint32_t k = 0; k < cfg->voices; k++)
        {
            if (!g_on[k]) continue;
            const float target = x->volume_param >= 0 ? g_p.end[x->volume_param] : 1.0f;
            const float g0 = g_gain[k];
            g_gain[k] = target;
            if (!x->render(&g_v[k], &g_p, g0, (target - g0) * (1.0f / float(kBlock)), L, R)) g_on[k] = false;
        }
        reinterpret_cast<uint32_t*>(out + blocks * 2 * kBlock)[b] = g_host->cycles() - t0;
        reinterpret_cast<uint32_t*>(out + blocks * 2 * kBlock)[blocks + b] = uint32_t(uintptr_t(g_host->query(1))) - q0;
        memcpy(g_p.start, g_p.end, sizeof(g_p.start));
    }
}

int32_t init(void*, const plug_host* h)
{
    g_host = h;
    return 0;
}
void process(void*, float* const*, uint32_t, uint32_t) {}
const void* query(uint32_t id) { return id == 0x7E ? reinterpret_cast<const void*>(&run) : nullptr; }

PLUG_DESCRIPTOR const plug_desc kDesc = {
    sizeof(plug_desc), PLUG_KIND_OFFLINE, "RenderHarness", "test", 0, nullptr, 8, init, nullptr, nullptr, process, query,
};
}   // namespace

extern "C" const plug_host* plug_rt_host;
extern "C" PLUG_ENTRY const plug_desc* plug_entry(const plug_host* host, uint32_t)
{
    plug_rt_host = host;
    return &kDesc;
}
