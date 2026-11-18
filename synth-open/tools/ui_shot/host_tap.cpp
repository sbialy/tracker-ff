// The tap of the master insert twins (plug_tempo_sim.sh). Host only, NOT firmware.
#include "plug/plug_abi.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

// ---- the tap (plug_tempo_sim.sh). The real audio chain runs on the host when the script says "audio 1" (Tracker::updateAudioBuffers: the
// sequencer ticks, then the insert node calls the plugin's functions). A twin's descriptor is wrapped so every call of the host is
// written down: the tempo and the beat the host delivered, whether the input bus was silent, the output level. "poke tap reset"
// clears the records, "poke tap dump" writes them (host_poke.cpp).
struct TapRec
{
    uint32_t slotTwin;
    float bpm, beat;
    uint32_t playing, haveClock;
    float inRms, outRms;
};
enum { kTaps = 4, kTapMax = 6000 };
struct Tap
{
    uint32_t uid;
    const plug_desc* orig;
    plug_desc desc;
    plug_ext_tempo tempo;
    plug_ext_tail tail;
    const plug_ext_tempo* origTempo;
    const plug_ext_tail* origTail;
    float bpm, beat;
    uint32_t playing, haveClock;
    char name[16];
    TapRec rec[kTapMax];
    uint32_t n, calls;
};
static Tap s_tap[kTaps];

static float rmsOf(const float* x, uint32_t n)
{
    double a = 0;
    for (uint32_t i = 0; i < n; i++) a += double(x[i]) * x[i];
    return float(sqrt(a / (n ? n : 1)));
}

template <int I> static void tapTempo(void* inst, float bpm)
{
    s_tap[I].bpm = bpm;
    s_tap[I].haveClock = 1;
    if (s_tap[I].origTempo->set_tempo) s_tap[I].origTempo->set_tempo(inst, bpm);
}
template <int I> static void tapTransport(void* inst, float beat, uint32_t playing)
{
    s_tap[I].beat = beat;
    s_tap[I].playing = playing;
    if (s_tap[I].origTempo->set_transport) s_tap[I].origTempo->set_transport(inst, beat, playing);
}
template <int I> static uint32_t tapQuiet(void* inst)
{
    return s_tap[I].origTail->quiet(inst);
}
template <int I> static void tapProcess(void* inst, float* const* ch, uint32_t channels, uint32_t frames)
{
    Tap& t = s_tap[I];
    const float in = rmsOf(ch[0], frames);
    t.orig->process(inst, ch, channels, frames);
    if (t.n < kTapMax) t.rec[t.n++] = TapRec{uint32_t(I), t.bpm, t.beat, t.playing, t.haveClock, in, rmsOf(ch[0], frames)};
    t.calls++;
}
template <int I> static const void* tapQuery(uint32_t ext)
{
    Tap& t = s_tap[I];
    const void* o = t.orig->query ? t.orig->query(ext) : nullptr;
    if (ext == PLUG_EXT_TEMPO && o)
    {
        t.origTempo = static_cast<const plug_ext_tempo*>(o);
        t.tempo = {sizeof(plug_ext_tempo), t.origTempo->set_tempo ? tapTempo<I> : nullptr, tapTransport<I>};
        return &t.tempo;
    }
    if (ext == PLUG_EXT_TAIL && o)
    {
        t.origTail = static_cast<const plug_ext_tail*>(o);
        t.tail = {sizeof(plug_ext_tail), nullptr, tapQuiet<I>, nullptr};
        return &t.tail;
    }
    return o;
}
template <int I> static const plug_desc* tapWrap(uint32_t uid, const plug_desc* d)
{
    Tap& t = s_tap[I];
    t.uid = uid;
    t.orig = d;
    t.desc = *d;
    t.desc.process = tapProcess<I>;
    t.desc.query = tapQuery<I>;
    snprintf(t.name, sizeof t.name, "%s", d->name);
    t.bpm = t.beat = 0;
    t.playing = t.haveClock = 0;
    return &t.desc;
}
extern "C" const plug_desc* uishotTapWrap(uint32_t uid, const plug_desc* d)
{
    int i = 0;
    while (i < kTaps && s_tap[i].uid && s_tap[i].uid != uid) i++;
    if (i >= kTaps) return d;
    switch (i)
    {
        case 0: return tapWrap<0>(uid, d);
        case 1: return tapWrap<1>(uid, d);
        case 2: return tapWrap<2>(uid, d);
        default: return tapWrap<3>(uid, d);
    }
}
extern "C" void uishotTapReset()
{
    for (Tap& t : s_tap) t.n = t.calls = 0;
}
// one line per call of process(): "tap NAME k=N bpm=B beat=X playing=P clock=C in=RMS out=RMS"
extern "C" int uishotTapDump(char* buf, int cap)
{
    int n = 0;
    for (Tap& t : s_tap)
        for (uint32_t k = 0; k < t.n && n < cap - 160; k++)
        {
            const TapRec& r = t.rec[k];
            n += snprintf(buf + n, size_t(cap - n), "tap %s k=%u bpm=%.3f beat=%.5f playing=%u clock=%u in=%.6f out=%.6f\n", t.name, k, double(r.bpm),
                          double(r.beat), r.playing, r.haveClock, double(r.inRms), double(r.outRms));
        }
    return n;
}
