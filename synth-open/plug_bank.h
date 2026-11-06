// The FlexNVM bank of the synth engine plugins (FEATURE_PLUG_HOST, phase 3): layout, /Plugins/installed.txt and the sector
// writer. Header only and free of the platform: synth-open/tools/plug_bank_test.cpp runs the same code on the Mac against a
// simulated flash that loses power after any command. plug_synth.cpp is the only firmware user.
//
// The bank is [0x10000000, 0x1001E000) of the data flash (120 KB, 30 sectors of 4 KB). The last two sectors belong to master
// insert 2 (plug_nvm.cpp) and are never touched here. An installed engine is one entry: its text (rebased for its flash
// address) followed by its rw segment's initial bytes (rebased for its rw address in the host's SRAM pool), 8 aligned and
// packed back to back across sector boundaries, so a sector may hold the end of one engine and the start of the next.
//
// Power cut rules:
//   - installed.txt is the index. An entry runs only when its state is 'o' AND the CRC-32 of its bytes in the flash equals the
//     CRC the index holds (bankCheck). Anything else is dropped at boot and never runs.
//   - before any flash command the index is rewritten with every entry about to be written in state 'w'. A cut during the
//     write leaves a 'w' (dropped) or, when a shared sector was erased, a neighbour with a CRC mismatch (dropped). Both come
//     back from their .plg on the card at that boot (bankPlan installs what /Plugins/Synth has and the bank does not).
//   - an erase only happens when a sector holds bytes that differ and are not blank. The bytes of the other live entries in that
//     sector are copied to RAM first and programmed back. A flash command is never issued when the bytes are already there.
#pragma once

#include "plug_index.h"

#include <stdint.h>
#include <string.h>

namespace plug
{

constexpr uintptr_t kBankRun = 0x10000000u;   // data flash (FlexNVM) base, executable
constexpr uint32_t kBankBytes = 0x1E000u;     // up to insert 2's two sectors at 0x1001E000
constexpr uint32_t kBankSector = 4096;
constexpr int kBankMax = 12;                  // installed engines (open_cold.cpp kPlugMax)
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
};

// ------------------------------------------------------------------------------------------------ installed.txt
// "tffbank 1" then one line per entry: state uid version fileCrc off len text rwRun rwLen entry crc file (hex, 8 digits)
inline int bankWrite(const Bank& b, char* t, int cap)
{
    int n = sc::putStr(t, 0, cap, "tffbank 1\r\n");
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
    const char* p = t;
    const char* e = t + (len > 0 ? len : 0);
    if (len < 9 || memcmp(t, "tffbank 1", 9) != 0) return;
    while (p < e && b.n < kBankMax)
    {
        const char* eol = p;
        while (eol < e && *eol != '\n') eol++;
        const char* q = p;
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

// The bank after this boot: the wanted engines back to back from the start of the bank and of the rw pool, the ones already
// installed first in their old order, new ones after them (adding an engine moves nothing). An engine whose place and file
// did not change keeps its bytes ('o'), every other one is to be written ('w'). Engines that do not fit are left out.
inline void bankPlan(const Bank& cur, const BankWant* w, int nw, uintptr_t pool, uint32_t poolBytes, Bank& out, int* skipped)
{
    *skipped = 0;
    out.n = 0;
    uint32_t off = 0, rw = uint32_t(pool);
    for (int r = 0; r <= cur.n; r++)   // round r < cur.n: the want installed as cur.e[r], last round: the new ones
        for (int i = 0; i < nw; i++)
        {
            const BankWant& x = w[i];
            int at = cur.n;
            for (int k = 0; k < cur.n; k++)
                if (cur.e[k].uid == x.uid && cur.e[k].fileCrc == x.fileCrc) at = k;
            if (at != r) continue;
            const uint32_t len = (x.len + 7u) & ~7u, rl = (x.rwLen + 7u) & ~7u;
            if (out.n == kBankMax || len > kBankBytes - off || rl > poolBytes - (rw - uint32_t(pool)))
            {
                (*skipped)++;
                continue;
            }
            BankEntry& e = out.e[out.n++];
            if (at < cur.n && cur.e[at].off == off && cur.e[at].rwRun == rw)
                e = cur.e[at];   // same file, same place: same bytes
            else
                e = {x.uid, x.version, x.fileCrc, off, len, x.text, rw, rl, x.entry, 0, 'w', {}};
            memcpy(e.file, x.file, kBankFileLen);
            off += len;
            rw += rl;
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

// ------------------------------------------------------------------------------------------------ boot sync
struct BankStatus
{
    int dropped;     // index entries that must not run (half written, damaged, outside the bank): dropped at this boot
    int installed;   // engines written at this boot
    int failed;      // could not be loaded or written (left out, the next boot tries again)
    int skipped;     // no room in the bank or the rw pool
    int commands;    // flash commands of this boot (0 when every engine was already in place)
};

// The whole boot step: read and check the index, plan, journal, write, verify, write the index back. Io:
//   int readIndex(char* buf, int cap)            installed.txt bytes, -1 when there is none
//   bool writeIndex(const char* t, int n)        replace installed.txt (temp file and rename: all or nothing)
//   const uint8_t* image(const BankEntry& e)     the .plg named by e (e.file, checked against e.uid and e.fileCrc) loaded
//                                                and rebased for kBankRun + e.off and e.rwRun, e.len bytes (padding 0xFF),
//                                                nullptr when it does not load
// Flash also counts its commands (int commands). out: the bank as it is now ('o' entries only), in index order.
template <class Flash, class Io>
BankStatus bankSync(Flash& f, Io& io, const BankWant* w, int nw, uintptr_t pool, uint32_t poolBytes, Bank& out, uint8_t* sec,
                    char* text, int cap)
{
    BankStatus st = {};
    Bank cur;
    const int half = cap / 2;   // text: the index as read, then the index as written
    const int got = io.readIndex(text, half);
    bankRead(text, got, cur);
    st.dropped = bankCheck(cur, f, pool, poolBytes);
    bankPlan(cur, w, nw, pool, poolBytes, out, &st.skipped);
    const int c0 = f.commands;
    int n = bankWrite(out, text + half, cap - half);
    bool pend = false;
    for (int i = 0; i < out.n; i++) pend = pend || out.e[i].state == 'w';
    // the journal: every entry about to be written is 'w' in the index before the first flash command. No journal, no command.
    if (pend && (n < 0 || !io.writeIndex(text + half, n)))
    {
        int k = 0;
        for (int i = 0; i < out.n; i++)
            if (out.e[i].state == 'o') out.e[k++] = out.e[i];
        st.failed = out.n - k;
        out.n = k;
        return st;
    }
    for (int i = 0; i < out.n; i++)
    {
        BankEntry& e = out.e[i];
        if (e.state != 'w') continue;
        const uint8_t* img = io.image(e);
        if (img && bankProgram(f, e.off, img, e.len, out, sec))
        {
            e.crc = crc32(0, f.at(e.off), e.len);
            e.state = 'o';
            st.installed++;
        }
        else
        {
            e.state = 'x';
            st.failed++;
        }
    }
    int k = 0;
    for (int i = 0; i < out.n; i++)
        if (out.e[i].state == 'o') out.e[k++] = out.e[i];
    out.n = k;
    st.commands = f.commands - c0;
    n = bankWrite(out, text + half, cap - half);
    // the index as it is now. Unchanged since the read (the usual boot) or never needed (no file, no engine): no SD write.
    if (n >= 0 && (got >= 0 || out.n) && (pend || st.dropped || n != got || memcmp(text, text + half, size_t(n)) != 0))
        io.writeIndex(text + half, n);
    return st;
}

}   // namespace plug
