// The extension module loader of Tracker+ and Mini (ref/ff-imx-port-plan.md R2.1g, P10 H7), synth-open/ext_imx.cpp. Built when
// PLATFORM_IMX and FEATURE_EXT_IMX (core/makefile IMX, the plus and mini sims) or UISHOT_EXT_IMX (the plus BRIDGE sim) are set. The
// 48 KiB slot is borrowed from the sample pool in develop mode only (core/source/Tracker.cpp borrowExtSlot, 0 B static SDRAM). The MK66 keeps synth-open/ext_module.cpp.
#pragma once

#if defined(PLATFORM_IMX) && (defined(FEATURE_EXT_IMX) || defined(UISHOT_EXT_IMX))
#define EXT_IMX 1

#include "ext_imx_abi.h"

#include <stdint.h>

extern "C" {
// TrackerImpl::init: slot = the EXT_SLOT_BYTES borrowed from the sample pool in develop mode (/.develop on the card), null otherwise
void ext_imx_boot(void* platform, uint8_t* slot);
const uint8_t* ext_imx_slot(void);                 // the slot of this session, null outside develop mode
void ext_imx_update(void);                         // TrackerImpl::update: the load after the key window, the marker, the module
const char* ext_imx_status_short(void);            // "On", "Loaded", "No module", "Refused", "Safe" (Shift held), "Off" (powered down), ...
void ext_imx_power_down(void);                     // mtSleep::powerOff: the module is never called again this power cycle
// a console line to the module's command hook: the reply length in out (NUL ended), -1 unknown verb, -2 no module loaded
int32_t ext_imx_command(const char* line, char* out, uint32_t cap);
// the services the module calls (ext_imx_abi.h svc_*)
void ext_imx_svc_log(const char* text);
uint32_t ext_imx_svc_millis(void);
int ext_imx_svc_exists(const char* path);
int ext_imx_svc_write_text(const char* path, const char* text, int n);
const char* ext_imx_svc_build_id(void);
}

#if defined(UI_SHOT_HOST)
// The plus sim: the module is Arm code, it runs under Unicorn (synth-open/tools/ui_shot/host_extimx.cpp, BRIDGE=1). The loader
// rules and the cache steps run in ext_imx.cpp on the host copy of the slot, the bridge then runs the slot's bytes at `run`.
struct ExtImxSim
{
    uint32_t run;       // the guest address of the slot, what a module for the sim is linked for
    uint32_t imageId;   // the image id the sim answers (a module packed with another one is refused)
    // copies slot[0, bytes) to `run`, fills the services of the hooks block and calls the entry: its result (0 = hooks filled)
    int32_t (*enter)(const uint8_t* slot, uint32_t bytes, uint32_t entryOffset);
    void (*update)(void);
    int32_t (*command)(const char* line, char* out, uint32_t cap);
};
extern const ExtImxSim* ext_imx_sim;
extern "C" void ext_imx_sim_ops(uint32_t* cleaned, uint32_t* invalidated, uint32_t* barriers);   // null without the bridge: a module is refused at the entry
#endif

#endif
