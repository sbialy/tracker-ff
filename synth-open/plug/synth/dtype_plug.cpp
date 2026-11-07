// DTYPE as a synth plugin: engine_dtype.cpp and ot-synths/dtype/dtype.cpp as they are (built with the firmware's own flags, engines/).
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kDtypeDef;
}
PLUG_SYNTH_PLUGIN("DTYPE", polysynth::open::kDtypeDef, 10, 0xA2)
