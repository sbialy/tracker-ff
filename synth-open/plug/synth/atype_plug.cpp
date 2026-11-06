// ATYPE as a synth plugin: engine_atype.cpp and ot-synths/atype/atype.cpp as they are (built with the firmware's own flags, build_plugs.sh).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kAtypeDef;
}
PLUG_SYNTH_PLUGIN("ATYPE", polysynth::open::kAtypeDef, 12, 0x84)
