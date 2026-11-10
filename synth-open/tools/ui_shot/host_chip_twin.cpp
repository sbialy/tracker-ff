// ui_shot: the one synth engine plugin that runs natively. CHIP's plugin source (engine_chip.cpp of tracker-engines-os, CHIP_PLUGIN_SRC in the
// Makefile) and the Polyend voice are compiled for the host and the library entry of chip.plg attaches from this extension, so the picker,
// Synth Parameters, Edit Patch, Macros and Automation pages show a real CHIP part (no audio is rendered by ui_shot, the pages read the
// engine's definition and the patch). gen_overlay.py makes plug_synth.cpp call uishotTwinExt for every library entry. Without the source
// the function returns null and the part says Missing: CHIP.
#include "plug_abi.h"

#ifdef UISHOT_CHIP_TWIN
#include "synth_plug.h"
namespace polysynth::open
{
extern const EngineDef kChipDef;
}
extern "C" const plug_ext_synth* uishotTwinExt(int stored)
{
    if (stored != 6)
    {
        return nullptr;
    }
    static bool filled;
    if (!filled)
    {
        plugsynth::fill("CHIP", polysynth::open::kChipDef, 6, 8);
        filled = true;
    }
    return &plugsynth::g_ext;
}
#else
extern "C" const plug_ext_synth* uishotTwinExt(int) { return nullptr; }
#endif
