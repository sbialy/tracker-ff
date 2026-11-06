// B-Type (OpenTracker 3.x module engine, MIT) as a synth plugin: engine_btype.cpp and ot-synths/btype/btype.cpp as they are.
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kBtypeDef;
}
PLUG_SYNTH_PLUGIN("BTYPE", polysynth::open::kBtypeDef, 16, 0x88)
