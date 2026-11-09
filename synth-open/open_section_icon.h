// The patch editor's section icon and grid label for a section name (FEATURE_SYNTH_OPEN, SynthPatchEditor.cpp). The stock
// editor picks them with a chain of substring tests: the first rules below are that chain in its order, with its labels and
// numbering. The rules after it give every section of the open engines (built in and card plugins) an icon from the same
// set: no section is left without one. synth-open/tools/section_icon_test.cpp walks every section of every engine of
// engines.mk and asserts a match. No new artwork: the icons are the stock section icons that SYNTH_OPEN builds keep (the
// Shaper icon is left out of them to save flash, so Shaper and Chip Tricks wear the Modulation icon).
//
//   section name contains   icon         grid label
//   Amp Envelope            Envelope     Amp Env                  (stock)
//   Amplifier               Envelope     Amp                      (stock)
//   Envelope                Envelope     Env <n>                  (stock, numbered 1, 2, ... in page order)
//   Contour                 Envelope     the engine's short name  (D-Type Loudness Contour and Filter Contour: envelopes)
//   Filter                  Filter       Filter                   (stock)
//   Voice                   Voice        Voice                    (stock)
//   LFO                     LFO          LFO <n>                  (stock)
//   Modulation              Modulation   Mod                      (stock)
//   Oscillator              Oscillator   Osc                      (stock)
//   Shaper                  Modulation   Shp                      (stock label, the Shaper icon is not in SYNTH_OPEN builds)
//   Kick Tom Snare Hihat Cymbal Percussion Mixer FX               (stock, own icons)
//   Operator                Oscillator   the engine's short name  (FM4, Arc-Type: an FM operator is an oscillator)
//   Global                  Voice        "
//   Channel                 Voice        "                        (B-Type, Hand-Type: a channel of the sound chip)
//   Chip Tricks             Modulation   "
//   Controllers             Modulation   "
//   Sample & Hold           LFO          "
//   Clap                    Percussion   "
//   Hi Hat                  Hihat        "
//   Rim                     Percussion   "                        (T-Type Rim & Bell)
//   Sends                   FX           "
//   Drums                   Percussion   "
//   Noise Pulse Wave        Oscillator   "                        (Hand-Type's chip channels)
//   Mix                     Mixer        "                        (CS-Type Mix and Ring Mod)
#pragma once

#include <cstdint>
#include <cstring>

namespace polysynth::open
{
enum class SectionIcon : int8_t
{
    kNone = -1,
    kEnv,
    kFilter,
    kGear,
    kLfo,
    kMod,
    kOsc,
    kKick,
    kTom,
    kSnare,
    kHihat,
    kCymbal,
    kPercussion,
    kMixer,
    kFX,
};

enum class SectionCount : uint8_t
{
    kNone,
    kEnv,   // numbered with the envelope counter (stock: Env 1, Env 2)
    kLfo,   // numbered with the LFO counter
};

struct SectionIconRule
{
    const char* match;       // substring of the section name
    const char* label;       // grid label, nullptr: the section's own short name (open::SectionDef::shortName)
    SectionIcon icon;
    SectionCount count;
};

inline constexpr SectionIconRule kSectionIconRules[] = {
    { "Amp Envelope", "Amp Env", SectionIcon::kEnv, SectionCount::kNone },
    { "Amplifier", "Amp", SectionIcon::kEnv, SectionCount::kNone },
    { "Envelope", "Env", SectionIcon::kEnv, SectionCount::kEnv },
    { "Contour", nullptr, SectionIcon::kEnv, SectionCount::kNone },
    { "Filter", "Filter", SectionIcon::kFilter, SectionCount::kNone },
    { "Voice", "Voice", SectionIcon::kGear, SectionCount::kNone },
    { "LFO", "LFO", SectionIcon::kLfo, SectionCount::kLfo },
    { "Modulation", "Mod", SectionIcon::kMod, SectionCount::kNone },
    { "Oscillator", "Osc", SectionIcon::kOsc, SectionCount::kNone },
    { "Shaper", "Shp", SectionIcon::kMod, SectionCount::kNone },
    { "Kick", "Kick", SectionIcon::kKick, SectionCount::kNone },
    { "Tom", "Tom", SectionIcon::kTom, SectionCount::kNone },
    { "Snare", "Snare", SectionIcon::kSnare, SectionCount::kNone },
    { "Hihat", "Hihat", SectionIcon::kHihat, SectionCount::kNone },
    { "Cymbal", "Cymbal", SectionIcon::kCymbal, SectionCount::kNone },
    { "Percussion", "Perc", SectionIcon::kPercussion, SectionCount::kNone },
    { "Mixer", "Mixer", SectionIcon::kMixer, SectionCount::kNone },
    { "FX", "FX", SectionIcon::kFX, SectionCount::kNone },
    { "Operator", nullptr, SectionIcon::kOsc, SectionCount::kNone },
    { "Global", nullptr, SectionIcon::kGear, SectionCount::kNone },
    { "Channel", nullptr, SectionIcon::kGear, SectionCount::kNone },
    { "Chip Tricks", nullptr, SectionIcon::kMod, SectionCount::kNone },
    { "Controllers", nullptr, SectionIcon::kMod, SectionCount::kNone },
    { "Sample & Hold", nullptr, SectionIcon::kLfo, SectionCount::kNone },
    { "Clap", nullptr, SectionIcon::kPercussion, SectionCount::kNone },
    { "Hi Hat", nullptr, SectionIcon::kHihat, SectionCount::kNone },
    { "Rim", nullptr, SectionIcon::kPercussion, SectionCount::kNone },
    { "Sends", nullptr, SectionIcon::kFX, SectionCount::kNone },
    { "Drums", nullptr, SectionIcon::kPercussion, SectionCount::kNone },
    { "Noise", nullptr, SectionIcon::kOsc, SectionCount::kNone },
    { "Pulse", nullptr, SectionIcon::kOsc, SectionCount::kNone },
    { "Wave", nullptr, SectionIcon::kOsc, SectionCount::kNone },
    { "Mix", nullptr, SectionIcon::kMixer, SectionCount::kNone },
};

// The first rule whose match is in name, nullptr when none is
inline const SectionIconRule* sectionIconRule(const char* name)
{
    for (const SectionIconRule& r : kSectionIconRules)
    {
        if (std::strstr(name, r.match))
        {
            return &r;
        }
    }
    return nullptr;
}
} // namespace polysynth::open
