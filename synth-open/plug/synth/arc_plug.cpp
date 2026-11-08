// ARCTYPE as a synth plugin: engine_arc.cpp and ot-synths/arc/arc.cpp as they are (built with the firmware's own flags, engines/).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kArcDef;
}
PLUG_SYNTH_PLUGIN("ARCTYPE", polysynth::open::kArcDef, 17, 0x89)
