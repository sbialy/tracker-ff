#ifndef FX34_H_
#define FX34_H_

// FX3 and FX4 of a pattern step (the "4 FX" project switch, project.mtff key `fx.slots 4`).
//
// The pattern file stays stock v6 (2 FX slots per step), so the two extra slots live next to it:
//  - in RAM as a sparse sorted list per pattern buffer (a Set, 256 entries at most, 4 B each),
//  - on the card as a per pattern sidecar (fx34_io.h) bound to the CRC of the .mtp.
// Nothing here runs while the switch is off: every entry point returns "empty" before it touches
// the SDRAM sets, so a project without the switch is byte for byte what 1.4.0 wrote before.
//
// Slot numbers are the player ranks of the whole firmware: rank 0 is the most significant slot
// (the right FX column of bank 1/2, "FX2"), rank 1 the left column ("FX1"), rank 2 the right column
// of bank 3/4 ("FX4"), rank 3 the left column ("FX3"). The step struct holds ranks 0 and 1 (fx[0],
// fx[1]), a Set holds ranks 2 and 3. A pair is (0,1) or (2,3): rank ^ 1 is the other slot of a pair.

#include "mtSequencer.h"

namespace fx34
{
constexpr int kSlots = 4;           // FX slots a step can have with the switch on
constexpr int kStored = 2;          // slots of the step struct (ranks 0 and 1)
constexpr int kCap = 256;           // extra FX per pattern
constexpr int kSetCount = 37;       // pattern buffers, see setOf()

typedef Sequencer::strPattern Pattern;
typedef Sequencer::strPattern::strTrack::strStep Step;

struct Fx
{
    uint8_t type;
    uint8_t value;
};

// 4 B per entry: key (track 4 bit, step 7 bit, extra slot 1 bit) << 16 | type << 8 | value. Sorted ascending.
struct Set
{
    uint16_t n;
    uint16_t reserved;
    uint32_t e[kCap];
};

static_assert(sizeof(Set) == 4 + 4 * kCap, "Set layout");

inline uint16_t keyOf(uint8_t track, uint8_t step, uint8_t rank)
{
    return uint16_t(((track & 15) << 8) | ((step & 127) << 1) | ((rank - kStored) & 1));
}
inline uint8_t entryType(uint32_t e) { return uint8_t(e >> 8); }
inline uint8_t entryValue(uint32_t e) { return uint8_t(e); }
inline uint16_t entryKey(uint32_t e) { return uint16_t(e >> 16); }
inline uint8_t entryTrack(uint32_t e) { return uint8_t((e >> 24) & 15); }
inline uint8_t entryStep(uint32_t e) { return uint8_t((e >> 17) & 127); }
inline uint8_t entryRank(uint32_t e) { return uint8_t(kStored + ((e >> 16) & 1)); }

extern bool g_on;   // the project switch, true only while a project that asks for 4 FX is open

inline bool enabled() { return g_on; }

// Turn the feature on or off. On: the sets are cleared (the caller loads the sidecars of the patterns in RAM
// afterwards). Off: the sets stay as they are but nothing reads them. Returns the previous state.
bool setEnabled(bool on);

// The set that belongs to a pattern buffer, nullptr while the switch is off or for a buffer that has none
// (a temporary pattern of an import, a pattern on the stack). Never creates anything.
Set* setOf(const Pattern* p);

// The same without the switch test: the file layer needs the set while the switch is still being turned on.
Set* setOfAlways(const Pattern* p);

// Where a step pointer lives: the pattern buffer, track and step index. False when the step is in no registered buffer.
bool locate(const Step* step, const Pattern** pattern, uint8_t* track, uint8_t* stepIdx);

// ---- read
inline uint16_t count(const Set* s) { return s ? s->n : 0; }
Fx get(const Set* s, uint8_t track, uint8_t step, uint8_t rank);          // rank 2..3, {0,0} when empty
// Both extra slots of a step with one search (out[0] is rank 2, out[1] rank 3). Returns true when at least one is set.
bool getStep(const Set* s, uint8_t track, uint8_t step, Fx out[2]);
bool stepHasAny(const Set* s, uint8_t track, uint8_t step);

// ---- write (every write returns false when the cap is reached and the entry is NOT stored)
enum Result : uint8_t { kOk, kFull };
Result put(Set* s, uint8_t track, uint8_t step, uint8_t rank, uint8_t type, uint8_t value);   // type 0 erases
void clearStep(Set* s, uint8_t track, uint8_t step);
void clearTrack(Set* s, uint8_t track);
// The whole track of another set (Performance mode switches a track of one pattern buffer into another)
void copyTrack(Set* dst, uint8_t dTrack, const Set* src, uint8_t sTrack);
void clearAll(Set* s);
void copySet(Set* dst, const Set* src);
// Move or copy the extra slots of one step to another (mask: bit 0 rank 2, bit 1 rank 3). The destination slots in the mask
// are overwritten, an empty source slot clears the destination one.
Result copyStep(Set* dst, uint8_t dTrack, uint8_t dStep, const Set* src, uint8_t sTrack, uint8_t sStep, uint8_t mask = 3);
void swapSteps(Set* s, uint8_t track, uint8_t stepA, uint8_t stepB, uint8_t mask = 3);
// Insert a gap: the extra slots of steps from..last move one step down (the one at last is dropped), step `from` is emptied.
void shiftDown(Set* s, uint8_t track, uint8_t from, uint8_t last);
// Delete a step: steps from+1..last move one step up, the one at last is emptied.
void shiftUp(Set* s, uint8_t track, uint8_t from, uint8_t last);
// Entries of a track in the steps from..to
uint16_t countRange(const Set* s, uint8_t track, uint8_t from, uint8_t to);
// Copy the steps 0..len-1 of a track to len..2*len-1 (a doubled pattern). The caller checks room() >= countRange() first.
void duplicateTrack(Set* s, uint8_t track, uint8_t len);

// Move the entries of a track to other steps. f(step) answers the new step or -1 to drop the entry. It must keep the order
// (never answer a smaller step for a larger one) and never answer one step for two, which every shift, expand and shrink does.
template <typename F>
void remapTrack(Set* s, uint8_t track, F f)
{
    if (!s || s->n == 0)
        return;
    uint16_t w = 0;
    for (uint16_t r = 0; r < s->n; r++)
    {
        uint32_t e = s->e[r];
        if (entryTrack(e) == track)
        {
            const int ns = f(int(entryStep(e)));
            if (ns < 0)
                continue;
            e = (uint32_t(keyOf(track, uint8_t(ns), entryRank(e))) << 16) | (e & 0xFFFFu);
        }
        s->e[w++] = e;
    }
    s->n = w;
}
void clearRange(Set* s, uint8_t track, uint8_t from, uint8_t to);          // steps from..to
// Drop every entry past a step (a shrunk track)
void clearBeyond(Set* s, uint8_t track, uint8_t lastKept);

// A write that was refused because the set is full. The pattern editor shows a popup and clears it.
bool takeRefusal();
void noteRefusal();

// Number of free entries of a set
inline uint16_t room(const Set* s) { return s ? uint16_t(kCap - s->n) : 0; }

// ---- slot access of a step with the step pointer only (the pattern editor and the clipboard code know steps, not patterns)
// Rank 0 and 1 read and write the struct, rank 2 and 3 the set of the buffer the step lives in.
Fx getFx(const Step* step, uint8_t rank);
Result setFx(Step* step, uint8_t rank, uint8_t type, uint8_t value);
// The FX of a step by rank when the caller knows the set already (a loop over a whole pattern)
inline Fx fxAt(const Step& step, const Set* set, uint8_t track, uint8_t stepIdx, uint8_t rank)
{
    if (rank < kStored)
    {
        const Fx f = { step.fx[rank].type, step.fx[rank].value };
        return f;
    }
    return get(set, track, stepIdx, rank);
}
// Both pairs empty (the step test of the editor)
bool hasExtra(const Step* step);


// The highest slot rank the pattern editor may address: 1 with the switch off (FX_SLOTS_MAX as before), 3 with it on
inline uint8_t maxSlot() { return g_on ? uint8_t(kSlots - 1) : uint8_t(kStored - 1); }

// One FX of a step for every rank. Ranks 0 and 1 point into the step, ranks 2 and 3 into a copy of the entry of the set that is
// written back (an erase when the type is 0) when the object leaves its scope. A full set refuses the write and raises takeRefusal().
struct FxRef
{
    Step::strFx* p;
    Step::strFx tmp;
    Set* set;
    uint8_t t, s, rank;
    FxRef(Pattern* pat, uint8_t track, uint8_t step, uint8_t r) : set(nullptr), t(track), s(step), rank(r)
    {
        if (r < kStored)
        {
            p = &pat->track[track].step[step].fx[r];
            return;
        }
        tmp.type = 0;
        tmp.value = 0;
        p = &tmp;
        set = setOf(pat);
        if (set)
        {
            const Fx f = get(set, track, step, r);
            tmp.type = f.type;
            tmp.value = f.value;
        }
    }
    ~FxRef()
    {
        if (set)
            put(set, t, s, rank, tmp.type, tmp.value);
    }
    FxRef(const FxRef&) = delete;
    FxRef& operator=(const FxRef&) = delete;
};

// ---- the project's sets, all of them (a project switch, a project close)
void resetAll();               // every set empty
void init();                   // the one time init of the SDRAM (called by setEnabled)

// ---- sidecar file image (fx34_io.cpp): the bytes of one set bound to the CRC of its pattern
constexpr uint32_t kMagic = 0x34584646;   // "FFX4"
constexpr uint8_t kFileVersion = 1;
constexpr size_t kFileMax = 12 + 4 * kCap + 4;
// Serialise a set, returns the size (0 for an empty set: no file is written for it)
size_t encode(const Set* s, uint32_t patternCrc, uint8_t* out);
enum DecodeResult : uint8_t { kDecodeOk, kDecodeStale, kDecodeBad };
// Parse an image. kDecodeStale: well formed but bound to another CRC (the set stays empty). kDecodeBad: damaged
DecodeResult decode(const uint8_t* in, size_t size, uint32_t patternCrc, Set* out);
}   // namespace fx34

#endif
