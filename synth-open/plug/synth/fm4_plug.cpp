// FM4 (OpenTracker 3.x module engine, MIT) as a synth plugin: engine_fm4.cpp and ot-synths/fm4/fm4.cpp as they are.
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kFm4Def;
}
PLUG_SYNTH_PLUGIN("FM4", polysynth::open::kFm4Def, 9, 0x80)
