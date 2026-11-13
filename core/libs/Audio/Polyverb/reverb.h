// Copyright 2014 Emilie Gillet.
//
// Author: Emilie Gillet (emilie.o.gillet@gmail.com)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//
// See http://creativecommons.org/licenses/MIT/ for more information.
//
// -----------------------------------------------------------------------------
//
// Adapted for Polyverb by Wojciech Jakóbczyk (jakobczyk.woj@gmail.com)
//
// -----------------------------------------------------------------------------
//
// Reverb, half rate.
//
// The Griesinger / Dattorro loop of the stock Polyverb, run at 22.05 kHz: the mono input sum is decimated by two with a 7 tap halfband
// ([-1 0 9 16 9 0 -1] / 32), the loop runs at half the sample rate with every line half as long (so the room size, the damping and the
// decay time stay the same), the wet pair is interpolated back to the full rate with the same halfband. The dry/wet mix stays at the
// full rate. Both line sets (7.8k and 5.5k samples) sit back to back in one 16 bit SRAM buffer of 13,728 samples (27,456 B, E2's ring is
// as long as its lines need, see kLen1 and kLen2), the SDRAM buffer is no longer touched. Nothing above about 10.5 kHz reaches the tail, the level, decay time and stereo width are the stock ones
// (measured against the stock reverb).

#ifndef POLYVERB_REVERB_H_
#define POLYVERB_REVERB_H_

#include "fx_engine.h"
#include "util.h"

// The two writes that carry all the feedback energy of the loop also feed the quiet detector (FEATURE_SYNTH_OPEN, see Process()). The
// stock build keeps the plain calls, so its code does not change.
#ifdef FEATURE_SYNTH_OPEN
#define REVERB_WRITE_TRACKED(c, d, s)  c.WriteQ(d, s)
#define REVERB_WRITE2_TRACKED(c, d, s) c.Write2Q(d, s)
#else
#define REVERB_WRITE_TRACKED(c, d, s)  c.Write(d, s)
#define REVERB_WRITE2_TRACKED(c, d, s) c.Write2(d, s)
#endif

class Reverb
{
  public:
    Reverb() {}
    ~Reverb() {}

    // buffer: the kBufferSamples x 16 bit SRAM line buffer (effect_polyverb.cpp). Both line sets live in it: E in the first kLen1
    // samples, E2 in the next kLen2.
    // buffer2 (the 16384 x 16 bit SDRAM buffer of the full rate reverb) is not used any more.
    void Init(uint16_t* buffer, uint16_t* buffer2)
    {
        (void)buffer2;
        engine_.Init(buffer, buffer + kLen1);
#ifdef FEATURE_SYNTH_OPEN
        quiet_samples_ = 0;
#endif
        // half the sample rate: the LFO advances twice as fast per reverb sample
        engine_.SetLFOFrequency(LFO_1, 1.0f / 32000.0f);
        engine_.SetLFOFrequency(LFO_2, 0.6f / 32000.0f);
        lp_ = 0.7f;
        diffusion_ = 0.625f;
        predelay_length_ = 0;
        ClearRate();
    }

    static constexpr size_t kBufferSamples = 8192 + 5536; // kLen1 + kLen2
    static constexpr size_t GetBufferSize() { return kBufferSamples * sizeof(uint16_t); }

    // size: full rate samples per block, even. In place: out = in + (wet - in) * amount.
    inline __attribute__((always_inline)) void Process(int16_t* left, int16_t* right, size_t size)
    {
        // The stock loop at half rate: every line (n + 1) / 2 long.
        typedef E::Reserve<
          75,
          E::Reserve<
            107,
            E::Reserve<
              160,
              E::Reserve<
                264,
                E::Reserve<
                  1091,
                  E::Reserve<1345, E::Reserve<2251, E::Reserve<predelay_buffer_size_>>>>>>>>
          Memory;
        typedef E::Reserve<1263, E::Reserve<1099, E::Reserve<3156>>> Memory2;
        E::DelayLine<Memory, 0> ap1;
        E::DelayLine<Memory, 1> ap2;
        E::DelayLine<Memory, 2> ap3;
        E::DelayLine<Memory, 3> ap4;
        E::DelayLine<Memory, 4> dap1a;
        E::DelayLine<Memory, 5> dap1b;
        E::DelayLine<Memory, 6> del1;
        E::DelayLine<Memory, 7> del0;
        E::DelayLine<Memory2, 0> dap2a;
        E::DelayLine<Memory2, 1> dap2b;
        E::DelayLine<Memory2, 2> del2;
        static_assert(decltype(del0)::base + decltype(del0)::length + 1 <= kLen1, "E ring too short for the lines");
        static_assert(decltype(del2)::base + decltype(del2)::length + 1 <= kLen2, "E2 ring too short for the lines");
        // Wrap() takes i < 2 n: the largest index is write_ptr_ (n - 1) + base + the largest integer offset of the modulated del2
        // read (3105.5 + 50, LFO value in 0..1) + 1 for the second tap of the interpolation
        static_assert(kLen2 - 1 + decltype(del2)::base + 3155 + 1 < 2 * kLen2, "E2 index beyond two ring lengths");
        E::Context c;
#ifdef FEATURE_SYNTH_OPEN
        c.nonzero_ = 0;
#endif

        const float kap = diffusion_;
        // one pole low pass: state += k (x - state), pole 1 - k per sample. At half rate the pole is squared.
        const float klp = 1.0f - (1.0f - lp_) * (1.0f - lp_);
        const float krt = reverb_time_;
        const float amount = amount_;
        const float gain = input_gain_;

        float lp_0 = lp_decay_0_;
        float lp_1 = lp_decay_1_;
        float lp_2 = lp_decay_2_;

        // halfband history, scalars so they stay in registers. Decimator: d0..d5 (full rate input sum, oldest first), interpolator
        // per channel: w0 w1 w2 (half rate wet, oldest first).
        float d0 = d0_, d1 = d1_, d2 = d2_, d3 = d3_, d4 = d4_, d5 = d5_;
        float wl0 = wl0_, wl1 = wl1_, wl2 = wl2_, wr0 = wr0_, wr1 = wr1_, wr2 = wr2_;

        const size_t ticks = size / 2;
        for (size_t k = 0; k < ticks; k++)
        {
            float wet;
            float apout = 0.0f;

            const float x0 = gain * ((float)left[2 * k] + (float)right[2 * k]) / 32768;
            const float x1 = gain * ((float)left[2 * k + 1] + (float)right[2 * k + 1]) / 32768;
            // 7 tap window d1 d2 d3 d4 d5 x0 x1 (oldest first), centre d4, the new history is d3 d4 d5 x0 x1
            const float y = (16.0f * d4 + 9.0f * (d3 + d5) - (d1 + x1)) * (1.0f / 32.0f);
            d0 = d2;
            d1 = d3;
            d2 = d4;
            d3 = d5;
            d4 = x0;
            d5 = x1;

            engine_.Start(&c);
            c.Load(y);
            c.Write(del0, 0.0f);
            c.Read(del0, predelay_length_, 1.0f);
            c.Lp(lp_0, klp);

            // Diffuse through 4 allpasses.
            c.Read(ap1 TAIL, kap);
            c.WriteAllPass(ap1, -kap);
            c.Read(ap2 TAIL, kap);
            c.WriteAllPass(ap2, -kap);
            c.Read(ap3 TAIL, kap);
            c.WriteAllPass(ap3, -kap);
            c.Read(ap4 TAIL, kap);
            c.WriteAllPass(ap4, -kap);
            c.Write(apout);

            // Main reverb loop.
            c.Load(apout);
            c.Interpolate2(del2, 3105.5f, LFO_2, 50.0f, krt);
            c.Lp(lp_1, klp);
            c.Read(dap1a TAIL, -kap);
            c.WriteAllPass(dap1a, kap);
            c.Read(dap1b TAIL, kap);
            c.WriteAllPass(dap1b, -kap);
            REVERB_WRITE_TRACKED(c, del1, 2.0f);
            c.Write(wet, 0.0f);
            {
                // interpolator: out even = w1, odd = between w1 and w2
                const float e = wl1;
                const float o = (9.0f * (wl1 + wl2) - (wl0 + wet)) * (1.0f / 16.0f);
                wl0 = wl1;
                wl1 = wl2;
                wl2 = wet;
                float delta = (e * 32768 - left[2 * k]) * amount;
                left[2 * k] += delta;
                delta = (o * 32768 - left[2 * k + 1]) * amount;
                left[2 * k + 1] += delta;
            }

            c.Load(apout);
            c.Read(del1 TAIL, krt);
            c.Lp(lp_2, klp);
            c.Read2(dap2a TAIL, kap);
            c.WriteAllPass2(dap2a, -kap);
            c.Read2(dap2b TAIL, -kap);
            c.WriteAllPass2(dap2b, kap);
            REVERB_WRITE2_TRACKED(c, del2, 2.0f);
            c.Write(wet, 0.0f);
            {
                const float e = wr1;
                const float o = (9.0f * (wr1 + wr2) - (wr0 + wet)) * (1.0f / 16.0f);
                wr0 = wr1;
                wr1 = wr2;
                wr2 = wet;
                float delta = (e * 32768 - right[2 * k]) * amount;
                right[2 * k] += delta;
                delta = (o * 32768 - right[2 * k + 1]) * amount;
                right[2 * k + 1] += delta;
            }
        }

        lp_decay_0_ = lp_0;
        lp_decay_1_ = lp_1;
        lp_decay_2_ = lp_2;
        d0_ = d0;
        d1_ = d1;
        d2_ = d2;
        d3_ = d3;
        d4_ = d4;
        d5_ = d5;
        wl0_ = wl0;
        wl1_ = wl1;
        wl2_ = wl2;
        wr0_ = wr0;
        wr1_ = wr1;
        wr2_ = wr2;
#ifdef FEATURE_SYNTH_OPEN
        // Quiet block = the two long lines of the feedback loop (del1, del2: all the recirculating energy passes through them, at the
        // latest after the longest line, 3156 samples) were written with values below 1 LSB of 16 bit (they compress to 0), and the three
        // low pass states are below 1 LSB. AudioEffectPolyverb::update() resets the count when the input has sound. kQuietSamples such
        // samples in a row (more than the longest line of the input path, predelay included, 2500 + 605 samples) and nothing above 1 LSB
        // is left to ring.
        constexpr float kTiny = 1.0f / 32768.0f;
        const bool tiny = lp_0 < kTiny && lp_0 > -kTiny && lp_1 < kTiny && lp_1 > -kTiny && lp_2 < kTiny && lp_2 > -kTiny;
        quiet_samples_ = (c.nonzero_ == 0 && tiny) ? std::min<uint32_t>(quiet_samples_ + ticks, kQuietSamples) : 0;
#endif
    }

#ifdef FEATURE_SYNTH_OPEN
    // true when the whole reverb state is below 1 LSB of 16 bit: a block of silent input then gives silent output. Process() can be
    // skipped, the output is zero (to within 1 LSB of the stock tail)
    inline bool IsQuiet() const { return quiet_samples_ >= kQuietSamples; }

    // a block with sound went in: the line buffers are about to fill
    inline void ClearQuiet() { quiet_samples_ = 0; }

    // The skipped block: the low pass states (below 1 LSB) and the halfband histories go to zero, the line buffers are already zero. The
    // write position and the LFO phase move as Process() would move them, so the next block with sound modulates as it would have.
    inline void SkipQuiet(size_t size)
    {
        engine_.Skip(size / 2);
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        ClearRate();
    }
#endif

    inline void SetAmount(float amount) { amount_ = amount; }

    inline void SetInputGain(float input_gain) { input_gain_ = input_gain; }

    inline void SetTime(float reverb_time) { reverb_time_ = reverb_time; }

    inline void SetDiffusion(float diffusion) { diffusion_ = diffusion; }

    inline void SetLP(float lp) { lp_ = lp; }

    inline void SetPredelayLength(float length)
    {
        predelay_length_ = length * predelay_buffer_size_;
    }

    inline void Reset()
    {
        engine_.Reset();
#ifdef FEATURE_SYNTH_OPEN
        quiet_samples_ = kQuietSamples;
#endif
        lp_decay_0_ = 0;
        lp_decay_1_ = 0;
        lp_decay_2_ = 0;
        ClearRate();
    }

    inline void SetActiveThreshold(float) {}

  private:
    // The rings. E: the lines of Memory end at 7800 (ap1 0, ap2 76, ap3 184, ap4 345, dap1a 610, dap1b 1702,
    // del1 3048, del0 5300 + 2500), 7801 cells with the one the predelay read at its full length touches. It stays a power of two: the
    // index wrap is then a mask (1 cycle), a ring of 7808 (the least that keeps the LFO phase, a multiple of 32) would free 768 B for
    // about 0.5 points. E2: dap2a 0, dap2b 1264, del2 2364 + 3156 = 5520, 5521 cells with the one the interpolated read of the modulated
    // del2 at its longest touches. Its wrap is a compare and a subtract (about 1.3 k cycles per block for 7 accesses per tick, 5.3 KB).
    // 5536 = 5521 rounded up to a multiple of 32: the compare constant is then one instruction immediate (58 cycles per block less
    // than 5521) and Wrap() keeps 16 cells of room for its i < 2 n rule (see the static_assert in Process()).
    static constexpr size_t kLen1 = 8192;
    static constexpr size_t kLen2 = 5536;
    static_assert(kBufferSamples == kLen1 + kLen2, "kBufferSamples follows the rings");
    typedef FxEngine<kLen1, FORMAT_16_BIT, kLen2> E;
    E engine_;

    static constexpr size_t predelay_buffer_size_ = 2500; // 5000 at the full rate
#ifdef FEATURE_SYNTH_OPEN
    static constexpr uint32_t kQuietSamples = 8192; // half a buffer of the full rate reverb: the longest line is 7,793 (E) and 5,518 (E2)
    uint32_t quiet_samples_ = 0;
#endif

    float amount_;

    float input_gain_;
    float reverb_time_;
    float diffusion_;
    float lp_;

    float lp_decay_0_;
    float lp_decay_1_;
    float lp_decay_2_;

    int predelay_length_;

    float d0_, d1_, d2_, d3_, d4_, d5_;
    float wl0_, wl1_, wl2_, wr0_, wr1_, wr2_;

    void ClearRate()
    {
        d0_ = d1_ = d2_ = d3_ = d4_ = d5_ = 0.0f;
        wl0_ = wl1_ = wl2_ = wr0_ = wr1_ = wr2_ = 0.0f;
    }

    DISALLOW_COPY_AND_ASSIGN(Reverb);
};

#endif // POLYVERB_REVERB_H_
