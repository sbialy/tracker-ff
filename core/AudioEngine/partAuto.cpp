// Tracker+ and Mini: the slots of the Synth Automation page and their run (partAuto.h). The original Tracker's open synth runs its own
// slots in the engine (open_synth.cpp), this file is empty there.
#include "ff_config.h"

#if defined(FEATURE_PART_AUTO) && !defined(FEATURE_SYNTH_OPEN)
#include "partAuto.h"

#include <new>
#include <type_traits>

#include "cr_section_macros.h"
#include "noinit_check.h"
#include "interruptsControl.h"
#include "mtAudioEngine.h"

// Tracker+ and Mini: the Synth Automation page's slots (partAuto.h). The config is plain data in the SDRAM (no static constructor), valid
// once clearSlots ran (the boot project load), the run state and the held notes in internal RAM
namespace partauto
{
namespace
{
NOINIT_PLAIN(Slot);
__NOINIT(EXTERNAL_RAM) Slot s_slots[kStoreParts][kSlots];
// The run state is raw zero bytes in .bss, not a ModState array: ModState has non-zero default member initialisers (rng), so the object
// would sit in .data and 240 B of it pushed the Mini .data + .dmabuffers end across a 0x800 boundary, which moved .bss up by 2,032 B (the
// aligned(2048) qh_buffer of the USB driver pins .bss to 0x800). clearSlots builds every state in place (placement new, then modReset
// sets rng), s_ready stays false until then and every reader checks it first
using State = polysynth::open::ModState;
alignas(State) unsigned char s_stateRaw[kStoreParts][kSlots][sizeof(State)];
static_assert(std::is_trivially_destructible_v<State> && std::is_trivially_copyable_v<State>, "State lives in raw bytes");
inline State& state(int part, int k)
{
    return *std::launder(reinterpret_cast<State*>(s_stateRaw[part][k]));
}
uint32_t s_held[kStoreParts][4];   // bit per MIDI note
uint8_t s_driven[kStoreParts];     // destinations the slots set last block
bool s_ready = false;
bool anyHeld(int part)
{
    return (s_held[part][0] | s_held[part][1] | s_held[part][2] | s_held[part][3]) != 0;
}
} // namespace

const Slot& slot(int part, int k)
{
    static const Slot kOff = {};
    return (s_ready && part >= 0 && part < kStoreParts && k >= 0 && k < kSlots) ? s_slots[part][k] : kOff;
}

void setSlot(int part, int k, const Slot& v)
{
    if (!s_ready || part < 0 || part >= kStoreParts || k < 0 || k >= kSlots)
        return;
    DISABLE_AUDIO_INTERRUPTS();
    s_slots[part][k] = v;
    ENABLE_AUDIO_INTERRUPTS();
}

void clearSlots()
{
    for (int p = 0; p < kStoreParts; p++)
    {
        for (int k = 0; k < kSlots; k++)
        {
            // Off with the page's defaults (open_patchdata.h ModSlot: Triangle, 16 steps, 50 %, 10 / 500 / 50 % / 500 ms), no destination
            const polysynth::open::ModSlot d;
            Slot& c = s_slots[p][k];
            c = Slot{};
            c.type = d.type;
            c.kind = polysynth::open::ModKind::kPart;
            c.shape = d.shape;
            c.speed = d.speed;
            c.dest = -1;
            c.amount = d.amount;
            c.sustain = d.sustain;
            c.attack = d.attack;
            c.decay = d.decay;
            c.release = d.release;
            polysynth::open::modReset(*new (s_stateRaw[p][k]) State, k);
        }
        s_driven[p] = 0;
    }
    s_ready = true;
}

AUDIO_CORE_RAM_COLD void noteOn(int part, uint8_t note)
{
    if (!s_ready || part < 0 || part >= kStoreParts || note > 127)
        return;
    const bool afterSilence = !anyHeld(part);
    s_held[part][note >> 5] |= 1u << (note & 31);
    for (int k = 0; k < kSlots; k++)
        polysynth::open::modNoteOn(s_slots[part][k], state(part, k), afterSilence);
}

AUDIO_CORE_RAM_COLD void noteOff(int part, uint8_t note)
{
    if (!s_ready || part < 0 || part >= kStoreParts || note > 127 || !anyHeld(part))
        return;
    s_held[part][note >> 5] &= ~(1u << (note & 31));
    if (anyHeld(part))
        return;
    for (int k = 0; k < kSlots; k++)
        polysynth::open::modGateOff(s_slots[part][k], state(part, k));
}

AUDIO_CORE_RAM void tick(int part, float bpm)
{
    if (!s_ready || part < 0 || part >= kStoreParts)
        return;
    float sum[kParts] = {};
    uint8_t driven = 0;
    for (int k = 0; k < kSlots; k++)
    {
        const Slot& c = s_slots[part][k];
        if (c.type == polysynth::open::ModType::kOff || c.kind != polysynth::open::ModKind::kPart || c.dest < 0 || c.dest >= kParts)
        {
            continue;
        }
        polysynth::open::modStep(c, state(part, k), bpm);
        sum[c.dest] += state(part, k).out * polysynth::open::kModPartRange[c.dest];
        driven = uint8_t(driven | (1u << c.dest));
    }
    for (int n = 0; n < kParts; n++)
    {
        if (driven & (1u << n))
            set(part, n, sum[n]);
        else if (s_driven[part] & (1u << n))
            clear(part, n);
    }
    s_driven[part] = driven;
}
} // namespace partauto
#endif
