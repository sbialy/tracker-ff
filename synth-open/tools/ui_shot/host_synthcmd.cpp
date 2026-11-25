// ui_shot commands for the synth engine scenarios (the synth play and engine sims). Host only, NOT firmware.
//
//   padon N / padoff N    hold / release pad N (a held chord: the patch editor's pads play notes)
//   voices                "voices N": synth voices sounding (the pool's count)
//   view                  "view part P engine NAME shown S t MS values v0 v1 .. points p0 .. p199": the engine's view of the open part
//                         (plug::synthView, what the patch editor draws, -127..127) and whether the control is shown on screen
//   waitview MS           run the main loop until the view's points differ from the view before the last encto (else from now) or
//                         MS virtual ms pass: "waitview changed|unchanged MS", MS counted from the start of that encto
//   encto I V [MAX]       turn the encoder (the selected value of the editor) until stored value I rounds to V, at most MAX turns
//   poly N                the voice count of the open part (the Params page's Polyphony)
//   pickmodel NAME        on the Params page: step the model list until the part plays engine NAME
//   maxreset              clear the audio gap and main loop stall maxima
//   cardstats / audiostats   counters of host_simcard.cpp
//   card rm PATH | card mv A B | card cp A B   change the SD folder while the unit runs (PATH relative to the card root)
// The display control table is private: this file sees it the way ui_shot_main.cpp sees samplePlayback's members.
#define private public
#define protected public
#include "display.h"
#undef private
#undef protected

#include "host_platform.h"
#include "eve_model.h"
#include "core/interface.h"
#include "instrumentEditor/instrumentEditor.h"
#include "mtStructs.h"
#include "open_engine.h"
#include "open_load.h"
#include "plug_host.h"

#include <algorithm>
#include <math.h>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <vector>

void hostRun(uint32_t ms);   // ui_shot_main.cpp: the main loop for ms virtual milliseconds

#ifndef FEATURE_SYNTH_OPEN
// The plus pin (PLATFORM=IMX BRIDGE=1, the plus sim on the stock synth stack, no open stack): what these commands read from the open stack, from the core's
// Synth instead. The engine name is the lib's (Synth::getInstrumentTypeName, polysynth::Synth::getInstrumentName), the values are the stored
// values of the part's polysynth::Patch, nothing counts sounding voices
#include "Synth.h"
namespace polysynth::open
{
const EngineDef* partPatch(int part, const float** values, int* count)
{
    static EngineDef d;
    static std::string name;
    static float v[256];
    if (part < 0 || part >= kSynthParts) return nullptr;
    ::Synth* s = instrumentEditor.getSynth();
    const int ti = s->getInstrumentTypeIdx(part);
    if (ti < 0) return nullptr; // a Missing part (core/synth/synth_types.h) has no patch of its engine
    name = s->getInstrumentTypeName(ti);
    d = EngineDef{};
    d.name = name.c_str();
    polysynth::Patch& p = s->getPatch(part);
    const int n = std::min(int(p.getMetadata().params.size()), 256);
    for (int i = 0; i < n; i++) v[i] = p.getParamValue(i).value_or(__builtin_nanf(""));
    if (values) *values = v;
    if (count) *count = n;
    return &d;
}
int voicesSounding() { return 0; }
}
#endif

namespace
{
constexpr int kPoints = 200;
using Exempt = HostFaultSuspend;   // eve_model.h: allocations of this host code are never failed
std::vector<int> s_snap;   // the view before the last encto, and when it started
uint32_t s_snapT;
bool s_snapOn;

bool viewNow(std::vector<int>& pts, const polysynth::open::EngineDef** dOut, const float** vOut, int* nOut)
{
    const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
    const float* values = nullptr;
    int count = 0;
    const polysynth::open::EngineDef* d = polysynth::open::partPatch(part, &values, &count);
    int8_t out[kPoints] = {};
#ifdef FEATURE_PLUG_HOST
    const bool ok = plug::synthView(d, values, count, out, kPoints);
#else
    const bool ok = false;   // the cycle view is a plugin engine feature, no plugin host in the plus sim
#endif
    Exempt ex;
    pts.assign(out, out + kPoints);
    if (dOut) *dOut = d;
    if (vOut) *vOut = values;
    if (nOut) *nOut = count;
    return ok;
}

bool shown()
{
#ifndef FEATURE_PLUG_HOST
    return false;   // the plugin engine cycle view is a plugin host control (the plus sim is built without it)
#else
    const hControl h = instrumentEditor.synthViewControl;
    if (h < 0 || h >= controlsCount || display.controlsTable[h] == nullptr) return false;
    return display.controlsTable[h]->isStyleSet(controlStyleShow);
#endif
}
}   // namespace

bool hostSynthCommand(const std::string& c, std::istringstream& is)
{
    if (c == "padon" || c == "padoff")
    {
        int p = 0;
        is >> p;
        if (c == "padon")
            mtInterface.padPressed(uint8_t(p), 0, 0, 100);
        else
            mtInterface.padReleased(uint8_t(p));
        hostRun(20);
        return true;
    }
    if (c == "voices")
    {
        printf("voices %d\n", polysynth::open::voicesSounding());
        return true;
    }
    if (c == "view")
    {
        std::vector<int> pts;
        const polysynth::open::EngineDef* d = nullptr;
        const float* v = nullptr;
        int n = 0;
        const bool ok = viewNow(pts, &d, &v, &n);
        printf("view part %d engine %s has %d shown %d t %u values", mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX,
               d && d->name ? d->name : "-", ok ? 1 : 0, shown() ? 1 : 0, hostp::g_nowMs);
        for (int i = 0; v && i < n; i++) printf(" %g", double(v[i]));
        printf(" points");
        for (int p : pts) printf(" %d", p);
        printf("\n");
        return true;
    }
    if (c == "waitview")
    {
        int ms = 0;
        is >> ms;
        // against the view before the last encto (latency from its first turn), else against the view now
        std::vector<int> a, b;
        uint32_t t0 = hostp::g_nowMs;
        if (s_snapOn)
        {
            Exempt ex;
            a = s_snap;
            t0 = s_snapT;
            s_snapOn = false;
        }
        else
            viewNow(a, nullptr, nullptr, nullptr);
        viewNow(b, nullptr, nullptr, nullptr);
        for (int t = 0; b == a && t < ms; t += 5)
        {
            hostRun(5);
            viewNow(b, nullptr, nullptr, nullptr);
        }
        printf("waitview %s %u\n", b != a ? "changed" : "unchanged", hostp::g_nowMs - t0);
        return true;
    }
    if (c == "encto")
    {
        int idx = 0, target = 0, max = 4000;
        is >> idx >> target >> max;
        viewNow(s_snap, nullptr, nullptr, nullptr);
        s_snapT = hostp::g_nowMs;
        s_snapOn = true;
        int turns = 0;
        for (; turns < max; turns++)
        {
            const float* v = nullptr;
            int n = 0;
            const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
            if (!polysynth::open::partPatch(part, &v, &n) || idx >= n) break;
            const long cur = lroundf(v[idx]);
            if (cur == target) break;
            const long d = (target - cur) * 3 / 4;
            const int step = int(d == 0 ? (target > cur ? 1 : -1) : (d > 40 ? 40 : (d < -40 ? -40 : d)));
            mtInterface.potChange(0, int16_t(step));
            hostRun(10);
        }
        const float* v = nullptr;
        int n = 0;
        polysynth::open::partPatch(mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX, &v, &n);
        printf("encto %d %s %g turns %d\n", idx, v && idx < n && lroundf(v[idx]) == target ? "ok" : "fail", v && idx < n ? double(v[idx]) : -1.0, turns);
        return true;
    }
    if (c == "poly")
    {   // the part's voice count as the Params page sets it (cInstrumentEditor::changeSynthPolyphony)
        int n = 1;
        is >> n;
        const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        instrumentEditor.changeSynthPolyphony(int16_t(n - mtProject.values.synthPart[part].voiceCount));
        hostRun(20);
        printf("poly %d\n", int(mtProject.values.synthPart[part].voiceCount));
        return true;
    }
    if (c == "pickmodel")
    {   // the Params page of a synth part: step the model list (Down, then Up) until the part's engine is NAME (3 s per step to attach).
        // As many steps each way as the list has rows, so every row is reached from any start (was 12 each: rows past start + 12 were not)
        std::string want;
        is >> want;
        const int part = mtProject.values.lastUsedInstrument - SYNTH_INSTRUMENTS_START_IDX;
        auto name = [&]() -> std::string {
            const float* v = nullptr;
            int n = 0;
            const polysynth::open::EngineDef* d = polysynth::open::partPatch(part, &v, &n);
            return d && d->name ? d->name : "-";
        };
        const int rows = std::max(int(instrumentEditor.modelListData.length), 12);
        for (int k = 0; k < 2 * rows && name() != want; k++)
        {
            const int id = k < rows ? 31 : 26;   // Down, then Up
            hostp::setHeld(id, true);
            mtInterface.buttonChange(uint8_t(id), buttonPress);
            hostRun(40);
            mtInterface.buttonChange(uint8_t(id), buttonRelease);
            hostp::setHeld(id, false);
            hostRun(3000);
        }
        printf("pickmodel %s %s\n", want.c_str(), name() == want ? "ok" : "fail");
        return name() == want;
    }
    if (c == "maxreset")
    {
        hostp::g_audio.maxGapMs = 0;
        hostp::g_audio.maxMainMs = 0;
        return true;
    }
    if (c == "cardstats")
    {
        const auto& s = hostp::g_card;
        printf("cardstats calls %u opens %u reads %u bytes %llu injected %u audio_violations %u wait_ms %u\n", s.calls, s.opens, s.reads,
               (unsigned long long)s.bytes, s.injected, s.audioViolations, s.waitMs);
        return true;
    }
    if (c == "audiostats")
    {
        printf("audiostats blocks %u max_late_ms %u max_main_ms %u t %u\n", hostp::g_audio.blocks, hostp::g_audio.maxGapMs, hostp::g_audio.maxMainMs, hostp::g_nowMs);
        return true;
    }
    if (c == "card")
    {
        std::string op, a, b;
        is >> op >> a >> b;
        const std::string r = hostp::sdRoot() + "/";
        std::string cmd;
        if (op == "rm")
            cmd = "rm -rf '" + r + a + "'";
        else if (op == "mv")
            cmd = "mv '" + r + a + "' '" + r + b + "'";
        else if (op == "cp")
            cmd = "cp -R '" + a + "' '" + r + b + "'";
        else
            return false;
        return system(cmd.c_str()) == 0;
    }
    return false;
}
