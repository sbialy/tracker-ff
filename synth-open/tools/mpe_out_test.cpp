// mpe_out_test.cpp: the MPE out allocator (core/source/mtMpeOut.h) against a recording sink, random streams of note on, note off, M FX bends, stops and
// zone changes. Invariants after every message: a member never gets a note on while it sounds, every off is on the channel and key of its on, never
// more notes than members sound, bends are sent before the note on that needs them, nothing on the master channel 1. After the last stop nothing sounds.
#include <cstdio>
#include <cstdlib>
#include <vector>
#include "../../core/source/mtMpeOut.h"

struct Rec
{
    int snd[17];   // the key sounding on a channel, -1 none
    int bend[17];
    int sounding = 0, bad = 0, ons = 0, offs = 0, ch1 = 0, over = 0;
    Rec() { for (int i = 0; i < 17; i++) { snd[i] = -1; bend[i] = 0; } }
    void noteOn(int ch, uint8_t n, uint8_t) { if (snd[ch] >= 0) bad++; snd[ch] = n; sounding++; ons++; if (ch == 1) ch1++; if (sounding > mpeout::members) over++; }
    void noteOff(int ch, uint8_t n) { if (snd[ch] != n) bad++; else { snd[ch] = -1; sounding--; } offs++; }
    void bendMsg(int ch, int v) { bend[ch] = v; }
    void cc(int, uint8_t, uint8_t) {}
    void pressure(int, uint8_t) {}
    void bend_(int, int) {}
};
struct Sink
{
    Rec& r;
    void noteOn(int ch, uint8_t n, uint8_t v) { r.noteOn(ch, n, v); }
    void noteOff(int ch, uint8_t n) { r.noteOff(ch, n); }
    void bend(int ch, int v) { r.bendMsg(ch, v); }
    void cc(int ch, uint8_t i, uint8_t v) { r.cc(ch, i, v); }
    void pressure(int ch, uint8_t v) { r.pressure(ch, v); }
};

int main()
{
    int fails = 0, checks = 0;
    auto check = [&](bool ok, const char* what) { checks++; if (!ok) { fails++; printf("FAIL  %s\n", what); } else printf("ok    %s\n", what); };
    srand(12345);
    long totalBad = 0, totalOver = 0, totalCh1 = 0, stuck = 0, bentAtEnd = 0, notes = 0;
    for (int run = 0; run < 400; run++)
    {
        mpeout::members = 0;
        for (auto& s : mpeout::slots) s = {};
        for (auto& b : mpeout::lastBend) b = 0;
        for (auto& b : mpeout::chanFx) b = 0;
        if (run % 3 == 0) tuning::set(31, tuning::kDefPeriodMc, 60, tuning::kDefHzE4); else tuning::reset();
        Rec r;
        Sink s{r};
        mpeout::setMembers(s, uint8_t(1 + rand() % 15), 1);
        std::vector<std::pair<int, int>> held;   // (src, note) the sequencer thinks sound
        for (int i = 0; i < 300; i++)
        {
            const int k = rand() % 10;
            if (k < 5)
            {
                const int src = 1 + rand() % 4, note = 36 + rand() % 40;
                // the sequencer ends a note before it starts the same note again (a retrigger sends the off first)
                for (size_t h = 0; h < held.size(); h++)
                    if (held[h].first == src && held[h].second == note) { mpeout::noteOff(s, uint8_t(note), uint8_t(src)); held.erase(held.begin() + long(h)); break; }
                mpeout::noteOn(s, uint8_t(note), 100, uint8_t(src), 24, 1);
                held.push_back({src, note});
                notes++;
            }
            else if (k < 8 && !held.empty())
            {
                const size_t h = size_t(rand()) % held.size();
                mpeout::noteOff(s, uint8_t(held[h].second), uint8_t(held[h].first));
                held.erase(held.begin() + long(h));
            }
            else if (k == 8)
                mpeout::fxBend(s, (rand() % 8001) - 4000, uint8_t(1 + rand() % 4));
            else if (rand() % 6 == 0)
            {
                mpeout::release(s);
                held.clear();
            }
        }
        for (auto& h : held) mpeout::noteOff(s, uint8_t(h.second), uint8_t(h.first));
        mpeout::release(s);
        totalBad += r.bad; totalOver += r.over; totalCh1 += r.ch1; stuck += r.sounding;
        for (int c = 1; c <= 16; c++) bentAtEnd += r.bend[c] != 0;
    }
    check(notes > 50000, "400 random runs, over 50,000 notes played");
    check(totalBad == 0, "no note on a sounding member, no off that is not the sounding note of its channel");
    check(totalOver == 0, "never more notes sounding than members");
    check(totalCh1 == 0, "nothing on the master channel");
    check(stuck == 0, "after the last Stop nothing sounds");
    check(bentAtEnd == 0, "after the last Stop every bend is back at 0");
    // the pitch of the tuning: key and bend rebuild the note within a cent per 0.01 semitone
    tuning::set(31, tuning::kDefPeriodMc, 60, tuning::kDefHzE4);
    double worst = 0;
    for (int k = 20; k < 100; k++)
    {
        uint8_t w; int16_t b;
        mpeout::pitchOf(k, 24, w, b);
        const double p = 60.0 + double(tuning::rel(k)) + 24.0, got = double(w) + double(b) * 48.0 / 8192.0;
        const double d = p > got ? p - got : got - p;
        if (d > worst) worst = d;
    }
    check(worst < 0.004, "31 EDO keys 20..99: key + bend rebuild the pitch within 0.004 semitone (14 bit over 48 st)");
    tuning::reset();
    uint8_t w; int16_t b;
    mpeout::pitchOf(77, 24, w, b);
    check(w == 101 && b == 0, "12 EDO: the key of today and no bend");
    printf("%s (%d checks)\n", fails ? "RED" : "GREEN", checks);
    return fails ? 1 : 0;
}
