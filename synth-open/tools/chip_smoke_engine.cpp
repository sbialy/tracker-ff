// Host smoke test only (run_smoke.sh with CHIP_PLUGIN_SRC): the CHIP plugin's engine source (engine_chip.cpp of tracker-engines-os) as one
// translation unit with the declaration that gives its definition external linkage. In the plugin build -DSYNTH_ENGINE_CHIP does that
// (engine_list.h declares kChipDef then), here the engine list must not get a built-in CHIP entry, so the declaration is made by hand.
#include "open_engine.h"
namespace polysynth::open
{
extern const EngineDef kChipDef;
}
#include CHIP_PLUGIN_SRC
