// 8TYPE as a synth plugin: engine_eight.cpp and ot-synths/eight/eight.cpp as they are (built with the firmware's own flags, build_plugs.sh).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kEightDef;
}
PLUG_SYNTH_PLUGIN("8TYPE", polysynth::open::kEightDef, 14, 0x86)
