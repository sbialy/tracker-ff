// Host "twins" of the master insert plugins. NOT firmware.
//
// A .plg is Thumb code that only a Cortex-M can run. The host UI build still has to show a plugin that is loaded: its name, its
// parameters and their values. The plugin sources are plain C/C++ with one entry point each (plug_entry), so the same sources are
// compiled natively into this binary (Makefile: the insert sources of tracker-effects-os and tracker-effects at their pinned tags with plug_entry
// renamed, Dirt through host_dirt.cpp with the PLUG_IN_IMAGE entry name) and plug::load() (overlay of plug_host.cpp, gen_overlay.py) asks uishot_twin() for the
// descriptor instead of jumping into the file's image. The file itself is still read, CRC checked and rebased by the real loader:
// a corrupt or foreign .plg fails as it does on the device. Audio is never run here.
#include "plug/plug_abi.h"

#include <stdint.h>
#include <string.h>

extern "C"
{
const plug_host* plug_rt_host;   // the plugin runtime's host pointer (plug_rt.c on the device)

const plug_desc* plug_entry_dirt(const plug_host*, uint32_t);
const plug_desc* plug_entry_ot_tape_plug(const plug_host*, uint32_t);
const plug_desc* plug_entry_ot_lofi_plug(const plug_host*, uint32_t);
const plug_desc* plug_entry_ff_console_plug(const plug_host*, uint32_t);   // Console and Tube: private source, see the Makefile (EXT)
const plug_desc* plug_entry_ff_tube_plug(const plug_host*, uint32_t);
const plug_desc* plug_entry_ot_bbd_plug(const plug_host*, uint32_t);
const plug_desc* plug_entry_ot_phaser_plug(const plug_host*, uint32_t);

static volatile uint32_t s_fake[4];
volatile uint32_t* uishot_fake_reg() { return &s_fake[0]; }

static uint32_t fnv1a(const char* s)
{
    uint32_t h = 2166136261u;
    for (; *s; s++) h = (h ^ uint8_t(*s)) * 16777619u;
    return h;
}

const plug_desc* uishot_twin(uint32_t uid, const plug_host* host)
{
    static const struct
    {
        const char* id;
        const plug_desc* (*entry)(const plug_host*, uint32_t);
    } kTwins[] = {
        {"ff.dirt", plug_entry_dirt},
        {"org.opentracker.fx.tape", plug_entry_ot_tape_plug},
        {"org.opentracker.fx.lofi", plug_entry_ot_lofi_plug},
        {"ff.console", plug_entry_ff_console_plug},
        {"ff.tube", plug_entry_ff_tube_plug},
        {"org.opentracker.fx.bbd", plug_entry_ot_bbd_plug},
        {"org.opentracker.fx.phaser", plug_entry_ot_phaser_plug},
    };
    for (const auto& t : kTwins)
        if (fnv1a(t.id) == uid) return t.entry(host, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
    return nullptr;   // a plugin with no twin (the synth engines: never run on the host, and Console and Tube when their private source was not available, their stub entry returns nullptr) fails to load, "Failed" on the page
}
}
