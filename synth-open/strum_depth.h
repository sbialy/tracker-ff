// The project's Strum ramp depth (firmware and host tools, NOT for plugins: a plugin asks the host through PLUG_EXT_STRUM, plug_abi.h).
// One live object in SRAM: the sequencer (mtSequencer.cpp sendMidiChordOn) and the built in Para engine (ACD note-on) read ramp_pct,
// the project load (plug_host.cpp, from project.mtff key strum.ramp) and the Strum ramp row of the File screen write it, and the same
// object is what the PLUG_EXT_STRUM host query hands to plugin engines. Plain constant initialisation: no static constructor.
//
// 0..100 percent, strum_ramp.h. A project without the setting plays with 0 (compat rule: it sounds as it did before the ramp existed),
// a new project starts at kDefaultNewProject. Without the FF core (no project.mtff) nothing can say 0, so such a build keeps the
// ramp at 100, as it was before the setting existed.
#pragma once

#include "plug/plug_abi.h"
#include "plug/strum_ramp.h"

namespace strumramp
{
inline constexpr uint32_t kDefaultNewProject = 100;
#ifdef FEATURE_FF_CORE
inline constexpr uint32_t kBootDepth = 0;
#else
inline constexpr uint32_t kBootDepth = 100;
#endif
inline plug_ext_strum g_depth = {sizeof(plug_ext_strum), kBootDepth};

inline int depthPct()
{
    return int(g_depth.ramp_pct);
}
inline void setDepth(int pct)
{
    g_depth.ramp_pct = uint32_t(pct < 0 ? 0 : (pct > 100 ? 100 : pct));
}
}   // namespace strumramp
