// Plugin synth engines on the stock Tracker+ / Mini synth stack, see PlugSynthVoice.h (IMX port P7 H5)
#include "PlugSynthVoice.h"

#if defined(FEATURE_SYNTH) && !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX)

#include "PolySynth/sdk/InstrumentTypeRegistry.h"
#include "PolySynth/sdk/InstrumentVoice.h"
#include "audio/synth_engine.h"   // synth-open/ot: SynthVoice, SynthParams, SynthNote, SynthSlotScratch (the engines' interface)
#include "plug_abi.h"

#if !defined(PLUS_SYNTH_HARNESS)
#include "plug_imx.h"
#endif

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <new>
#include <string>

namespace polysynth
{
namespace plugimx
{
namespace
{
static_assert(kAudioBufferSize == int(ot::audio::kBlock), "one lib block is one engine block");
constexpr uint32_t kLayout = PLUG_SYNTH_LAYOUT(sizeof(ot::audio::SynthVoice), sizeof(ot::audio::SynthParams),
                                               sizeof(ot::audio::SynthNote), sizeof(ot::audio::SynthSlotScratch));
constexpr int kStatesPerSlot = 3;   // one per synth part that plays the engine (kSynthParts)

// What one instrument (part) of the engine shares between its voices: the parameters and the slot scratch of the OpenTracker
// interface. Keyed by the instrument's patch (the voices of one instrument share it), made at the first voice call of that patch
struct PartState
{
    const Patch* patch = nullptr;
    bool prepared = false;   // prepare ran for this block (cleared after the instrument's post process)
    bool valid = false;      // params.start holds a block's values
    ot::audio::SynthSlotScratch scratch;
    ot::audio::SynthParams params;
};

struct SlotEngine
{
    const plug_ext_synth* ext = nullptr;
    uint32_t gen = 0;   // bumped by every detach: a voice or a type of an earlier engine in this slot plays nothing
    int typeId = -1;
    void* work = nullptr;
    PartState* states[kStatesPerSlot] = {};
    float tempo = 0.f;
};

InstrumentTypeRegistry* g_registry;
#if defined(PLATFORM_IMX) && !defined(UI_SHOT_HOST)
// SRAM_OC on IMX (CPU only): its 96 B of initialised .data sat 184 B under the 0x800 boundary that qh_buffer pins .bss to on the Mini,
// 185 B more would have moved .bss up by 2,048 B (synth-open/tools/imx_sram_cliff.py)
__attribute__((section(".data.$RAM3"))) SlotEngine g_slots[kSlots];
#else
SlotEngine g_slots[kSlots];
#endif
// R of the engine for the voice being rendered (the lib renders the voices one after the other in the audio interrupt)
float g_right[kAudioBufferSize];

PartState* stateFor(SlotEngine& s, const Patch* patch)
{
    for (auto* st : s.states)
        if (st && st->patch == patch) return st;
    for (auto*& st : s.states)
        if (!st || !st->patch)
        {
            if (!st) st = new (std::nothrow) PartState();
            if (!st) return nullptr;
            *st = PartState{};
            st->patch = patch;
            st->params.engine = s.typeId;
            st->params.count = int(std::min<uint32_t>(s.ext->param_count, uint32_t(ot::audio::kSynthParamMax)));
            for (int i = 0; i < st->params.count; i++) st->params.end[i] = st->params.start[i] = s.ext->params[i].def;
            if (s.ext->slot_reset) s.ext->slot_reset(&st->scratch);
            return st;
        }
    return s.states[0];   // more instruments than parts: share the first (never on the unit, 3 parts)
}

class PlugVoice final : public InstrumentVoice
{
  public:
    PlugVoice(int slot, uint32_t gen) : slot(slot), gen(gen) {}

    void onMidiEvent(const InstrumentVoiceContext& ctx, const MidiEvent& event) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st) return;
        tempo(*s, ctx.tempo);
        if (event.type == MidiEvent::Type::kNoteOn && event.data.note.velocity > 0)
        {
            const bool reused = on;
            if (!reused)
            {
                v = ot::audio::SynthVoice{};
                std::memset(v.state, 0, sizeof v.state);
                gain = 0.f;
            }
            ot::audio::SynthNote n;
            n.note = event.data.note.note;
            n.pitch = float(n.note);
            vel = float(event.data.note.velocity) * (1.0f / 127.0f);
            n.velocity = s->ext->volume_param < 0 ? vel : 1.0f;
            n.reused = reused;
            n.overlap = reused && v.gate;
            v.slotScratch = &st->scratch;
            v.note = n.note;
            v.gate = true;
            v.fading = false;
            on = true;
            note = n.note;
            s->ext->note_on(&v, &st->params, &n);
        }
        else if ((event.type == MidiEvent::Type::kNoteOff || event.type == MidiEvent::Type::kNoteOn) && on && v.gate &&
                 event.data.note.note == note)
        {
            v.gate = false;
            s->ext->note_off(&v, &st->params);
        }
    }

    void onParamChange(int paramId, Patch::optional_paramvalue_t value) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st || !value || paramId < 0 || paramId >= st->params.count) return;
        if (paramId == s->ext->volume_param)
        {
            // The Velocity macro (M7) targets Volume per voice: the lib sends this voice's value, the part's params take the value
            // without the per-voice macros (the voice gain applies the velocity, renderVoiceAudio, as the MK66 shim)
            value = getPatch()->getParamFinalValue(paramId);
            if (!value) return;
        }
        if (st->params.end[paramId] != *value)
        {
            st->params.end[paramId] = *value;
            st->params.serial++;
        }
    }

    void renderVoiceAudio(const InstrumentVoiceContext& ctx, std::optional<AudioBufferStereoRef>, AudioBufferStereoRef out,
                          std::span<AudioBufferRef>) override
    {
        // The part is mono (the stock mixer takes a stereo patch only with aux sends, synth_PolySynth.cpp): the engine adds into L and
        // a scratch R, the voice output is 0.5 (L + R), the fold of the MK66 shim (open_synth.cpp renderAudioBuffer)
        float* l = out[0].data();
        std::fill(l, l + kAudioBufferSize, 0.0f);
        SlotEngine* s = engine();
        PartState* st = s && on ? stateFor(*s, getPatch()) : nullptr;
        if (!st)
        {
            on = false;
            return;
        }
        tempo(*s, ctx.tempo);
        prepare(*s, *st);
        const int volParam = s->ext->volume_param;
        // The MK66 shim's gain rule (open_synth.cpp renderAudioBuffer): clamp(vol * (1 + k * (vel - 1)), min, max) with k the Velocity
        // macro's depth on Volume (open_patch.h velocityAmount: the sum of its modifiers on Volume, 1 by default, makeMetadata).
        // Written as the MK66 does it: vol * vel is the same value in exact maths but not in float (1 LSB at low velocities)
        float target = 1.0f;
        if (volParam >= 0 && volParam < st->params.count)
        {
            const plug_synth_param& p = s->ext->params[volParam];
            target = std::clamp(st->params.end[volParam] * (1.0f + velocityAmount(volParam) * (vel - 1.0f)), p.min, p.max);
        }
        else
        {
            // No Volume (T-Type, PERC): the voice gain is the note velocity and the engine divides it back out per hit (ttype.cpp render
            // g / vLast), as the MK66 shim does (open_synth.cpp renderAudioBuffer: hasVol false, vol 1, velK 1, range 0..1)
            target = std::clamp(1.0f * (1.0f + 1.0f * (vel - 1.0f)), 0.0f, 1.0f);
        }
        const float g0 = gain;
        const float gInc = (target - g0) * (1.0f / float(kAudioBufferSize));
        gain = target;
        v.bendStart = v.bend;
        std::fill(g_right, g_right + kAudioBufferSize, 0.0f);
        if (!s->ext->render(&v, &st->params, g0, gInc, l, g_right)) on = false;
        for (int i = 0; i < kAudioBufferSize; i++) l[i] = 0.5f * (l[i] + g_right[i]);
    }

    void renderPostProcessAudio(const InstrumentVoiceContext&, AudioBufferStereoRef out) override
    {
        SlotEngine* s = engine();
        PartState* st = s ? stateFor(*s, getPatch()) : nullptr;
        if (!st) return;
        prepare(*s, *st);
        if ((s->ext->flags & PLUG_SYNTH_SLOT_FX) && s->ext->slot_post)
        {
            // the slot FX (JU6 chorus) runs on the folded mix in both channels and is folded again
            float* l = out[0].data();
            std::memcpy(g_right, l, sizeof(float) * kAudioBufferSize);
            s->ext->slot_post(slot, &st->scratch, &st->params, l, g_right);
            for (int i = 0; i < kAudioBufferSize; i++) l[i] = 0.5f * (l[i] + g_right[i]);
        }
        // the block is done: the next one starts at these values
        std::memcpy(st->params.start, st->params.end, sizeof(float) * size_t(st->params.count));
        st->params.moving = false;
        st->prepared = false;
    }

    bool isActive() override { return on && engine(); }
    bool handlesNote(note_t n) override { return on && v.gate && note == n; }
    bool handlesAnyNote() override { return on && v.gate; }
    tiny_optional<note_t> getLastNote() override { return on ? tiny_optional<note_t>(note) : tiny_optional<note_t>(); }
    tiny_optional<float> getCurrentNoteVelocity() override { return on ? tiny_optional<float>(vel) : tiny_optional<float>(); }

  private:
    SlotEngine* engine() const { return g_slots[slot].ext && g_slots[slot].gen == gen ? &g_slots[slot] : nullptr; }
    float velocityAmount(int volParam) const
    {
        const auto& macros = getPatch()->getMetadata().macros;
        constexpr size_t kVel = size_t(Patch::SystemMacroId::kVelocity);
        float k = 0.0f;
        if (kVel < macros.size())
            for (const auto& mod : macros[kVel].modifiers)
                if (mod.paramId && *mod.paramId == volParam) k += mod.amount;
        return k;
    }
    static void tempo(SlotEngine& s, float bpm)
    {
        if (bpm != s.tempo && bpm > 0.f && s.ext->set_tempo)
        {
            s.ext->set_tempo(bpm);
            s.tempo = bpm;
        }
    }
    static void prepare(SlotEngine& s, PartState& st)
    {
        if (st.prepared) return;
        if (!st.valid)
        {
            std::memcpy(st.params.start, st.params.end, sizeof(float) * size_t(st.params.count));
            st.valid = true;
        }
        st.params.moving = std::memcmp(st.params.start, st.params.end, sizeof(float) * size_t(st.params.count)) != 0;
        if (s.ext->prepare) s.ext->prepare(&st.scratch, &st.params);
        st.prepared = true;
    }

    int slot;
    uint32_t gen;
    ot::audio::SynthVoice v{};
    bool on = false;
    note_t note = 0;
    float vel = 1.f, gain = 0.f;
};

class SilentVoice final : public InstrumentVoice
{
  public:
    void onMidiEvent(const InstrumentVoiceContext&, const MidiEvent&) override {}
    void onParamChange(int, Patch::optional_paramvalue_t) override {}
    void renderVoiceAudio(const InstrumentVoiceContext&, std::optional<AudioBufferStereoRef>, AudioBufferStereoRef out,
                          std::span<AudioBufferRef>) override
    {
        std::fill(out[0].begin(), out[0].end(), 0.0f);
        std::fill(out[1].begin(), out[1].end(), 0.0f);
    }
    bool isActive() override { return false; }
    bool handlesNote(note_t) override { return false; }
    bool handlesAnyNote() override { return false; }
    tiny_optional<note_t> getLastNote() override { return {}; }
    tiny_optional<float> getCurrentNoteVelocity() override { return {}; }
};

// The value text of a plugin engine parameter, the MK66 shim's formatValue (open_cold.cpp) for the PLUG_FMT_* formats, character for
// character. No float printf here (the plus sim's guest links nano printf without _printf_float), so the two helpers give printf's
// results by integer means: lround of a float, and "%.Nf" of a float (double(v) * 10^N is exact for N <= 2, ties go to even as printf)
long roundf32(float x)   // std::lround(float): half away from zero
{
    const float a = x < 0.0f ? -x : x;
    long t = long(a);
    if (a - float(t) >= 0.5f) t++;
    return x < 0.0f ? -t : t;
}

int fixedText(char* buf, size_t cap, float v, int decimals, const char* unit)   // snprintf(buf, cap, "%.<decimals>f<unit>", v)
{
    const unsigned long scale = decimals == 2 ? 100u : 10u;   // 32 bit: nano printf has no %llu, the values print below 4e7
    const double x = (v < 0.0f ? -double(v) : double(v)) * double(scale);
    unsigned long i = (unsigned long)x;
    const double frac = x - double(i);
    if (frac > 0.5 || (frac == 0.5 && (i & 1u))) i++;
    return snprintf(buf, cap, "%s%lu.%0*lu%s", std::signbit(v) ? "-" : "", i / scale, decimals, i % scale, unit);
}

// MK66 waveFileShape (open_load.h): ALCO's A File / B File, an index into the card's /Waves list. The IMX host has no wave list (no
// PLUG_EXT_FILES, plug_imx.cpp), the MK66 case of a card without /Waves: the row is not listed and the value reads as with no list
bool waveFileParam(const plug_synth_param& p)
{
    const size_t n = p.name ? std::strlen(p.name) : 0;
    return p.fmt == PLUG_FMT_INT && p.min == 0.0f && p.max == 1024.0f && n > 5 && !std::strcmp(p.name + n - 5, " File");
}

std::string formatValue(const plug_synth_param& d, float v)
{
    char buf[24];
    switch (d.fmt)
    {
        case PLUG_FMT_PCT: snprintf(buf, sizeof(buf), "%ld%%", roundf32(v * 100.0f)); break;
        case PLUG_FMT_PCT_MAX:
        {
            const float m = std::max(d.min < 0.0f ? -d.min : d.min, d.max < 0.0f ? -d.max : d.max);
            snprintf(buf, sizeof(buf), "%ld%%", roundf32(m > 0 ? v / m * 100.0f : 0.0f));
            break;
        }
        case PLUG_FMT_HZ:
            if (v >= 1000.0f)
                fixedText(buf, sizeof(buf), v / 1000.0f, 2, " kHz");
            else
                snprintf(buf, sizeof(buf), "%ld Hz", roundf32(v));
            break;
        case PLUG_FMT_SEC:
            if (v < 1.0f)
                snprintf(buf, sizeof(buf), "%ld ms", roundf32(v * 1000.0f));
            else
                fixedText(buf, sizeof(buf), v, 2, " s");
            break;
        case PLUG_FMT_LFO_HZ: fixedText(buf, sizeof(buf), v, 2, " Hz"); break;
        case PLUG_FMT_ENUM:
        {
            const int idx = std::clamp(int(roundf32(v)), 0, int(d.max));
            return d.enum_names && d.enum_names[idx] ? std::string(d.enum_names[idx]) : std::string();
        }
        case PLUG_FMT_VAL100: snprintf(buf, sizeof(buf), "%ld", roundf32(v * 100.0f)); break;
        case PLUG_FMT_ON_OFF: return v >= 0.5f ? "On" : "Off";
        case PLUG_FMT_SEMI: snprintf(buf, sizeof(buf), "%ld st", roundf32(v)); break;
        case PLUG_FMT_CENTS: snprintf(buf, sizeof(buf), "%ld c", roundf32(v)); break;
        case PLUG_FMT_INT:
            if (waveFileParam(d)) return roundf32(v) <= 0 ? "Sine" : "Missing";   // MK66 waveLabel without a stored path or list
            snprintf(buf, sizeof(buf), "%ld", roundf32(v));
            break;
        case PLUG_FMT_RATIO: fixedText(buf, sizeof(buf), 0.25f * float(std::clamp(int(roundf32(v)), 0, 47) + 1), 2, ""); break;
        case PLUG_FMT_MS10: fixedText(buf, sizeof(buf), v * 0.1f, 2, " ms"); break;
        case PLUG_FMT_DB: fixedText(buf, sizeof(buf), v, 1, " dB"); break;
        case PLUG_FMT_UNIT: fixedText(buf, sizeof(buf), v, 2, ""); break;
        case PLUG_FMT_PAN:
        {
            const long q = roundf32(v * 100.0f);
            if (q == 0) return "0";
            snprintf(buf, sizeof(buf), "%c%ld", q < 0 ? 'L' : 'R', q < 0 ? -q : q);
            break;
        }
        default: snprintf(buf, sizeof(buf), "%ld", roundf32(v)); break;   // past PLUG_FMT_UNIT: the built-in PERC's only, refused at load
    }
    return buf;
}

// The lib's metadata from the engine's tables (strings copied: the slot's text goes when the slot is loaded again)
void makeMetadata(int slot, uint32_t gen, Patch::Metadata& m)
{
    using Param = Patch::Metadata::Param;
    using Layout = Patch::Metadata::EditorLayout;
    const SlotEngine& s = g_slots[slot];
    if (!s.ext || s.gen != gen) return;
    const plug_ext_synth& x = *s.ext;
    const uint32_t n = std::min<uint32_t>(x.param_count, uint32_t(ot::audio::kSynthParamMax));
    m.params.resize(n);
    for (uint32_t i = 0; i < n; i++)
    {
        const plug_synth_param& p = x.params[i];
        Param& q = m.params[i];
        q.displayName = p.name ? p.name : "";
        q.defaultValue = p.def;
        q.minValue = Patch::paramvalue_short_t(p.min);
        q.maxValue = Patch::paramvalue_short_t(p.max);
        q.editCurve = p.curve < PLUG_CURVE_COUNT ? static_cast<Param::Curve>(p.curve) : Param::Curve::kLinear;
        q.isEnum = p.fmt == PLUG_FMT_ENUM;
        // every parameter prints as on the MK66 (units, scaling, decimals): the engine's own float range and format, not the lib's
        // __fp16 copy of the range
        const plug_synth_param* d = &p;
        q.valueFormatter = [d, slot, gen](const Patch&, Patch::paramvalue_t value) -> std::string
        {
            if (!g_slots[slot].ext || g_slots[slot].gen != gen) return {};
            return formatValue(*d, value);
        };
    }
    Layout layout;
    for (uint32_t i = 0; i < x.section_count; i++)
    {
        const plug_synth_section& sec = x.sections[i];
        Layout::EditorSection e;
        e.name = sec.name ? sec.name : "";
        e.shortName = sec.short_name ? sec.short_name : e.name;
        for (uint32_t k = 0; k < sec.count; k++)
            if (sec.ids[k] < n && !waveFileParam(x.params[sec.ids[k]])) e.elements.push_back({ Layout::EditorElement::Type::kParam, int(sec.ids[k]) });
        layout.sections.push_back(std::move(e));
    }
    m.layoutFlat = layout;
    m.layout23 = std::move(layout);
    m.instrumentTypeId = x.type_id;
    m.outputChannels = Patch::Metadata::OutputChannels::kMono;   // see renderVoiceAudio
    m.maxVoices = int(x.max_voices ? x.max_voices : 1);
    const plug_synth_macro* macros = x.macros;
    const uint32_t nm = std::min<uint32_t>(x.macro_count, 6u);
    const int vol = x.volume_param >= 0 && uint32_t(x.volume_param) < n ? int(x.volume_param) : -1;
    m.initDefaultMacros = [macros, nm, n, vol, slot, gen](std::vector<Patch::Metadata::Macro>& out)
    {
        if (!g_slots[slot].ext || g_slots[slot].gen != gen) return;
        for (uint32_t i = 0; i < nm && i < out.size(); i++)
        {
            const plug_synth_macro& mc = macros[i];
            out[i].displayName = mc.name ? mc.name : "";
            out[i].bipolar = mc.bipolar != 0;
            out[i].modifiers.clear();
            for (uint32_t t = 0; t < mc.count && t < 2; t++)
                if (mc.targets[t].param < n) out[i].modifiers.push_back({ int(mc.targets[t].param), mc.targets[t].amount });
        }
        // M7 Velocity on Volume at depth 1, the MK66 default (open_cold.cpp): the user edits the depth on the M7 page, the patch keeps it
        constexpr size_t kVel = size_t(Patch::SystemMacroId::kVelocity);
        if (vol >= 0 && kVel < out.size()) out[kVel].modifiers = { { vol, 1.0f } };
    };
}
}   // namespace

namespace
{
void registerSlot(int slot)
{
    const SlotEngine& s = g_slots[slot];
    const uint32_t gen = s.gen;
    auto type = std::make_unique<InstrumentType>();
    type->name = s.ext->name ? s.ext->name : "";
    type->createMetadata = [slot, gen](Patch&, Patch::Metadata& m) { makeMetadata(slot, gen, m); };
    type->createVoice = [slot, gen](int) -> std::unique_ptr<InstrumentVoice>
    {
        if (g_slots[slot].ext && g_slots[slot].gen == gen) return std::make_unique<PlugVoice>(slot, gen);
        return std::make_unique<SilentVoice>();
    };
    g_registry->addInstrumentType(s.typeId, std::move(type));
}
}   // namespace

// A new Synth (one per boot on the unit, the sim's harness can make more) gets the engines that are attached already
void setRegistry(InstrumentTypeRegistry* registry)
{
    g_registry = registry;
    for (int slot = 0; slot < kSlots; slot++)
        if (g_slots[slot].ext) registerSlot(slot);
}

void detach(int slot)
{
    if (slot < 0 || slot >= kSlots) return;
    SlotEngine& s = g_slots[slot];
    s.gen++;
    s.ext = nullptr;
    s.typeId = -1;
    s.tempo = 0.f;
    ::operator delete(s.work);
    s.work = nullptr;
    for (auto*& st : s.states)
    {
        delete st;
        st = nullptr;
    }
}

int slotType(int slot) { return slot >= 0 && slot < kSlots ? g_slots[slot].typeId : -1; }

int attach(int slot, uint32_t entryAddr, const plug_host* host)
{
    if (!g_registry) return -1;
    if (slot < 0 || slot >= kSlots) return -2;
    detach(slot);
    const auto entry = reinterpret_cast<plug_entry_fn>(uintptr_t(entryAddr));
    const plug_desc* d = entry ? entry(host, (uint32_t(PLUG_ABI_MAJOR) << 16) | PLUG_ABI_MINOR) : nullptr;
    if (!d || d->kind != PLUG_KIND_SYNTH || !d->query) return -3;
    alignas(8) static uint8_t inst[16];   // a synth engine keeps its state in its own bss, the instance is unused (plug_desc 8 B)
    if (d->instance_size > sizeof inst) return -4;
    std::memset(inst, 0, sizeof inst);
    if (d->init && d->init(inst, host) != 0) return -6;
    const auto* x = static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH));
    if (!x || x->struct_size < offsetof(plug_ext_synth, service) || !x->note_on || !x->note_off || !x->render || !x->params) return -4;
    if (x->layout != kLayout) return -5;
    SlotEngine& s = g_slots[slot];
    if (x->work_bytes)
    {
        s.work = ::operator new(x->work_bytes, std::nothrow);
        if (!s.work) return -7;
        std::memset(s.work, 0, x->work_bytes);
        if (x->set_work) x->set_work(s.work, x->work_bytes);
    }
    s.ext = x;
    s.typeId = x->type_id;
    registerSlot(slot);
    return x->type_id;
}

#if !defined(PLUS_SYNTH_HARNESS)
int attachFile(int slot, const uint8_t* file, uint32_t len)
{
    if (slot < 0 || slot >= kSlots) return -2;
    detach(slot);   // the voices of the slot's engine stop before its code is overwritten
    uint32_t entryAddr = 0;
    const plug::Err e = plug::imx::load(file, len, PLUG_KIND_SYNTH, slot, entryAddr);
    if (e != plug::kOk) return -100 - int(e);
    return attach(slot, entryAddr, plug::imx::host());
}
#endif

}   // namespace plugimx
}   // namespace polysynth

#endif
