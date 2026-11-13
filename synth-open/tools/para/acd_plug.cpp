// Mac test only (tools/para/acd_bench.py), never shipped: the built-in ACD engine (engine_acd.cpp, ot/audio/engines/acd.cpp) as a
// synth engine plugin, built with the firmware's engine flags, so the emulator runs the same Cortex-M4 code paths as the image.
// ACD_PLUG_CHORD (a tree with PLUG_EXT_CHORD): the plugin answers the chord query too (the firmware's checkSynth and checkChord see it).
#include "synth_plug.h"

#include "audio/engines/acd.h"

namespace polysynth::open
{
extern const EngineDef kAcdDef;
}
#ifdef ACD_PLUG_CHORD
PLUG_SYNTH_PLUGIN_CHORD("ACD", polysynth::open::kAcdDef, 200, 0x7A00, 0, nullptr, ot::audio::acd::kChord, ot::audio::acd::kChordMode)
#else
PLUG_SYNTH_PLUGIN("ACD", polysynth::open::kAcdDef, 200, 0x7A00)
#endif
