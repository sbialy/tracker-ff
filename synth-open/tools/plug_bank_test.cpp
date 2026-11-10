// Host test of the synth engine bank (synth-open/plug_bank.h): the FlexNVM cache of /Plugins/Synth that the firmware fills on
// demand (plug_synth.cpp synthEnsure), against a simulated flash that can lose power after any command.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_bank_test.cpp -o /tmp/plug_bank_test && /tmp/plug_bank_test
//
// Checked:
//   - installed.txt round trip with the erase counters, an index without them (hosts before round E), damaged lines dropped
//   - boot (bankOpen): no index and no plugin writes nothing, a damaged entry and the entry of a .plg that left the card or
//     changed are dropped, never a flash command
//   - install on demand: every engine starts on a sector boundary and shares no sector, an engine already there costs nothing
//   - the 14 plugins (formflux group included) at their pinned sizes: how many fit at once (30 sectors), LRU eviction when the bank is full,
//     the engines the parts use (pinned) are never evicted and their bytes never change while the others come and go
//   - an engine that comes back to the sectors it left finds its bytes there: only what another engine overwrote is written
//   - a project whose engines do not fit together: the one that does not fit fails (kBankNoRoom), nothing else changes
//   - the rw pool is a limit too: an engine whose rw does not fit evicts by rw
//   - power cut after EVERY flash command of an install that evicts and erases: the next boot never keeps an inexact entry, the
//     pinned engines are intact, the install then completes; a journal that cannot be written issues no command
//   - wear: the "c" counters equal the erases the flash really did (never fewer after a cut), installs spread over the free
//     sectors, worst case erases per pick for a user cycling through more engines than the bank holds (printed)
//   - insert 2's sectors (0x1E000..) are never erased or programmed
#include "plug_bank.h"

#include <cstdio>
#include <algorithm>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

using namespace plug;

namespace
{
int g_fail = 0;
int g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        printf("FAIL  %s\n", what);
        g_fail++;
    }
}

constexpr uintptr_t kPool = 0x20027000;
constexpr uint32_t kPoolBytes = 7680;   // plug_synth.cpp

struct Flash
{
    uint8_t m[0x20000];
    int commands = 0;
    int cutAt = -1;   // the command with this number (0 based) is cut half way, every later one fails
    bool dead = false;
    uint32_t rng = 1;
    uint32_t erased[32] = {};   // real erases per sector (the wear the counters must follow)
    Flash() { memset(m, 0xFF, sizeof(m)); }
    const uint8_t* at(uint32_t off) { return m + off; }
    uint8_t noise()
    {
        rng = rng * 1103515245u + 12345u;
        return uint8_t(rng >> 16);
    }
    bool begin()
    {
        if (dead) return false;
        if (commands == cutAt)
        {
            dead = true;
            commands++;
            return false;
        }
        commands++;
        return true;
    }
    bool erase(uint32_t s)
    {
        if (s & (kBankSector - 1) || s >= kBankBytes) { check(false, "erase outside the bank or unaligned"); return false; }
        const bool cut = !dead && commands == cutAt;
        if (!begin())
        {
            if (cut)
            {
                for (uint32_t i = 0; i < kBankSector / 2; i++) m[s + i] = 0xFF;   // half erased
                erased[s / kBankSector]++;
            }
            return false;
        }
        memset(m + s, 0xFF, kBankSector);
        erased[s / kBankSector]++;
        return true;
    }
    bool program(uint32_t off, const uint8_t* d)
    {
        if ((off & 7u) || off + 8 > kBankBytes) { check(false, "program outside the bank or unaligned"); return false; }
        for (int k = 0; k < 8; k++)
            if (m[off + k] != 0xFF) { check(false, "program over a programmed phrase"); return false; }
        const bool cut = !dead && commands == cutAt;
        if (!begin())
        {
            if (cut)
                for (int k = 0; k < 8; k++) m[off + k] = d[k] | (noise() & 0x5A);   // some bits did not take
            return false;
        }
        memcpy(m + off, d, 8);
        return true;
    }
};

// An engine on the card: its bytes depend on where it is placed (the loader rebases text for the flash address, rw for the pool
// address), so an engine at another place really is other bytes.
struct Plg
{
    uint32_t uid, version, fileCrc, len, text, rwLen;
    std::string file;
};

std::vector<uint8_t> imageOf(const Plg& p, uint32_t off, uint32_t rwRun, uint32_t padLen)
{
    std::vector<uint8_t> v(padLen, 0);
    uint32_t h = p.uid ^ p.fileCrc;
    for (uint32_t i = 0; i < p.len; i++)
    {
        h = h * 0x01000193u ^ i;
        v[i] = uint8_t(h >> 24);
    }
    for (uint32_t i = 0; i + 4 <= p.len; i += 64)   // "rebase words"
    {
        const uint32_t w = i < p.text ? uint32_t(kBankRun + off + i) : rwRun + i;
        memcpy(&v[i], &w, 4);
    }
    return v;
}

struct Io
{
    std::string index;
    bool haveIndex = false;
    int indexWrites = 0;
    bool failWrite = false;
    Flash* f = nullptr;
    std::map<std::string, Plg> card;
    std::vector<uint8_t> stage;
    int readIndex(char* b, int cap)
    {
        if (!haveIndex) return -1;
        const int n = int(index.size()) < cap ? int(index.size()) : cap;
        memcpy(b, index.data(), size_t(n));
        return n;
    }
    bool writeIndex(const char* t, int n)
    {
        if (failWrite || (f && f->dead)) return false;   // the power is gone: the old file stays
        index.assign(t, size_t(n));
        haveIndex = true;
        indexWrites++;
        return true;
    }
    const uint8_t* image(const BankEntry& e)
    {
        auto it = card.find(e.file);
        if (it == card.end() || it->second.uid != e.uid || it->second.fileCrc != e.fileCrc) return nullptr;
        stage = imageOf(it->second, e.off, e.rwRun, e.len);
        return stage.data();
    }
};

BankWant want(const Plg& p)
{
    BankWant x = {};
    x.uid = p.uid;
    x.version = p.version;
    x.fileCrc = p.fileCrc;
    x.len = p.len;
    x.text = p.text;
    x.rwLen = p.rwLen;
    snprintf(x.file, sizeof(x.file), "%s", p.file.c_str());
    return x;
}

char g_text[4096];
uint8_t g_sec[kBankSector];
Bank g_work[2];

BankOpenStatus open(Flash& f, Io& io, Bank& b)
{
    io.f = &f;
    std::vector<BankWant> w;
    for (const auto& kv : io.card) w.push_back(want(kv.second));
    return bankOpen(f, io, w.data(), int(w.size()), kPool, kPoolBytes, b, g_text, int(sizeof(g_text)));
}

// The firmware's ranking: engines on a part never go (-1), else the least recently used first
struct Cache
{
    std::map<uint32_t, uint32_t> lastUse;   // uid -> tick
    std::vector<uint32_t> pinned;            // uids the parts use
    uint32_t tick = 0;
    std::vector<uint32_t> evicted;
};

BankInstallStatus install(Flash& f, Io& io, Bank& b, const std::string& file, Cache& c, uint32_t pool = kPoolBytes)
{
    io.f = &f;
    const Plg& p = io.card.at(file);
    c.lastUse[p.uid] = ++c.tick;
    auto rank = [&](const BankEntry& e) -> long {
        for (uint32_t u : c.pinned)
            if (u == e.uid) return -1;
        return long(c.lastUse.count(e.uid) ? c.lastUse[e.uid] : 0);
    };
    auto onEvict = [&](const BankEntry& e) { c.evicted.push_back(e.uid); };
    return bankInstall(f, io, want(p), rank, onEvict, kPool, pool, b, g_sec, g_text, int(sizeof(g_text)), g_work);
}

// Every entry of the bank holds exactly the image of its file for its place (what "never run a half-written entry" needs)
bool exact(Flash& f, const Io& io, const Bank& b)
{
    for (int i = 0; i < b.n; i++)
    {
        const BankEntry& e = b.e[i];
        auto it = io.card.find(e.file);
        if (it == io.card.end()) return false;
        const auto img = imageOf(it->second, e.off, e.rwRun, e.len);
        if (e.state != 'o' || memcmp(f.at(e.off), img.data(), e.len) != 0) return false;
    }
    return true;
}

bool layoutOk(const Bank& b, uint32_t pool = kPoolBytes)
{
    for (int i = 0; i < b.n; i++)
    {
        const BankEntry& x = b.e[i];
        if (x.off % kBankSector || x.off + x.len > kBankBytes || x.rwRun < kPool || x.rwRun + x.rwLen > kPool + pool) return false;
        for (int k = 0; k < b.n; k++)
            if (k != i && (overlaps(uint32_t(sectorFirst(x)), uint32_t(sectorEnd(x) - sectorFirst(x)), uint32_t(sectorFirst(b.e[k])),
                                    uint32_t(sectorEnd(b.e[k]) - sectorFirst(b.e[k]))) ||
                           overlaps(x.rwRun, x.rwLen, b.e[k].rwRun, b.e[k].rwLen)))
                return false;
    }
    return true;
}

bool insert2Blank(const Flash& f)
{
    for (uint32_t i = kBankBytes; i < 0x20000; i++)
        if (f.m[i] != 0xFF) return false;
    return true;
}

int find(const Bank& b, uint32_t uid)
{
    for (int i = 0; i < b.n; i++)
        if (b.e[i].uid == uid) return i;
    return -1;
}

Plg plg(const char* file, uint32_t uid, uint32_t len, uint32_t text, uint32_t rwLen, uint32_t crc = 0)
{
    return Plg{uid, 0x10000, crc ? crc : uid * 7u + len, (len + 3u) & ~3u, text, rwLen, file};
}

// The 14 synth plugins of release 1.2.0 with the formflux group at their pinned sizes (image, text, rw + bss), engine DSP at -O2 (FM4 and
// B-Type -O3), 12.3.rel1 AUTO-F build (engines/rebuild_pinned.sh ... --with formflux). Every count below follows kN, nothing pins 12.
struct P
{
    const char* f;
    uint32_t uid, len, text, rw;
};
const P kAll[] = {
    {"ju6.plg", 0x73ac7ee0, 7180, 7176, 1648},
    {"fm4.plg", 0xea3d5a7a, 10652, 10648, 112},
    {"btype.plg", 0x5e9b11f7, 13872, 13864, 120},
    {"phz.plg", 0x6670b917, 14728, 14720, 120},
    {"dtype.plg", 0x6754cf41, 11660, 11656, 112},
    {"cstype.plg", 0xf73018f5, 12588, 12584, 112},
    {"atype.plg", 0x24084bf2, 13468, 13464, 112},
    {"ttype.plg", 0x318f12b1, 16492, 16488, 168},
    {"hand.plg", 0xf00e588e, 12632, 12624, 120},
    {"arc.plg", 0xc66464e9, 19472, 19464, 120},
    {"pluck.plg", 0xb4a152b0, 6196, 6192, 120},
    {"eight.plg", 0x9d7c57e2, 21160, 21152, 120},
    {"form.plg", 0xdbc3be3d, 22148, 22144, 112},
    {"flux.plg", 0x28785da0, 21868, 21864, 120},
    {"chip.plg", 0xb16d36ed, 13328, 13320, 4200},   // last: the indices above are used by the tests below
};
constexpr int kN = int(sizeof(kAll) / sizeof(kAll[0]));

Io allPlugins()
{
    Io io;
    for (const P& p : kAll) io.card[p.f] = plg(p.f, p.uid, p.len, p.text, p.rw);
    return io;
}

uint32_t sectorsOf(uint32_t len)
{
    return (align8(len) + kBankSector - 1) / kBankSector;
}
}   // namespace

int main()
{
    // ---- index codec
    {
        Bank b = {};
        b.n = 2;
        b.e[0] = {0x73ac7ee0, 0x10000, 0x1234, 0, 6952, 6944, 0x20027000, 1648, 0, 0xdeadbeef, 'o', "ju6.plg"};
        b.e[1] = {0xea3d5a7a, 0x10000, 0x5678, 8192, 8168, 8160, 0x20027670, 112, 0, 0xfeedf00d, 'w', "fm4.plg"};
        for (int k = 0; k < kBankSectors; k++) b.wear[k] = uint32_t(k * 3 + 1);
        char t[2048];
        const int n = bankWrite(b, t, sizeof(t));
        Bank r;
        bankRead(t, n, r);
        check(n > 0 && r.n == 2 && memcmp(&r.e[0], &b.e[0], sizeof(BankEntry)) == 0 && memcmp(&r.e[1], &b.e[1], sizeof(BankEntry)) == 0 &&
                memcmp(r.wear, b.wear, sizeof(b.wear)) == 0,
              "installed.txt round trip, erase counters included");
        std::string s(t, size_t(n));
        s.insert(s.find("\r\no ") + 2, "o 1 2\r\n");   // a cut line before the real one
        bankRead(s.data(), int(s.size()), r);
        check(r.n == 2, "a damaged line is dropped, the others stay");
        std::string old = s;
        const size_t c0 = old.find("\r\nc ") + 2;
        old.erase(c0, old.find("\r\n", c0) + 2 - c0);
        bankRead(old.data(), int(old.size()), r);
        uint32_t sum = 0;
        for (uint32_t w : r.wear) sum += w;
        check(r.n == 2 && sum == 0, "an index of a host before round E (no c line): entries kept, counters start at 0");
        bankRead("garbage", 7, r);
        check(r.n == 0, "no header: empty bank");
        check(bankWrite(b, t, 40) < 0, "a full buffer is refused");
    }

    // ---- insert 2's erase counters (the end of the "c" line): round trip, old index, journal, persist rules, size
    {
        Bank b = {};
        b.n = 1;
        b.e[0] = {0x73ac7ee0, 0x10000, 0x1234, 0, 6952, 6944, 0x20027000, 1648, 0, 0xdeadbeef, 'o', "ju6.plg"};
        for (int k = 0; k < kBankSectors; k++) b.wear[k] = uint32_t(k + 1);
        b.insWear[0] = 7;
        b.insWear[1] = 0x1234;
        char t[2048];
        int n = bankWrite(b, t, sizeof(t));
        Bank r;
        bankRead(t, n, r);
        check(n > 0 && r.insWear[0] == 7 && r.insWear[1] == 0x1234 && memcmp(r.wear, b.wear, sizeof(b.wear)) == 0 && r.n == 1,
              "insert 2 counters round trip on the c line, the 30 sector counters and the entry unchanged");
        // an index of a host that knows only the 30 (round E to 1.2.0): the 30 are kept, insert 2 starts at 0
        std::string s30(t, size_t(n));
        const size_t c0 = s30.find("\r\nc") + 3;   // the space before the first counter
        const size_t cEnd = s30.find("\r\n", c0);
        size_t cut = c0;
        for (int k = 0; k < kBankSectors; k++) cut = s30.find(' ', cut + 1);   // the space before insert 2's first
        s30.erase(cut, cEnd - cut);
        bankRead(s30.data(), int(s30.size()), r);
        check(r.n == 1 && r.wear[29] == 30 && r.insWear[0] == 0 && r.insWear[1] == 0, "a 30 counter c line: sector counters kept, insert 2 at 0");
        // one of the two missing or damaged: neither is taken, the 30 stay
        std::string s31(t, size_t(n));
        s31.replace(s31.find("\r\n", s31.find("\r\nc") + 3) - 8, 8, "zz");   // the last counter, 8 digits
        bankRead(s31.data(), int(s31.size()), r);
        check(r.n == 1 && r.wear[29] == 30 && r.insWear[0] == 0 && r.insWear[1] == 0, "a damaged insert 2 counter: both start at 0, the rest stays");
        // what a host before this change makes of the new line: reads the 30 and ignores the rest (same loop, no end of line check)
        {
            const char* q = t + 12;   // after "tffbank 1\r\nc"
            const char* e = t + n;
            bool ok = true;
            uint32_t w[kBankSectors];
            for (int k = 0; k < kBankSectors && ok; k++) ok = q < e && *q++ == ' ' && sc::getHex(q, e, w[k]);
            check(ok && w[0] == 1 && w[29] == 30, "the previous parser (30 only) still reads the 30 counters of the new line");
        }
        // the worst index still fits the 2,048 B buffer of plug_synth.cpp: 12 entries with the longest file name, all counters at 8 digits
        Bank big = {};
        big.n = kBankMax;
        for (int i = 0; i < kBankMax; i++)
        {
            big.e[i] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFF8u, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu,
                        0xFFFFFFFFu, 'o', "abcdefghijklmnopqrstuvw"};
        }
        for (uint32_t& w : big.wear) w = 0xFFFFFFFFu;
        big.insWear[0] = big.insWear[1] = 0xFFFFFFFFu;
        const int nb = bankWrite(big, t, 2048);
        check(nb > 0 && nb < 2048, "the largest possible index (12 entries, longest names, 8 digit counters) fits 2,048 B");
        printf("      largest installed.txt %d of 2048 B\n", nb);

        // bankCountInsert: counted in the bank and the index, only the sectors in the mask
        Io io;
        Bank c = {};
        c.wear[3] = 5;
        check(bankCountInsert(io, c, 3, true, t, sizeof(t)) && c.insWear[0] == 1 && c.insWear[1] == 1 && io.indexWrites == 1,
              "insert 2 erases of both sectors counted and written");
        check(bankCountInsert(io, c, 2, true, t, sizeof(t)) && c.insWear[0] == 1 && c.insWear[1] == 2 && io.indexWrites == 2,
              "only the sector of the mask goes up");
        Bank back;
        bankRead(io.index.data(), int(io.index.size()), back);
        check(back.insWear[0] == 1 && back.insWear[1] == 2 && back.wear[3] == 5, "the written index holds both kinds of counter");
        check(!bankCountInsert(io, c, 0, true, t, sizeof(t)) && io.indexWrites == 2 && c.insWear[0] == 1, "no erase: nothing counted, nothing written");
        check(!bankCountInsert(io, c, 1, false, t, sizeof(t)) && c.insWear[0] == 2 && io.indexWrites == 2,
              "index not trusted (not read at boot): counted in RAM, the card index is not replaced");
        io.failWrite = true;
        check(!bankCountInsert(io, c, 1, true, t, sizeof(t)) && c.insWear[0] == 3, "a card error: still counted in RAM (the install goes on), reports false");
        io.failWrite = false;

        // the counters survive a boot and an install (the journal and the final index carry them)
        Flash f;
        Io io2 = allPlugins();
        Bank b2;
        Cache cache;
        open(f, io2, b2);
        bankCountInsert(io2, b2, 3, true, g_text, int(sizeof(g_text)));
        bankCountInsert(io2, b2, 1, true, g_text, int(sizeof(g_text)));
        install(f, io2, b2, "ju6.plg", cache);
        check(b2.insWear[0] == 2 && b2.insWear[1] == 1, "an install keeps insert 2's counters");
        Bank rb;
        BankOpenStatus os = open(f, io2, rb);
        check(os.indexRead && rb.insWear[0] == 2 && rb.insWear[1] == 1 && rb.n == 1, "reboot: insert 2's counters and the engine come back from the card");
        Io none;
        check(!open(f, none, rb).indexRead, "no index on the card: indexRead is false");
    }

    // ---- boot: no plugin, no index: nothing written. Stale and damaged entries dropped without a flash command.
    {
        Flash f;
        Io none;
        Bank b;
        BankOpenStatus st = open(f, none, b);
        check(none.indexWrites == 0 && !none.haveIndex && b.n == 0 && st.dropped == 0 && f.commands == 0,
              "no synth plugin, no index: nothing written");
        Io io = allPlugins();
        Cache c;
        open(f, io, b);
        check(install(f, io, b, "ju6.plg", c).err == 0 && install(f, io, b, "fm4.plg", c).err == 0 &&
                install(f, io, b, "phz.plg", c).err == 0 && b.n == 3 && exact(f, io, b),
              "three engines installed on demand");
        const int cmd = f.commands, wr = io.indexWrites;
        open(f, io, b);
        check(b.n == 3 && f.commands == cmd && io.indexWrites == wr, "reboot: the cache stays, no flash command, no index write");
        io.card["fm4.plg"] = plg("fm4.plg", 0xea3d5a7a, 8172, 8168, 112, 0x4242);   // a new version on the card
        io.card.erase("phz.plg");                                                    // removed from the card
        BankOpenStatus o = open(f, io, b);
        check(o.stale == 2 && b.n == 1 && b.e[0].uid == 0x73ac7ee0 && f.commands == cmd && io.indexWrites == wr + 1,
              "reboot after the card changed: the old FM4 and the gone PHZ are dropped (index only), JU6 stays");
        f.m[b.e[0].off + 100] ^= 0x10;   // a damaged JU6
        o = open(f, io, b);
        check(o.dropped == 1 && b.n == 0, "a damaged entry is dropped at boot");
        check(insert2Blank(f), "boot: insert 2 sectors untouched");
    }

    // ---- install on demand: sector aligned, nothing shared, an installed engine costs nothing
    {
        Flash f;
        Io io = allPlugins();
        Bank b;
        Cache c;
        open(f, io, b);
        BankInstallStatus a = install(f, io, b, "ju6.plg", c);
        BankInstallStatus m = install(f, io, b, "fm4.plg", c);
        check(a.err == 0 && m.err == 0 && b.n == 2 && exact(f, io, b) && layoutOk(b), "JU6 and FM4: exact, own sectors, own rw");
        check(a.erases == 0 && m.erases == 0, "a blank bank: programs only, no erase");
        const int cmd = f.commands, wr = io.indexWrites;
        BankInstallStatus again = install(f, io, b, "fm4.plg", c);
        check(again.err == 0 && again.at == find(b, 0xea3d5a7a) && f.commands == cmd && io.indexWrites == wr,
              "FM4 again: already there, no command, no index write");
        printf("info  JU6 install: %d commands (%d phrases), FM4: %d commands\n", a.commands, int(align8(6956) / 8), m.commands);
    }

    // ---- the shipped plugins: how many fit, LRU, pinned engines untouched
    {
        uint32_t total = 0, sectors = 0, rwTotal = 0;
        for (const P& p : kAll)
        {
            total += align8(p.len);
            sectors += sectorsOf(p.len);
            rwTotal += align8(p.rw);
        }
        // The rw pool must hold every set of engines the flash can hold at once (at most kBankMax entries in kBankSectors), else the
        // pool would decide evictions and force flash rewrites. All of them together may exceed it: they never are resident together.
        uint32_t worstRw = 0;
        int worstN = 0;
        for (uint32_t m = 1; m < (1u << kN); m++)
        {
            uint32_t sec = 0, rw = 0;
            int n = 0;
            for (int i = 0; i < kN; i++)
                if (m & (1u << i))
                {
                    sec += sectorsOf(kAll[i].len);
                    rw += align8(kAll[i].rw);
                    n++;
                }
            if (n <= kBankMax && sec <= uint32_t(kBankSectors) && rw > worstRw)
            {
                worstRw = rw;
                worstN = n;
            }
        }
        printf("info  %d synth plugins: %u B packed, %u whole sectors of %d, rw %u B all, %u of %u B for the worst set that fits the bank (%d engines)\n",
               kN, total, sectors, kBankSectors, rwTotal, worstRw, kPoolBytes, worstN);
        check(sectors > uint32_t(kBankSectors) && worstRw <= kPoolBytes,
              "all plugins: more sectors than the bank, every set the bank can hold fits the rw pool");
        uint32_t big3 = 0, big3rw = 0;
        {
            std::vector<uint32_t> s, r;
            for (const P& p : kAll)
            {
                s.push_back(sectorsOf(p.len));
                r.push_back(align8(p.rw));
            }
            std::sort(s.rbegin(), s.rend());
            std::sort(r.rbegin(), r.rend());
            big3 = s[0] + s[1] + s[2] + s[3];
            big3rw = r[0] + r[1] + r[2];
            printf("info  worst 3 parts + the engine being picked: %u sectors of %d; worst 3 at once rw %u B, + the one picked %u B\n",
                   big3, kBankSectors, big3rw, big3rw + r[3]);
        }
        check(big3 <= uint32_t(kBankSectors), "the 3 biggest engines on the parts plus the 4th biggest fit the bank together");

        Flash f;
        Io io = allPlugins();
        Bank b;
        Cache c;
        open(f, io, b);
        int failed = 0, firstEvict = -1;
        for (int i = 0; i < kN; i++)
        {
            BankInstallStatus st = install(f, io, b, kAll[i].f, c);
            failed += st.err != 0;
            if (st.evicted && firstEvict < 0) firstEvict = i;
        }
        check(failed == 0 && exact(f, io, b) && layoutOk(b) && insert2Blank(f), "every plugin picked in turn: every install works");
        check(firstEvict > 0 && !c.evicted.empty() && c.evicted[0] == kAll[0].uid, "the bank fills, then the least recently used goes first (JU6)");
        printf("info  %d in card order: %d resident before the first eviction, %d resident at the end, %zu evicted\n", kN, firstEvict, b.n,
               c.evicted.size());

        // three parts pin PHZ, T-Type and 8-Type (three of the biggest), the others are picked again and again
        c.pinned = {0x6670b917, 0x318f12b1, 0x9d7c57e2};
        for (uint32_t u : c.pinned)
            for (const P& p : kAll)
                if (p.uid == u) install(f, io, b, p.f, c);
        std::vector<std::vector<uint8_t>> keep;
        std::vector<uint32_t> keepOff;
        for (uint32_t u : c.pinned)
        {
            const int i = find(b, u);
            keep.emplace_back(f.at(b.e[i].off), f.at(b.e[i].off) + b.e[i].len);
            keepOff.push_back(b.e[i].off);
        }
        c.evicted.clear();
        int bad = 0;
        for (int round = 0; round < 40; round++)
        {
            const P& p = kAll[(round * 5) % kN];
            if (install(f, io, b, p.f, c).err) bad++;
            if (!exact(f, io, b) || !layoutOk(b)) bad++;
        }
        bool pinnedEvicted = false, pinnedChanged = false;
        for (uint32_t u : c.evicted)
            for (uint32_t q : c.pinned) pinnedEvicted = pinnedEvicted || u == q;
        for (size_t k = 0; k < c.pinned.size(); k++)
        {
            const int i = find(b, c.pinned[k]);
            pinnedChanged = pinnedChanged || i < 0 || b.e[i].off != keepOff[k] || memcmp(f.at(keepOff[k]), keep[k].data(), keep[k].size()) != 0;
        }
        check(bad == 0 && !c.evicted.empty(), "40 picks with three engines pinned: all installs work, evictions happen");
        check(!pinnedEvicted && !pinnedChanged, "the pinned engines are never evicted, their bytes never change (never erased)");
        check(insert2Blank(f), "all plugins: insert 2 sectors untouched");
    }

    // ---- an engine that comes back to the sectors it left
    {
        Flash f;
        Io io;
        io.card["a.plg"] = plg("a.plg", 0xA, 40000, 39992, 64);
        io.card["b.plg"] = plg("b.plg", 0xB, 40000, 39992, 64);
        io.card["c.plg"] = plg("c.plg", 0xC, 40000, 39992, 64);
        io.card["d.plg"] = plg("d.plg", 0xD, 4000, 3992, 64);
        Bank b;
        Cache c;
        open(f, io, b);
        install(f, io, b, "a.plg", c);
        install(f, io, b, "b.plg", c);
        install(f, io, b, "c.plg", c);   // 3 x 10 sectors: the bank is full
        const uint32_t aOff = b.e[find(b, 0xA)].off;
        BankInstallStatus d = install(f, io, b, "d.plg", c);   // evicts A (least recent)
        check(d.err == 0 && d.evicted == 1 && find(b, 0xA) < 0, "a small engine evicts the least recently used big one");
        check(b.e[find(b, 0xD)].off == aOff && d.erases == 1, "it goes into A's sectors (erases only the one sector it needs)");
        c.pinned = {0xB, 0xC};
        BankInstallStatus back = install(f, io, b, "a.plg", c);   // D must go, A's other 9 sectors still hold A
        check(back.err == 0 && find(b, 0xD) < 0 && b.e[find(b, 0xA)].off == aOff && exact(f, io, b),
              "A comes back to its own sectors when D goes");
        printf("info  A back in its sectors: %d commands, %d erase (only the sector D used), a full install is %d commands\n",
               back.commands, back.erases, int(align8(40000) / 8));
        check(back.erases == 1 && back.commands <= 1 + int(kBankSector / 8), "only the sector D took is erased and programmed again");
    }

    // ---- engines that do not fit together, and an engine bigger than the bank
    {
        Flash f;
        Io io;
        io.card["a.plg"] = plg("a.plg", 0xA, 40000, 39992, 64);
        io.card["b.plg"] = plg("b.plg", 0xB, 40000, 39992, 64);
        io.card["c.plg"] = plg("c.plg", 0xC, 40000, 39992, 64);
        io.card["d.plg"] = plg("d.plg", 0xD, 5000, 4992, 64);
        io.card["huge.plg"] = plg("huge.plg", 0xE, 130000, 129992, 64);
        io.card["fat.plg"] = plg("fat.plg", 0xF, 4000, 3992, kPoolBytes + 8);
        Bank b;
        Cache c;
        open(f, io, b);
        c.pinned = {0xA, 0xB, 0xC, 0xD};   // a project with A B C on the parts, and D asked for
        install(f, io, b, "a.plg", c);
        install(f, io, b, "b.plg", c);
        install(f, io, b, "c.plg", c);
        const std::string idx = io.index;
        const int cmd = f.commands;
        BankInstallStatus d = install(f, io, b, "d.plg", c);
        check(d.err == kBankNoRoom && b.n == 3 && io.index == idx && f.commands == cmd && exact(f, io, b),
              "the 4th engine does not fit next to 3 pinned ones: kBankNoRoom, nothing changes (Missing: D)");
        check(install(f, io, b, "huge.plg", c).err == kBankTooBig && f.commands == cmd, "an engine bigger than the bank: kBankTooBig");
        check(install(f, io, b, "fat.plg", c).err == kBankTooBig, "rw bigger than the pool: kBankTooBig");
    }

    // ---- the rw pool is a limit too
    {
        Flash f;
        Io io;
        const uint32_t third = kPoolBytes / 2 - 8;   // two of them fit the pool, three do not
        io.card["r1.plg"] = plg("r1.plg", 0x11, 3000, 2000, third);
        io.card["r2.plg"] = plg("r2.plg", 0x12, 3000, 2000, third);
        io.card["r3.plg"] = plg("r3.plg", 0x13, 3000, 2000, third);
        Bank b;
        Cache c;
        open(f, io, b);
        install(f, io, b, "r1.plg", c);
        install(f, io, b, "r2.plg", c);
        BankInstallStatus r = install(f, io, b, "r3.plg", c);
        check(r.err == 0 && r.evicted == 1 && find(b, 0x11) < 0 && layoutOk(b) && exact(f, io, b),
              "3 x 3,832 B rw in a 7,680 B pool: the third evicts the oldest by rw, with sectors to spare");
    }

    // ---- power cut after every command of an install that evicts and erases, and a journal that cannot be written
    {
        Io base = allPlugins();
        Flash f0;
        Bank b0;
        Cache c0;
        open(f0, base, b0);
        for (int i = 0; i < 11; i++) install(f0, base, b0, kAll[i].f, c0);   // a full, used bank
        c0.pinned = {kAll[8].uid, kAll[9].uid, kAll[10].uid};                // three parts
        Flash ref = f0;   // the uncut run of 8-Type (the biggest, evicts and erases)
        Io refIo = base;
        Bank refB = b0;
        Cache refC = c0;
        BankInstallStatus rs = install(ref, refIo, refB, "eight.plg", refC);
        check(rs.err == 0 && rs.evicted > 0 && rs.erases > 0 && exact(ref, refIo, refB), "8-Type into a full bank: evicts, erases, exact");
        printf("info  8-Type into a full bank: %d commands, %d erases, %d evicted\n", rs.commands, rs.erases, rs.evicted);
        int notExact = 0, pinnedLost = 0, notDone = 0, underCounted = 0;
        for (int cut = 0; cut < rs.commands; cut++)
        {
            Flash f = f0;
            f.cutAt = f.commands + cut;
            f.rng = uint32_t(cut * 2654435761u);
            Io io = base;
            Bank b = b0;
            Cache c = c0;
            install(f, io, b, "eight.plg", c);   // dies at command `cut`
            f.dead = false;
            f.cutAt = -1;
            Bank nb;
            open(f, io, nb);   // the next boot
            if (!exact(f, io, nb)) notExact++;
            for (uint32_t u : c0.pinned)
                if (find(nb, u) < 0) pinnedLost++;
            for (int k = 0; k < kBankSectors; k++)
                if (nb.wear[k] < f.erased[k] - f0.erased[k] + b0.wear[k]) underCounted++;
            if (install(f, io, nb, "eight.plg", c).err || !exact(f, io, nb) || find(nb, kAll[11].uid) < 0 || !insert2Blank(f)) notDone++;
        }
        char what[160];
        snprintf(what, sizeof(what), "cut after each of %d commands: nothing inexact survives the next boot", rs.commands);
        check(notExact == 0, what);
        check(pinnedLost == 0, "cut: the pinned engines are still installed and exact");
        check(underCounted == 0, "cut: the erase counters never count fewer erases than the flash did");
        check(notDone == 0, "cut: picking 8-Type again after the boot installs it, exact");
        Flash f = f0;
        Io io = base;
        io.failWrite = true;
        Bank b = b0;
        Cache c = c0;
        c.evicted.clear();
        const int cmd = f.commands;
        BankInstallStatus j = install(f, io, b, "eight.plg", c);
        check(j.err == kBankJournal && f.commands == cmd && b.n == b0.n && c.evicted.empty(),
              "the journal cannot be written: no flash command, nothing evicted");
    }

    // ---- wear: counters follow the flash, installs spread, worst case per pick
    {
        Flash f;
        Io io = allPlugins();
        Bank b;
        Cache c;
        open(f, io, b);
        c.pinned = {kAll[3].uid, kAll[7].uid, kAll[9].uid};   // PHZ, T-Type, Arc-Type on the parts
        for (uint32_t u : c.pinned)
            for (const P& p : kAll)
                if (p.uid == u) install(f, io, b, p.f, c);
        // the worst user: a cycle through the 9 other engines, so with LRU every pick misses the cache once the bank is full
        const int picks = 900;
        int misses = 0, erases = 0;
        for (int i = 0; i < picks; i++)
        {
            static const int kCycle[9] = {0, 1, 2, 4, 5, 6, 8, 10, 11};
            BankInstallStatus st = install(f, io, b, kAll[kCycle[i % 9]].f, c);
            misses += st.commands > 0;
            erases += st.erases;
        }
        bool same = true;
        uint32_t maxW = 0, minW = ~0u;
        int used = 0;
        for (int k = 0; k < kBankSectors; k++)
        {
            same = same && b.wear[k] == f.erased[k];
            if (f.erased[k])
            {
                used++;
                maxW = f.erased[k] > maxW ? f.erased[k] : maxW;
                minW = f.erased[k] < minW ? f.erased[k] : minW;
            }
        }
        check(same, "the c counters equal the erases the flash did, sector by sector");
        check(exact(f, io, b) && insert2Blank(f), "after 900 picks: exact, insert 2 untouched");
        const double perPick = double(maxW) / picks;
        printf("info  wear, 900 picks cycling 9 engines past 3 pinned: %d misses, %d erases, %d sectors erased, per sector min %u max %u "
               "(%.3f erases of the worst sector per pick)\n",
               misses, erases, used, minW, maxW, perPick);
        printf("info  wear: 10,000 cycles / %.3f = %.0f such picks until the first sector reaches 10,000, %.1f picks a day for 10 years\n",
               perPick, 10000.0 / perPick, 10000.0 / perPick / 3652.0);
        const double avg = double(erases) / used;
        printf("info  wear: %.1f erases per erased sector on average, the worst %u (%.2f x the average)\n", avg, maxW, maxW / avg);
        check(maxW <= 1.25 * avg, "installs spread over the free sectors (the most erased within 1.25 x the average)");
    }

    // ---- poly-plug-lib: a library of 40 and 100 engines (the 14 sizes in turn, new uids and files) is a cache like the 14: the boot keeps the
    // entries of files still on the card, 3 parts pin their engines, the picker walks every engine of the library twice and loads each, and
    // also a project whose 3 engines are anywhere in the library. The bank never holds more than it can, every install succeeds, installed.txt
    // stays within the firmware's 2,048 B (plug_synth.cpp s_itext), nothing grows with the library
    for (int nLib : {40, 100})
    {
        Io io;
        std::vector<std::string> files;
        for (int i = 0; i < nLib; i++)
        {
            const P& q = kAll[i % kN];
            char fn[kBankFileLen];
            snprintf(fn, sizeof(fn), "lib%03d_%s", i, q.f);   // up to 23 characters (kBankFileLen 24)
            io.card[fn] = plg(fn, 0xA0000000u + uint32_t(i) * 0x9E37u, q.len, q.text, q.rw);
            files.push_back(fn);
        }
        Flash f;
        Bank b = {};
        open(f, io, b);
        Cache c;
        const int pins[3] = {1, nLib / 2, nLib - 1};
        bool ok = true;
        for (int p : pins)
        {
            ok = ok && install(f, io, b, files[size_t(p)], c).err == kBankOk;
            c.pinned.push_back(io.card[files[size_t(p)]].uid);
        }
        int loads = 0, misses = 0, maxN = 0, maxIndex = 0;
        for (int round = 0; round < 2; round++)
            for (int i = 0; i < nLib; i++)
            {
                const int cmd = f.commands;
                const BankInstallStatus st = install(f, io, b, files[size_t(i)], c);
                ok = ok && st.err == kBankOk && find(b, io.card[files[size_t(i)]].uid) >= 0;
                loads++;
                misses += f.commands != cmd;
                maxN = b.n > maxN ? b.n : maxN;
                maxIndex = int(io.index.size()) > maxIndex ? int(io.index.size()) : maxIndex;
            }
        bool pinnedThere = true;
        for (uint32_t u : c.pinned) pinnedThere = pinnedThere && find(b, u) >= 0;
        // a project load: three engines from anywhere in the library, the old ones no longer pinned
        c.pinned.clear();
        const int proj[3] = {3, nLib / 3, nLib - 2};
        for (int p : proj)
        {
            ok = ok && install(f, io, b, files[size_t(p)], c).err == kBankOk;
            c.pinned.push_back(io.card[files[size_t(p)]].uid);
        }
        for (int p : proj) pinnedThere = pinnedThere && find(b, io.card[files[size_t(p)]].uid) >= 0;
        // reboot with the library: the cache is kept as it is (every entry's file is on the card)
        Bank r = {};
        const int cmd = f.commands;
        const BankOpenStatus os = open(f, io, r);
        char what[220];
        snprintf(what, sizeof(what), "%d engine library: %d loads all succeed, the pinned engines stay, at most %d installed at once (kBankMax %d), "
                 "installed.txt at most %d B (2,048)", nLib, loads + 6, maxN, kBankMax, maxIndex);
        check(ok && pinnedThere && maxN <= kBankMax && maxIndex <= 2048 && exact(f, io, b) && layoutOk(b) && insert2Blank(f), what);
        snprintf(what, sizeof(what), "%d engine library: the reboot keeps the %d installed engines, no flash command, nothing stale", nLib, b.n);
        check(r.n == b.n && os.stale == 0 && os.dropped == 0 && f.commands == cmd, what);
        printf("info  %d engine library: %d picker loads, %d needed the flash (the rest were cached)\n", nLib, loads, misses);
    }

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "PASSED", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
