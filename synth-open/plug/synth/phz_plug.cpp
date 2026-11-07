// PHZ (our clean-room phase distortion engine, synth-open/phz) as a synth plugin: engine_phz.cpp and phz/phz.cpp as they are
// (built with the firmware's own flags, engines/).
#define PLUG_SYNTH_AUTHOR "tracker-ff (MIT)"
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kPhzDef;
}
PLUG_SYNTH_PLUGIN("PHZ", polysynth::open::kPhzDef, 8, 0x82)
