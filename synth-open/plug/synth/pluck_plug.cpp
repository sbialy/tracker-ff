// PLK (Pluck, our clean-room Karplus-Strong engine, synth-open/pluck) as a synth plugin: engine_pluck.cpp and pluck/pluck.cpp
// with -DPLUCK_WORK_POOL, the 8.7 KB delay line pool in host memory (work_bytes, SDRAM on the device) instead of .bss. The
// render compare links the firmware's own objects (pool in .bss) behind this file with -DPLUG_FWOBJ: no work memory then.
#define PLUG_SYNTH_AUTHOR "tracker-ff (MIT)"
#ifndef PLUG_FWOBJ
#define PLUCK_WORK_POOL
#endif
#include "synth_plug.h"
#include "pluck/pluck.h"
namespace polysynth::open
{
extern const EngineDef kPlkDef;
}
#ifdef PLUG_FWOBJ
PLUG_SYNTH_PLUGIN("PLK", polysynth::open::kPlkDef, 18, 0x8A)
#else
PLUG_SYNTH_PLUGIN_WORK("PLK", polysynth::open::kPlkDef, 18, 0x8A, ot::audio::pluck::workBytes(), ot::audio::pluck::setWork)
#endif
