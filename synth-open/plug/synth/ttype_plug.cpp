// TTYPE as a synth plugin: engine_ttype.cpp and ot-synths/ttype/ttype.cpp as they are (built with the firmware's own flags, build_plugs.sh).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kTtypeDef;
}
PLUG_SYNTH_PLUGIN("TTYPE", polysynth::open::kTtypeDef, 13, 0x85)
