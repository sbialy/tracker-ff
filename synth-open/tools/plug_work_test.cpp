// Host test of the synth plugin work arena (synth-open/plug_work.h), the allocator plug_synth.cpp attach() and detach() use.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_work_test.cpp -o /tmp/plug_work_test && /tmp/plug_work_test
//
// Checked:
//   - the table allocator: 8 byte blocks, lowest gap first, a block that goes back coalesces with its neighbours, nothing past the end
//   - compact: blocks slide down in offset order, the ones already in place are not reported, the free space is one gap after it
//   - workTake releases only what pick() names, one at a time, stops when release does not give the block back, packs the blocks
//     that must stay only when nothing else can go and the request fits in total, refuses without touching anything otherwise
//   - the firmware's policy (plug_synth.cpp attach: the engines of the other parts are pinned, the part's old engine goes last, least
//     recently used first) with the 14 engines of release 1.2.0 + formflux: every engine through 4 parts 100 times, no attach fails,
//     the high water does not grow after the first round
//   - the same with one more hungry engine (1,000 B): the old bump allocator (a block per entry until the next boot) refuses it, the
//     arena does not, because the limit is now the engines in use at the same time
//   - exhaustive: every arena state 3 and 4 parts can reach (any release order, a bank eviction leaves the block parked): no
//     request is refused while it fits with the engines the other parts play, and how many attaches have to pack (restart engines)
#include "plug_work.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <set>
#include <string>
#include <vector>

using namespace plug;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

constexpr uint32_t kArena = 16384;   // plug_synth.cpp kWorkBytes
constexpr int kLib = 16;             // plug::kLibMax

struct Eng
{
    const char* name;
    uint32_t work;   // PLUG_EXT_SYNTH work_bytes (validate_synth.py prints them)
};
// The 14 plugin engines of 1.2.0 with the formflux group: only four ask for work memory
const Eng kSet[] = {{"JU6", 0},  {"PHZ", 0},      {"FM4", 0},   {"DTYPE", 0},   {"CSTYPE", 0}, {"ATYPE", 0}, {"TTYPE", 512},
                    {"8TYPE", 1544}, {"HANDTYPE", 0}, {"BTYPE", 0}, {"ARCTYPE", 0}, {"PLK", 8772}, {"FORM", 0}, {"FLUX", 4676}};
constexpr int kSetN = int(sizeof(kSet) / sizeof(kSet[0]));

// plug_synth.cpp in miniature: library entries, attach on demand, the arena, the release order of attach()
struct Model
{
    std::vector<Eng> lib;
    int parts[4];
    int nParts;
    bool attached[kLib] = {};
    uint32_t used[kLib] = {};
    uint32_t tick = 0;
    WorkArena<kLib> a;
    int fails = 0, capacityFails = 0, releases = 0, packs = 0, restarted = 0;

    Model(const std::vector<Eng>& l, int np) : lib(l), nParts(np)
    {
        a.reset(kArena);
        for (int p = 0; p < 4; p++) parts[p] = -1;
    }
    // part p picks engine e (Synth::setInstrumentType -> synthEnsure): want = e and the other parts' engines, keep = the old one
    bool pick(int p, int e)
    {
        int want[4], nw = 0;
        want[nw++] = e;
        for (int q = 0; q < nParts; q++)
            if (q != p && parts[q] >= 0) want[nw++] = parts[q];
        const int keep = parts[p];
        used[e] = ++tick;
        bool ok = true;
        if (!attached[e])
        {
            if (lib[e].work && a.len[e])
                ;   // its parked block is still there: attach reuses it
            else if (lib[e].work)
            {
                auto inWant = [&](int j) { return std::find(want, want + nw, j) != want + nw; };
                auto victim = [&]() {   // plug_synth.cpp workVictim: parked first, then attached and unused, the old engine last, LRU
                    int best = -1;
                    long bestRank = 0;
                    for (int j = 0; j < int(lib.size()); j++)
                    {
                        if (!a.len[j] || j == e || inWant(j)) continue;
                        const long r = long(used[j]) + (attached[j] ? (1L << 29) : 0L) + (j == keep ? (1L << 30) : 0L);
                        if (best < 0 || r < bestRank)
                        {
                            best = j;
                            bestRank = r;
                        }
                    }
                    return best;
                };
                auto release = [&](int j) {
                    attached[j] = false;   // detach (engineGone, x = nullptr) when attached, then the block goes back
                    a.give(j);
                    releases++;
                };
                auto restart = [&](const int*, int n) {
                    packs++;
                    restarted += n;
                };
                ok = workTake(a, e, lib[e].work, victim, release, restart);
            }
            if (ok)
                attached[e] = true;
            else
            {
                uint32_t need = 0;
                for (int i = 0; i < nw; i++) need += workRound(lib[want[i]].work);
                (need <= kArena ? fails : capacityFails)++;
            }
        }
        if (ok) parts[p] = e;
        return ok;
    }
    // the flash bank evicts an engine no part plays: it is detached, its block stays parked
    void evict(int j)
    {
        for (int p = 0; p < nParts; p++)
            if (parts[p] == j) return;
        attached[j] = false;
    }
    uint32_t inUseBytes() const
    {
        uint32_t s = 0;
        bool seen[kLib] = {};
        for (int p = 0; p < nParts; p++)
            if (parts[p] >= 0 && !seen[parts[p]])
            {
                seen[parts[p]] = true;
                s += workRound(lib[parts[p]].work);
            }
        return s;
    }
};

// The old allocator (before round G): an entry keeps its block until the next boot
struct Bump
{
    uint32_t usedBytes = 0;
    bool has[kLib] = {};
    int fails = 0;
    void attach(int e, uint32_t work)
    {
        if (!work || has[e]) return;
        if (workRound(work) > kArena - usedBytes)
        {
            fails++;
            return;
        }
        usedBytes += workRound(work);
        has[e] = true;
    }
};

// ---- exhaustive search over the reachable arena states (any release order, any bank eviction of an engine no part uses)
struct Search
{
    std::vector<Eng> lib;
    int nParts;
    std::set<std::string> seen;
    std::vector<std::string> todo;
    long fragFails = 0, capFails = 0, packs = 0;
    int maxMoved = 0;
    struct St
    {
        int parts[4];
        uint32_t off[kLib], len[kLib];
    };
    static std::string key(const St& s) { return std::string(reinterpret_cast<const char*>(&s), sizeof(St)); }
    void push(const St& s)
    {
        const std::string k = key(s);
        if (seen.insert(k).second) todo.push_back(k);
    }
    WorkArena<kLib> arenaOf(const St& s) const
    {
        WorkArena<kLib> a;
        a.reset(kArena);
        memcpy(a.off, s.off, sizeof(a.off));
        memcpy(a.len, s.len, sizeof(a.len));
        return a;
    }
    // e needs a block, the blocks of `want` must stay: every order of releases the firmware could do (only while nothing fits)
    void place(St s, int e, const bool* want, int p)
    {
        WorkArena<kLib> a = arenaOf(s);
        if (a.take(e, lib[e].work))
        {
            s.off[e] = a.off[e];
            s.len[e] = a.len[e];
            s.parts[p] = e;
            push(s);
            return;
        }
        bool any = false;
        for (int j = 0; j < int(lib.size()); j++)
            if (s.len[j] && !want[j])
            {
                any = true;
                St t = s;
                t.len[j] = 0;
                t.off[j] = 0;
                place(t, e, want, p);
            }
        if (!any)
        {   // only blocks that must stay are left: pack them if the request fits in total (workTake), else refused for room
            uint32_t need = 0;
            for (int j = 0; j < int(lib.size()); j++)
                if (want[j]) need += workRound(lib[j].work);
            if (need > kArena)
            {
                capFails++;
                return;
            }
            a = arenaOf(s);
            int moved[kLib];
            const int m = a.compact(moved);
            if (!a.take(e, lib[e].work))
            {
                fragFails++;
                return;
            }
            packs++;
            maxMoved = m > maxMoved ? m : maxMoved;
            memcpy(s.off, a.off, sizeof(s.off));
            s.len[e] = a.len[e];
            s.parts[p] = e;
            push(s);
        }
    }
    void run()
    {
        St s0;
        memset(&s0, 0, sizeof(s0));
        for (int p = 0; p < 4; p++) s0.parts[p] = -1;
        push(s0);
        while (!todo.empty())
        {
            St s;
            const std::string k = todo.back();
            todo.pop_back();
            memcpy(&s, k.data(), sizeof(St));
            // a bank eviction detaches an engine no part plays and leaves its block parked: no new arena state
            for (int p = 0; p < nParts; p++)
                for (int e = 0; e < int(lib.size()); e++)
                {
                    if (e == s.parts[p]) continue;
                    bool want[kLib] = {};
                    want[e] = true;
                    for (int q = 0; q < nParts; q++)
                        if (q != p && s.parts[q] >= 0) want[s.parts[q]] = true;
                    St t = s;
                    if (!lib[e].work || t.len[e])
                    {
                        t.parts[p] = e;
                        push(t);
                    }
                    else
                        place(t, e, want, p);
                }
        }
    }
};

// engines without work memory behave alike in the arena: one stands for all of them (keeps the search small)
std::vector<Eng> hungryOnly(const std::vector<Eng>& l)
{
    std::vector<Eng> r;
    for (const Eng& e : l)
        if (e.work) r.push_back(e);
    r.push_back({"none", 0});
    return r;
}

}   // namespace

int main()
{
    setvbuf(stdout, nullptr, _IOLBF, 0);
    // ---- the allocator
    {
        WorkArena<4> a;
        a.reset(100);
        check(a.size == 96, "the arena is a whole number of 8 byte units");
        check(a.take(0, 10) && a.off[0] == 0 && a.len[0] == 16, "a block is rounded to 8 and starts at the bottom");
        check(a.take(1, 24) && a.off[1] == 16 && a.take(2, 8) && a.off[2] == 40, "the next blocks follow it (0..16, 16..40, 40..48)");
        check(!a.take(3, 56) && a.fit(56) < 0 && a.largestGap() == 48, "a request bigger than the largest gap is refused");
        a.give(1);
        check(a.largestGap() == 48 && a.fit(24) == 16 && a.fit(32) == 48, "a freed block is a gap, the lowest gap that fits wins");
        a.give(0);
        check(a.largestGap() == 48 && a.fit(40) == 0, "freed neighbours coalesce into one gap (0..40)");
        check(!a.take(2, 8), "an entry holds one block at a time");
        check(a.take(3, 48) && a.off[3] == 48 && a.used() == 56 && a.blocks() == 2 && a.peak == 96, "the tail gap is used to the end");
        check(!a.take(0, 0) && !a.take(0, 200), "nothing for 0 bytes or more than the arena");
    }
    {   // workTake: release first, pack only when nothing can go
        WorkArena<4> a;
        a.reset(96);
        a.take(2, 8);
        a.off[2] = 40;   // 2 at 40..48, 3 at 48..96: one gap 0..40
        a.take(3, 48);
        a.off[3] = 48;
        int calls = 0, restarts = 0;
        auto never = [&]() { return -1; };
        auto rel = [&](int) { calls++; };
        auto noRestart = [&](const int*, int m) { restarts += m; };
        check(!workTake(a, 0, 48, never, rel, noRestart) && calls == 0 && restarts == 0 && a.off[2] == 40 && a.off[3] == 48,
              "48 B with 40 B free and nothing to release: refused, nothing moves");
        auto pick3 = [&]() { return a.len[3] ? 3 : -1; };
        auto keepIt = [&](int) { calls++; };
        check(!workTake(a, 0, 48, pick3, keepIt, noRestart) && calls == 1 && restarts == 0,
              "a release that keeps the block stops workTake (no endless loop)");
        auto give = [&](int j) { a.give(j); calls++; };
        check(workTake(a, 0, 48, pick3, give, noRestart) && calls == 2 && restarts == 0 && a.off[0] == 48 && !a.len[3],
              "a release that gives the block back makes room, no packing needed (48 B at 48)");
        a.give(0);
        a.take(1, 16);   // 1 at 0..16, 2 at 40..48, 3 at 64..80: gaps 16..40, 48..64, 80..96 (24 + 16 + 16 = 56 B free)
        a.take(3, 16);
        a.give(3);
        a.off[3] = 64;
        a.len[3] = 16;
        check(a.fit(40) < 0 && a.largestGap() == 24 && a.size - a.used() == 56, "56 B free in three gaps, 40 B fit nowhere");
        int seen[4] = {}, nMoved = -1;
        auto rec = [&](const int* m, int n) {
            nMoved = n;
            for (int i = 0; i < n; i++) seen[m[i]] = 1;
        };
        check(workTake(a, 0, 40, never, rel, rec) && nMoved == 2 && seen[2] && seen[3] && !seen[1] && a.off[1] == 0 && a.off[2] == 16 &&
                a.off[3] == 24 && a.off[0] == 40 && a.used() == 80,
              "fragmented, nothing to release: packed (1 stays at 0, 2 and 3 move and restart), the 40 B go after them");
        check(!workTake(a, 0, 8, never, rel, rec), "an entry that has a block gets no second one");
        int order[4];
        check(a.compact(order) == 0, "packing a packed arena moves nothing");
    }

    const std::vector<Eng> set(kSet, kSet + kSetN);

    // ---- every engine through 4 parts, 100 rounds (the firmware has 3 parts, 4 is a margin)
    for (int extra = 0; extra < 2; extra++)
    {
        std::vector<Eng> l = set;
        if (extra) l.push_back({"NEXT", 1000});   // one more engine with work memory
        const int n = int(l.size());
        Model m(l, 4);
        Bump old;
        uint32_t peakRound1 = 0;
        int picks = 0, refused = 0;
        for (int round = 0; round < 100; round++)
        {
            for (int p = 0; p < 4; p++)
                for (int e = 0; e < n; e++)
                {
                    const int idx = (e + p * 3 + round) % n;   // the parts walk the list from different places
                    refused += !m.pick(p, idx);
                    old.attach(idx, l[idx].work);
                    picks++;
                    if (picks % 7 == 0) m.evict((idx + 5) % n);   // the flash bank drops an engine now and then
                }
            if (round == 9) peakRound1 = m.a.peak;
        }
        printf("info  %s: %d picks, arena high water %u B (after round 10 %u), most in blocks at once %u B, %d releases, %d packs "
               "restarting %d engines; old bump allocator %u B, %d refused\n",
               extra ? "1.2.0 + formflux + a 1,000 B engine" : "1.2.0 + formflux", picks, m.a.peak, peakRound1, m.a.peakUsed, m.releases,
               m.packs, m.restarted, old.usedBytes, old.fails);
        char what[200];
        snprintf(what, sizeof(what), "%s, 4 parts x %d engines x 100 rounds: no attach refused, the high water stays bounded (<= the arena, no growth after round 10)",
                 extra ? "+ a 1,000 B engine" : "1.2.0 + formflux", n);
        check(refused == 0 && m.fails == 0 && m.capacityFails == 0 && m.a.peak == peakRound1 && m.a.peak <= kArena, what);
        if (extra)
            check(old.fails > 0, "the old bump allocator refuses the 5th hungry engine once the other four were touched (attach error 5)");
        else
            check(old.fails == 0, "(the old bump allocator fits the shipped set: the four hungry engines are 15,512 B)");
    }

    // ---- exhaustive: every reachable state, every release order
    for (int extra = 0; extra < 2; extra++)
        for (int np = 3; np <= 4; np++)
        {
            if (extra && np == 4) continue;   // millions of states, the 3 part run already covers the firmware
            std::vector<Eng> l = set;
            if (extra) l.push_back({"NEXT", 1000});
            Search s{hungryOnly(l), np};
            s.run();
            printf("info  %s, %d parts: %zu reachable arena states, %ld attaches pack (restart at most %d engines), %ld refused for room\n",
                   extra ? "+ a 1,000 B engine" : "1.2.0 + formflux", np, s.seen.size(), s.packs, s.maxMoved, s.capFails);
            char what[200];
            snprintf(what, sizeof(what), "%s, %d parts: no request is refused while it fits with the engines the other parts play",
                     extra ? "+ a 1,000 B engine" : "1.2.0 + formflux", np);
            check(s.fragFails == 0 && s.capFails == 0 && s.maxMoved <= np - 1 && (extra || s.packs == 0), what);
            if (!extra) check(s.packs == 0, "the shipped set never packs: no engine is ever restarted, as with the old allocator");
        }
    {   // a made-up set with more than 16 KB in use: refusals are for room only, never for fragmentation
        std::vector<Eng> big = {{"A", 8000}, {"B", 6000}, {"C", 3000}, {"D", 2000}, {"E", 1000}, {"none", 0}};
        Search s{big, 3};
        s.run();
        printf("info  made-up A 8,000 B 6,000 C 3,000 D 2,000 E 1,000, 3 parts: %zu states, %ld attaches pack, %ld refused for room\n",
               s.seen.size(), s.packs, s.capFails);
        check(s.fragFails == 0 && s.capFails > 0, "made-up set over 16 KB: refused for room only (A + B + C > 16,384), never for fragmentation");
    }

    printf("%s  %d failed\n", g_fail ? "FAILED" : "PASSED", g_fail);
    return g_fail ? 1 : 0;
}
