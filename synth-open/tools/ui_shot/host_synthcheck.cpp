// ui_shot BRIDGE=1 only (emu parity plan T5, ref/emu-parity-plan.md): the attach of a synth engine plugin (plug_synth.cpp start()) on the
// bridge. The engine's text runs from the FlexNVM window (host_nvm.cpp), its rw from s_pool's window. Every function start() calls is the
// release image's own code under libunicorn: plug_entry with the image's plug::s_host, plug::checkDesc, the descriptor's init and query.
// checkSynth, checkChord and chordStrum are inlined into start() in the image (no symbol to call), so they are ported here to reads of the
// guest bytes in the device layouts (4 byte pointers): the same conditions in the same order, the same 32 bit address arithmetic
// (in(p, lo, n) is p - lo < n, a pointer plus an index wraps at 2^32). host_synthcheck_test.cc proves the verdicts equal to the image's
// own start() (run under the bridge on the same bank bytes) for every card engine and a set of corrupt and ABI mismatch variants.
#include "host_synthcheck.h"

#include "host_ucbridge.h"
#include "plug/plug_abi.h"
#include "plug/plug_chord.h"
#include "audio/synth_engine.h"

#include <mutex>
#include <stdio.h>
#include <string.h>

namespace ucbs
{
namespace
{
// ---- device layouts (plug_abi.h on the unit, 4 byte pointers)
namespace X   // plug_ext_synth
{
constexpr uint32_t struct_size = 0, layout = 4, stored = 8, patch_engine = 12, type_id = 16, name = 20, params = 24, param_count = 28,
                   sections = 32, section_count = 36, macros = 40, macro_count = 44, volume_param = 48, max_voices = 52, set_work = 64,
                   prepare = 68, slot_reset = 72, set_tempo = 76, note_on = 80, note_off = 84, render = 88, slot_post = 92,
                   set_send_tap = 96, service = 100, view = 104;
constexpr uint32_t ext11 = 100;   // kSynthExt11: every field up to set_send_tap
}
namespace P   // plug_synth_param, 24 B
{
constexpr uint32_t name = 0, min = 8, max = 12, fmt = 16, curve = 17, enum_names = 20, size = 24;
}
namespace S   // plug_synth_section, 16 B
{
constexpr uint32_t name = 0, short_name = 4, ids = 8, count = 12, size = 16;
}
namespace M   // plug_synth_macro, 24 B
{
constexpr uint32_t name = 0, count = 5, t0 = 8, t1 = 16, size = 24;
}
namespace C   // plug_ext_chord
{
constexpr uint32_t struct_size = 0, param_index = 4, mode_param = 8, strum_param = 12, size = 16;
}
constexpr uint32_t kDescInit = 28, kDescQuery = 44;   // plug_desc
constexpr uint32_t kSynthSectionMax = 10;             // plug_synth.cpp
constexpr uint32_t kParamMax = uint32_t(ot::audio::kSynthParamMax);

// guest reads: what the unit's load gives (0 where no window is, the unit would read whatever the bus answers there)
uint32_t r32(uint32_t g)
{
    const uint8_t* p = ucb::g2h(g, 4);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}
uint8_t r8(uint32_t g)
{
    const uint8_t* p = ucb::g2h(g, 1);
    return p ? *p : 0;
}
float rf(uint32_t g)
{
    const uint32_t v = r32(g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}

inline bool in(uint32_t p, uint32_t lo, uint32_t n)
{
    return p - lo < n;
}
inline bool fn(uint32_t f, uint32_t lo, uint32_t n, bool need)
{
    return f ? (f & 1u) && in(f & ~1u, lo, n) : !need;
}
size_t gstrnlen(uint32_t s, size_t max)
{
    size_t n = 0;
    while (n < max && r8(s + uint32_t(n))) n++;
    return n;
}
bool gstrchr(uint32_t s, char c)
{
    for (uint32_t i = 0;; i++)
    {
        const char ch = char(r8(s + i));
        if (ch == c) return true;
        if (!ch || !ucb::g2h(s + i, 1)) return false;
    }
}

void (*s_queryHook)(uint32_t, uint32_t);
uint32_t query(uint32_t q, uint32_t id)
{
    const uint32_t r = ucb::call(q, id);
    if (s_queryHook) s_queryHook(id, r);
    return r;
}

uint32_t s_inst;   // the 8 bytes init gets (start()'s static inst: the image's own, else a guest heap block)
void setup()
{
    static std::once_flag once;
    std::call_once(once, [] {
        s_inst = ucb::sym("plug::(anonymous namespace)::start(int, plug_ext_synth const**)::inst", 8);
        if (!s_inst)
            if (void* p = ucb::heapAlloc(8)) s_inst = ucb::h2gOr0(p, 8);
        // syncCode (DSB, ISB, the LMEM cache's PCCCR, which no window backs) when the image's own start() or load() runs on a lane:
        // the bridge's codeChanged already did what it does
        if (const uint32_t sc = ucb::sym("plug::syncCode()"))
            ucb::override(sc, [](void*) {});
    });
}

struct Faults
{
    uint64_t f0 = ucb::faults();
    bool any() const { return ucb::faults() != f0; }
};
}   // namespace

void setQueryHook(void (*h)(uint32_t, uint32_t))
{
    s_queryHook = h;
}

bool checkSynth(uint32_t x, uint32_t t, uint32_t tn, uint32_t rw, uint32_t rwn)
{
    const uint32_t ss = r32(x + X::struct_size);
    if (!x || !(in(x, t, tn) || in(x, rw, rwn)) || ss < X::ext11 || r32(x + X::layout) != ucb::deviceSynthLayout()) return false;
    const uint32_t np = r32(x + X::param_count);
    const uint32_t name = r32(x + X::name);
    const int32_t vol = int32_t(r32(x + X::volume_param));
    if (!in(name, t, tn) || !r8(name) || gstrnlen(name, 9) > 8 || gstrchr(name, '/') || np > kParamMax ||
        r32(x + X::section_count) > kSynthSectionMax || r32(x + X::macro_count) > 6 || r32(x + X::max_voices) - 1u > 3u ||
        vol >= int32_t(np) || vol < -1 || !fn(r32(x + X::note_on), t, tn, true) || !fn(r32(x + X::note_off), t, tn, true) ||
        !fn(r32(x + X::render), t, tn, true))
        return false;
    for (uint32_t o : {X::prepare, X::slot_reset, X::set_tempo, X::slot_post, X::set_send_tap, X::set_work})
        if (!fn(r32(x + o), t, tn, false)) return false;
    // PLUG_HAS(x, plug_ext_synth, service / view): struct_size covers the 4 byte field
    if ((ss >= X::service + 4 && !fn(r32(x + X::service), t, tn, false)) || (ss >= X::view + 4 && !fn(r32(x + X::view), t, tn, false)))
        return false;
    const uint32_t params = r32(x + X::params);
    if (np && (!in(params, t, tn) || !in(params + (np - 1) * P::size, t, tn))) return false;
    for (uint32_t i = 0; i < np; i++)
    {
        const uint32_t p = params + i * P::size;
        const uint8_t fmt = r8(p + P::fmt);
        const float mn = rf(p + P::min), mx = rf(p + P::max);
        if (!in(r32(p + P::name), t, tn) || fmt >= PLUG_FMT_COUNT || r8(p + P::curve) >= PLUG_CURVE_COUNT || !(mx >= mn)) return false;
        if (fmt == PLUG_FMT_ENUM)
        {
            // ABI 1.2: up to 128 names. ne is taken only once max is known to be 0..127 (the unit's float to unsigned conversion of
            // anything else never reaches a use either)
            const uint32_t en = r32(p + P::enum_names);
            if (mn != 0.0f || mx > 127.0f) return false;
            const uint32_t ne = uint32_t(mx) + 1u;
            if (!in(en, t, tn) || !in(en + (ne - 1) * 4, t, tn)) return false;
            for (uint32_t k = 0; k < ne; k++)
                if (!in(r32(en + 4 * k), t, tn)) return false;
        }
    }
    const uint32_t ns = r32(x + X::section_count), sec = r32(x + X::sections);
    if (ns && (!in(sec, t, tn) || !in(sec + (ns - 1) * S::size, t, tn))) return false;
    for (uint32_t s = 0; s < ns; s++)
    {
        const uint32_t c = sec + s * S::size;
        const uint32_t ids = r32(c + S::ids), cnt = r8(c + S::count);
        if (!in(r32(c + S::name), t, tn) || !in(r32(c + S::short_name), t, tn) || (cnt && (!in(ids, t, tn) || !in(ids + cnt - 1, t, tn))))
            return false;
        for (uint32_t k = 0; k < cnt; k++)
            if (r8(ids + k) >= np) return false;
    }
    const uint32_t nm = r32(x + X::macro_count), mac = r32(x + X::macros);
    if (nm && (!in(mac, t, tn) || !in(mac + (nm - 1) * M::size, t, tn))) return false;
    for (uint32_t m = 0; m < nm; m++)
    {
        const uint32_t c = mac + m * M::size;
        const uint32_t cnt = r8(c + M::count);
        if (!in(r32(c + M::name), t, tn) || cnt > 2 || (cnt && r8(c + M::t0) >= np) || (cnt > 1 && r8(c + M::t1) >= np)) return false;
    }
    return true;
}

bool checkChord(uint32_t c, uint32_t x, uint32_t t, uint32_t tn, uint32_t rw, uint32_t rwn)
{
    if (!c || !(in(c, t, tn) || in(c, rw, rwn)) || r32(c + C::struct_size) < C::strum_param) return false;
    const uint32_t np = r32(x + X::param_count);
    const uint32_t pi = r32(c + C::param_index), mp = r32(c + C::mode_param);
    if (pi >= np || mp >= np || pi == mp) return false;
    const uint32_t params = r32(x + X::params);
    const uint32_t ch = params + pi * P::size, md = params + mp * P::size;
    const uint8_t cf = r8(ch + P::fmt), mf = r8(md + P::fmt);
    const bool chOk = (cf == PLUG_FMT_ENUM || cf == PLUG_FMT_INT) && rf(ch + P::min) == 0.0f && rf(ch + P::max) == float(PLUG_CHORD_COUNT - 1);
    const bool mdOk = (mf == PLUG_FMT_ENUM || mf == PLUG_FMT_INT || mf == PLUG_FMT_ON_OFF) && rf(md + P::min) == 0.0f && rf(md + P::max) >= 1.0f;
    return chOk && mdOk;
}

int8_t chordStrum(uint32_t c, uint32_t x)
{
    const uint32_t sp = r32(c + C::strum_param), np = r32(x + X::param_count);
    if (r32(c + C::struct_size) < C::size || sp >= np || sp == r32(c + C::param_index) || sp == r32(c + C::mode_param)) return -1;
    const uint32_t st = r32(x + X::params) + sp * P::size;
    return r8(st + P::fmt) == PLUG_FMT_INT && rf(st + P::min) == -63.0f && rf(st + P::max) == 63.0f ? int8_t(sp) : int8_t(-1);
}

int start(const StartIn& in, StartOut& out)
{
    out = StartOut{};
    if (!ucb::init()) return out.fault = true, 1;
    setup();
    const uint32_t t = in.text;
    const uint32_t init = in.len - in.textLen;   // rw initial bytes and the zero padding
    uint8_t* const rw = ucb::g2h(in.rwRun, in.rwLen ? in.rwLen : 1);
    const uint8_t* const src = ucb::g2h(t + in.textLen, init ? init : 1);
    if (!rw || !src || init > in.rwLen) return out.fault = true, 1;
    memcpy(rw, src, init);
    memset(rw + init, 0, in.rwLen - init);
    ucb::codeChanged();   // syncCode
    const Faults f;
    const uint32_t d = ucb::entry((t + in.entry) | 1u);   // plug_entry(&s_host, ABI) of the image's host table
    if (f.any()) return out.fault = true, 1;
    if (!d) return 1;
    const int cd = ucb::checkDesc(d, t, in.textLen, PLUG_KIND_SYNTH);
    if (f.any()) return out.fault = true, 1;
    if (cd != 0) return 2;
    const uint32_t ir = ucb::call(r32(d + kDescInit), s_inst, ucb::hostTable());
    if (f.any()) return out.fault = true, 1;
    if (ir != 0) return 3;
    const uint32_t q = r32(d + kDescQuery);
    const uint32_t x = fn(q, t, in.textLen, true) ? query(q, PLUG_EXT_SYNTH) : 0;
    if (f.any()) return out.fault = true, 1;
    if (!checkSynth(x, t, in.textLen, in.rwRun, in.rwLen)) return 4;
    const uint32_t c = query(q, PLUG_EXT_CHORD);   // an ABI 1.1 engine answers NULL
    if (f.any()) return out.fault = true, 1;
    const bool chordOk = checkChord(c, x, t, in.textLen, in.rwRun, in.rwLen);
    out.chordSet = true;
    out.chord = chordOk ? int8_t(r32(c + C::param_index)) : int8_t(-1);
    out.mode = chordOk ? int8_t(r32(c + C::mode_param)) : int8_t(-1);
    out.strum = chordOk ? chordStrum(c, x) : int8_t(-1);
    const uint32_t nm = r32(x + X::name);
    bool same = true;
    for (size_t i = 0; i < in.nameCap; i++)   // strncmp(x->name, s_lib[k].name, sizeof(s_lib[k].name))
    {
        const char a = char(r8(nm + uint32_t(i))), b = in.name[i];
        if (a != b)
        {
            same = false;
            break;
        }
        if (!a) break;
    }
    if (int32_t(r32(x + X::stored)) != in.stored || int32_t(r32(x + X::patch_engine)) != in.patch ||
        int32_t(r32(x + X::type_id)) != in.type || !same)
        return 8;
    out.ext = x;
    return 0;
}
}   // namespace ucbs
