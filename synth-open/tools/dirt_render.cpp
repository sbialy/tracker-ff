// Host render of the live Dirt insert (synth-open/ff_dirt.cpp, the same per block code as the firmware, plain float fallbacks for the
// ARM asm). Builds a short synthetic loop (kick, snare, hats, bass, chord pad, about -3 dBFS peak), runs it dry and through each
// preset of ffdirt::kPresets and writes 16 bit 44.1 kHz stereo WAV files, then runs a few self checks.
//   synth-open/tools/dirt_render.sh [outdir]      default build/dirt-wav
//   dirt_render --check in.bin out.bin <preset> <blocks>   compares the output of the ARM bench (check_arm.py) with the host code
#include "ff_dirt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

using namespace ffdirt;

static const double kRate = 44100.0;

static void writeWav(const char* path, const std::vector<int16_t>& l, const std::vector<int16_t>& r)
{
    FILE* f = fopen(path, "wb");
    if (!f) { perror(path); exit(1); }
    const uint32_t n = (uint32_t)l.size(), bytes = n * 4, rate = 44100;
    const uint16_t ch = 2, bits = 16, align = 4, fmt = 1;
    const uint32_t riff = 36 + bytes, fmtLen = 16, byteRate = rate * 4;
    fwrite("RIFF", 1, 4, f); fwrite(&riff, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f); fwrite(&fmtLen, 4, 1, f);
    fwrite(&fmt, 2, 1, f); fwrite(&ch, 2, 1, f); fwrite(&rate, 4, 1, f); fwrite(&byteRate, 4, 1, f);
    fwrite(&align, 2, 1, f); fwrite(&bits, 2, 1, f); fwrite("data", 1, 4, f); fwrite(&bytes, 4, 1, f);
    for (uint32_t i = 0; i < n; i++) { fwrite(&l[i], 2, 1, f); fwrite(&r[i], 2, 1, f); }
    fclose(f);
}

static uint32_t gSeed = 12345;
static float noise() { gSeed = gSeed * 1664525u + 1013904223u; return (float)(int32_t)gSeed / 2147483648.0f; }

// 4 bars at 120 BPM, a master bus mix before any limiting
static void makeLoop(std::vector<float>& L, std::vector<float>& R)
{
    const int beat = (int)(kRate * 0.5), n = beat * 16;
    L.assign(n, 0.0f); R.assign(n, 0.0f);
    static const int bassNote[8] = {0, 0, 3, 0, 5, 5, 3, 7};   // semitones above A1, one per half bar
    for (int b = 0; b < 16; b++)
    {
        const int t0 = b * beat;
        // kick every beat: a sine sweep 120 -> 45 Hz
        for (int i = 0; i < beat; i++)
        {
            const double t = i / kRate;
            const double ph = 2 * M_PI * (45.0 * t + 75.0 * (1 - exp(-t * 28.0)) / 28.0);
            const float v = (float)(sin(ph) * exp(-t * 9.0)) * 0.9f;
            L[t0 + i] += v; R[t0 + i] += v;
        }
        // snare on 2 and 4: noise + 190 Hz body
        if (b & 1)
            for (int i = 0; i < beat; i++)
            {
                const double t = i / kRate;
                const float v = (float)((noise() * 0.5 + sin(2 * M_PI * 190.0 * t) * 0.4) * exp(-t * 18.0)) * 0.6f;
                L[t0 + i] += v; R[t0 + i] += v * 0.9f;
            }
        // hats on the 8ths
        for (int h = 0; h < 2; h++)
            for (int i = 0; i < (int)(kRate * 0.04); i++)
            {
                const double t = i / kRate;
                float v = noise() * (float)exp(-t * 90.0) * 0.18f;
                static float prev;
                const float hp = v - prev; prev = v;   // crude high-pass
                L[t0 + h * beat / 2 + i] += hp * 0.7f; R[t0 + h * beat / 2 + i] += hp;
            }
    }
    // bass: saw, one note per half bar
    for (int s = 0; s < 8; s++)
    {
        const double f = 55.0 * pow(2.0, bassNote[s] / 12.0);
        const int t0 = s * beat * 2;
        double ph = 0;
        for (int i = 0; i < beat * 2; i++)
        {
            ph += f / kRate; if (ph >= 1) ph -= 1;
            const double env = exp(-(i / kRate) * 2.2) * (i < 200 ? i / 200.0 : 1.0);
            const float v = (float)((2 * ph - 1) * env) * 0.35f;
            L[t0 + i] += v; R[t0 + i] += v;
        }
    }
    // pad: A minor chord of detuned saws over the whole loop, a bit wider on the right
    static const double cf[3] = {220.0, 261.63, 329.63};
    for (int c = 0; c < 3; c++)
        for (int side = 0; side < 2; side++)
        {
            double ph = 0; const double f = cf[c] * (side ? 1.004 : 0.996);
            for (int i = 0; i < n; i++)
            {
                ph += f / kRate; if (ph >= 1) ph -= 1;
                (side ? R : L)[i] += (float)(2 * ph - 1) * 0.07f;
            }
        }
    float pk = 0;
    for (int i = 0; i < n; i++) pk = fmaxf(pk, fmaxf(fabsf(L[i]), fabsf(R[i])));
    const float g = 0.7f / pk;   // about -3 dBFS
    for (int i = 0; i < n; i++) { L[i] *= g; R[i] *= g; }
}

static int16_t s16(float x)
{
    x *= 32768.0f; x = x < -32768.0f ? -32768.0f : (x > 32767.0f ? 32767.0f : x);
    return (int16_t)(int32_t)x;
}

// the wet schedule of AudioEffectDirt: 0 -> 1 across the first block, then 1
static void renderPreset(const Preset& p, const std::vector<int16_t>& inL, const std::vector<int16_t>& inR,
                         std::vector<int16_t>& outL, std::vector<int16_t>& outR)
{
    Coef c; makeCoef(c, p);
    State st; st.reset();
    outL = inL; outR = inR;
    const size_t blocks = inL.size() / kBlock;
    for (size_t b = 0; b < blocks; b++)
        process(c, st, &outL[b * kBlock], &outR[b * kBlock], b == 0 ? 0.0f : 1.0f, 1.0f);
}

static int checks(const std::vector<int16_t>& inL, const std::vector<int16_t>& inR)
{
    int bad = 0;
    for (int pi = 0; pi < kPresetCount; pi++)
    {
        Coef c; makeCoef(c, kPresets[pi]);
        // 1. wet 0 is the input, bit for bit, through all state
        State st; st.reset();
        std::vector<int16_t> l = inL, r = inR;
        for (size_t b = 0; b + 1 < l.size() / kBlock; b++) process(c, st, &l[b * kBlock], &r[b * kBlock], 0.0f, 0.0f);
        size_t diff = 0;
        for (size_t i = 0; i < (l.size() / kBlock - 1) * kBlock; i++) diff += l[i] != inL[i] || r[i] != inR[i];
        printf("check %-10s wet 0 passes the input untouched: %s\n", kPresets[pi].name, diff ? "FAIL" : "ok");
        bad += diff != 0;
        // 2. full scale square + silence: no NaN, the output stays inside int16, state decays to a finite value
        st.reset();
        int16_t sl[kBlock], sr[kBlock];
        for (int blk = 0; blk < 200; blk++)
        {
            for (int i = 0; i < kBlock; i++) { sl[i] = (blk < 100) ? ((i & 8) ? 32767 : -32768) : 0; sr[i] = sl[i]; }
            process(c, st, sl, sr, 1.0f, 1.0f);
        }
        const bool fin = isfinite(st.lp[0]) && isfinite(st.hold[0]) && isfinite(st.dc[0]);
        printf("check %-10s full scale square then silence, finite state: %s (hold %.4f lp %.5f dc %.5f)\n", kPresets[pi].name,
               fin ? "ok" : "FAIL", st.hold[0], st.lp[0], st.dc[0]);
        bad += !fin;
        // 3. zero in, zero out (the quantiser is mid-tread: it has a level at 0)
        st.reset();
        memset(sl, 0, sizeof(sl)); memset(sr, 0, sizeof(sr));
        process(c, st, sl, sr, 1.0f, 1.0f);
        int nz = 0; for (int i = 0; i < kBlock; i++) nz += sl[i] != 0 || sr[i] != 0;
        printf("check %-10s silence in: %d non zero samples out\n", kPresets[pi].name, nz);
        if (nz) bad++;
        // 3b. zero in, exact zero out for every Bits setting 4..16 (mid-tread), through a few blocks and a state that held sound first
        for (int bits = 4; bits <= 16; bits++)
        {
            Preset q = kPresets[pi]; q.bits = bits;
            Coef qc; makeCoef(qc, q); State qs; qs.reset();
            int z = 0;
            for (int blk = 0; blk < 3; blk++)
            {
                memset(sl, 0, sizeof(sl)); memset(sr, 0, sizeof(sr));
                process(qc, qs, sl, sr, 1.0f, 1.0f);
                for (int i = 0; i < kBlock; i++) z += sl[i] != 0 || sr[i] != 0;
            }
            if (z) { printf("check %-10s Bits %d: zero in, %d non zero samples out FAIL\n", kPresets[pi].name, bits, z); bad++; }
        }
    }
    return bad;
}

int main(int argc, char** argv)
{
    if (argc >= 6 && !strcmp(argv[1], "--check"))
    {
        // in.bin: 2 x 128 int16 (the bench pattern), out.bin: 2 x 128 int16 after <blocks> blocks of the ARM build
        FILE* fi = fopen(argv[2], "rb"); FILE* fo = fopen(argv[3], "rb");
        if (!fi || !fo) { fprintf(stderr, "cannot open\n"); return 2; }
        int16_t in[2][kBlock], out[2][kBlock], mine[2][kBlock];
        if (fread(in, 2, 2 * kBlock, fi) != 2 * kBlock || fread(out, 2, 2 * kBlock, fo) != 2 * kBlock) return 2;
        const int pi = atoi(argv[4]) - 1, blocks = atoi(argv[5]);
        Coef c; makeCoef(c, kPresets[pi]); State st; st.reset();
        for (int b = 0; b < blocks; b++)
        {
            memcpy(mine, in, sizeof(in));
            process(c, st, mine[0], mine[1], b == 0 ? 0.0f : 1.0f, 1.0f);
        }
        int maxd = 0, nd = 0;
        for (int ch = 0; ch < 2; ch++) for (int i = 0; i < kBlock; i++) { const int d = abs(mine[ch][i] - out[ch][i]); if (d > maxd) maxd = d; nd += d != 0; }
        printf("preset %d, %d blocks: ARM vs host max difference %d LSB, %d of %d samples differ\n", pi + 1, blocks, maxd, nd, 2 * kBlock);
        return maxd > 1 ? 1 : 0;
    }

    const char* dir = argc > 1 ? argv[1] : ".";
    std::vector<float> fl, fr;
    makeLoop(fl, fr);
    std::vector<int16_t> inL(fl.size()), inR(fl.size());
    for (size_t i = 0; i < fl.size(); i++) { inL[i] = s16(fl[i]); inR[i] = s16(fr[i]); }
    inL.resize(inL.size() / kBlock * kBlock); inR.resize(inL.size());
    char path[1024];
    snprintf(path, sizeof(path), "%s/dirt_00_dry.wav", dir);
    writeWav(path, inL, inR);
    printf("%-34s\n", path);
    for (int pi = 0; pi < kPresetCount; pi++)
    {
        std::vector<int16_t> oL, oR;
        renderPreset(kPresets[pi], inL, inR, oL, oR);
        int pk = 0; double e = 0, ei = 0;
        for (size_t i = 0; i < oL.size(); i++) { pk = abs(oL[i]) > pk ? abs(oL[i]) : pk; e += (double)oL[i] * oL[i]; ei += (double)inL[i] * inL[i]; }
        snprintf(path, sizeof(path), "%s/dirt_%02d_%s.wav", dir, pi + 1, kPresets[pi].name + 5);
        for (char* q = path + strlen(dir); *q; q++) if (*q == ' ') *q = '_';
        writeWav(path, oL, oR);
        printf("%-60s peak %5d rms %.1f dB (dry %.1f dB)\n", path, pk, 10 * log10(e / oL.size() / 1073741824.0), 10 * log10(ei / oL.size() / 1073741824.0));
    }
    return checks(inL, inR) ? 1 : 0;
}
