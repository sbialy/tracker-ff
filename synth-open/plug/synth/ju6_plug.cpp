// JU6 (OpenTracker 3.x module engine, MIT) as a synth plugin: engine_ju6.cpp and ot-synths/ju6/ju6.cpp as they are.
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kJu6Def;
}
PLUG_SYNTH_PLUGIN("JU6", polysynth::open::kJu6Def, 7, 0x81)
