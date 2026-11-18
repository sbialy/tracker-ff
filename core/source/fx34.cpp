#include "fx34.h"

#include <cr_section_macros.h>
#include <string.h>
#include <stddef.h>

// The extra FX slots of every pattern buffer. Plain data in the external RAM: no constructor runs for it, the sets are
// cleared by init() when the switch is first turned on. While the switch is off nothing here is read or written.

extern Sequencer::strPattern seq[2];
extern Sequencer::strPattern copyPastePattern;
extern Sequencer::strPattern fileManagerPatternBuffer;
extern Sequencer::strPattern fileManagerPerformancePatternBuffer;
extern Sequencer::strPattern songTrackCopy[2];
extern Sequencer::strPattern undoPatternBuffer[20];
extern Sequencer::strPattern undoSongBuffer[10];

namespace fx34
{
bool g_on = false;

__NOINIT(EXTERNAL_RAM) static Set s_sets[kSetCount];
static bool s_inited = false;
static bool s_refused = false;

namespace
{
struct Range
{
    const Pattern* base;
    uint8_t count;
    uint8_t first;
};

// The order is the one of the file header (undo and song undo last, they are the rarest to be asked for)
const Range kRanges[] = {
    { seq, 2, 0 },
    { &copyPastePattern, 1, 2 },
    { &fileManagerPatternBuffer, 1, 3 },
    { &fileManagerPerformancePatternBuffer, 1, 4 },
    { songTrackCopy, 2, 5 },
    { undoPatternBuffer, 20, 7 },
    { undoSongBuffer, 10, 27 },
};

int findSetIndex(const Pattern* p)
{
    for (const Range& r : kRanges)
    {
        if (p >= r.base && p < r.base + r.count)
        {
            return r.first + int(p - r.base);
        }
    }
    return -1;
}

// first entry whose key is >= key
inline uint16_t lowerBound(const Set* s, uint16_t key)
{
    uint16_t lo = 0, hi = s->n;
    while (lo < hi)
    {
        const uint16_t mid = uint16_t((lo + hi) >> 1);
        if (entryKey(s->e[mid]) < key)
            lo = uint16_t(mid + 1);
        else
            hi = mid;
    }
    return lo;
}

inline void removeAt(Set* s, uint16_t idx)
{
    memmove(&s->e[idx], &s->e[idx + 1], size_t(s->n - idx - 1) * sizeof(uint32_t));
    s->n--;
}

inline uint32_t makeEntry(uint8_t track, uint8_t step, uint8_t rank, uint8_t type, uint8_t value)
{
    return (uint32_t(keyOf(track, step, rank)) << 16) | (uint32_t(type) << 8) | value;
}

uint32_t crc32(const uint8_t* p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++)
    {
        c ^= p[i];
        for (int b = 0; b < 8; b++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
    }
    return ~c;
}
}   // namespace

void init()
{
    if (s_inited)
        return;
    for (int i = 0; i < kSetCount; i++) { s_sets[i].n = 0; s_sets[i].reserved = 0; }
    s_inited = true;
}

void resetAll()
{
    if (!s_inited)
        return;
    for (int i = 0; i < kSetCount; i++) s_sets[i].n = 0;
}

bool setEnabled(bool on)
{
    const bool was = g_on;
    if (on && !was)
    {
        init();
        resetAll();
    }
    g_on = on;
    return was;
}

Set* setOfAlways(const Pattern* p)
{
    if (!s_inited)
        return nullptr;
    const int i = findSetIndex(p);
    return i < 0 ? nullptr : &s_sets[i];
}

Set* setOf(const Pattern* p)
{
    return g_on ? setOfAlways(p) : nullptr;
}

bool locate(const Step* step, const Pattern** pattern, uint8_t* track, uint8_t* stepIdx)
{
    const char* sp = (const char*)step;
    for (const Range& r : kRanges)
    {
        const char* lo = (const char*)r.base;
        if (sp < lo || sp >= lo + sizeof(Pattern) * r.count)
            continue;
        const size_t patIdx = size_t(sp - lo) / sizeof(Pattern);
        const Pattern* p = r.base + patIdx;
        const size_t off = size_t(sp - (const char*)&p->track[0]);
        if (sp < (const char*)&p->track[0] || off >= sizeof(p->track))
            return false;
        const size_t t = off / sizeof(p->track[0]);
        const size_t rem = off % sizeof(p->track[0]);
        const size_t stepBase = size_t((const char*)&p->track[0].step[0] - (const char*)&p->track[0]);
        if (rem < stepBase)
            return false;
        *pattern = p;
        *track = uint8_t(t);
        *stepIdx = uint8_t((rem - stepBase) / sizeof(Step));
        return true;
    }
    return false;
}

Fx get(const Set* s, uint8_t track, uint8_t step, uint8_t rank)
{
    Fx f = { 0, 0 };
    if (!s || s->n == 0 || rank < kStored || rank >= kSlots)
        return f;
    const uint16_t key = keyOf(track, step, rank);
    const uint16_t i = lowerBound(s, key);
    if (i < s->n && entryKey(s->e[i]) == key)
    {
        f.type = entryType(s->e[i]);
        f.value = entryValue(s->e[i]);
    }
    return f;
}

bool getStep(const Set* s, uint8_t track, uint8_t step, Fx out[2])
{
    out[0].type = out[0].value = out[1].type = out[1].value = 0;
    if (!s || s->n == 0)
        return false;
    const uint16_t key0 = keyOf(track, step, kStored);
    uint16_t i = lowerBound(s, key0);
    bool any = false;
    for (int k = 0; k < 2 && i < s->n; k++, i++)
    {
        const uint16_t key = entryKey(s->e[i]);
        if (key == key0) { out[0].type = entryType(s->e[i]); out[0].value = entryValue(s->e[i]); any = true; }
        else if (key == uint16_t(key0 | 1)) { out[1].type = entryType(s->e[i]); out[1].value = entryValue(s->e[i]); any = true; }
        else break;
    }
    return any;
}

bool stepHasAny(const Set* s, uint8_t track, uint8_t step)
{
    if (!s || s->n == 0)
        return false;
    const uint16_t i = lowerBound(s, keyOf(track, step, kStored));
    return i < s->n && (entryKey(s->e[i]) >> 1) == (keyOf(track, step, kStored) >> 1);
}

Result put(Set* s, uint8_t track, uint8_t step, uint8_t rank, uint8_t type, uint8_t value)
{
    if (!s || rank < kStored || rank >= kSlots)
        return kOk;
    const uint16_t key = keyOf(track, step, rank);
    const uint16_t i = lowerBound(s, key);
    const bool exists = i < s->n && entryKey(s->e[i]) == key;
    if (type == 0)
    {
        if (exists)
            removeAt(s, i);
        return kOk;
    }
    if (exists)
    {
        s->e[i] = makeEntry(track, step, rank, type, value);
        return kOk;
    }
    if (s->n >= kCap)
    {
        noteRefusal();
        return kFull;
    }
    memmove(&s->e[i + 1], &s->e[i], size_t(s->n - i) * sizeof(uint32_t));
    s->e[i] = makeEntry(track, step, rank, type, value);
    s->n++;
    return kOk;
}

void clearStep(Set* s, uint8_t track, uint8_t step)
{
    if (!s || s->n == 0)
        return;
    const uint16_t k = keyOf(track, step, kStored);
    uint16_t i = lowerBound(s, k);
    while (i < s->n && (entryKey(s->e[i]) >> 1) == (k >> 1)) removeAt(s, i);
}

void clearRange(Set* s, uint8_t track, uint8_t from, uint8_t to)
{
    if (!s || s->n == 0 || from > to)
        return;
    const uint16_t lo = lowerBound(s, keyOf(track, from, kStored));
    uint16_t hi = lowerBound(s, uint16_t(keyOf(track, to, kStored) + 2));
    if (hi > lo)
    {
        memmove(&s->e[lo], &s->e[hi], size_t(s->n - hi) * sizeof(uint32_t));
        s->n = uint16_t(s->n - (hi - lo));
    }
}

void clearTrack(Set* s, uint8_t track)
{
    clearRange(s, track, 0, 127);
}

void copyTrack(Set* dst, uint8_t dTrack, const Set* src, uint8_t sTrack)
{
    if (!dst || dst == src)
        return;
    clearTrack(dst, dTrack);
    if (!src || src->n == 0)
        return;
    for (uint16_t i = 0; i < src->n; i++)
    {
        const uint32_t e = src->e[i];
        if (entryTrack(e) == sTrack)
            put(dst, dTrack, entryStep(e), entryRank(e), entryType(e), entryValue(e));
    }
}

void clearAll(Set* s)
{
    if (s)
        s->n = 0;
}

void clearBeyond(Set* s, uint8_t track, uint8_t lastKept)
{
    if (lastKept < 127)
        clearRange(s, track, uint8_t(lastKept + 1), 127);
}

void copySet(Set* dst, const Set* src)
{
    if (!dst || dst == src)
        return;
    if (!src || src->n == 0)
    {
        dst->n = 0;
        return;
    }
    dst->n = src->n;
    memcpy(dst->e, src->e, size_t(src->n) * sizeof(uint32_t));
}

Result copyStep(Set* dst, uint8_t dTrack, uint8_t dStep, const Set* src, uint8_t sTrack, uint8_t sStep, uint8_t mask)
{
    if (!dst)
        return kOk;
    Fx f[2];
    if (src)
        getStep(src, sTrack, sStep, f);
    else
        f[0].type = f[0].value = f[1].type = f[1].value = 0;
    Result r = kOk;
    for (int k = 0; k < 2; k++)
    {
        if (!(mask & (1 << k)))
            continue;
        if (put(dst, dTrack, dStep, uint8_t(kStored + k), f[k].type, f[k].value) == kFull)
            r = kFull;
    }
    return r;
}

void swapSteps(Set* s, uint8_t track, uint8_t stepA, uint8_t stepB, uint8_t mask)
{
    if (!s || stepA == stepB)
        return;
    Fx a[2], b[2];
    getStep(s, track, stepA, a);
    getStep(s, track, stepB, b);
    // erase first: the set never grows past what it holds before, so a full set cannot refuse half of a swap
    for (int k = 0; k < 2; k++)
    {
        if (!(mask & (1 << k)))
            continue;
        put(s, track, stepA, uint8_t(kStored + k), 0, 0);
        put(s, track, stepB, uint8_t(kStored + k), 0, 0);
    }
    for (int k = 0; k < 2; k++)
    {
        if (!(mask & (1 << k)))
            continue;
        put(s, track, stepA, uint8_t(kStored + k), b[k].type, b[k].value);
        put(s, track, stepB, uint8_t(kStored + k), a[k].type, a[k].value);
    }
}

void shiftDown(Set* s, uint8_t track, uint8_t from, uint8_t last)
{
    if (from > last)
        return;
    remapTrack(s, track, [from, last](int st) { return st < from || st > last ? st : (st == last ? -1 : st + 1); });
}

void shiftUp(Set* s, uint8_t track, uint8_t from, uint8_t last)
{
    if (from > last)
        return;
    remapTrack(s, track, [from, last](int st) { return st < from || st > last ? st : (st == from ? -1 : st - 1); });
}

uint16_t countRange(const Set* s, uint8_t track, uint8_t from, uint8_t to)
{
    if (!s || s->n == 0 || from > to)
        return 0;
    const uint16_t lo = lowerBound(s, keyOf(track, from, kStored));
    const uint16_t hi = lowerBound(s, uint16_t(keyOf(track, to, kStored) + 2));
    return hi > lo ? uint16_t(hi - lo) : 0;
}

void duplicateTrack(Set* s, uint8_t track, uint8_t len)
{
    if (!s || s->n == 0 || len == 0 || len > 64)
        return;
    const uint16_t lo = lowerBound(s, keyOf(track, 0, kStored));
    uint16_t hi = lowerBound(s, uint16_t(keyOf(track, uint8_t(len - 1), kStored) + 2));
    // newest first: an insert lands behind the source, so the sources before it keep their index
    while (hi > lo)
    {
        hi--;
        const uint32_t e = s->e[hi];
        put(s, track, uint8_t(entryStep(e) + len), entryRank(e), entryType(e), entryValue(e));
    }
}

bool takeRefusal()
{
    const bool r = s_refused;
    s_refused = false;
    return r;
}

void noteRefusal()
{
    s_refused = true;
}

Fx getFx(const Step* step, uint8_t rank)
{
    Fx f = { 0, 0 };
    if (rank < kStored)
    {
        f.type = step->fx[rank].type;
        f.value = step->fx[rank].value;
        return f;
    }
    const Pattern* p;
    uint8_t t, s;
    if (g_on && locate(step, &p, &t, &s))
        return get(setOf(p), t, s, rank);
    return f;
}

Result setFx(Step* step, uint8_t rank, uint8_t type, uint8_t value)
{
    if (rank < kStored)
    {
        step->fx[rank].type = type;
        step->fx[rank].value = value;
        return kOk;
    }
    const Pattern* p;
    uint8_t t, s;
    if (g_on && locate(step, &p, &t, &s))
        return put(setOf(p), t, s, rank, type, value);
    return kOk;
}

bool hasExtra(const Step* step)
{
    const Pattern* p;
    uint8_t t, s;
    if (g_on && locate(step, &p, &t, &s))
        return stepHasAny(setOf(p), t, s);
    return false;
}

size_t encode(const Set* s, uint32_t patternCrc, uint8_t* out)
{
    if (!s || s->n == 0)
        return 0;
    size_t o = 0;
    auto w32 = [&](uint32_t v) { for (int i = 0; i < 4; i++) out[o++] = uint8_t(v >> (8 * i)); };
    w32(kMagic);
    out[o++] = kFileVersion;
    out[o++] = 0;
    out[o++] = uint8_t(s->n);
    out[o++] = uint8_t(s->n >> 8);
    w32(patternCrc);
    for (uint16_t i = 0; i < s->n; i++) w32(s->e[i]);
    w32(crc32(out, o));
    return o;
}

DecodeResult decode(const uint8_t* in, size_t size, uint32_t patternCrc, Set* out)
{
    auto r32 = [&](size_t o) { return uint32_t(in[o]) | (uint32_t(in[o + 1]) << 8) | (uint32_t(in[o + 2]) << 16) | (uint32_t(in[o + 3]) << 24); };
    if (size < 16 || r32(0) != kMagic || in[4] != kFileVersion)
        return kDecodeBad;
    const uint16_t n = uint16_t(in[6] | (in[7] << 8));
    if (n == 0 || n > kCap || size != size_t(12 + 4 * n + 4))
        return kDecodeBad;
    if (r32(size - 4) != crc32(in, size - 4))
        return kDecodeBad;
    uint32_t prev = 0;
    for (uint16_t i = 0; i < n; i++)
    {
        const uint32_t e = r32(12 + 4 * i);
        if (i && entryKey(e) <= entryKey(prev))
            return kDecodeBad;   // not sorted or a duplicate key
        if (entryType(e) == 0 || (e >> 28) != 0)
            return kDecodeBad;
        prev = e;
    }
    if (r32(8) != patternCrc)
        return kDecodeStale;
    if (out)
    {
        out->n = n;
        for (uint16_t i = 0; i < n; i++) out->e[i] = r32(12 + 4 * i);
    }
    return kDecodeOk;
}
}   // namespace fx34
