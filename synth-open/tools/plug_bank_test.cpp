// Host test of the synth engine bank (synth-open/plug_bank.h): the code the firmware runs at boot to install /Plugins/Synth
// into the FlexNVM data flash, against a simulated flash that can lose power after any command.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_bank_test.cpp -o /tmp/plug_bank_test && /tmp/plug_bank_test
//
// Checked:
//   - installed.txt round trip, damaged lines dropped
//   - a fresh install of JU6 and FM4 sized engines, then a reboot that issues no flash command and no index write
//   - packing: engines share sectors (no whole sector per engine)
//   - the 12 synth plugins at their build_plugs.sh sizes: together 139,768 B, more than the bank, so the one that comes last
//     is skipped (never half written); the 11 without 8-Type fit, and all 12 fit the SRAM pool
//   - the shipped JU6, FM4 and B-Type plugins fit the bank and the SRAM pool together, bytes left printed
//   - replacing the engine in the middle (new version) erases the shared sectors and keeps the neighbours intact, the ones
//     behind it move and are rewritten, a new engine goes after the installed ones (nothing else moves)
//   - removing engines and adding a bigger one lays the bank out again, every engine stays valid
//   - power cut after EVERY flash command of an install and of a replace: the next boot never keeps an entry whose bytes
//     differ from its image (it is dropped), and installs everything again; the bank then equals an uncut run
//   - insert 2's sectors (0x1E000..) are never erased or programmed
#include "plug_bank.h"

#include <cstdio>
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
constexpr uint32_t kPoolBytes = 3072;   // plug_synth.cpp

struct Flash
{
    uint8_t m[0x20000];
    int commands = 0;
    int cutAt = -1;   // the command with this number (0 based) is cut half way, every later one fails
    bool dead = false;
    uint32_t rng = 1;
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
                for (uint32_t i = 0; i < kBankSector / 2; i++) m[s + i] = 0xFF;   // half erased
            return false;
        }
        memset(m + s, 0xFF, kBankSector);
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
// address), so a moved engine really must be rewritten.
struct Plg
{
    uint32_t uid, version, fileCrc, len, text, rwLen;
    std::string file;
};

std::vector<uint8_t> imageOf(const Plg& p, uint32_t off, uint32_t rwRun, uint32_t padLen)
{
    std::vector<uint8_t> v(padLen, 0xFF);
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
        if (f->dead) return false;   // the power is gone: the old file stays
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

std::vector<BankWant> wants(const Io& io, const std::vector<std::string>& order)
{
    std::vector<BankWant> w;
    for (const auto& name : order)
    {
        const Plg& p = io.card.at(name);
        BankWant x = {};
        x.uid = p.uid;
        x.version = p.version;
        x.fileCrc = p.fileCrc;
        x.len = p.len;
        x.text = p.text;
        x.rwLen = p.rwLen;
        x.entry = 0;
        snprintf(x.file, sizeof(x.file), "%s", p.file.c_str());
        w.push_back(x);
    }
    return w;
}

BankStatus boot(Flash& f, Io& io, const std::vector<std::string>& order, Bank& out)
{
    io.f = &f;
    static uint8_t sec[kBankSector];
    static char text[4096];
    auto w = wants(io, order);
    return bankSync(f, io, w.data(), int(w.size()), kPool, kPoolBytes, out, sec, text, int(sizeof(text)));
}

// Every entry of the bank holds exactly the image of its file for its place (what "never run a half-written entry" needs)
bool exact(Flash& f, const Io& io, const Bank& b)
{
    for (int i = 0; i < b.n; i++)
    {
        const BankEntry& e = b.e[i];
        const Plg& p = io.card.at(e.file);
        const auto img = imageOf(p, e.off, e.rwRun, e.len);
        if (e.state != 'o' || memcmp(f.at(e.off), img.data(), e.len) != 0) return false;
    }
    return true;
}

bool insert2Blank(const Flash& f)
{
    for (uint32_t i = kBankBytes; i < 0x20000; i++)
        if (f.m[i] != 0xFF) return false;
    return true;
}

Plg plg(const char* file, uint32_t uid, uint32_t len, uint32_t text, uint32_t rwLen, uint32_t crc = 0)
{
    return Plg{uid, 0x10000, crc ? crc : uid * 7u + len, (len + 3u) & ~3u, text, rwLen, file};
}

bool sharesSector(const Bank& b)
{
    for (int i = 0; i < b.n; i++)
        for (int k = 0; k < b.n; k++)
            if (i != k && (b.e[i].off + b.e[i].len - 1) / kBankSector == b.e[k].off / kBankSector) return true;
    return false;
}
}   // namespace

int main()
{
    // ---- index codec
    {
        Bank b = {};
        b.n = 2;
        b.e[0] = {0x73ac7ee0, 0x10000, 0x1234, 0, 6952, 6944, 0x20027000, 1648, 0, 0xdeadbeef, 'o', "ju6.plg"};
        b.e[1] = {0xea3d5a7a, 0x10000, 0x5678, 6952, 8168, 8160, 0x20027670, 112, 0, 0xfeedf00d, 'w', "fm4.plg"};
        char t[1024];
        const int n = bankWrite(b, t, sizeof(t));
        Bank r;
        bankRead(t, n, r);
        check(n > 0 && r.n == 2 && memcmp(&r.e[0], &b.e[0], sizeof(BankEntry)) == 0 && memcmp(&r.e[1], &b.e[1], sizeof(BankEntry)) == 0,
              "installed.txt round trip");
        std::string s(t, size_t(n));
        s.insert(s.find("\r\no ") + 2, "o 1 2\r\n");   // a cut line before the real one
        bankRead(s.data(), int(s.size()), r);
        check(r.n == 2, "a damaged line is dropped, the others stay");
        bankRead("garbage", 7, r);
        check(r.n == 0, "no header: empty bank");
        check(bankWrite(b, t, 40) < 0, "a full buffer is refused");
    }

    Io card;
    card.card["ju6.plg"] = plg("ju6.plg", 0x73ac7ee0, 6948, 6944, 1648);
    card.card["fm4.plg"] = plg("fm4.plg", 0xea3d5a7a, 8164, 8160, 112);
    const std::vector<std::string> two = {"ju6.plg", "fm4.plg"};

    // ---- fresh install, then a quiet reboot
    {
        Flash f;
        Io io = card;
        Bank b;
        BankStatus st = boot(f, io, two, b);
        check(st.installed == 2 && b.n == 2 && exact(f, io, b), "fresh install of JU6 and FM4");
        check(sharesSector(b), "JU6 and FM4 share a sector (packed, not one sector each)");
        check(b.e[1].off == ((b.e[0].len + 7u) & ~7u), "FM4 right after JU6");
        const int writes = io.indexWrites;
        st = boot(f, io, two, b);
        check(st.commands == 0 && st.installed == 0 && io.indexWrites == writes && b.n == 2, "reboot: no flash command, no index write");
        Flash g;
        Io none = card;
        none.card.clear();
        st = boot(g, none, {}, b);
        check(none.indexWrites == 0 && !none.haveIndex && b.n == 0, "no synth plugin, no index: nothing written");
        check(insert2Blank(f), "insert 2 sectors untouched");
    }

    // ---- the shipped synth plugins at their build_plugs.sh sizes (image, text, rw + bss): JU6, FM4 and B-Type together
    {
        Io io;
        io.card["ju6.plg"] = plg("ju6.plg", 0x73ac7ee0, 6948, 6944, 1648);
        io.card["fm4.plg"] = plg("fm4.plg", 0xea3d5a7a, 8172, 8168, 112);
        io.card["btype.plg"] = plg("btype.plg", 0x5e9b11f7, 11296, 11288, 120);
        const std::vector<std::string> three = {"ju6.plg", "fm4.plg", "btype.plg"};
        Flash f;
        Bank b;
        BankStatus st = boot(f, io, three, b);
        check(st.installed == 3 && st.skipped == 0 && b.n == 3 && exact(f, io, b), "JU6, FM4 and B-Type all install");
        check(insert2Blank(f), "JU6, FM4 and B-Type: insert 2 sectors untouched");
        uint32_t end = 0, rw = 0;
        for (int i = 0; i < b.n; i++)
        {
            end = b.e[i].off + b.e[i].len > end ? b.e[i].off + b.e[i].len : end;
            rw = b.e[i].rwRun + b.e[i].rwLen - uint32_t(kPool) > rw ? b.e[i].rwRun + b.e[i].rwLen - uint32_t(kPool) : rw;
        }
        check(end <= kBankBytes && rw <= kPoolBytes, "JU6, FM4 and B-Type inside the bank and the pool");
        printf("info  JU6 + FM4 + B-Type: bank %u of %u B used, %u B left, SRAM pool %u of %u B used, %u B left\n", end, kBankBytes,
               kBankBytes - end, rw, kPoolBytes, kPoolBytes - rw);
    }

    // ---- the 12 synth plugins at their build_plugs.sh sizes (image, text, rw + bss)
    {
        struct P { const char* f; uint32_t uid, len, text, rw; };
        const P kAll[12] = {
            {"ju6.plg", 0x73ac7ee0, 6956, 6952, 1648},    {"fm4.plg", 0xea3d5a7a, 8172, 8168, 112},
            {"btype.plg", 0x5e9b11f7, 11296, 11288, 120}, {"phz.plg", 0x6670b917, 14568, 14560, 120},
            {"dtype.plg", 0x6754cf41, 10788, 10784, 112}, {"cstype.plg", 0xf73018f5, 11804, 11800, 112},
            {"atype.plg", 0x24084bf2, 11060, 11056, 112}, {"ttype.plg", 0x318f12b1, 15028, 15024, 160},
            {"hand.plg", 0xf00e588e, 11224, 11216, 120},  {"arc.plg", 0xc66464e9, 14664, 14656, 120},
            {"pluck.plg", 0xb4a152b0, 6420, 6416, 120},   {"eight.plg", 0x9d7c57e2, 17760, 17752, 120},
        };
        uint32_t total = 0, rwTotal = 0;
        Io io;
        std::vector<std::string> all, eleven;
        for (const P& p : kAll)
        {
            io.card[p.f] = plg(p.f, p.uid, p.len, p.text, p.rw);
            all.push_back(p.f);
            if (strcmp(p.f, "eight.plg")) eleven.push_back(p.f);
            total += (p.len + 7) & ~7u;
            rwTotal += (p.rw + 7) & ~7u;
        }
        printf("info  12 synth plugins: bank %u of %u B (%d B over), SRAM pool %u of %u B\n", total, kBankBytes,
               int(total) - int(kBankBytes), rwTotal, kPoolBytes);
        check(total > kBankBytes && rwTotal <= kPoolBytes, "all 12: more than the bank, inside the SRAM pool");
        {
            Flash f;
            Bank b;
            BankStatus st = boot(f, io, all, b);
            check(st.installed == 11 && st.skipped == 1 && b.n == 11 && exact(f, io, b) && insert2Blank(f),
                  "all 12 on the card: 11 install, the last one (8-Type) is skipped, insert 2 untouched");
        }
        {
            Flash f;
            Bank b;
            BankStatus st = boot(f, io, eleven, b);
            uint32_t end = 0;
            for (int i = 0; i < b.n; i++) end = b.e[i].off + b.e[i].len > end ? b.e[i].off + b.e[i].len : end;
            check(st.installed == 11 && st.skipped == 0 && exact(f, io, b), "the 11 without 8-Type all install");
            printf("info  11 without 8-Type: bank %u of %u B, %u B left\n", end, kBankBytes, kBankBytes - end);
        }
    }

    // ---- a new version of the engine in the middle: shared sectors erased, neighbours kept
    {
        Io io = card;
        io.card["mid.plg"] = plg("mid.plg", 0x5555, 5000, 4992, 64);
        const std::vector<std::string> three = {"ju6.plg", "mid.plg", "fm4.plg"};
        Flash f;
        Bank b;
        boot(f, io, three, b);
        check(b.n == 3 && exact(f, io, b), "three packed");
        io.card["mid.plg"] = plg("mid.plg", 0x5555, 4000, 3992, 64, 0x9999);   // smaller: fits its old gap
        BankStatus st = boot(f, io, three, b);
        check(st.installed == 2 && b.n == 3 && exact(f, io, b) && b.e[1].uid == 0xea3d5a7a && b.e[1].off == b.e[0].len,
              "a new version goes last: FM4 moves up into its old place behind JU6 (shared sectors erased), all exact");
        io.card["mid.plg"] = plg("mid.plg", 0x5555, 9000, 8992, 64, 0x7777);   // bigger: goes after FM4
        st = boot(f, io, three, b);
        check(st.installed == 1 && b.n == 3 && exact(f, io, b), "a new version of the last one: only it is rewritten, all exact");
        io.card["new.plg"] = plg("new.plg", 0x6666, 3000, 2992, 64);
        const std::vector<std::string> four = {"new.plg", "ju6.plg", "mid.plg", "fm4.plg"};   // listed first on the card
        st = boot(f, io, four, b);
        check(st.installed == 1 && st.commands > 0 && b.n == 4 && exact(f, io, b) && b.e[3].uid == 0x6666,
              "a new engine goes after the installed ones whatever the listing order: nothing else moves");
    }

    // ---- remove two of eight and add a bigger one: the bank is laid out again, everything exact
    {
        Io io;
        std::vector<std::string> order;
        for (int i = 0; i < 8; i++)
        {
            const std::string n = "f" + std::to_string(i) + ".plg";
            io.card[n] = plg(n.c_str(), 0x2000 + i, 14000, 13992, 64);
            order.push_back(n);
        }
        Flash f;
        Bank b;
        boot(f, io, order, b);
        check(b.n == 8 && exact(f, io, b), "eight 14 KB engines");
        order.erase(order.begin() + 1);   // f1 and f5 removed: two 14 KB gaps
        order.erase(order.begin() + 4);
        io.card["big.plg"] = plg("big.plg", 0x3000, 20000, 19992, 64);
        order.push_back("big.plg");
        BankStatus st = boot(f, io, order, b);
        check(b.n == 7 && st.skipped == 0 && exact(f, io, b) && insert2Blank(f), "20 KB engine after a relayout, all exact");
    }

    // ---- power cut after every command: install of two, replace of the middle one of three by a bigger one (written to blank
    // space) and by a smaller one (FM4 moves into the old place: the sectors it shares with JU6 are erased, JU6's bytes put back)
    const char* const kScen[3] = {"install", "replace bigger", "replace, FM4 moves up"};
    for (int scenario = 0; scenario < 3; scenario++)
    {
        Io base = card;
        base.card["mid.plg"] = plg("mid.plg", 0x5555, 5000, 4992, 64);
        const std::vector<std::string> order = scenario ? std::vector<std::string>{"ju6.plg", "mid.plg", "fm4.plg"} : two;
        Flash f0;
        Bank b0;
        if (scenario) boot(f0, base, order, b0);   // the bank before the replace
        Io after = base;
        if (scenario == 1) after.card["mid.plg"] = plg("mid.plg", 0x5555, 7000, 6992, 64, 0x4242);
        if (scenario == 2) after.card["mid.plg"] = plg("mid.plg", 0x5555, 4000, 3992, 64, 0x4343);
        // the uncut run: how many commands, and the end state
        Flash ref = f0;
        Io refIo = after;
        Bank refB;
        const int total = boot(ref, refIo, order, refB).commands;
        int bad = 0, notExact = 0, neighbourLost = 0;
        for (int cut = 0; cut < total; cut++)
        {
            Flash f = f0;
            f.cutAt = f.commands + cut;
            f.rng = uint32_t(cut * 2654435761u);
            Io io = after;
            Bank b;
            boot(f, io, order, b);   // dies at command `cut`
            f.dead = false;
            f.cutAt = -1;
            // next boot: what passes the check must be exact
            Bank chk;
            bankRead(io.index.data(), int(io.index.size()), chk);
            const int dropped = bankCheck(chk, f, kPool, kPoolBytes);
            if (!exact(f, io, chk)) notExact++;
            if (dropped && scenario == 2) neighbourLost += chk.n < 2;
            boot(f, io, order, b);
            if (!(b.n == refB.n && exact(f, io, b) && insert2Blank(f))) bad++;
        }
        char what[120];
        snprintf(what, sizeof(what), "%s: cut after each of %d commands, nothing inexact survives the check", kScen[scenario], total);
        check(notExact == 0, what);
        snprintf(what, sizeof(what), "%s: cut after each of %d commands, the next boot completes the bank", kScen[scenario], total);
        check(bad == 0, what);
        if (scenario == 2)
            check(neighbourLost > 0, "replace in place: some cuts do destroy a neighbour (the CRC path is exercised)");
        printf("info  %s: %d flash commands uncut\n", kScen[scenario], total);
    }

    printf("%s  %d checks, %d failed\n", g_fail ? "FAILED" : "PASSED", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
