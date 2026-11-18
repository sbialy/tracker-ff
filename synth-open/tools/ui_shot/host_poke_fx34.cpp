// host_poke.cpp helper for fx34_play_sim.sh: what the playback of FX3 and FX4 (ranks 2 and 3, core/source/fx34.h) left in the players. Host only,
// NOT firmware. The private members of the engine and the sequencer are read through #define private public.
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <ranges>
#include <string>
#include <variant>
#define private public
#include "mtAudioEngine.h"
#include "mtSequencer.h"
#undef private
#include "fx34.h"
#include "mtStructs.h"

extern Sequencer sequencer;

// "fx34play ARGS", ARGS one of
//   player T            the sample player of track T: the FX it holds per rank, the hold mask and the value of a few parameters
//   synth P             the synth part P: the value of every source of the panning and the delay send, and the one that plays
//   slots T             the 4 FX ranks of the step track T started last (stepToSend)
//   custom T            Performance mode on, track T in custom play mode 15: the Performance FX of every step is a Roll (id 3)
extern "C" void hostPokeFx34(const char* args, std::string* out)
{
    char what[16] = {};
    long a = 0, b = 0, c = 0;
    sscanf(args, "%15s %ld %ld %ld", what, &a, &b, &c);
    char buf[600];
    if (!strcmp(what, "player"))
    {
        playerEngine& p = instrumentPlayer[a];
        const int pan = (int)playerEngine::parameterList::panning;
        const int dly = (int)playerEngine::parameterList::delaySend;
        const int vol = (int)playerEngine::parameterList::volume;
        const int cut = (int)playerEngine::parameterList::filterCutoff;
        snprintf(buf, sizeof buf,
                 "player %ld fx %u,%u,%u,%u val %u,%u,%u,%u mask pan %u dly %u vol %u cut %u rows pan %u/%u | pan %d dly %u vol %u cut %.3f",
                 a, p.lastSeqFx[0], p.lastSeqFx[1], p.lastSeqFx[2], p.lastSeqFx[3], p.lastSeqVal[0], p.lastSeqVal[1], p.lastSeqVal[2],
                 p.lastSeqVal[3], p.seqHoldMask[pan], p.seqHoldMask[dly], p.seqHoldMask[vol], p.seqHoldMask[cut],
                 p.trackControlParameter[(int)playerEngine::controlType::sequencerMode][pan],
                 p.trackControlParameter[(int)playerEngine::controlType::sequencerMode2][pan], (int)p.currentSeqModValues.panning,
                 (unsigned)p.currentSeqModValues.delaySend, (unsigned)p.currentSeqModValues.volume, (double)p.currentSeqModValues.filterCutoff);
    }
    else if (!strcmp(what, "synth"))
    {
#ifdef FEATURE_SYNTH
        audioEngine& e = engine;
        std::string l;
        for (int param : { (int)audioEngine::SynthParam::kPanning, (int)audioEngine::SynthParam::kDelaySend })
        {
            char t[200];
            snprintf(t, sizeof t, " p%d [", param);
            l += t;
            for (int src = 0; src < (int)audioEngine::SynthValuesSource::kSynthValuesSourceCount; src++)
            {
                const auto& v = e.synthParams[a][param][src];
                if (v.has_value()) snprintf(t, sizeof t, "%s%.1f", src ? " " : "", (double)*v);
                else snprintf(t, sizeof t, "%s-", src ? " " : "");
                l += t;
            }
            snprintf(t, sizeof t, "] = %.1f", (double)e.calculateParamValue((int)a, (audioEngine::SynthParam)param));
            l += t;
        }
        snprintf(buf, sizeof buf, "synth %ld%s ids %d,%d,%d,%d", a, l.c_str(), e.lastSynthFxId[a][0].value_or(0), e.lastSynthFxId[a][1].value_or(0),
                 e.lastSynthFxId[a][2].value_or(0), e.lastSynthFxId[a][3].value_or(0));
#else
        snprintf(buf, sizeof buf, "synth none");
#endif
    }
    else if (!strcmp(what, "slots"))
    {
        const auto& s = sequencer.player.track[a].stepToSend; // the step the track started last
        snprintf(buf, sizeof buf, "slots %ld %u:%u %u:%u %u:%u %u:%u", a, s.fx[0].type, s.fx[0].value, s.fx[1].type, s.fx[1].value, s.fx[2].type,
                 s.fx[2].value, s.fx[3].type, s.fx[3].value);
    }
    else if (!strcmp(what, "custom"))
    {   // custom play mode 15 (playModeDefs.h CS15) plays steps 1, 2, 3 ... and puts a Roll (3) on every step: the Performance FX of the track
        sequencer.enterPerformanceMode();
        sequencer.player.track[a].performancePlayMode = Sequencer::PLAYMODE_CUSTOM_1 + 14;
        snprintf(buf, sizeof buf, "custom %ld mode 15", a);
    }
    else
        snprintf(buf, sizeof buf, "fx34play: unknown '%s'", what);
    *out = buf;
}
