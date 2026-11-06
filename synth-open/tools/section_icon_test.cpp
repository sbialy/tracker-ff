// Host test (Mac): every section page of every engine of engines.mk (the built-in ones and the card plugins, which are the
// same EngineDefs) gets an icon and a grid label from synth-open/open_section_icon.h, the table SynthPatchEditor.cpp uses
// in SYNTH_OPEN builds. Built by run_section_icon_test.sh with every -DSYNTH_ENGINE_<NAME> of SYNTH_ENGINES_ALL.
//
// Checked: each section name matches a rule, the rule has an icon, the label is not empty and fits the grid (7 characters,
// instrumentEditor.h kMaxSynthGridStringLen 8 with the terminator, "Env 2" counted), the stock names keep the stock chain's
// icon and label (the first rules), and the table lists every name it prints.
#include "open_engine.h"
#include "open_section_icon.h"

#include <cstdio>
#include <cstring>
#include <string>

namespace polysynth::open
{
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, ...) extern const EngineDef DEF;
#include "engine_list.h"
#undef SYNTH_ENGINE
} // namespace polysynth::open

using namespace polysynth::open;

namespace
{
int g_fail, g_pass;
void check(bool ok, const std::string& what)
{
    ok ? g_pass++ : g_fail++;
    if (!ok) printf("FAIL  %s\n", what.c_str());
}
const char* kIconName[] = { "Envelope", "Filter", "Voice", "LFO", "Modulation", "Oscillator", "Kick", "Tom", "Snare",
                            "Hihat", "Cymbal", "Percussion", "Mixer", "FX" };
} // namespace

int main()
{
    static const struct
    {
        const char* engine;
        const EngineDef* def;
    } kEngines[] = {
#define SYNTH_ENGINE(NAME, STORED, TYPE_ID, FACTORY, DEF, ...) { #NAME, &DEF },
#include "engine_list.h"
#undef SYNTH_ENGINE
    };
    int engines = 0, sections = 0;
    for (const auto& e : kEngines)
    {
        engines++;
        int env = 1, lfo = 1;
        printf("%-8s", e.engine);
        for (int i = 0; i < e.def->sectionCount; i++)
        {
            const SectionDef& s = e.def->sections[i];
            sections++;
            const SectionIconRule* r = sectionIconRule(s.name);
            check(r != nullptr, std::string(e.engine) + " \"" + s.name + "\": no rule matches");
            if (!r) continue;
            check(r->icon != SectionIcon::kNone, std::string(e.engine) + " \"" + s.name + "\": no icon");
            const char* label = r->label ? r->label : s.shortName;
            int n = r->count == SectionCount::kEnv ? env++ : r->count == SectionCount::kLfo ? lfo++ : 0;
            char grid[32];
            n ? snprintf(grid, sizeof(grid), "%s %d", label, n) : snprintf(grid, sizeof(grid), "%s", label);
            check(label && *label && strlen(grid) <= 7, std::string(e.engine) + " \"" + s.name + "\": label \"" + grid + "\"");
            printf("  %s=%s[%s]", s.name, kIconName[int(r->icon)], grid);
        }
        printf("\n");
    }
    // the stock chain's names keep its icons and labels
    static const struct
    {
        const char* name;
        SectionIcon icon;
        const char* label;
    } kStock[] = {
        { "Amp Envelope", SectionIcon::kEnv, "Amp Env" }, { "Amplifier", SectionIcon::kEnv, "Amp" },
        { "Envelope 2", SectionIcon::kEnv, "Env" },      { "Filter", SectionIcon::kFilter, "Filter" },
        { "Voice", SectionIcon::kGear, "Voice" },        { "LFO 1", SectionIcon::kLfo, "LFO" },
        { "Modulation", SectionIcon::kMod, "Mod" },      { "Oscillator", SectionIcon::kOsc, "Osc" },
        { "Kick", SectionIcon::kKick, "Kick" },          { "Toms", SectionIcon::kTom, "Tom" },
        { "Snare", SectionIcon::kSnare, "Snare" },       { "Hihat", SectionIcon::kHihat, "Hihat" },
        { "Cymbal", SectionIcon::kCymbal, "Cymbal" },    { "Percussion", SectionIcon::kPercussion, "Perc" },
        { "Mixer", SectionIcon::kMixer, "Mixer" },       { "FX", SectionIcon::kFX, "FX" },
    };
    for (const auto& k : kStock)
    {
        const SectionIconRule* r = sectionIconRule(k.name);
        check(r && r->icon == k.icon && r->label && !strcmp(r->label, k.label), std::string("stock \"") + k.name + "\" as before");
    }
    // the names the brief listed as unmatched before this table
    static const char* const kNew[] = { "Operator 1", "Global", "Channel", "Channel 2", "Chip Tricks", "Controllers",
                                        "Layer I Filters", "Sample & Hold", "Envelopes", "Loudness Contour",
                                        "Filter Contour", "Clap", "Hi Hats", "Rim & Bell", "Sends", "Drums", "Noise", "Pulse",
                                        "Wave", "Mix and Ring Mod" };
    for (const char* n : kNew) check(sectionIconRule(n) != nullptr, std::string("\"") + n + "\" has an icon");
    check(engines == 18, "18 engines in engine_list.h (6 stock named, JU6 PHZ FM4, the 8 other OpenTracker 3.x engines, PLK), got " +
                           std::to_string(engines));
    printf("%d engines, %d sections, %d checks passed, %d failed\n", engines, sections, g_pass, g_fail);
    printf(g_fail ? "FAILED\n" : "PASSED\n");
    return g_fail ? 1 : 0;
}
