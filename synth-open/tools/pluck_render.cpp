// PLK host tool, NOT firmware. Drives the engine directly through ISynthEngine (block by block, 128 frames, parameters ramp
// start -> end like the shim does), no shim, no hardware.
//   synth-open/tools/pluck_render.sh [outdir]      WAV files (default build/pluck-wav), then the checks below
//   pluck_render --check                           checks only (exit status 1 when one fails)
// WAV files (mono, 16 bit, 44118 Hz):
//   10_arp_<exciter>.wav     C major arpeggio E1 to E6, default patch, once per Exciter
//   20_damping_short/long    the Noise arpeggio at Damping 85 % and 5 %
//   30_stretch.wav           E3 plucked 8 times, Stretch 0 -> 100 %
//   31_stretch_bell.wav      the same at a long Damping on C4 with Click, Brightness 100 %
//   40_pickpos.wav           A2 plucked 8 times, Pick Pos 0 -> 100 %
//   50_brightness.wav        A2 plucked 8 times, Brightness 0 -> 100 %
//   60_velocity.wav          A3 at velocity 0.2 / 0.4 / 0.6 / 0.8 / 1.0
//   70_release.wav           A3 held 0.3 s with Release 20 ms / 150 ms / 600 ms / 2 s
//   80_chord.wav             four voices: A2 E3 A3 C#4, Mallet
//   00_all.wav               everything in this order
// Checks: pitch within 5 cents of the equal tempered target for E1 to E7 at 13 parameter sets, decay time of the fundamental (Damping
// is the same on every key), Exciter loudness, no clipping, finite output, idle voice ends.
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "open_engine.h"
#include "pluck/pluck.h"

using namespace ot::audio;
namespace polysynth::open { extern const EngineDef kPlkDef; }

namespace
{
constexpr int kRate = 44118;   // WAV header only
constexpr double kSr = 44117.647;

struct Patch { float v[pluck::kParamCount]; };

Patch initPatch()
{
    Patch p;
    const auto& d = polysynth::open::kPlkDef;
    for (int i = 0; i < d.paramCount; i++) p.v[i] = d.params[i].def;
    return p;
}

struct Note { double t, len; int note; float vel; Patch patch; bool own; };
Note mk(double t, double len, int note, float vel = 1.0f) { Note n{}; n.t = t; n.len = len; n.note = note; n.vel = vel; return n; }
Note mkp(double t, double len, int note, const Patch& p, float vel = 1.0f) { Note n = mk(t, len, note, vel); n.patch = p; n.own = true; return n; }

struct Result { std::vector<float> out; int blocksAlive = 0; bool finite = true; };

// Renders notes of a patch (one engine voice per note, summed). A note carries its own patch when n.own.
Result render(const Patch& base, const std::vector<Note>& notes, double seconds)
{
    ISynthEngine* eng = pluck::engine();
    SynthSlotScratch sc;
    eng->slotReset(sc);
    struct V { SynthVoice v; bool on = true, alive = true, started = false; };
    std::vector<V> voices(notes.size());
    for (auto& vv : voices) { std::memset(&vv.v, 0, sizeof(vv.v)); vv.v.slotScratch = &sc; }
    Result r;
    const int blocks = int(seconds * kSr / kBlock) + 1;
    for (int b = 0; b < blocks; b++)
    {
        const double t = double(b) * kBlock / kSr;
        float L[kBlock] = {}, R[kBlock] = {};
        for (size_t i = 0; i < notes.size(); i++)
        {
            V& vv = voices[i];
            const Patch& pt = notes[i].own ? notes[i].patch : base;
            SynthParams P;
            P.count = pluck::kParamCount;
            std::memcpy(P.start, pt.v, sizeof(pt.v));
            std::memcpy(P.end, pt.v, sizeof(pt.v));
            P.serial = 1;
            if (!vv.started && t >= notes[i].t)
            {
                SynthNote n; n.pitch = float(notes[i].note); n.note = uint8_t(notes[i].note); n.velocity = notes[i].vel;
                vv.v.gate = true; eng->noteOn(vv.v, P, n); vv.started = true;
            }
            if (vv.started && vv.on && t >= notes[i].t + notes[i].len) { vv.v.gate = false; eng->noteOff(vv.v, P); vv.on = false; }
            if (vv.started && vv.alive) { vv.alive = eng->render(vv.v, P, pt.v[pluck::kVolume], 0.0f, L, R); r.blocksAlive += vv.alive; }
        }
        for (int k = 0; k < kBlock; k++) { if (!std::isfinite(L[k]) || L[k] != R[k]) r.finite = false; }
        r.out.insert(r.out.end(), L, L + kBlock);
    }
    return r;
}

void writeWav(const std::string& path, const std::vector<float>& x)
{
    FILE* f = fopen(path.c_str(), "wb");
    if (!f) { perror(path.c_str()); return; }
    const uint32_t n = uint32_t(x.size()), bytes = n * 2, rate = kRate, br = rate * 2;
    const uint16_t fmt = 1, ch = 1, align = 2, bits = 16;
    const uint32_t sixteen = 16, riff = 36 + bytes;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&sixteen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&br, 4, 1, f); fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f);
    fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (float v : x) { const int16_t s = int16_t(std::lround(std::clamp(v, -1.0f, 1.0f) * 32767.0f)); fwrite(&s, 2, 1, f); }
    fclose(f);
}

std::vector<float> gAll;
std::string gDir;

void emit(const std::string& name, std::vector<float> x, float gain = 0.8f)
{
    float pk = 0; double e = 0;
    for (float v : x) { pk = std::max(pk, std::fabs(v)); e += v * v; }
    for (float& v : x) v *= gain;
    writeWav(gDir + "/" + name + ".wav", x);
    printf("%-26s %5.1f s  peak %.2f  rms %.3f\n", (name + ".wav").c_str(), double(x.size()) / kRate, pk * gain, std::sqrt(e / std::max<size_t>(x.size(), 1)) * gain);
    gAll.insert(gAll.end(), x.begin(), x.end());
    gAll.insert(gAll.end(), kRate / 2, 0.0f);
}

const char* const kExcName[] = { "noise", "click", "saw", "mallet" };
const int kArp[] = { 28, 31, 36, 40, 43, 48, 52, 55, 60, 64, 67, 72, 76, 79, 84, 88 };   // E1 G1 C2 E2 G2 C3 E3 G3 C4 E4 G4 C5 E5 G5 C6 E6

std::vector<Note> arpeggio(double step, double len)
{
    std::vector<Note> n;
    for (size_t i = 0; i < sizeof(kArp) / sizeof(kArp[0]); i++) n.push_back(mk(0.05 + double(i) * step, len, kArp[i]));
    return n;
}

std::vector<float> sweepPluck(Patch p, int param, float from, float to, int note, int count, double step, double hold, double tail)
{
    std::vector<Note> n;
    for (int i = 0; i < count; i++)
    {
        Patch q = p;
        q.v[param] = from + (to - from) * float(i) / float(count - 1);
        n.push_back(mkp(0.05 + i * step, hold, note, q));
    }
    return render(p, n, 0.05 + count * step + tail).out;
}

// ------------------------------------------------------------------ checks

int gFail = 0;
void check(bool ok, const char* what, const char* detail = "")
{
    printf("  %s: %s %s\n", ok ? "ok  " : "FAIL", what, detail);
    if (!ok) gFail++;
}

// Hann windowed DFT magnitude of x[from, from+n) at f Hz
double dftMag(const std::vector<float>& x, size_t from, size_t n, double f)
{
    double re = 0, im = 0;
    const double w = 2.0 * M_PI * f / kSr;
    for (size_t i = 0; i < n; i++)
    {
        const double h = 0.5 - 0.5 * std::cos(2.0 * M_PI * double(i) / double(n));
        const double s = double(x[from + i]) * h;
        re += s * std::cos(w * double(i)); im -= s * std::sin(w * double(i));
    }
    return std::sqrt(re * re + im * im);
}

double hz(double note) { return 440.0 * std::pow(2.0, (note - 69.0) / 12.0); }

// Fundamental near the target, in cents of error (peak of the DFT magnitude over +-80 cents in 1 cent steps, parabolic refinement)
double pitchErrorCents(const std::vector<float>& x, size_t from, size_t n, double target)
{
    const int span = 80;
    std::vector<double> m(2 * span + 1);
    int best = 0;
    for (int c = -span; c <= span; c++)
    {
        m[c + span] = dftMag(x, from, n, target * std::pow(2.0, c / 1200.0));
        if (m[c + span] > m[best]) best = c + span;
    }
    if (best == 0 || best == 2 * span) return 999.0;   // no peak inside the window
    const double a = std::log(m[best - 1] + 1e-12), b = std::log(m[best] + 1e-12), c = std::log(m[best + 1] + 1e-12);
    const double d = 0.5 * (a - c) / (a - 2 * b + c);
    return double(best - span) + d;
}

struct Variant { const char* name; int param[3]; float val[3]; };

void runChecks()
{
    printf("\nChecks\n");
    const Variant variants[] = {
        { "default", { -1, -1, -1 }, { 0, 0, 0 } },
        { "noise bri 0", { pluck::kBrightness, -1, -1 }, { 0, 0, 0 } },
        { "noise bri 1", { pluck::kBrightness, -1, -1 }, { 1, 0, 0 } },
        { "click", { pluck::kExciter, -1, -1 }, { 1, 0, 0 } },
        { "saw", { pluck::kExciter, -1, -1 }, { 2, 0, 0 } },
        { "mallet", { pluck::kExciter, -1, -1 }, { 3, 0, 0 } },
        { "pick 0", { pluck::kPickPos, -1, -1 }, { 0, 0, 0 } },
        { "pick 1", { pluck::kPickPos, -1, -1 }, { 1, 0, 0 } },
        { "stretch .3", { pluck::kStretch, -1, -1 }, { 0.3f, 0, 0 } },
        { "stretch 1", { pluck::kStretch, -1, -1 }, { 1, 0, 0 } },
        { "damp 0", { pluck::kDamping, -1, -1 }, { 0, 0, 0 } },
        { "damp .6", { pluck::kDamping, -1, -1 }, { 0.6f, 0, 0 } },
        { "tune+fine", { pluck::kTune, pluck::kFine, -1 }, { 3, -37, 0 } },
    };
    for (const Variant& var : variants)
    {
        Patch p = initPatch();
        for (int k = 0; k < 3; k++) if (var.param[k] >= 0) p.v[var.param[k]] = var.val[k];
        const double offset = (var.param[0] == pluck::kTune) ? 3.0 - 0.37 : 0.0;
        double worst = 0; int worstNote = 0;
        double lowErr = 0, highErr = 0;
        for (int note = 28; note <= 100; note++)
        {
            const double target = hz(note + offset);
            const double periods = 20.0;
            const size_t win = std::max<size_t>(size_t(0.3 * kSr), size_t(periods * kSr / target));
            Result r = render(p, { mk(0.0, 5.0, note) }, 0.06 + double(win) / kSr + 0.05);
            const size_t from = size_t(0.04 * kSr);
            double e = pitchErrorCents(r.out, from, std::min(win, r.out.size() - from), target);
            if (std::fabs(e) > std::fabs(worst)) { worst = e; worstNote = note; }
            if (note == 28) lowErr = e;
            if (note == 100) highErr = e;
        }
        char d[160];
        snprintf(d, sizeof(d), "%-12s worst %+6.2f cents at note %d (E1 %+5.2f, E7 %+5.2f)", var.name, worst, worstNote, lowErr, highErr);
        check(std::fabs(worst) <= 5.0, "pitch within 5 cents, E1 to E7:", d);
    }

    // Decay: the fundamental falls by 60 dB in the Damping time on every key (Damping 0.35: 2.64 s), measured from two windows
    {
        Patch p = initPatch();
        const double expect = 15.0 * std::exp(-5.01063 * 0.35);
        double lo = 1e9, hi = 0; int loN = 0, hiN = 0;
        printf("  decay of the fundamental, Damping 35 %% (expected T60 %.2f s):\n  ", expect);
        for (int note = 28; note <= 96; note += 4)
        {
            const double f = hz(note);
            Result r = render(p, { mk(0.0, 9.0, note) }, 2.0);
            const size_t n1 = size_t(0.30 * kSr), t1 = size_t(0.10 * kSr), t2 = size_t(0.90 * kSr);
            const double a1 = dftMag(r.out, t1, n1, f * std::pow(2.0, pitchErrorCents(r.out, t1, n1, f) / 1200.0));
            const double a2 = dftMag(r.out, t2, n1, f * std::pow(2.0, pitchErrorCents(r.out, t2, n1, f) / 1200.0));
            const double t60 = 60.0 * (double(t2 - t1) / kSr) / (20.0 * std::log10(a1 / a2));
            printf("%d:%.2f ", note, t60);
            if (t60 < lo) { lo = t60; loN = note; }
            if (t60 > hi) { hi = t60; hiN = note; }
        }
        printf("\n");
        char d[160];
        snprintf(d, sizeof(d), "T60 %.2f s (note %d) to %.2f s (note %d), expected %.2f s", lo, loN, hi, hiN, expect);
        check(lo > expect * 0.8 && hi < expect * 1.25, "damping even across the keyboard (within -20 / +25 %):", d);
    }

    // Loudness per Exciter (RMS of the first 0.5 s, default patch, mean over the arpeggio notes), peak and clipping
    {
        printf("  loudness by exciter (RMS dBFS of the first 0.5 s, then peak), notes E1 C3 C4 C5 C6:\n");
        double meanDb[4]; bool clip = false, fin = true;
        for (int e = 0; e < 4; e++)
        {
            Patch p = initPatch(); p.v[pluck::kExciter] = float(e);
            double sum = 0; int cnt = 0;
            printf("    %-7s", kExcName[e]);
            for (int note : { 28, 48, 60, 72, 84 })
            {
                Result r = render(p, { mk(0.0, 5.0, note) }, 0.5);
                double s = 0; float pk = 0;
                for (float v : r.out) { s += double(v) * v; pk = std::max(pk, std::fabs(v)); }
                const double db = 10.0 * std::log10(s / double(r.out.size()) + 1e-12);
                printf(" %6.1f/%.2f", db, pk);
                sum += db; cnt++;
                clip = clip || pk > 0.98f; fin = fin && r.finite;
            }
            meanDb[e] = sum / cnt;
            printf("   mean %.1f\n", meanDb[e]);
        }
        double mn = 0, mx = -1e9;
        for (int e = 0; e < 4; e++) { mx = std::max(mx, meanDb[e]); mn = std::min(mn, meanDb[e]); }
        mn = *std::min_element(meanDb, meanDb + 4);
        char d[96]; snprintf(d, sizeof(d), "spread %.1f dB", mx - mn);
        check(mx - mn <= 9.0, "exciters within 9 dB of each other:", d);
        check(!clip, "no clipping at the default patch");
        check(fin, "output finite and mono");
    }

    // An idle voice ends: a held note on a short Damping stops rendering on its own, so the shim's partIdle path can see silence
    {
        Patch p = initPatch(); p.v[pluck::kDamping] = 0.9f;
        Result r = render(p, { mk(0.0, 9.0, 60) }, 3.0);
        const int blocks = int(3.0 * kSr / kBlock);
        char d[96]; snprintf(d, sizeof(d), "alive %d of %d blocks (held, Damping 90 %%)", r.blocksAlive, blocks);
        check(r.blocksAlive < blocks / 2, "voice frees itself once silent:", d);
        Result r2 = render(initPatch(), { mk(0.0, 0.2, 60) }, 4.0);
        snprintf(d, sizeof(d), "alive %d of %d blocks (note off at 0.2 s, Release 150 ms)", r2.blocksAlive, int(4.0 * kSr / kBlock));
        check(r2.blocksAlive < int(0.9 * kSr / kBlock), "note off damps the string and ends it:", d);
    }
}
} // namespace

int main(int argc, char** argv)
{
    if (argc > 1 && !strcmp(argv[1], "--check")) { runChecks(); printf("\n%s\n", gFail ? "FAILED" : "all checks passed"); return gFail ? 1 : 0; }
    gDir = argc > 1 ? argv[1] : "build/pluck-wav";

    for (int e = 0; e < 4; e++)
    {
        Patch p = initPatch(); p.v[pluck::kExciter] = float(e);
        emit(std::string("10_arp_") + kExcName[e], render(p, arpeggio(0.3, 0.25), 0.05 + 16 * 0.3 + 3.0).out);
    }
    {
        Patch p = initPatch(); p.v[pluck::kDamping] = 0.85f;
        emit("20_damping_short", render(p, arpeggio(0.3, 0.25), 0.05 + 16 * 0.3 + 1.0).out);
        p.v[pluck::kDamping] = 0.05f;
        emit("20_damping_long", render(p, arpeggio(0.3, 0.25), 0.05 + 16 * 0.3 + 6.0).out);
    }
    {
        Patch p = initPatch();
        emit("30_stretch", sweepPluck(p, pluck::kStretch, 0, 1, 52, 8, 1.0, 0.9, 2.5));
        p.v[pluck::kExciter] = 1; p.v[pluck::kBrightness] = 1; p.v[pluck::kDamping] = 0.15f;
        emit("31_stretch_bell", sweepPluck(p, pluck::kStretch, 0, 1, 60, 8, 1.6, 1.5, 3.0));
    }
    {
        Patch p = initPatch();
        emit("40_pickpos", sweepPluck(p, pluck::kPickPos, 0, 1, 45, 8, 1.0, 0.9, 2.0));
        emit("50_brightness", sweepPluck(p, pluck::kBrightness, 0, 1, 45, 8, 1.0, 0.9, 2.0));
    }
    {
        std::vector<Note> n;
        for (int i = 0; i < 5; i++) n.push_back(mk(0.05 + i * 1.2, 1.0, 57, 0.2f + 0.2f * float(i)));
        emit("60_velocity", render(initPatch(), n, 0.05 + 5 * 1.2 + 1.5).out);
        n.clear();
        const float rel[] = { 0.02f, 0.15f, 0.6f, 2.0f };
        for (int i = 0; i < 4; i++) { Patch q = initPatch(); q.v[pluck::kRelease] = rel[i]; q.v[pluck::kDamping] = 0.0f; n.push_back(mkp(0.05 + i * 3.0, 0.3, 57, q)); }
        emit("70_release", render(initPatch(), n, 0.05 + 4 * 3.0).out);
        Patch c = initPatch(); c.v[pluck::kExciter] = 3; c.v[pluck::kDamping] = 0.2f;
        emit("80_chord", render(c, { mk(0.05, 4, 45), mk(0.07, 4, 52), mk(0.09, 4, 57), mk(0.11, 4, 61) }, 5.0).out);
    }
    {
        FILE* f = nullptr; (void)f;
        writeWav(gDir + "/00_all.wav", gAll);
        printf("%-26s %5.1f s\n", "00_all.wav", double(gAll.size()) / kRate);
    }
    runChecks();
    printf("\n%s\n", gFail ? "FAILED" : "all checks passed");
    return gFail ? 1 : 0;
}
