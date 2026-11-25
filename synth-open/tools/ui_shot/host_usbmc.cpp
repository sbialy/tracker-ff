// USB audio 8+3 mono on the host (ui_shot), host only, NOT firmware. The sink the MK66 platform gives the tap (ffUsbAudioMultiBlock)
// is replaced by hostUsbMultiSink: it runs the platform's own interleave (platforms/mk66/usbaudio/UsbAudioOut.cpp, included below,
// with its 2 block synth delay) on every block, as the device does while the computer records. A probe stream created after the tap
// (so it runs after it in the same update_all) reads where Song Stems taps (post pan) and the tracks + synth mixer, per block:
//   track c : USB channel c == the MonoToStereo L and R of track c in a block where L == R (centre pan, unity both sides). A block
//             with L != R is panned: there the USB channel must still carry the dry signal while the silent side is 0 (pre pan).
//   synth p : USB channel 8+p == (L + R) >> 1 of the part's panning amplifiers one block earlier (they run one block behind the synth)
//   bus     : with the mixer stage at Hard Clip, all tracks centre and mono parts: sat16(sum of the 11 USB channels) == the tracks
//             mixer L and R of the same block (the Tracker's own bus before the sends, inserts and limiter)
//   decode  : the 11 interleaved channels decode to the tap inputs (tracks now, parts two blocks back)
// "usbmc 1" attaches the probe and clears the counters, "usbmc dump" prints them, "usbmc sat N" sets the mixer stage,
// "usbmc ipan N V" the panning of instrument N.
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <sstream>
#include <string>
#define private public
#define protected public
#include "mtAudioEngine.h"
#undef protected
#undef private
#include "SynthVoice.h"
#include "host_platform.h"

#include "../../../platforms/mk66/usbaudio/UsbAudioOut.cpp"

namespace
{
constexpr int kIn = 14;
constexpr int kN = 128;
int16_t s_in[3][kIn][kN];   // tap inputs of this block [0] and the two before (null = 0)
int16_t s_ilv[11 * kN];
ff::usbaudio::MultiDelay s_dl;
bool s_fresh;
long s_sinkBlocks;

struct Counters
{
    long blocks = 0, sinkMissing = 0;
    long trackCmp[8] = {}, trackBad[8] = {}, trackLive[8] = {}, panned[8] = {}, pannedBad[8] = {};
    long synthCmp[3] = {}, synthBad[3] = {}, synthLive[3] = {};
    long busCmp = 0, busBad = 0, busLive = 0;
    long decodeBad = 0;
} c;

int16_t at(const int16_t* p, int f) { return p ? p[f] : 0; }

class Probe : public AudioStream
{
  public:
    // 0..7 MonoToStereo L, 8..15 MonoToStereo R, 16..18 part pan L, 19..21 part pan R, 22/23 tracks mixer L/R
    Probe() : AudioStream(24, q) {}
    audio_block_t* q[24];
    void update() override
    {
        audio_block_t* b[24];
        const int16_t* d[24];
        for (int i = 0; i < 24; i++)
        {
            b[i] = receiveReadOnly(i);
            d[i] = b[i] ? b[i]->data : nullptr;
        }
        check(d);
        for (int p = 0; p < 6; p++)
            for (int f = 0; f < kN; f++) panPrev[p][f] = at(d[16 + p], f);
        for (int i = 0; i < 24; i++)
            if (b[i]) release(b[i]);
    }
    int16_t panPrev[6][kN] = {};   // the panning amplifiers' output of the block before

  private:
    void check(const int16_t* const* d)
    {
        if (!s_fresh)
        {
            c.sinkMissing++;
            return;
        }
        s_fresh = false;
        c.blocks++;
        bool allCentre = true, monoParts = true;
        for (int t = 0; t < 8; t++)
        {
            bool centre = true, live = false, dry = true;
            for (int f = 0; f < kN; f++) centre = centre && at(d[t], f) == at(d[8 + t], f);
            bool bad = false;
            for (int f = 0; f < kN; f++)
            {
                const int16_t u = s_ilv[f * 11 + t];
                live = live || u != 0;
                if (centre) bad = bad || u != at(d[t], f);
                // panned: the USB channel is the dry signal, the louder side at most as loud (linear pan, centre unity)
                else dry = dry && std::abs(int(at(d[t], f))) <= std::abs(int(u)) + 1 && std::abs(int(at(d[8 + t], f))) <= std::abs(int(u)) + 1;
            }
            if (centre)
            {
                c.trackCmp[t]++;
                c.trackBad[t] += bad;
            }
            else
            {
                allCentre = false;
                c.panned[t]++;
                c.pannedBad[t] += !(dry && live);
            }
            c.trackLive[t] += live;
        }
        for (int p = 0; p < 3; p++)
        {
            c.synthCmp[p]++;
            bool bad = false, live = false;
            for (int f = 0; f < kN; f++)
            {
                const int16_t u = s_ilv[f * 11 + 8 + p];
                live = live || u != 0;
                bad = bad || u != int16_t((int32_t(panPrev[p][f]) + int32_t(panPrev[3 + p][f])) >> 1);
                monoParts = monoParts && s_in[2][8 + p][f] == s_in[2][11 + p][f];
            }
            c.synthBad[p] += bad;
            c.synthLive[p] += live;
        }
        if (allCentre && monoParts && mtProject.values.saturationType == kHardClip)
        {
            c.busCmp++;
            bool bad = false, live = false;
            for (int side = 0; side < 2; side++)
                for (int f = 0; f < kN; f++)
                {
                    int32_t sum = 0;
                    for (int k = 0; k < 11; k++) sum += s_ilv[f * 11 + k];
                    const int32_t sat = sum > 32767 ? 32767 : sum < -32768 ? -32768 : sum;
                    live = live || sat != 0;
                    bad = bad || sat != at(d[22 + side], f);
                }
            c.busBad += bad;
            c.busLive += live;
        }
        bool db = false;
        for (int f = 0; f < kN; f++)
        {
            for (int t = 0; t < 8; t++) db = db || s_ilv[f * 11 + t] != s_in[0][t][f];
            for (int p = 0; p < 3; p++)
                db = db || s_ilv[f * 11 + 8 + p] != int16_t((int32_t(s_in[2][8 + p][f]) + int32_t(s_in[2][11 + p][f])) >> 1);
        }
        c.decodeBad += db;
    }
};

Probe* s_probe;

void attach()
{
    if (s_probe) return;
    audioEngine& e = engine;
    s_probe = new Probe();
    for (int t = 0; t < 8; t++)
    {
#ifndef PLATFORM_IMX
        new AudioConnection(instrumentPlayer[t].getAudioVoice()->getMonoToStereo(), 0, s_probe, uint8_t(t));
        new AudioConnection(instrumentPlayer[t].getAudioVoice()->getMonoToStereo(), 1, s_probe, uint8_t(8 + t));
#else   // the Tracker+ voice is stereo (AudioVoice.h): its two panning amplifiers are the left and right outputs
        new AudioConnection(instrumentPlayer[t].getAudioVoice()->getPanningAmplifier(AudioVoice::kLeft), 0, s_probe, uint8_t(t));
        new AudioConnection(instrumentPlayer[t].getAudioVoice()->getPanningAmplifier(AudioVoice::kRight), 0, s_probe, uint8_t(8 + t));
#endif
    }
    for (int p = 0; p < 3; p++)
    {
        new AudioConnection(e.synthVoice.getPanningAmplifier(p, SynthVoice::Channels::kLeft), 0, s_probe, uint8_t(16 + p));
        new AudioConnection(e.synthVoice.getPanningAmplifier(p, SynthVoice::Channels::kRight), 0, s_probe, uint8_t(19 + p));
    }
    new AudioConnection(e.mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kLeft), 0, s_probe, 22);
    new AudioConnection(e.mixerAndFxs.getTracksMixer(MixerAndFxChannelType::kRight), 0, s_probe, 23);
}
}   // namespace

void hostUsbMultiSink(const int16_t* const* in)
{
    memmove(s_in[1], s_in[0], sizeof(s_in[0]) * 2);
    for (int i = 0; i < kIn; i++)
    {
        if (in[i]) memcpy(s_in[0][i], in[i], sizeof s_in[0][i]);
        else memset(s_in[0][i], 0, sizeof s_in[0][i]);
    }
    ff::usbaudio::interleaveMulti(s_ilv, in, s_dl);   // the platform's kernel, as ffUsbAudioMultiBlock calls it
    // negative controls of usb_multich_sim.sh: the synth delay off, one LSB on track 2
    static const bool noDelay = getenv("UISHOT_USBMC_NODELAY") != nullptr, lsb = getenv("UISHOT_USBMC_TRACKLSB") != nullptr;
    if (noDelay)
        for (int f = 0; f < kN; f++)
            for (int p = 0; p < 3; p++) s_ilv[f * 11 + 8 + p] = int16_t((int32_t(s_in[0][8 + p][f]) + int32_t(s_in[0][11 + p][f])) >> 1);
    if (lsb)
        for (int f = 0; f < kN; f++) s_ilv[f * 11 + 1] = int16_t(s_ilv[f * 11 + 1] + 1);
    s_fresh = true;
    s_sinkBlocks++;
}

void hostUsbMcCommand(const std::string& args)
{
    std::istringstream is(args);
    std::string a;
    is >> a;
    if (a == "1")
    {
        attach();
        c = Counters();
        s_fresh = false;
        return;
    }
    if (a == "sat")
    {   // sat N: the mixer stage (0 Hard Clip), as the project load sets it
        int n = 0;
        is >> n;
        mtProject.values.saturationType = uint8_t(n);
        engine.setSaturationType(SaturationType(n));
        return;
    }
    if (a == "ipan")
    {   // ipan N V: the panning of instrument N (0..100, 50 centre), taken at the next note-on
        int n = 0, v = 50;
        is >> n >> v;
        mtProject.instrument[n].panning = int16_t(v);
        return;
    }
    if (a == "dump")
    {
        printf("usbmc sink %ld blocks %ld sinkmissing %ld bus %ld bad %ld live %ld decodebad %ld\n", s_sinkBlocks, c.blocks, c.sinkMissing,
               c.busCmp, c.busBad, c.busLive, c.decodeBad);
        for (int t = 0; t < 8; t++)
            printf("usbmc track %d centre %ld bad %ld panned %ld bad %ld live %ld\n", t + 1, c.trackCmp[t], c.trackBad[t], c.panned[t],
                   c.pannedBad[t], c.trackLive[t]);
        for (int p = 0; p < 3; p++)
            printf("usbmc synth %d cmp %ld bad %ld live %ld\n", p + 1, c.synthCmp[p], c.synthBad[p], c.synthLive[p]);
        fflush(stdout);
    }
}
