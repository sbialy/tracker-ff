// The FlexNVM bank of the synth engine plugins (FEATURE_PLUG_HOST): layout, /Plugins/installed.txt and the sector
// writer. Header only and free of the platform: synth-open/tools/plug_bank_test.cpp runs the same code on the Mac against a
// simulated flash that loses power after any command. plug_synth.cpp is the only firmware user.
//
// The bank is [0x10000000, 0x1001E000) of the data flash (120 KB, 30 sectors of 4 KB). The last two sectors belong to master
// insert 2 (plug_nvm.cpp) and are never touched here. An installed engine is one entry: its text (rebased for its flash
// address) followed by its rw segment's initial bytes (rebased for its rw address in the host's SRAM pool).
//
// Cache: /Plugins/Synth on the card is the library, the bank is a cache of it. Nothing is installed at boot:
// bankOpen only checks the index and drops what must not run. bankInstall puts one engine in when a part needs it. A new engine
// starts on a sector boundary and takes whole sectors that hold no byte of another live entry, so an install never erases a
// sector of an engine that may be playing, and the engines in the bank never move. When there is no room (sectors or the rw
// pool), the entries the caller ranks lowest are dropped from the index first (least recently used, never one a part needs).
// Dropping costs no flash command: the sectors are erased by the install that reuses them, and an engine that comes back to the
// sectors it left finds its bytes there and costs no command at all.
//
// Power cut rules:
//   - installed.txt is the index. An entry runs only when its state is 'o' AND the CRC-32 of its bytes in the flash equals the
//     CRC the index holds (bankCheck). Anything else is dropped at boot and never runs.
//   - before any flash command the index is rewritten without the evicted entries and with the new one in state 'w' (the
//     journal). A cut during the write leaves a 'w', dropped at the next boot, the engine is installed again when a part needs
//     it. The other entries' sectors are never erased or programmed by an install.
//   - a flash command is never issued when the bytes are already there.
//   - the index carries one erase counter per sector ("c" line, wear): the erases an install will do are counted in the
//     journal, before the first command, so a cut can only count one too many, never one too few.
#pragma once

#include "plug_index.h"

#include <stdint.h>
#include <string.h>

namespace plug
{

constexpr uintptr_t kBankRun = 0x10000000u;   // data flash (FlexNVM) base, executable
constexpr uint32_t kBankBytes = 0x1E000u;     // up to insert 2's two sectors at 0x1001E000
constexpr uint32_t kBankSector = 4096;
constexpr int kBankSectors = int(kBankBytes / kBankSector);   // 30
constexpr int kBankMax = 12;                  // installed engines at once (the 12 shipped ones need 38 whole sectors, 30 exist)
constexpr int kBankFileLen = 24;

struct BankEntry
{
    uint32_t uid, version, fileCrc;   // the .plg it came from: header uid, version and crc32 (identity of the file)
    uint32_t off, len, text;          // offset in the bank, bytes in the flash (text + rw initial bytes), text bytes
    uint32_t rwRun, rwLen;            // rw address the image was rebased for, rw + bss bytes it needs there
    uint32_t entry;                   // plug_entry offset in the text
    uint32_t crc;                     // CRC-32 of the len bytes in the flash
    char state;                       // 'o' written and verified, 'w' being written (never runs)
    char file[kBankFileLen];          // name in /Plugins/Synth
};

struct Bank
{
    int n;
    BankEntry e[kBankMax];
    uint32_t wear[kBankSectors];   // erases of each sector since the counters began (the "c" line of installed.txt)
};

// ------------------------------------------------------------------------------------------------ installed.txt
// "tffbank 1", "c" and the 30 erase counters (hex), then one line per entry: state uid version fileCrc off len text rwRun rwLen
// entry crc file (hex, 8 digits). Older hosts drop the "c" line (its state is not 'o' or 'w') and lose the counters.
inline int bankWrite(const Bank& b, char* t, int cap)
{
    int n = sc::putStr(t, 0, cap, "tffbank 1\r\nc");
    for (int k = 0; k < kBankSectors; k++)
    {
        if (n < cap) t[n++] = ' ';
        n = sc::putHex(t, n, cap, b.wear[k]);
    }
    n = sc::putStr(t, n, cap, "\r\n");
    for (int i = 0; i < b.n; i++)
    {
        const BankEntry& e = b.e[i];
        const uint32_t v[11] = {e.uid, e.version, e.fileCrc, e.off, e.len, e.text, e.rwRun, e.rwLen, e.entry, e.crc, 0};
        if (n < cap) t[n++] = e.state;
        for (int k = 0; k < 10; k++)
        {
            if (n < cap) t[n++] = ' ';
            n = sc::putHex(t, n, cap, v[k]);
        }
        if (n < cap) t[n++] = ' ';
        n = sc::putStr(t, n, cap, e.file);
        n = sc::putStr(t, n, cap, "\r\n");
    }
    return n < cap ? n : -1;   // -1: did not fit
}

// Lines that do not parse are dropped (their engine is not run, the next install brings it back)
inline void bankRead(const char* t, int len, Bank& b)
{
    b.n = 0;
    memset(b.wear, 0, sizeof(b.wear));
    const char* p = t;
    const char* e = t + (len > 0 ? len : 0);
    if (len < 9 || memcmp(t, "tffbank 1", 9) != 0) return;
    while (p < e && b.n < kBankMax)
    {
        const char* eol = p;
        while (eol < e && *eol != '\n') eol++;
        const char* q = p;
        if (*q == 'c')
        {   // the erase counters: all 30 or none
            uint32_t w[kBankSectors];
            bool ok = true;
            q++;
            for (int k = 0; k < kBankSectors && ok; k++) ok = q < eol && *q++ == ' ' && sc::getHex(q, eol, w[k]);
            if (ok) memcpy(b.wear, w, sizeof(w));
            p = eol + 1;
            continue;
        }
        BankEntry& x = b.e[b.n];
        memset(&x, 0, sizeof(x));
        x.state = *q++;
        uint32_t* v[10] = {&x.uid, &x.version, &x.fileCrc, &x.off, &x.len, &x.text, &x.rwRun, &x.rwLen, &x.entry, &x.crc};
        bool ok = x.state == 'o' || x.state == 'w';
        for (int k = 0; k < 10 && ok; k++) ok = q < eol && *q++ == ' ' && sc::getHex(q, eol, *v[k]);
        if (ok && q < eol && *q == ' ')
        {
            q++;
            int f = 0;
            while (q < eol && *q != '\r' && f < kBankFileLen - 1) x.file[f++] = *q++;
            if (f) b.n++;
        }
        p = eol + 1;
    }
}

// ------------------------------------------------------------------------------------------------ layout
inline bool overlaps(uint32_t a, uint32_t al, uint32_t b, uint32_t bl)
{
    return a < b + bl && b < a + al;
}

// Drops every entry that must not run: not 'o', outside the bank or the pool, overlapping an earlier one, or (Flash) bytes
// that do not match the CRC. Returns how many were dropped. flash(off) is a read pointer into the bank.
template <class Flash>
int bankCheck(Bank& b, Flash& f, uintptr_t pool, uint32_t poolBytes)
{
    int kept = 0, dropped = 0;
    for (int i = 0; i < b.n; i++)
    {
        const BankEntry& x = b.e[i];
        bool ok = x.state == 'o' && !(x.off & 7u) && x.len && x.len <= kBankBytes && x.off <= kBankBytes - x.len &&
                  x.text <= x.len && x.entry < x.text && !(x.rwRun & 7u) && x.rwRun >= pool && x.rwLen >= x.len - x.text &&
                  x.rwLen <= poolBytes && x.rwRun - pool <= poolBytes - x.rwLen;
        for (int k = 0; k < kept && ok; k++)
            ok = !overlaps(x.off, x.len, b.e[k].off, b.e[k].len) && !overlaps(x.rwRun, x.rwLen, b.e[k].rwRun, b.e[k].rwLen);
        if (ok) ok = crc32(0, f.at(x.off), x.len) == x.crc;
        if (ok)
            b.e[kept++] = x;
        else
            dropped++;
    }
    b.n = kept;
    return dropped;
}

struct BankWant   // a synth .plg on the card (scanIndex): what it needs
{
    uint32_t uid, version, fileCrc;
    uint32_t len, text, rwLen, entry;   // image bytes, rw_offset, rw + bss, entry_offset
    char file[kBankFileLen];
};

inline uint32_t align8(uint32_t v)
{
    return (v + 7u) & ~7u;
}

// Sectors [first, first + count) of an entry
inline int sectorFirst(const BankEntry& x)
{
    return int(x.off / kBankSector);
}
inline int sectorEnd(const BankEntry& x)
{
    return int((x.off + x.len + kBankSector - 1) / kBankSector);
}

// A run of whole sectors for len bytes that touches no sector of an entry of b. Of the runs that fit, the one whose most erased
// sector has the fewest erases (b.wear), the lowest one of those: installs spread over the free sectors instead of wearing out the
// first free run. False when there is none.
inline bool bankFindFlash(const Bank& b, uint32_t len, uint32_t* off)
{
    const int need = int((len + kBankSector - 1) / kBankSector);
    bool used[kBankSectors] = {};
    for (int i = 0; i < b.n; i++)
        for (int s = sectorFirst(b.e[i]); s < sectorEnd(b.e[i]) && s < kBankSectors; s++) used[s] = true;
    int best = -1;
    uint32_t bestWear = 0;
    for (int s = 0; s + need <= kBankSectors; s++)
    {
        bool free = true;
        uint32_t w = 0;
        for (int k = s; k < s + need && free; k++)
        {
            free = !used[k];
            w = b.wear[k] > w ? b.wear[k] : w;
        }
        if (free && (best < 0 || w < bestWear))
        {
            best = s;
            bestWear = w;
        }
    }
    if (best < 0) return false;
    *off = uint32_t(best) * kBankSector;
    return true;
}

// rw bytes in the pool that overlap no entry of b (first fit from the pool start). False when there is no gap.
inline bool bankFindRw(const Bank& b, uintptr_t pool, uint32_t poolBytes, uint32_t rl, uint32_t* rw)
{
    uint32_t at = uint32_t(pool);
    for (;;)
    {
        if (rl > uint32_t(pool) + poolBytes - at) return false;
        bool moved = false;
        for (int i = 0; i < b.n; i++)
            if (overlaps(at, rl, b.e[i].rwRun, b.e[i].rwLen))
            {
                at = align8(b.e[i].rwRun + b.e[i].rwLen);
                moved = true;
            }
        if (!moved)
        {
            *rw = at;
            return true;
        }
    }
}

// ------------------------------------------------------------------------------------------------ flash writer
// Flash: const uint8_t* at(uint32_t off), bool erase(uint32_t sectorOff), bool program(uint32_t off, const uint8_t* data8).
// Writes src[0..len) at bank offset off (8 aligned, len a multiple of 8). live: the entries whose bytes must survive (the one
// being written may be among them). sec: kBankSector bytes of RAM. True when the flash reads back src.
template <class Flash>
bool bankProgram(Flash& f, uint32_t off, const uint8_t* src, uint32_t len, const Bank& live, uint8_t* sec)
{
    for (uint32_t s = off & ~(kBankSector - 1); s < off + len; s += kBankSector)
    {
        const uint32_t lo = s > off ? s : off;
        const uint32_t hi = s + kBankSector < off + len ? s + kBankSector : off + len;
        // per phrase: equal (nothing), blank and different (program), programmed and different (the sector needs an erase)
        bool erase = false, any = false;
        for (uint32_t i = lo; i < hi && !erase; i += 8)
            if (memcmp(f.at(i), src + (i - off), 8) != 0)
            {
                any = true;
                for (int k = 0; k < 8 && !erase; k++) erase = f.at(i)[k] != 0xFF;
            }
        if (!any) continue;   // already there: no command
        if (!erase)
        {
            for (uint32_t i = lo; i < hi; i += 8)
                if (memcmp(f.at(i), src + (i - off), 8) != 0 && !f.program(i, src + (i - off))) return false;
            continue;
        }
        // erase: keep the bytes of the other written ('o') entries in this sector, drop dead ones, put back what is not blank
        memset(sec, 0xFF, kBankSector);
        for (int k = 0; k < live.n; k++)
        {
            const BankEntry& x = live.e[k];
            const uint32_t a = x.off > s ? x.off : s, z = x.off + x.len < s + kBankSector ? x.off + x.len : s + kBankSector;
            if (x.state == 'o' && a < z && !overlaps(x.off, x.len, off, len)) memcpy(sec + (a - s), f.at(a), z - a);
        }
        memcpy(sec + (lo - s), src + (lo - off), hi - lo);
        if (!f.erase(s)) return false;
        for (uint32_t i = 0; i < kBankSector; i += 8)
        {
            bool ff = true;
            for (int k = 0; k < 8 && ff; k++) ff = sec[i + k] == 0xFF;
            if (!ff && !f.program(s + i, sec + i)) return false;
        }
    }
    return memcmp(f.at(off), src, len) == 0;
}


// ------------------------------------------------------------------------------------------------ boot: open the cache
struct BankOpenStatus
{
    int dropped;   // index entries that must not run (half written, damaged, outside the bank): dropped
    int stale;     // entries whose .plg left the card or changed (another version): dropped, installed again on demand
};

// Io (both steps):
//   int readIndex(char* buf, int cap)            installed.txt bytes, -1 when there is none
//   bool writeIndex(const char* t, int n)        replace installed.txt (temp file and rename: all or nothing)
//   const uint8_t* image(const BankEntry& e)     the .plg named by e (e.file, checked against e.uid and e.fileCrc) loaded
//                                                and rebased for kBankRun + e.off and e.rwRun, e.len bytes (padding 0),
//                                                nullptr when it does not load
// Boot: read and check the index, keep the entries whose file is still on the card (w: the library), write the index back
// only when something was dropped. Never a flash command. text: cap bytes of scratch.
template <class Flash, class Io>
BankOpenStatus bankOpen(Flash& f, Io& io, const BankWant* w, int nw, uintptr_t pool, uint32_t poolBytes, Bank& out, char* text,
                        int cap)
{
    BankOpenStatus st = {};
    const int got = io.readIndex(text, cap);
    bankRead(text, got, out);
    st.dropped = bankCheck(out, f, pool, poolBytes);
    int k = 0;
    for (int i = 0; i < out.n; i++)
    {
        bool want = false;
        for (int j = 0; j < nw && !want; j++) want = w[j].uid == out.e[i].uid && w[j].fileCrc == out.e[i].fileCrc;
        if (want)
            out.e[k++] = out.e[i];
        else
            st.stale++;
    }
    out.n = k;
    if (got >= 0 && (st.dropped || st.stale))
    {
        const int n = bankWrite(out, text, cap);
        if (n >= 0) io.writeIndex(text, n);
    }
    return st;
}

// ------------------------------------------------------------------------------------------------ install one engine
enum BankErr
{
    kBankOk = 0,
    kBankTooBig = 1,    // more than the whole bank or the whole rw pool, or the index is full of engines a part needs
    kBankNoRoom = 2,    // the engines that must stay leave no room
    kBankNoFile = 3,    // the .plg did not load (gone, changed, unreadable)
    kBankJournal = 4,   // installed.txt could not be written: no flash command was issued
    kBankFlash = 5,     // erase, program or verify failed (the entry is dropped, its sectors are free again)
};

struct BankInstallStatus
{
    int err;          // BankErr
    int at;           // index of the engine in the bank (when err == 0)
    int evicted;      // entries dropped to make room
    int commands;     // flash commands of this install (0: the bytes were already there)
    int erases;       // sector erases of this install
};

// Puts engine w into the bank b unless it is there. rank(const BankEntry&) says what may go to make room: < 0 never (a part
// needs it), else the lowest rank goes first (the caller ranks by last use, and the engines of the parts above all others).
// onEvict(const BankEntry&) runs for every dropped entry after the journal is on the card and before the first flash command
// (the firmware detaches the engine there). sec: kBankSector bytes, text: cap bytes of scratch, work: two Banks of scratch
// (the plan and the journal, about 1 KB each, kept off the stack: the firmware's main loop has 2 KB to spare under the audio
// interrupt), none of them may alias b.
template <class Flash, class Io, class Rank, class OnEvict>
BankInstallStatus bankInstall(Flash& f, Io& io, const BankWant& w, Rank rank, OnEvict onEvict, uintptr_t pool, uint32_t poolBytes,
                              Bank& b, uint8_t* sec, char* text, int cap, Bank (&work)[2])
{
    BankInstallStatus st = {};
    st.at = -1;
    for (int i = 0; i < b.n; i++)
        if (b.e[i].uid == w.uid && b.e[i].fileCrc == w.fileCrc && b.e[i].state == 'o')
        {
            st.at = i;
            return st;
        }
    const uint32_t len = align8(w.len), rl = align8(w.rwLen);
    if (!len || len > kBankBytes || rl > poolBytes)
    {
        st.err = kBankTooBig;
        return st;
    }
    // the plan, on a copy: drop the lowest ranked entries until the engine has whole free sectors and an rw gap
    Bank& plan = work[0];
    plan = b;
    bool gone[kBankMax] = {};
    uint32_t off = 0, rw = 0;
    for (;;)
    {
        if (plan.n < kBankMax && bankFindFlash(plan, len, &off) && bankFindRw(plan, pool, poolBytes, rl, &rw)) break;
        int victim = -1;
        long best = 0;
        for (int i = 0; i < plan.n; i++)
        {
            const long r = long(rank(plan.e[i]));
            if (r >= 0 && (victim < 0 || r < best))
            {
                victim = i;
                best = r;
            }
        }
        if (victim < 0)
        {
            st.err = kBankNoRoom;
            return st;
        }
        for (int i = 0; i < b.n; i++)
            if (b.e[i].uid == plan.e[victim].uid && b.e[i].off == plan.e[victim].off) gone[i] = true;
        plan.e[victim] = plan.e[--plan.n];
    }
    // the new entry, rebased for its place
    BankEntry e = {w.uid, w.version, w.fileCrc, off, len, w.text, rw, rl, w.entry, 0, 'w', {}};
    memcpy(e.file, w.file, kBankFileLen);
    const uint8_t* img = io.image(e);
    if (!img)
    {
        st.err = kBankNoFile;
        return st;
    }
    // the journal: the evicted entries gone, the new one 'w', the erases about to happen counted, in the index before any command
    Bank& j = work[1];
    j.n = 0;
    memcpy(j.wear, b.wear, sizeof(j.wear));
    for (int i = 0; i < b.n; i++)
        if (!gone[i]) j.e[j.n++] = b.e[i];
    for (uint32_t s = off; s < off + len; s += kBankSector)
    {
        const uint32_t hi = s + kBankSector < off + len ? s + kBankSector : off + len;
        bool erase = false;   // as bankProgram decides it: a phrase that differs and is not blank
        for (uint32_t i = s; i < hi && !erase; i += 8)
            if (memcmp(f.at(i), img + (i - off), 8) != 0)
                for (int k = 0; k < 8 && !erase; k++) erase = f.at(i)[k] != 0xFF;
        if (erase)
        {
            j.wear[s / kBankSector]++;
            st.erases++;
        }
    }
    const int at = j.n;
    j.e[j.n++] = e;
    int n = bankWrite(j, text, cap);
    if (n < 0 || !io.writeIndex(text, n))
    {
        st.err = kBankJournal;
        st.erases = 0;
        return st;
    }
    for (int i = 0; i < b.n; i++)
        if (gone[i])
        {
            onEvict(b.e[i]);
            st.evicted++;
        }
    const int c0 = f.commands;
    const bool ok = bankProgram(f, off, img, len, j, sec);
    st.commands = f.commands - c0;
    if (ok)
    {
        j.e[at].crc = crc32(0, f.at(off), len);
        j.e[at].state = 'o';
        st.at = at;
    }
    else
    {
        j.n--;   // its sectors are free again, the next install there erases them
        st.err = kBankFlash;
    }
    b = j;
    n = bankWrite(b, text, cap);
    if (n >= 0) io.writeIndex(text, n);   // a cut here leaves the 'w' of the journal: dropped at the next boot, nothing runs
    return st;
}

// Drops entry i from the bank and the index (no flash command)
template <class Io>
bool bankForget(Io& io, Bank& b, int i, char* text, int cap)
{
    if (i < 0 || i >= b.n) return false;
    for (int k = i; k + 1 < b.n; k++) b.e[k] = b.e[k + 1];
    b.n--;
    const int n = bankWrite(b, text, cap);
    return n >= 0 && io.writeIndex(text, n);
}

}   // namespace plug
