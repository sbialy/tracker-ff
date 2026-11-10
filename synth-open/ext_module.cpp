// Extension module loader (FEATURE_EXT_HOOKS, MK66 spike). In develop mode (/.develop at the card root) the boot loads
// /Plugins/Ext/module.plg: a plugin file (plug_abi.h) of kind PLUG_KIND_EXT, linked at a fixed run address against this exact
// image. header.reserved[0] is the CRC-32 of this image's flash (text and the .data load image, [0, _etext + .data)), reserved[1]
// the run address it was linked for. Anything wrong (no file, a damaged file, another image, a crash during the last load, Shift
// at power on) leaves every hook null and the boot goes on as a normal image.
#if defined(FEATURE_EXT_HOOKS) && defined(FEATURE_PLUG_HOST)

#include "devhooks.h"
#include "plug_host.h"

#include "FileSystemAdapter.h"
#include "Platform.h"

#include <string.h>

extern "C" {
ext_hooks_t ext_hooks;
volatile uint32_t ext_site[2];
extern uint32_t _etext, _sdata, _edata;   // mk66fx1m0.ld
}

namespace
{
constexpr uint32_t PLUG_KIND_EXT = 6;
constexpr uint32_t kExtBytes = 48 * 1024;   // text and rw of the module, SDRAM (free past the statics, no sample memory)
constexpr const char* kPath = "/Plugins/Ext/module.plg";
constexpr const char* kMarker = "/Plugins/.ext-loading";   // a crash between the load and kMarkerMs of clean running
constexpr uint32_t kMarkerMs = 3000;
__attribute__((section(".noinit.$RAM4.ext"), aligned(16))) uint8_t s_ext[kExtBytes];

FileSystem* s_fs;
Timer* s_timer;
uint32_t now() { return s_timer ? s_timer->millis() : 0; }
uint32_t s_markerMs;
bool s_markerSet;

uint32_t imageCrc()
{
    const uint32_t text = uint32_t(&_etext);
    const uint8_t* flash;
    asm("mov %0, #0" : "=r"(flash));   // address 0 (the vector table), not a null the compiler may assume away
    uint32_t c = plug::crc32(0, flash, text);
    return plug::crc32(c, reinterpret_cast<const uint8_t*>(text), uint32_t(&_edata) - uint32_t(&_sdata));
}

// the module's entry: fills hooks, 0 = ok. api is this image's PLUG_ABI word, as for every plugin
typedef int32_t (*ext_entry_fn)(ext_hooks_t* hooks, uint32_t abi, uint32_t develop);
}   // namespace

extern "C" void ext_boot(void* platformPtr, int develop)
{
    memset(&ext_hooks, 0, sizeof(ext_hooks));
    if (!develop) return;
    Platform* platform = static_cast<Platform*>(platformPtr);
    FileSystem* fs = platform->getFileSystem();
    s_fs = fs;
    s_timer = platform->getTimer();
    if (fs->exists(kMarker))
    {   // the module crashed while starting last time: not this session
        fs->remove(kMarker);
        return;
    }
    if (plugUiShiftHeld()) return;
    plug::Place pl;
    pl.textDst = s_ext;
    pl.textLen = kExtBytes;
    pl.textRun = uintptr_t(s_ext);
    pl.run = false;
    plug::Loaded out;
    if (plug::load(fs, kPath, pl, PLUG_KIND_EXT, out) != plug::kOk) return;
    if (out.hdr.reserved[1] != uint32_t(uintptr_t(s_ext)) || out.hdr.reserved[0] != imageCrc()) return;
    plug::writeText(kMarker, "ext", 3);
    s_markerSet = true;
    s_markerMs = now();
    *reinterpret_cast<volatile uint32_t*>(0x4000D000) = 0;   // system MPU: fetch from SDRAM
    plug::syncCode();
    ext_hooks_t h;
    memset(&h, 0, sizeof(h));
    const ext_entry_fn entry = reinterpret_cast<ext_entry_fn>((uintptr_t(s_ext) + out.hdr.entry_offset) | 1u);
    if (entry(&h, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR, 1) == 0)
    {
        asm volatile("cpsid i" ::: "memory");
        ext_hooks = h;
        asm volatile("cpsie i" ::: "memory");
    }
}

extern "C" void ext_update(void)
{
    if (s_markerSet && now() - s_markerMs >= kMarkerMs)
    {
        s_fs->remove(kMarker);
        s_markerSet = false;
    }
    if (ext_hooks.update) ext_hooks.update();
}

#endif
