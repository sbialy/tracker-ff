// Host test of the order and visibility logic of the synth model list (synth-open/synth_order.h): the file format, hiding, order,
// engines that are new or gone, a hidden engine in use, wrap around, a corrupt or absent file, the limits.
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/synth_order_test.cpp -o /tmp/synth_order_test && /tmp/synth_order_test
#include "synth_order.h"

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace synthorder;

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}

struct Rig
{
    List l;
    int16_t entryUi[kMaxEntries];
    View v;
    std::vector<const char*> names;
    bool used[kViewMax] = {};

    void load(const std::string& text, std::vector<const char*> engines)
    {
        names = engines;
        parse(l, text.c_str(), int(text.size()));
        reconcile(l, names.data(), int(names.size()), entryUi);
    }
    void build(bool edit = false) { buildView(l, entryUi, int(names.size()), used, edit, v); }
    // the names of the view, "A B -C" style is not needed: hidden ones show only in the edit view
    std::string shown()
    {
        std::string s;
        for (int p = 0; p < v.n; p++)
        {
            if (p) s += ' ';
            s += names[size_t(v.ui[p])];
        }
        return s;
    }
};
}   // namespace

int main()
{
    static Rig r;
    const std::vector<const char*> eng = {"ACD", "FAT", "VAP", "WTFM", "PERC", "CHIP", "JU6", "PHZ"};

    // no file: every engine, in the order of the engine list
    r.load("", eng);
    r.build();
    check(r.shown() == "ACD FAT VAP WTFM PERC CHIP JU6 PHZ", "no file: default order, all shown");
    check(r.l.n == 8, "no file: an entry per engine after reconcile");

    // order and hide
    r.load("PHZ\r\nJU6\r\n-FAT\r\nACD\r\n", eng);
    r.build();
    check(r.shown() == "PHZ JU6 ACD VAP WTFM PERC CHIP", "order of the file, FAT hidden, unlisted engines after");
    r.build(true);
    check(r.shown() == "PHZ JU6 FAT ACD VAP WTFM PERC CHIP", "edit view: hidden engine in its place");

    // case, spaces, comments, blank lines, LF only, a space after the minus
    r.load("# my list\n\n  jU6  \n- vap\n\tacd\n", eng);
    r.build();
    check(r.shown() == "JU6 ACD FAT WTFM PERC CHIP PHZ", "lenient parse: case, spaces, comment, '- name'");

    // a hidden engine that the open project uses is at the top, before the ordered ones
    r.load("PHZ\nJU6\n-FAT\nACD\n-WTFM\n", eng);
    r.used[1] = true;   // FAT
    r.used[3] = true;   // WTFM
    r.build();
    check(r.shown() == "FAT WTFM PHZ JU6 ACD VAP PERC CHIP", "hidden but used: at the top (in file order), visible ones after");
    check(viewPos(r.v, 1) == 0 && viewPos(r.v, 3) == 1, "the used hidden engines are rows 0 and 1");
    r.used[1] = r.used[3] = false;
    r.used[0] = true;   // a visible engine in use keeps its place
    r.build();
    check(r.shown() == "PHZ JU6 ACD VAP PERC CHIP", "an engine in use that is not hidden stays where the file puts it");
    r.used[0] = false;

    // plugin gone: its line is ignored by the view, kept in the entries, the rest in order
    r.load("PHZ\nGONE\n-ALSOGONE\nACD\n", eng);
    r.build();
    check(r.shown() == "PHZ ACD FAT VAP WTFM PERC CHIP JU6", "an entry without an engine is ignored");
    bool kept = false;
    for (int k = 0; k < r.l.n; k++) kept |= nameEq(r.l.e[k].name, "GONE");
    check(kept, "the entry of an engine that is gone stays in the list (for the save)");
    std::vector<char> text(4096);
    int n = format(r.l, text.data(), int(text.size()));
    std::string t(text.data(), size_t(n));
    check(t.find("GONE\r\n") != std::string::npos && t.find("-ALSOGONE\r\n") != std::string::npos, "and in the saved text, hidden flag too");

    // a new engine appears: at the end, visible
    r.load("ACD\nFAT\n", {"ACD", "FAT", "NEWONE"});
    r.build();
    check(r.shown() == "ACD FAT NEWONE", "a new engine is added at the end, visible");
    r.load("NEWONE\nACD\n", {"ACD", "FAT", "NEWONE"});
    r.build();
    check(r.shown() == "NEWONE ACD FAT", "a listed new engine goes where the file says");

    // duplicates in the file: the first counts
    r.load("ACD\n-ACD\nFAT\n", eng);
    r.build();
    check(r.shown().rfind("ACD FAT", 0) == 0, "a repeated name: the first line counts");

    // two engines of one name: each takes an entry, both shown
    r.load("DUP\n", {"DUP", "X", "DUP"});
    r.build();
    check(r.shown() == "DUP DUP X" || r.shown() == "DUP X DUP", "two engines of one name are both listed");
    check(r.v.n == 3, "two engines of one name: three rows");

    // corrupt files
    {
        std::string bin;
        for (int i = 0; i < 600; i++) bin += char((i * 37) & 0xFF);
        r.load(bin, eng);
        r.build();
        check(r.shown() == "ACD FAT VAP WTFM PERC CHIP JU6 PHZ", "binary garbage: default order");
        r.load(std::string("\xFF\xFE\x00\x01", 4), eng);
        r.build();
        check(r.v.n == 8, "a few bytes of junk: everything shown");
        r.load("-\n-   \n#\n   \n\r\n", eng);
        r.build();
        check(r.shown() == "ACD FAT VAP WTFM PERC CHIP JU6 PHZ", "lines without a name are skipped");
        std::string longline(5000, 'A');
        r.load(longline + "\nFAT\n", eng);
        r.build();
        check(r.v.n == 8 && r.shown().rfind("AAAA", 0) != 0, "a very long line is cut and matches nothing");
        parse(r.l, nullptr, 0);
        check(r.l.n == 0, "no text: no entries");
        parse(r.l, "ACD", -1);
        check(r.l.n == 0, "a read error (negative length): no entries");
        // the same text with a NUL inside the line: that line is skipped, the next one counts
        r.load(std::string("AC\0D\nFAT\n", 9), eng);
        r.build();
        check(r.shown().rfind("FAT", 0) == 0, "a line with a NUL byte is skipped");
    }

    // everything hidden, nothing in use: the view is not empty
    r.load("-ACD\n-FAT\n", {"ACD", "FAT"});
    r.build();
    check(r.v.n == 2, "all hidden and none in use: all shown (never an empty list)");
    r.used[0] = true;
    r.build();
    check(r.shown() == "ACD", "all hidden, one in use: only that one");
    r.used[0] = false;

    // wrap around
    check(step(0, -1, 5, true) == 4, "up on the first entry goes to the last");
    check(step(4, 1, 5, true) == 0, "down on the last entry goes to the first");
    check(step(2, 1, 5, true) == 3 && step(2, -1, 5, true) == 1, "inside the list a step is a step");
    check(step(0, -1, 5, false) == 0 && step(4, 1, 5, false) == 4, "without wrap (the encoder) it stops at the ends");
    check(step(3, 4, 5, false) == 4 && step(1, -4, 5, false) == 0, "a bigger step without wrap stops at the end");
    check(step(0, -1, 1, true) == 0 && step(0, 1, 1, true) == 0, "a list of one row stays on it");
    check(step(0, 1, 0, true) == 0, "an empty list is row 0");
    {   // wrap in the view with a hidden engine: the ends are the ends of the view
        r.load("-FAT\nPHZ\nACD\n", {"ACD", "FAT", "PHZ"});
        r.build();
        check(r.shown() == "PHZ ACD", "view PHZ ACD");
        check(r.v.ui[step(0, -1, r.v.n, true)] == 0, "up on PHZ goes to the last visible one (ACD)");
        check(r.v.ui[step(r.v.n - 1, 1, r.v.n, true)] == 2, "down on ACD goes to the first visible one (PHZ)");
    }

    // moving an entry (edit mode): in the edit view, past entries without an engine
    r.load("A\nGONE\nB\nC\n", {"A", "B", "C"});
    r.build(true);
    check(r.shown() == "A B C", "edit view of A B C");
    check(moveEntry(r.l, r.entryUi, r.v.entry[0], 1), "move A down succeeds");
    r.build(true);
    check(r.shown() == "B A C", "A is below B");
    check(nameEq(r.l.e[1].name, "GONE"), "the entry of the gone engine kept its place in the file");
    check(!moveEntry(r.l, r.entryUi, r.v.entry[0], -1), "the first row cannot move up");
    check(!moveEntry(r.l, r.entryUi, r.v.entry[2], 1), "the last row cannot move down");
    check(moveEntry(r.l, r.entryUi, r.v.entry[2], -1), "C up");
    r.build(true);
    check(r.shown() == "B C A", "C is above A");
    {   // the order of the file against a rebind: the engine of every entry is still the one the entry names
        int16_t again[kMaxEntries];
        bind(r.l, r.names.data(), 3, again);
        bool same = true;
        for (int k = 0; k < r.l.n; k++) same &= again[k] == r.entryUi[k];
        check(same, "moveEntry keeps entryUi equal to a fresh bind");
    }

    // hide through the entry and save and reload: a round trip
    r.load("ACD\nFAT\nVAP\n", {"ACD", "FAT", "VAP", "WTFM"});
    r.build(true);
    r.l.e[r.v.entry[1]].hidden = true;
    moveEntry(r.l, r.entryUi, r.v.entry[3], -1);   // WTFM above VAP
    n = format(r.l, text.data(), int(text.size()));
    std::string saved(text.data(), size_t(n));
    r.load(saved, {"ACD", "FAT", "VAP", "WTFM"});
    r.build();
    check(r.shown() == "ACD WTFM VAP", "save, read back: FAT hidden, WTFM moved");
    r.build(true);
    check(r.shown() == "ACD FAT WTFM VAP", "save, read back (edit view)");
    check(saved.rfind("# Synth list", 0) == 0 && saved.find("\r\n-FAT\r\n") != std::string::npos, "the text has the comment head and a -FAT line");

    // the text is cut before a line that does not fit, never inside one
    {
        char small[300];
        r.load("ACD\nFAT\nVAP\nWTFM\nPERC\nCHIP\nJU6\nPHZ\n", eng);
        int m = format(r.l, small, int(sizeof(small)));
        check(m > 0 && m < int(sizeof(small)), "a small buffer: the length stays inside");
        std::string cut(small, size_t(m));
        check(cut.size() >= 2 && cut.substr(cut.size() - 2) == "\r\n", "a small buffer: the text ends at a line end");
    }

    // limits: 256 engines, a file of 700 lines (a part of them junk), everything still reconciles
    {
        static char names[256][8];
        std::vector<const char*> big;
        for (int i = 0; i < 256; i++)
        {
            snprintf(names[i], sizeof(names[i]), "E%03d", i);
            big.push_back(names[i]);
        }
        std::string f;
        for (int i = 0; i < 700; i++) f += "X" + std::to_string(i) + "\n";   // 700 engines that do not exist
        r.load(f, big);
        r.build();
        check(r.v.n == 256, "256 engines and a file full of junk entries: all 256 shown");
        check(r.l.n <= kMaxEntries, "the entry list never passes its capacity");
        std::string f2;
        for (int i = 255; i >= 0; i--) f2 += std::string(i % 2 ? "-" : "") + names[i] + "\n";
        r.load(f2, big);
        r.build();
        check(r.v.n == 128 && r.v.ui[0] == 254, "256 engines reversed, the odd ones hidden: 128 rows, the first is E254");
        r.build(true);
        check(r.v.n == 256 && r.v.ui[0] == 255, "edit view: all 256");
    }

    // names longer than the cut still match (both sides are cut)
    r.load("ABCDEFGHIJKLMNOPQRSTUVWXYZ0123\n", {"ABCDEFGHIJKLMNOPQRSTUVWXYZ0123", "B"});
    r.build();
    check(r.v.n == 2 && r.v.ui[0] == 0, "a name longer than 23 characters is matched on its first 23");


    // the rows of the edit list fit the clip of the list column with the "-" first (synthorder::fitRow). The widths are those of the
    // list's font (font 1) measured in ui_shot ("poke textw"): 8 px for every printable glyph, 9 px for # % & K W w
    {
        auto glyph = [](char c) { return strchr("#%&KWw", c) && c ? 9 : 8; };
        auto pxOf = [&](const char* t) { int w = 0; for (; *t; t++) w += glyph(*t); return w; };
        struct Row { const char* name; bool hidden; const char* want; };
        const Row rows[] = {
            {"ACD", true, "-ACD"},             {"ACD", false, "ACD"},
            {"CSTYPE", true, "-CSTYPE"},       {"ARCTYPE", true, "-ARCTYPE"},     {"ARCTYPE", false, "ARCTYPE"},
            {"8TYPE", true, "-8TYPE"},         {"HANDTYPE", true, "-HAND"},       {"HANDTYPE", false, "HAND"},
            {"Hand-Type", true, "-Hand"},      {"Hand-Type", false, "Hand"},      {"HANDTYPE", true, "-HAND"},
            {"WWWWWWWW", true, "-WWWWWW"},        {"WWWWWWWW", false, "WWWWWW"},   {"abcdefghij", true, "-abcdefg"},  {"abcdefgh", false, "abcdefg"},
            {"TYPE", true, "-TYPE"},           {"-TYPE", true, "--TYPE"},          {"Type", false, "Type"},
        };
        for (const Row& t : rows)
        {
            char out[12];
            fitRow(out, sizeof(out), t.name, t.hidden, glyph);
            std::string what = std::string("row ") + (t.hidden ? "-" : "") + t.name + " -> '" + out + "'";
            check(strcmp(out, t.want) == 0, (what + ", wanted '" + t.want + "'").c_str());
            check(pxOf(out) <= kRowPx - kRowMarginPx + (t.hidden ? 0 : glyph('-')), (what + " fits the clip").c_str());
            check(!t.hidden || out[0] == '-', (what + ": the mark is first").c_str());
        }
        // every name length and glyph mix of up to 15 characters: the hidden row always fits the clip, mark first, and never overflows out
        const char alpha[] = "WMiK-8x#";
        int bad = 0;
        for (int seed = 0; seed < 4000; seed++)
        {
            char nm[16];
            int len = 1 + seed % 15;
            unsigned h = seed * 2654435761u;
            for (int i = 0; i < len; i++) { nm[i] = alpha[(h >> (i * 2)) % 8]; h = h * 1664525u + 1013904223u; }
            nm[len] = 0;
            char out[12];
            memset(out, 0x7f, sizeof(out));
            fitRow(out, sizeof(out), nm, true, glyph);
            if (!(out[0] == '-' && strlen(out) <= 11 && strlen(out) >= 2 && pxOf(out) <= kRowPx - kRowMarginPx))
            {
                bad++;
                printf("      bad: %s -> %s\n", nm, out);
            }
        }
        check(bad == 0, "4000 mixed names of 1..15 characters: a hidden row always fits, mark first, 2..11 characters");
    }

    printf("%s\n", g_fail ? "FAILED" : "ALL OK");
    return g_fail ? 1 : 0;
}
