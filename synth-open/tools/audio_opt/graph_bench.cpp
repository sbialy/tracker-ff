// Audio graph equivalence and cost bench (NOT firmware). Host side: run_graph.sh / run_graph.py (Unicorn, Cortex-M4F).
//
// The real library objects of the original Tracker's audio graph, compiled from a source tree (the working tree or a git snapshot)
// with the firmware's compiler and flags: AudioStream, AudioBitDepth, AudioFilterStateVariable, AudioAmplifier, MonoToStereo,
// AudioAnalyzeRMS, AudioMixer<10/11/4/7>, AudioEffectShortDelay, AudioEffectPolyverb, AudioEffectLimiter, AudioMux, AudioOutputStream,
// AudioRecordQueue (the export taps). Wiring and construction order follow core/AudioEngine (AudioVoice.cpp, MixerAndFxs.cpp,
// AudioOutput.cpp, mtAudioEngine.cpp connectVoiceTo*, connectMixerTo*). Only the voice SOURCE is a stand-in for AudioPlayMemory: its idle
// branch is the one of AudioPlayMemory::envelopeUpdate (effect_envelope.cpp), which is all an idle voice does. -DIDLE_NULL = an idle
// voice transmits nothing, without it a zero block (stock).
//
// A deterministic script (LCG, no host randomness) switches voices on and off, changes gains, filter, overdrive, bit depth, pan, send
// levels, delay and reverb settings while voices sound and while they are idle, toggles the limiter sidechain and the export taps.
// Everything that leaves the graph is logged: the output stream buffers (gOutLog), a hash of every block drained from an export queue
// (gQBlockHash), the meters (gMeter). The host compares the logs of two builds byte for byte.
#ifdef BENCH_DEBUG
#define private public // debug builds read private state
#endif
#ifndef NARROW_SENDS
#include "MixerAndFxs.h" // only the constants (kSendsInputsCount, kFirstSynthSendInput), none of its code is linked
#else
#include "effect_polyverb.h"
#endif
#include "AudioStream.h"
#include "amplifier.h"
#include "analyze_rms.h"
#include "cr_section_macros.h"
#include "effect_bitDepth.h"
#include "effect_limiter.h"
#include "effect_shortDelay.h"
#include "filter_variable.h"
#include "mixer.h"
#include "monoToStereo.h"
#include "mtStructs.h"
#include "mux.h"
#include "Output.h"
#include "record_queue.h"

strMtProject mtProject;

// ---- link stubs: what the firmware provides elsewhere and the bench never calls
void disableAudioInterrupt() {}
void enableAudioInterrupt() {}
char sequencer[8192] __attribute__((aligned(8)));                  // Sequencer, only AudioEffectShortDelay::setRate (never called) reads it
extern "C"
{
    float _ZN9Sequencer15getCurrentTempoEv() { return 120.0f; }  // Sequencer::getCurrentTempo()
    void* __dso_handle;
    void _exit(int) { for (;;) {} }
    int _getpid() { return 1; }
    int _kill(int, int) { return -1; }
    void* _sbrk(int n)
    {
        static char heap[65536] __attribute__((aligned(8)));
        static unsigned used;
        if (used + n > sizeof(heap))
            return (void*)-1;
        void* p = heap + used;
        used += n;
        return p;
    }
}
// the firmware's SDRAM objects (Tracker.cpp)
__attribute__((section(".sdram"), aligned(4))) int16_t externalRamDelayBuff[3 * MAX_DELAYLINE_LENGTH];

namespace
{
constexpr int kVoices = 8;
constexpr int kBlocksMax = 3072;
constexpr int kStemQueues = 6 + 2 * kVoices;

struct SrcVoice : public AudioStream
{
    SrcVoice()
      : AudioStream(0, NULL)
    {
    }
    bool active = false;
    int32_t amp = 0;
    uint32_t rng = 1;
    void update() override
    {
        if (active)
        {
            audio_block_t* b = allocate();
            if (!b)
                return;
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
            {
                rng = rng * 1664525u + 1013904223u;
                int32_t n = (int32_t)(rng >> 16) - 32768; // noise
                int32_t saw = ((i * 509 + (int32_t)(rng & 0xff)) & 0xffff) - 32768;
                b->data[i] = (int16_t)(((n >> 2) + (saw >> 1)) * amp >> 15);
            }
            transmit(b);
            release(b);
            return;
        }
        // AudioPlayMemory::envelopeUpdate with an idle envelope (effect_envelope.cpp)
#ifdef IDLE_NULL
        return;
#else
        audio_block_t* b = allocate();
        if (!b)
            return;
        memset(b->data, 0, AUDIO_BLOCK_SAMPLES * 2);
        transmit(b);
        release(b);
#endif
    }
};

alignas(8) audio_block_t gPool[70]; // AudioMemory(70)

struct Voice
{
    SrcVoice src;
    AudioBitDepth bitDepth;
    AudioFilterStateVariable filter;
    AudioAmplifier amp;
    MonoToStereo m2s;
    AudioAnalyzeRMS rms;
};
Voice gV[kVoices];
#ifdef NARROW_SENDS
constexpr int kSendsIn = 2 + kVoices; // the width without synth send inputs (before the MK66 send fix)
#else
constexpr int kSendsIn = MixerAndFxs<kVoices>::kSendsInputsCount; // the firmware's width
#endif
#ifndef NARROW_SENDS
#ifdef OLD_SEND_INDEX
constexpr int kFirstSynthSend = 2 + 2 * kVoices; // negative control: the old Tracker+ layout index against the MK66 mixer
#else
constexpr int kFirstSynthSend = MixerAndFxs<kVoices>::kFirstSynthSendInput;
#endif
#endif
// stand-in for the synth part send outputs (PolySynth kSend1/2ChannelOffset): transmits nothing unless sc 7 turns it on
struct SynthSend : public AudioStream
{
    SynthSend()
      : AudioStream(0, NULL)
    {
    }
    bool active = false;
    uint32_t rng = 99;
    void update() override
    {
        if (!active)
            return;
        audio_block_t* b = allocate();
        if (!b)
            return;
        for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
        {
            rng = rng * 1664525u + 1013904223u;
            b->data[i] = (int16_t)((int32_t)(rng >> 16) - 32768) >> 2;
        }
        transmit(b);
        release(b);
    }
};
SynthSend gSynthSend[kSynthParts][2]; // [part][0 delay, 1 reverb]
struct ConnPeek : public AudioConnection
{
    using AudioConnection::isConnected;
};
constexpr int kTracksIn = kVoices + kSynthParts; // kTracksAndSynthVoices
AudioMixer<kSendsIn> gPreFx[2];                  // [0] delay, [1] reverb
AudioMixer<kTracksIn> gTracks[2];
AudioMixer<4> gTs[2]; // dry, delay, reverb, input
AudioAnalyzeRMS gDelayRms[2], gRevRms[2], gDryRms[2];
AudioEffectShortDelay gDelay;
AudioEffectPolyverb gRev;
AudioMux gMux;
AudioEffectLimiter gLim[2];
AudioOutputStream gOut;
AudioMixer<7> gFin[2];
AudioAnalyzeRMS gMasterRms[2];
AudioRecordQueue gQMaster[2], gQRev[2], gQDelay[2], gQVoice[2][kVoices];

alignas(4) int16_t gOutBuf[2][AUDIO_BLOCK_SAMPLES];
AudioConnection* gC[400];
int gN;
void con(AudioStream* s, int si, AudioStream* d, int di)
{
    gC[gN++] = new AudioConnection(s, si, d, di);
}
void con(AudioStream* s, AudioStream* d)
{
    gC[gN++] = new AudioConnection(s, d);
}

AudioRecordQueue* gStem[kStemQueues];
int gScenario;
int gPpMode = -1;

uint32_t mix32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7feb352dU;
    x ^= x >> 15;
    x *= 0x846ca68bU;
    x ^= x >> 16;
    return x;
}
} // namespace

extern "C"
{
    int16_t gOutLog[kBlocksMax][2][AUDIO_BLOCK_SAMPLES];
    uint32_t gQBlockHash[kBlocksMax];
    uint32_t gDbg[8];
    uint8_t gMisc[kBlocksMax]; // per block: bit 0 delay isSilent()
    float gMeter[kBlocksMax / 8 + 1][24];
    uint32_t gStats[16]; // 0 blocks, 1 silent output blocks, 2 queue blocks drained, 3 memory_used, 4 memory_used_max
    uint32_t gBlock;

    extern void (*__init_array_start[])(void);
    extern void (*__init_array_end[])(void);

    void bench_setup(int sc)
    {
        static bool once;
        if (!once)
        {
            once = true;
            for (auto f = __init_array_start; f < __init_array_end; f++)
                (*f)();
        }
        // 10: scenario 0 with mono fixed, 11: scenario 0 with ping pong fixed (no mode switch at all), 20 + k: scenario k with ping pong fixed,
        // 30 + k: scenario k (steady) that asks for ping pong at block 20 (cycles of the mode switch, measure with warm-up 40 and 80)
        gPpMode = sc >= 30 ? 2 : sc >= 20 ? 1 : sc >= 10 ? sc - 10 : -1;
        if (sc >= 10)
            sc = sc >= 20 ? sc % 10 : 0;
        gScenario = sc;
        AudioStream::initialize_memory(gPool, 70);
        AudioFilterStateVariable::initCutoffLookup();

        for (int i = 0; i < kVoices; i++)
        {
            Voice& v = gV[i];
            v.src.rng = 12345u + 977u * i;
            con(&v.src, &v.bitDepth);
            con(&v.bitDepth, &v.filter);
            con(&v.filter, &v.amp);
            con(&v.amp, &v.m2s);
            con(&v.amp, &v.rms);
            con(&v.m2s, 0, &gTracks[0], i);
            con(&v.m2s, 1, &gTracks[1], i);
            con(&v.filter, 0, &gPreFx[1], i);
            con(&v.filter, 0, &gPreFx[0], i);
            con(&v.amp, 0, &gMux, i);
            con(&v.m2s, 0, &gQVoice[0][i], 0);
            con(&v.m2s, 1, &gQVoice[1][i], 0);
        }
#ifndef NARROW_SENDS
        // the synth sends exactly as connectSynthToMixer makes them, and a count of the ones AudioConnection::connect accepted
        gStats[8] = gStats[9] = 0;
        for (int p = 0; p < kSynthParts; p++)
        {
            con(&gSynthSend[p][0], 0, &gPreFx[0], kFirstSynthSend + p);
            gStats[8]++;
            gStats[9] += static_cast<ConnPeek*>(gC[gN - 1])->isConnected;
            con(&gSynthSend[p][1], 0, &gPreFx[1], kFirstSynthSend + p);
            gStats[8]++;
            gStats[9] += static_cast<ConnPeek*>(gC[gN - 1])->isConnected;
        }
#endif
        con(&gPreFx[1], 0, &gRev, 0);
        con(&gPreFx[1], 0, &gRev, 1);
        con(&gPreFx[0], &gDelay);
        for (int c = 0; c < 2; c++)
        {
            con(&gTracks[c], 0, &gTs[c], 0);
            con(&gDelay, c, &gTs[c], 1);
            con(&gRev, c, &gTs[c], 2);
            con(&gRev, c, &gRevRms[c], 0);
            con(&gDelay, c, &gDelayRms[c], 0);
            con(&gTracks[c], 0, &gDryRms[c], 0);
            con(&gMux, 0, &gLim[c], 1);
            con(&gTs[c], 0, &gLim[c], 0);
            con(&gLim[c], 0, &gFin[c], 0);
            con(&gFin[c], 0, &gOut, c);
            con(&gRev, c, &gQRev[c], 0);
            con(&gDelay, c, &gQDelay[c], 0);
            con(&gFin[c], &gQMaster[c]);
            con(&gFin[c], &gMasterRms[c]);
        }

        mtProject.values.limiterMode = limiterModeSubtle;
        gLim[0].begin();
        gLim[1].begin();
        gOut.setOutBuffers(gOutBuf[0], gOutBuf[1]);

        gDelay.setPingpongEnable(gPpMode == 1);
        gDelay.setSyncEnable(false);
        gDelay.setTime(sc == 1 ? 60.0f : 300.0f);
        gDelay.setFeedback(sc == 1 ? 0.2f : 0.45f);
        gDelay.setFilterFrequency(8000.0f);
        gDelay.setBuffer(externalRamDelayBuff);
        gDelay.unblockUpdate();

        gRev.reset();
        gRev.setTime(sc == 1 ? 0.1f : 0.7f);
        gRev.setDamp(0.2f);
        gRev.SetPredelayLength(0.0f);
        gRev.SetDiffusion(0.625f);
        gRev.unblockUpdate();

        int k = 0;
        gStem[k++] = &gQMaster[0];
        gStem[k++] = &gQMaster[1];
        gStem[k++] = &gQRev[0];
        gStem[k++] = &gQRev[1];
        gStem[k++] = &gQDelay[0];
        gStem[k++] = &gQDelay[1];
        for (int i = 0; i < kVoices; i++)
        {
            gStem[k++] = &gQVoice[0][i];
            gStem[k++] = &gQVoice[1][i];
        }

        // send levels: the firmware sets one gain per voice and fx (mtAudioEngine.cpp, sendsAmpLogValues)
        for (int i = 0; i < kVoices; i++)
        {
            float rs = (sc == 4 || sc == 6) ? 0.0f : ((i & 1) ? 0.35f : 0.0f);
            float ds = (sc == 4 || sc == 6) ? 0.0f : ((i & 2) ? 0.5f : 0.0f);
            gPreFx[1].gain(i, rs);
            gPreFx[0].gain(i, ds);
        }
        if (sc == 5)
        {
            for (int i = 0; i < kVoices; i++)
            {
                gV[i].filter.connect();
                gV[i].filter.setCutoff(0.5f);
                gPreFx[1].gain(i, 0.0f);
                gPreFx[0].gain(i, 0.0f);
            }
        }
#ifndef NARROW_SENDS
        if (sc == 7)
        {
            // three synth parts sounding with delay and reverb send up (what the fix makes audible), no track voice
            for (int p = 0; p < kSynthParts; p++)
            {
                gSynthSend[p][0].active = gSynthSend[p][1].active = true;
                gSynthSend[p][0].rng = 99 + p;
                gSynthSend[p][1].rng = 199 + p;
                gPreFx[0].gain(kFirstSynthSend + p, 0.4f);
                gPreFx[1].gain(kFirstSynthSend + p, 0.3f);
            }
        }
#endif
        if (sc == 3 || sc == 4)
        {
            for (int i = 0; i < 3; i++)
            {
                gV[i].src.active = true;
                gV[i].src.amp = 8000;
            }
            gPreFx[1].gain(1, sc == 4 ? 0.0f : 0.4f);
            gPreFx[0].gain(2, sc == 4 ? 0.0f : 0.5f);
        }
    }

    // the script: events before block b
    static void bench_script(int b)
    {
        const int sc = gScenario;
        if (sc == 6)
        {
            // loud bursts without fx sends: the tracks mixer and with it the limiter input end at once, so the limiter sees NULL while its
            // gain is still recovering. The sidechain is switched on and off during and after the silences
            const int ph = b % 400;
            for (int v = 0; v < kVoices; v++)
            {
                gV[v].src.active = ph < 40 && v < 4;
                gV[v].src.amp = (b >= 800 && b < 1000) ? 2500 : 30000; // 2500: a burst the limiter does not touch, g stays 1.0
            }
            // sidechain on before a burst, off right after it (the sidechain look-ahead delay still holds the burst), on again later
            if (b == 390 || b == 520 || b == 790 || b == 920 || b == 1190 || b == 1320 || b == 1590 || b == 1720)
            {
                gLim[0].setSidechainEnable(true);
                gLim[1].setSidechainEnable(true);
            }
            if (b == 440 || b == 600 || b == 840 || b == 1000 || b == 1240 || b == 1400 || b == 1640 || b == 1800)
            {
                gLim[0].setSidechainEnable(false);
                gLim[1].setSidechainEnable(false);
            }
            // a long release: the peak detector is still decaying through the silence while the gain is 1.0
            if (b == 790)
            {
                gLim[0].setRelease(500.0f);
                gLim[1].setRelease(500.0f);
            }
            if (b == 1250)
            {
                gLim[0].setRelease(0.512f);
                gLim[1].setRelease(0.512f);
            }
            if (b % 100 == 0)
                gMux.setOut((b / 100) % 4);
            if (b == 5)
            {
                for (int i = 0; i < kStemQueues; i++)
                    gStem[i]->begin();
            }
            return;
        }
        if (sc == 5)
        {
            for (int v = 0; v < kVoices; v++)
            {
                gV[v].src.active = b < 20; // one burst, then everything idle with the filters on and ringing
                gV[v].src.amp = 9000;
            }
            return;
        }
        if (gPpMode == 2 && b == 20)
            gDelay.setPingpongEnable(true);
        if (sc >= 2)
            return; // steady scenarios for the cycle numbers: no events
        const int phase = (sc == 1) ? (b % 700) : b;
        // voices on and off
        for (int v = 0; v < kVoices; v++)
        {
            SrcVoice& s = gV[v].src;
            uint32_t r = mix32((uint32_t)(b * 31 + v * 7919 + 1 + sc * 13));
            if (sc == 0)
            {
                if (b >= 1400 && b < 1700)
                {
                    s.active = false; // everything idle: tails ring out and (later) go quiet
                }
                else if (b >= 2000)
                {
                    s.active = false;
                }
                else if ((r % 48) == 0)
                {
                    s.active = !s.active;
                    static const int32_t amps[4] = { 600, 6000, 14000, 30000 };
                    s.amp = ((r >> 8) % 11 == 0) ? 0 : amps[(r >> 12) & 3]; // amp 0: active voice, zero block
                }
            }
            else
            {
                bool on = phase < 48 && v < 4;
                s.active = on;
                if (on && phase == 0)
                    s.amp = 9000 + 4000 * v;
            }
        }
        const int pv = (b / 23) % kVoices;
        if (b % 23 == 0)
        {
            static const float g[6] = { 0.0f, 0.2f, 1.0f, 0.0f, 0.5f, 1.0f };
            uint32_t r = mix32((uint32_t)(b * 5 + 3 + sc));
            gPreFx[1].gain(pv, g[r % 6]);
            gPreFx[0].gain(pv, g[(r >> 8) % 6]);
        }
        if (b % 37 == 0)
        {
            static const float g[6] = { 1.0f, 0.7f, 0.0f, 0.3f, 1.0f, 1.0f };
            uint32_t r = mix32((uint32_t)(b * 7 + 1 + sc));
            gV[(b / 37) % kVoices].amp.gain(g[r % 6]);
        }
        if (b % 41 == 0)
        {
            static const float g[3] = { 1.0f, 0.5f, 0.0f };
            uint32_t r = mix32((uint32_t)(b * 11 + 5 + sc));
            Voice& v = gV[(b / 41) % kVoices];
            v.m2s.gain(g[r % 3], MonoToStereo::left);
            v.m2s.gain(g[(r >> 8) % 3], MonoToStereo::right);
        }
        if (b % 29 == 0)
        {
            uint32_t r = mix32((uint32_t)(b * 13 + 9 + sc));
            Voice& v = gV[(b / 29) % kVoices];
            if (r % 3 == 0)
                v.filter.connect();
            else if (r % 3 == 1)
                v.filter.disconnect();
            v.filter.setCutoff(((r >> 4) % 100) / 100.0f);
            v.filter.resonance(0.7f + ((r >> 12) % 8) * 0.5f);
            v.filter.setType((r >> 16) % 3);
            v.filter.setOverdriveAmount(((r >> 20) % 4) == 0 ? 40 : 0);
            v.bitDepth.setBitDepth(((r >> 24) % 4) == 0 ? 8 : 16);
        }
        if (b % 53 == 0)
        {
            static const float g[4] = { 1.0f, 0.5f, 0.0f, 2.5f }; // 2.5: a boost, the sum clips
            uint32_t r = mix32((uint32_t)(b * 17 + 2 + sc));
            gTracks[(r >> 4) & 1].gain((r >> 8) % kVoices, g[r % 4]);
            gTs[(r >> 6) & 1].gain(1 + ((r >> 10) & 1), g[(r >> 12) & 3]);
            gFin[(r >> 14) & 1].gain(0, ((r >> 16) & 3) == 0 ? 0.9f : 1.0f);
        }
        if (sc == 0)
        {
            if (b == 100 || b == 1300)
            {
                gLim[0].setSidechainEnable(true);
                gLim[1].setSidechainEnable(true);
            }
            if (b == 900 || b == 1800)
            {
                gLim[0].setSidechainEnable(false);
                gLim[1].setSidechainEnable(false);
            }
            if (b % 211 == 0)
                gMux.setOut((b / 211) % kVoices);
            // the project's saturation type is set on the tracks and the tracks+sends mixers (mtAudioEngine.cpp, setSaturationType)
            if (b == 700 || b == 1300 || b == 1900 || b == 2400)
            {
                static const SaturationType t[4] = { SaturationType::kSaturate, SaturationType::kOverdrive, SaturationType::kSoftClip, SaturationType::kHardClip };
                const SaturationType st = t[(b == 700) ? 0 : (b == 1300) ? 1 : (b == 1900) ? 2 : 3];
                for (int c = 0; c < 2; c++)
                {
                    gTracks[c].setSaturationType(st);
                    gTs[c].setSaturationType(st);
                }
            }
            if (gPpMode < 0 && b == 300)
                gDelay.setPingpongEnable(true);
            if (gPpMode < 0 && (b == 700 || b == 1700))
                gDelay.setPingpongEnable(false);
            if (b == 500)
                gDelay.setTime(120.0f);
            if (b == 1000)
                gDelay.setTime(500.0f);
            if (b == 1100)
                gDelay.setFeedback(0.7f);
            if (b == 600)
                gRev.setTime(0.85f);
            if (b == 1100)
                gRev.setDamp(0.6f);
            if (b == 2000)
            {
                gRev.setTime(0.2f);
                gDelay.setFeedback(0.15f);
                gDelay.setTime(100.0f);
            }
            if (b == 200 || b == 1450)
            {
                for (int i = 0; i < 6 + 8; i++)
                    gStem[i]->begin(); // master, reverb, delay, voices 0..3
            }
            if (b == 1400)
            {
                for (int i = 0; i < kStemQueues; i++)
                    gStem[i]->end();
            }
        }
        else if (b == 5)
        {
            for (int i = 0; i < 6; i++)
                gStem[i]->begin();
        }
    }

    static inline uint32_t fnv(uint32_t h, uint32_t w)
    {
        return (h ^ w) * 16777619u;
    }

    static void bench_log(int b)
    {
        // output stream buffers
        bool silent = true;
        for (int c = 0; c < 2; c++)
        {
            memcpy(gOutLog[b][c], gOutBuf[c], sizeof(gOutBuf[c]));
            for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
                if (gOutBuf[c][i])
                    silent = false;
        }
        gStats[1] += silent;
        // export taps: drain every queue like the exporter does
        uint32_t h = 2166136261u;
        for (int q = 0; q < kStemQueues; q++)
        {
            while (gStem[q]->available())
            {
                const int16_t* d = gStem[q]->readBuffer();
                h = fnv(h, q);
                for (int i = 0; i < AUDIO_BLOCK_SAMPLES; i++)
                    h = fnv(h, (uint16_t)d[i]);
                gStem[q]->freeBuffer();
                gStats[2]++;
            }
        }
        gQBlockHash[b] = h;
        gMisc[b] = gDelay.isSilent() ? 1 : 0;
#ifdef BENCH_DEBUG
        gDbg[0] = gRev.reverb.quiet_samples_;
        gDbg[1] = gRev.reverb.lp_decay_0_ < 1e-30f && gRev.reverb.lp_decay_0_ > -1e-30f;
        gDbg[2] = gDelay.quietSamplesCount;
        gDbg[3] = gLim[0].relaxed;
#endif
        // meters, every 8 blocks
        if ((b & 7) == 0)
        {
            float* m = gMeter[b >> 3];
            int k = 0;
            for (int v = 0; v < kVoices; v++)
                m[k++] = gV[v].rms.available() ? gV[v].rms.read() : -1.0f;
            for (int c = 0; c < 2; c++)
            {
                m[k++] = gDelayRms[c].available() ? gDelayRms[c].read() : -1.0f;
                m[k++] = gRevRms[c].available() ? gRevRms[c].read() : -1.0f;
                m[k++] = gDryRms[c].available() ? gDryRms[c].read() : -1.0f;
                m[k++] = gMasterRms[c].available() ? gMasterRms[c].read() : -1.0f;
                m[k++] = gLim[c].isAvailableGainToGet() ? gLim[c].getAverageGainChange() : -1.0f;
            }
        }
    }

    void bench_run(int blocks)
    {
        for (int i = 0; i < blocks; i++)
        {
            int b = gBlock;
            if (b >= kBlocksMax)
                return;
            bench_script(b);
            AudioStream::update_all();
            bench_log(b);
            gBlock = b + 1;
            gStats[0] = gBlock;
            gStats[3] = AudioStream::memory_used;
            gStats[4] = AudioStream::memory_used_max;
        }
    }
}
