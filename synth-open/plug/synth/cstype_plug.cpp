// CSTYPE as a synth plugin: engine_cstype.cpp and ot-synths/cstype/cstype.cpp as they are (built with the firmware's own flags, engines/).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kCstypeDef;
}
PLUG_SYNTH_PLUGIN("CSTYPE", polysynth::open::kCstypeDef, 11, 0x83)
