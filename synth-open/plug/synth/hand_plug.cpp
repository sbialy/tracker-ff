// HANDTYPE as a synth plugin: engine_hand.cpp and ot-synths/hand/hand.cpp as they are (built with the firmware's own flags, build_plugs.sh).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kHandDef;
}
PLUG_SYNTH_PLUGIN("HANDTYPE", polysynth::open::kHandDef, 15, 0x87)
