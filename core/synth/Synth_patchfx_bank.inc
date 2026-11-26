// Synth.cpp, included where this block was: the Patch FX slot bank of Tracker+ and Mini (FEATURE_SYNTH_PATCHFX without the open synth, IMX port P12)
#if defined(FEATURE_SYNTH_PATCHFX) && !defined(FEATURE_SYNTH_OPEN)
#include "noinit_check.h"
namespace
{
// The Patch FX bank of Tracker+ and Mini (IMX port P12). Row kPatchSlots is the shadow row: the open slot's live values, kept when the
// first Patch FX replaces them and put back by a Patch FX to the open slot or restorePatchFx. A value the patch does not set (nullopt)
// is kept as kUnsetBits. 84 is the most params a stock engine has (PERC, measured on the plus pin), the same bound as the MK66 bank.
constexpr int kBankParams = 84;
constexpr int kBankRows = 16 + 1; // Synth::kPatchSlots + the shadow row
constexpr uint32_t kUnsetBits = 0x7FC0FFEEu;
constexpr int kKindWords = (kBankParams + 31) / 32;
struct PatchBank
{
    float row[kBankRows][kBankParams];
    polysynth::Patch* live; // the part's patch as the audio callback may use it, swapped with parts[].patch with audio off
    uint32_t filled;        // bit n: slot n has a row of the part's engine
    uint32_t roundMask[kKindWords];  // bit i: param i blends rounded to an integer (Morph FX), set with the rows (reloadPatchBank)
    uint32_t switchMask[kKindWords]; // bit i: param i switches at 50 (enums and the integral params the MK66 switches)
    polysynth::open::SlotNames names; // names of slots 1..15 while they are not open ([0] unused) and the Morph target (-1: default)
    int16_t params;         // params of the rows (the engine's count), 0: no rows
    int8_t current;         // the open slot
    int8_t applied;         // the slot the Patch FX left in the part, -1: the open slot's own values
    int8_t namesBad;        // <P>.names is there but could not be read: it is not written over with empty names
};
// SDRAM (17,160 B for the rows, paid by the IMX sample pool cut ea4599a9, + 540 B a part for names and kinds), built by Synth::init
// (ffSynthPatchFxBoot) before anything reads it. Raw bytes: SlotNames has default member initialisers, so a PatchBank object here
// was built by a static constructor before main() sets up the SEMC (the 2.0.0 beta 1 Tracker+ and Mini boot hang, same as f563f27)
alignas(PatchBank) __NOINIT(EXTERNAL_RAM) unsigned char s_bankMem[kSynthParts][sizeof(PatchBank)];
NOINIT_PLAIN(decltype(s_bankMem));
// s_bank[part] and range for over the parts, the same use as the array it replaces. Empty and constexpr: no static constructor
struct PatchBankParts
{
    PatchBank& operator[](int part) const
    {
        return *std::launder(reinterpret_cast<PatchBank*>(s_bankMem[part]));
    }
    PatchBank* begin() const
    {
        return &(*this)[0];
    }
    PatchBank* end() const
    {
        return begin() + kSynthParts;
    }
};
constexpr PatchBankParts s_bank{};
// the <P>.names file as it is read or written (main thread only), SDRAM like the bank
__NOINIT(EXTERNAL_RAM) uint8_t s_namesBuf[polysynth::open::kSlotNamesFileMax];
NOINIT_PLAIN(decltype(s_namesBuf));

// Patch Vary (Synth::varyPatch): the undo row of the last Vary, the generator state and the amount, SDRAM like the bank (editor state, main
// thread only, nothing of it is saved), written by Synth::init
struct VaryState
{
    float undo[kBankParams];        // the values before the last Vary (an unset one as kUnsetBits, like the bank rows)
    const polysynth::Patch* patch;  // the live patch the undo belongs to (a new patch, slot or engine makes another)
    uint32_t rng;                   // the MK66 s_varyRng
    int16_t count;                  // params in undo
    uint8_t percent;                // 5..100, 100 = Randomize
    uint8_t valid;
};
__NOINIT(EXTERNAL_RAM) VaryState s_vary;
NOINIT_PLAIN(VaryState);

// The stock params the MK66 open ports (synth-open/engine_*.cpp ParamDef fmt, open_cold.cpp morphKind) do not blend: kSemi and kInt
// round to the nearest integer, kRatio, kNote and kBodyWave switch at 50 like the enums. Matched by the stock display name (every param of
// that name: PERC has three voices) of a built-in engine (Synth::kSynthTypes order: ACD FAT VAP WTFM PERC DIRT CHIP). The stock ACD has no
// Strum (the one MK66 kInt there). DIRT, CHIP and plugin engines: enums switch, the rest blend.
struct MorphRule
{
    uint8_t builtin;
    char kind; // 'r' round, 's' switch
    const char* name;
};
constexpr MorphRule kMorphRules[] = {
    { 2, 'r', "Tune 1" },    { 2, 'r', "Tune 2" },                                  // VAP kSemi
    { 3, 's', "Ratio 1" },   { 3, 's', "Ratio 2" },                                 // WTFM kRatio
    { 4, 's', "Body Wave" }, { 4, 's', "Pitch" },          { 4, 'r', "Timbre" },    // PERC kBodyWave, kNote, kInt
};

inline bool maskBit(const uint32_t* m, int i)
{
    return (m[i >> 5] >> (i & 31)) & 1u;
}

inline uint32_t bitsOf(float v)
{
    uint32_t b;
    memcpy(&b, &v, sizeof(b));
    return b;
}

inline uint32_t valueBits(const polysynth::Patch::optional_paramvalue_t& v)
{
    return v.has_value() ? bitsOf(*v) : kUnsetBits;
}

void rowFromPatch(const polysynth::Patch& patch, float* row, int n)
{
    for (int i = 0; i < n; i++)
    {
        const uint32_t b = valueBits(patch.getParamValue(i));
        memcpy(&row[i], &b, sizeof(b));
    }
}

int bankParams(const polysynth::Patch& patch)
{
    const int n = int(patch.getMetadata().params.size());
    return n <= kBankParams ? n : 0; // an engine with more params gets no bank (the Patch FX is ignored)
}

// main thread, the part's rows off (params 0): the Morph kinds of the engine's params
void setMorphKinds(PatchBank& b, const polysynth::Patch& patch, int builtin, int n)
{
    const auto& meta = patch.getMetadata().params;
    for (int w = 0; w < kKindWords; w++)
    {
        b.roundMask[w] = 0;
        b.switchMask[w] = 0;
    }
    for (int i = 0; i < n; i++)
    {
        char kind = meta[i].isEnum ? 's' : 'l';
        for (const MorphRule& r : kMorphRules)
        {
            if (r.builtin == builtin && meta[i].displayName == r.name)
            {
                kind = r.kind;
            }
        }
        if (kind != 'l')
        {
            (kind == 'r' ? b.roundMask : b.switchMask)[i >> 5] |= 1u << (i & 31);
        }
    }
}

std::string namesFilePath(int part)
{
    return fmt::format("{}/{}{}", kWorkspaceFolder, part, polysynth::open::kSlotNamesExt);
}
} // namespace

static std::string slotFileName(int part, int slot)
{
    return slot == 0 ? fmt::format("{}", part) : fmt::format("{}-{}", part, slot);
}
#endif
