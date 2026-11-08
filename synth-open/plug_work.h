// Work memory of the synth engine plugins (PLUG_EXT_SYNTH work_bytes, PLK's delay line pool, FLUX's buffers ...). Header only and
// free of the platform: plug_synth.cpp is the firmware user, synth-open/tools/plug_work_test.cpp runs the same code on the Mac.
//
// The arena (plug_synth.cpp s_work, 16 KB of SDRAM) holds one block per ATTACHED library entry. The table below is the only
// bookkeeping, the arena itself carries no header: a block is (off, len) at its entry's index and the free space is every gap
// between blocks, so a block that goes back coalesces with its free neighbours by construction. A request takes the lowest gap
// that is big enough (first fit, 8 byte aligned).
//
// A block is taken at the engine's first attach (zeroed at every attach and handed to the engine with set_work after init, as the
// ABI says) and stays with the entry while nobody else needs the room, also across a detach (eviction from the flash bank): an
// engine that comes back finds its block, nothing moves. When a request finds no gap, workTake gives back the blocks no part needs,
// one at a time (plug_synth.cpp attach: parked blocks of detached engines first, then attached engines no part plays, which are
// detached, the part's old engine last, least recently used first in each group). So no part loses anything to a release, and a
// release happens only when the room is really wanted. Before round G the arena was a bump allocator: an entry kept its block
// until the next boot, the limit was the sum of every engine touched since boot. Now it is the engines in use at the same time.
//
// Audio interrupt safety: a block is only given back when its engine is detached, and detach first takes the engine away from every
// part and from its adapter with the audio interrupt off (open::engineGone, PlugSynth::x = nullptr). From then on nothing the audio
// interrupt runs can reach the block, so it may be zeroed and handed to another engine on the main loop. A block that moves when
// the arena is packed belongs to an engine that is stopped the same way first (open::engineRestartLocked). Nothing here runs in
// the interrupt.
//
// Fragmentation: m blocks that stay (the engines of the other parts) cut the free space into at most m + 1 gaps, so first fit alone
// may refuse a request that fits in total. plug_work_test finds such states for the 1.2.0 set (8-Type at 0 and PLK in the middle leave
// 3,648 + 2,416 B, FLUX needs 4,680). workTake therefore packs the blocks that stay when nothing else can go: a request is refused only
// when it does not fit together with the engines the other parts play. The price of packing is a restart of the engines whose block
// moved (their notes are cut, at most one per other part), counted in /Plugins/synth.txt. It can only happen once the engines in use
// came close to the arena size and blocks were released: the 1.2.0 set never releases (all four hungry engines fit at once).
// plug_work_test walks every arena state 3 and 4 parts can reach (any release order) and counts the attaches that pack.
#pragma once

#include <stdint.h>

namespace plug
{

constexpr uint32_t kWorkAlign = 8;

inline uint32_t workRound(uint32_t n)
{
    return (n + (kWorkAlign - 1u)) & ~(kWorkAlign - 1u);
}

template <int N>
struct WorkArena
{
    uint32_t size;
    uint32_t off[N], len[N];   // the block of entry k, len 0: none
    uint32_t peak;             // high water: the highest end offset a block ever had
    uint32_t peakUsed;         // the most bytes in blocks at once
    int moved[N];              // workTake's list of the blocks compact moved (here, not on the stack: N follows the library)

    void reset(uint32_t bytes)
    {
        size = bytes & ~(kWorkAlign - 1u);
        peak = peakUsed = 0;
        for (int k = 0; k < N; k++) off[k] = len[k] = 0;
    }

    // the offset of the lowest gap of at least n bytes (rounded to 8), -1 when there is none
    int32_t fit(uint32_t n) const
    {
        n = workRound(n);
        if (!n || n > size) return -1;
        uint32_t at = 0;
        for (bool again = true; again;)
        {   // jump past every block that overlaps [at, at + n): no lower start can fit (bankFindRw does the same)
            again = false;
            for (int k = 0; k < N; k++)
                if (len[k] && off[k] < at + n && at < off[k] + len[k])
                {
                    at = off[k] + len[k];
                    again = true;
                    if (at > size - n) return -1;
                }
        }
        return int32_t(at);
    }

    bool take(int k, uint32_t n)
    {
        if (k < 0 || k >= N || len[k]) return false;
        const int32_t o = fit(n);
        if (o < 0) return false;
        off[k] = uint32_t(o);
        len[k] = workRound(n);
        peak = off[k] + len[k] > peak ? off[k] + len[k] : peak;
        const uint32_t u = used();
        peakUsed = u > peakUsed ? u : peakUsed;
        return true;
    }

    void give(int k)
    {
        if (k >= 0 && k < N) len[k] = 0;
    }

    uint32_t used() const
    {
        uint32_t u = 0;
        for (int k = 0; k < N; k++) u += len[k];
        return u;
    }

    int blocks() const
    {
        int n = 0;
        for (int k = 0; k < N; k++) n += len[k] != 0;
        return n;
    }

    // Slides every block down to the bottom, in the order of their offsets, so the free space is one gap at the end. out[] gets the
    // entries whose block changed place (their engines must start again there, see workTake), the count is returned. A block never
    // moves up and never onto another block that stays, so the ones that keep their place keep their bytes. No scratch the size of
    // the table: the next block is the one with the lowest old offset above the last one placed (a block placed never moves up, so
    // its new offset is below that mark and it is not picked again).
    int compact(int* out)
    {
        uint32_t at = 0, mark = 0;
        int m = 0;
        for (bool first = true;; first = false)
        {
            int k = -1;
            for (int j = 0; j < N; j++)
                if (len[j] && (first || off[j] > mark) && (k < 0 || off[j] < off[k])) k = j;
            if (k < 0) break;
            mark = off[k];
            if (off[k] != at)
            {
                off[k] = at;
                out[m++] = k;
            }
            at += len[k];
        }
        return m;
    }

    // the biggest request that would fit now
    uint32_t largestGap() const
    {
        uint32_t best = 0;
        for (int s = -1; s < N; s++)
        {   // a gap starts at 0 or at the end of a block and runs to the next block start (or the end)
            if (s >= 0 && !len[s]) continue;
            const uint32_t a = s < 0 ? 0 : off[s] + len[s];
            uint32_t b = size;
            bool inside = false;
            for (int k = 0; k < N; k++)
            {
                if (!len[k]) continue;
                if (off[k] <= a && a < off[k] + len[k]) inside = true;
                if (off[k] >= a && off[k] < b) b = off[k];
            }
            if (!inside && b - a > best) best = b - a;
        }
        return best;
    }
};

// Entry k gets n bytes (plug_synth.cpp attach). While no gap fits:
//   1. pick() names an entry whose block may go (a detached engine's parked block, an attached engine no part needs, the part's old
//      engine last), release(j) gives it back (plug_synth.cpp: detach when attached, then give). Nothing audible: no part plays them.
//   2. when pick() has none left, the blocks that are left belong to the engines the parts play (and must keep). If they and n fit the
//      arena together, they are packed to the bottom (compact) and restart(moved, m) starts the m engines whose block moved again at
//      the new place, voices cut (plug_synth.cpp restartMoved). n then fits the one gap at the end.
// False: the engines that must stay and n do not fit together (16 KB is too small for this combination), nothing was moved.
// So fragmentation never refuses a request, its worst case is the restart: at most one engine per other part (2 with 3 parts) has its
// notes cut when the arena is packed, only at an attach that needs it.
template <int N, class Pick, class Release, class Restart>
bool workTake(WorkArena<N>& a, int k, uint32_t n, Pick pick, Release release, Restart restart)
{
    if (k < 0 || k >= N || a.len[k]) return false;   // one block per entry
    while (!a.take(k, n))
    {
        const int j = pick();
        if (j >= 0 && j < N && j != k && a.len[j])
        {
            release(j);
            if (a.len[j]) return false;   // release did not give it back: stop, no endless loop
            continue;
        }
        if (!n || workRound(n) > a.size - a.used()) return false;
        const int m = a.compact(a.moved);
        restart(a.moved, m);
        return a.take(k, n);
    }
    return true;
}

}   // namespace plug
