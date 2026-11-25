// Microtuning, phase 1 (ref/microtuning-scope.md): the project tuning is a 128 key to pitch table that is never stored. A key k sounds at
//   P(k) = 60 + 12 * log2(hz / 261.6256) + (k - root) * period / (100 * N)    semitones of the MIDI scale (60 = 261.6256 Hz)
// N notes per period (1..128), the period in cents (default 1200), the key that holds the 1/1 (root, default 60) and the Hz of the 1/1. The default
// (12, 1200, 60, 261.6256) gives P(k) = k exactly, so a project without a tuning sounds as it always did (bit identical, see tuneoff below).
// Pure arithmetic and no firmware includes: the sample table fill (play_memory.cpp), the synth shim (open_synth.cpp), the sidecar and the host sims
// all include this one header.
#ifndef MT_TUNING_H
#define MT_TUNING_H

#include <stdint.h>
#include <cmath>

namespace tuning
{
constexpr int kMaxN = 128;
constexpr int kKeys = 128;                 // keys 0..127: the pattern byte, the table length
constexpr uint32_t kDefPeriodMc = 1200000; // the period in thousandths of a cent
constexpr uint32_t kMinPeriodMc = 10000;   // 10 cents
constexpr uint32_t kMaxPeriodMc = 4800000; // 4800 cents
constexpr uint8_t kDefRoot = 60;
constexpr uint32_t kDefHzE4 = 2616256;     // 261.6256 Hz in 1e-4 Hz (key 60 of a synth part today)
constexpr uint32_t kMinHzE4 = 80000;       // 8 Hz
constexpr uint32_t kMaxHzE4 = 40000000;    // 4000 Hz
constexpr float kRelLimit = 84.0f;         // |P - 60| in semitones: a sample rate of 2^7 at most

struct Cfg
{
    uint8_t n;          // notes per period
    uint8_t root;       // the key of the 1/1
    uint32_t periodMc;  // period in 1e-3 cent
    uint32_t hzE4;      // 1/1 in 1e-4 Hz
    uint8_t file;       // 1: the degrees are the cents of degMc (a Scala file, phase 2), 0: n equal steps of period / n
    uint8_t names;      // 0: degree names (the default), 1: ups and downs names where the tuning has them (phase 3, noteNameUd)
    uint8_t udOk;       // set() keeps it: n has a fifth that names every degree with one up or down mark at most (udCheck)
};
inline Cfg cfg = {12, kDefRoot, kDefPeriodMc, kDefHzE4, 0, 0, 0};

// The Scala scale (phase 2, core/source/mtScala.h): the cents of degree d in 1e-3 cent, degMc[0] = 0, degMc[1..n-1] the file's lines, the period is
// cfg.periodMc. In the SDRAM like the note name buffer (512 B + 24 B, no SRAM) and NOT zeroed at boot: read only while cfg.file is set
#if defined(__arm__) || defined(__thumb__)
#define MT_TUNE_SDRAM __attribute__((section(".noinit.$RAM4.tunescale")))
#else
#define MT_TUNE_SDRAM
#endif
constexpr int kNameMax = 24;               // the file name with its NUL: a longer name is not listed
#if defined(__arm__) || defined(__thumb__)
// One definition (mtTuneScale.cpp), not an inline variable: GCC puts an inline variable of a named section in a COMDAT group of type progbits,
// and the assembler wants nobits for a .noinit section ("setting incorrect section type for .noinit.$RAM4.tunescale"). The data stays noinit.
extern uint32_t degMc[kMaxN];
extern char fileName[kNameMax];
#else
inline uint32_t degMc[kMaxN];
inline char fileName[kNameMax];
#endif

inline bool isDefault() { return cfg.n == 12 && cfg.root == kDefRoot && cfg.periodMc == kDefPeriodMc && cfg.hzE4 == kDefHzE4 && !cfg.file; }
// the pattern note names, the octave shift, Scale snap and the pad scale test follow N: with 12 notes per period everything stays as it was
inline bool tunedNames() { return cfg.n != 12; }
// the names are ups and downs: chosen, N has a fifth for them, an octave EDO (not a Scala file, not a stretched period)
inline bool udActive() { return cfg.names && cfg.udOk && !cfg.file && cfg.periodMc == kDefPeriodMc; }
inline int octaveKeys() { return cfg.n; }
inline void reset() { cfg = Cfg{12, kDefRoot, kDefPeriodMc, kDefHzE4, 0, 0, 0}; }
// Ups and downs note names (phase 3, scope section 4 scheme B): the xen wiki EDO notation. The nominals C D E F G A B come from the fifth
// f = round(N * log2(3/2)): C 0, D 2f-N, E 4f-2N, F N-f, G f, A 3f-N, B 5f-2N, a sharp is 7f-4N steps, a flat the same down. A degree that is no nominal
// or sharp or flat is one step off the nearest one: "^" one step up, "v" one step down ("^C#4", "vDb2": mark, letter, accidental, octave = 4 characters).
// It needs a fifth with limma 3N-5f >= 1 and sharp >= 1 and every degree within one step of a nominal, sharp or flat (udCheck). Degrees stay the default
inline int udFifth(int n) { return int(float(n) * 0.5849625f + 0.5f); }
// the name of degree e (0..n-1): letter li (C D E F G A B), accidental s (-1 flat, 0, 1 sharp), up or down mark u (-1, 0, 1) and the octave shift w of the
// letter (the degree may be the flat of the next C). The fewest marks win, then a natural, then no octave wrap, then a sharp over a flat, then an up over a down
inline bool udFind(int n, int f, int sh, int e, int& li, int& s, int& u, int& w)
{
    static const int8_t kC[7] = {0, 2, 4, -1, 1, 3, 5};   // fifths up from C of the letters C D E F G A B
    int best = 99;
    for (int i = 0; i < 7; i++)
    {
        const int p0 = ((kC[i] * f) % n + n) % n;
        for (int a = -1; a <= 1; a++)
            for (int x = -1; x <= 1; x++)
            {
                const int uu = e - p0 - a * sh - x * n;
                if (uu < -1 || uu > 1) continue;
                const int cost = (uu < 0 ? -uu : uu) * 16 + (a ? 8 : 0) + (x ? 4 : 0) + (a < 0 ? 2 : 0) + (uu < 0 ? 1 : 0);
                if (cost < best) { best = cost; li = i; s = a; u = uu; w = x; }
            }
    }
    return best < 99;
}
__attribute__((noinline)) inline bool udCheck(int n)
{
    if (n < 5) return false;
    const int f = udFifth(n), sh = 7 * f - 4 * n;
    if (3 * n - 5 * f < 1 || sh < 1) return false;
    int li = 0, s = 0, u = 0, w = 0;
    for (int e = 0; e < n; e++)
        if (!udFind(n, f, sh, e, li, s, u, w)) return false;
    return true;
}
// Scale snap in a tuning (phase 3): the 12 note scales of Config become degree masks of N bits (N up to 128, 4 words), rooted at the 1/1 key. With a fifth
// that names the degrees (limma and sharp both above 0) a 12 note scale is spelled as intervals above C on the nominals of the ups and downs names: the
// semitones 0 2 4 5 7 9 11 are the seven nominals, the minor second, third, sixth and seventh (1 3 8 10) a flat (7f-4N steps) below D E A B and the
// tritone (6) a sharp above F. Major is the 7 nominals (17: 0 3 6 7 10 13 16, 31: 0 5 10 13 18 23 28), Minor has the flat third, sixth and seventh (31: 0 5 8
// 13 18 21 26). Any other N maps the semitone s to round(s * N / 12). The mask is built by the caller (scales.cpp), this is the degree of one semitone
inline int semiDegree(int s, int n)
{
    static const uint8_t kLetter[12] = {0, 1, 1, 2, 2, 3, 3, 4, 5, 5, 6, 6};   // C Db D Eb E F F# G Ab A Bb B
    static const int8_t kFifths[7] = {0, 2, 4, -1, 1, 3, 5};
    const int f = udFifth(n), sh = 7 * f - 4 * n;
    if (3 * n - 5 * f < 1 || sh < 1) return ((s * n + 6) / 12) % n;
    int p = ((kFifths[kLetter[s]] * f) % n + n) % n;
    if ((0x50A >> s) & 1) p -= sh;        // the semitones 1 3 8 10 are flats
    else if (s == 6) p += sh;             // 6 is the sharp fourth
    return (p % n + n) % n;
}
// Finetune is in cents when the keys are not 100 cents apart (a tuning of N not 12, another period or a Scala file), else a hundredth of a key as always
inline bool fineCents() { return cfg.n != 12 || cfg.periodMc != kDefPeriodMc || cfg.file; }
// Chord and Arp intervals are written in semitones (a major triad is 0 4 7): in a tuning of N notes per period they land on the key round(i * N / 12), the same
// fraction of the period. With 12 notes it is i. Paraphonic synth engines get the chord NUMBER and keep spelling it in 12 TET
inline int chordStep(int semis) { return (semis * cfg.n + 6) / 12; }
// Scale snap works in the degrees of N notes (a tuning that is not 12 notes and not a Scala file) and is locked Off for a Scala file
inline bool snapDegrees() { return cfg.n != 12 && !cfg.file; }
inline bool snapLocked() { return cfg.n != 12 && cfg.file; }
inline void set(int n, uint32_t periodMc, int root, uint32_t hzE4)
{
    cfg.n = uint8_t(n < 1 ? 1 : (n > kMaxN ? kMaxN : n));
    cfg.udOk = udCheck(cfg.n) ? 1 : 0;
    cfg.periodMc = periodMc < kMinPeriodMc ? kMinPeriodMc : (periodMc > kMaxPeriodMc ? kMaxPeriodMc : periodMc);
    cfg.root = uint8_t(root < 0 ? 0 : (root > 127 ? 127 : root));
    cfg.hzE4 = hzE4 < kMinHzE4 ? kMinHzE4 : (hzE4 > kMaxHzE4 ? kMaxHzE4 : hzE4);
}

// One key step in semitones: period / (100 * N), exactly 1.0f for 12 notes of 1200 cents
inline float stepSemis() { return float(cfg.periodMc) / (100000.0f * float(cfg.n)); }
// P(k) - 60: the pitch of key k in semitones from the 1/1 Hz of the default, clamped to +-84. Exact (k - 60) for the default
inline float rel(int k)
{
    const float hz = cfg.hzE4 == kDefHzE4 ? 0.0f : 12.0f * std::log2(float(cfg.hzE4) / float(kDefHzE4));
    float r;
    if (cfg.file)
    {   // cents of the key: whole periods of the period plus the degree, integer in 1e-3 cent (clamped before it becomes a float, exact below 2^24)
        const int n = cfg.n, d = k - int(cfg.root);
        const int o = d >= 0 ? d / n : -((-d + n - 1) / n);   // floor(d / n)
        int32_t mc = o * int32_t(cfg.periodMc) + int32_t(degMc[d - o * n]);
        constexpr int32_t kLim = int32_t(kRelLimit * 100000.0f);
        mc = mc > kLim ? kLim : (mc < -kLim ? -kLim : mc);
        r = hz + float(mc) / 100000.0f;
    }
    else
        r = hz + float(k - int(cfg.root)) * stepSemis();
    r = r > kRelLimit ? kRelLimit : (r < -kRelLimit ? -kRelLimit : r);
    return r;
}
// What a synth part adds to the key: P(k) - k. Exactly 0.0f for the default
inline float synthOffset(int k) { return isDefault() ? 0.0f : 60.0f + rel(k) - float(k); }
// The Table row of a synth part is a semitone value, a sample voice's one is a key step: scale the first by the mean step (1.0f by default)
inline float tableScale() { return stepSemis(); }

// The name of key k when N is not 12 (the note cell has 4 characters): "DD.O" degree 0..N-1, a dot and the octave counter, which counts the
// periods from key 0 (R = ceil(root / N), so key 0 is octave 0 for a root that is a multiple of N and the counter never goes below 0).
// N 1: the key number. N 2..10: one digit degree and up to two digits octave ("3.18"). N 11..99: two digit degree, one base 36 octave
// character ("17.2"). N 100..128: three digit degree and the octave digit without the dot ("1231"). out gets 5 bytes
inline void noteNameUd(int k, char* out)
{
    const int n = cfg.n, f = udFifth(n), sh = 7 * f - 4 * n;
    const int d = k - int(cfg.root), o = d >= 0 ? d / n : -((-d + n - 1) / n);   // floor(d / n)
    int li = 0, s = 0, u = 0, w = 0;
    udFind(n, f, sh, d - o * n, li, s, u, w);
    int oc = o + w + (int(cfg.root) + n - 1) / n;
    oc = oc < 0 ? 0 : (oc > 9 ? 9 : oc);
    int pos = 0;
    if (u) out[pos++] = u > 0 ? '^' : 'v';
    out[pos++] = "CDEFGAB"[li];
    if (s) out[pos++] = s > 0 ? '#' : 'b';
    out[pos++] = char('0' + oc);
    while (pos < 5) out[pos++] = 0;
}
inline void noteName(int k, char* out)
{
    if (udActive()) { noteNameUd(k, out); return; }
    const int n = cfg.n;
    int pos = 0;
    auto put = [&](char c) { if (pos < 4) out[pos++] = c; };
    auto dec = [&](int v, int w) {   // v in w digits, zero padded to w only for the octave (degrees are not padded)
        char t[4];
        int c = 0;
        do { t[c++] = char('0' + v % 10); v /= 10; } while (v > 0 && c < 3);
        for (int i = c; i < w; i++) put('0');
        while (c) put(t[--c]);
    };
    if (n == 1)
    {
        dec(k, 0);
    }
    else
    {
        const int r = (int(cfg.root) + n - 1) / n;
        int d = k - int(cfg.root);
        int o = d >= 0 ? d / n : -((-d + n - 1) / n);   // floor(d / n)
        d -= o * n;
        o += r;
        if (o < 0) o = 0;
        dec(d, 0);
        if (n <= 99) put('.');
        if (n <= 10)
        {
            dec(o > 99 ? 99 : o, 0);
        }
        else
        {
            const int c = o > 35 ? 35 : o;
            put(char(c < 10 ? '0' + c : 'A' + (c - 10)));
        }
    }
    out[pos] = 0;
    for (int i = pos + 1; i < 5; i++) out[i] = 0;
}
}   // namespace tuning

#endif
