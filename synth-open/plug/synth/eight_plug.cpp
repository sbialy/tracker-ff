// 8TYPE as a synth plugin: engine_eight.cpp and ot-synths/eight/eight.cpp as they are (built with the firmware's own flags, build_plugs.sh),
// with -DEIGHT_WORK_POOL: the two Links of the voice pass in host work memory (work_bytes, SDRAM on the device) instead of the audio
// interrupt's stack (3,728 B voicePass frame before). The render compare links the firmware's own objects (Links on the stack) behind
// this file with -DPLUG_FWOBJ: no work memory then, and the same checksum proves the same DSP.
#ifndef PLUG_FWOBJ
#define EIGHT_WORK_POOL
#endif
#include "synth_plug.h"
#include "ot-synths/eight/eight.h"
namespace polysynth::open
{
extern const EngineDef kEightDef;
}
#ifdef PLUG_FWOBJ
PLUG_SYNTH_PLUGIN("8TYPE", polysynth::open::kEightDef, 14, 0x86)
#else
PLUG_SYNTH_PLUGIN_WORK("8TYPE", polysynth::open::kEightDef, 14, 0x86, ot::audio::eight::workBytes(), ot::audio::eight::setWork)
#endif
