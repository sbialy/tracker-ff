// TTYPE as a synth plugin: engine_ttype.cpp and ot-synths/ttype/ttype.cpp as they are (built with the firmware's own flags, build_plugs.sh),
// with -DTTYPE_WORK_POOL: the 512 B square bank block of render in host work memory (work_bytes, SDRAM on the device) instead of the
// audio interrupt's stack (render frame 2,856 B before). The render compare links the firmware's own objects (block on the stack)
// behind this file with -DPLUG_FWOBJ: no work memory then, and the same checksum proves the same DSP.
#ifndef PLUG_FWOBJ
#define TTYPE_WORK_POOL
#endif
#include "synth_plug.h"
#include "ot-synths/ttype/ttype.h"
namespace polysynth::open
{
extern const EngineDef kTtypeDef;
}
#ifdef PLUG_FWOBJ
PLUG_SYNTH_PLUGIN("TTYPE", polysynth::open::kTtypeDef, 13, 0x85)
#else
PLUG_SYNTH_PLUGIN_WORK("TTYPE", polysynth::open::kTtypeDef, 13, 0x85, ot::audio::ttype::workBytes(), ot::audio::ttype::setWork)
#endif
