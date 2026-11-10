// Extension module loader (FEATURE_EXT_HOOKS, MK66). In develop mode (/.develop at the card root) the boot loads
// /Plugins/Ext/module.plg into an SDRAM slot and calls its entry, which fills ext_hooks. The rules (order of the checks, the crash
// marker, the status line) are ext_loader.h, shared with the host test tools/ext_loader_test.cpp. Anything wrong leaves every
// hook null and the boot goes on as a normal image.
#if defined(FEATURE_EXT_HOOKS) && defined(FEATURE_PLUG_HOST)

#include "devhooks.h"
#include "ext_loader.h"
#include "open_load.h"
#include "plug_host.h"

#include "FileSystemAdapter.h"
#include "Platform.h"

#ifndef UI_SHOT_HOST
#include "display.h"
#endif

#include <string.h>

extern "C" {
ext_hooks_t ext_hooks;
volatile uint32_t ext_site[2];
#ifndef UI_SHOT_HOST
extern uint32_t _etext, _sdata, _edata;   // mk66fx1m0.ld
extern void (*_VectorsRam[])(void);
extern void (*const _VectorsFlash[])(void);   // mk20dx128.c
void ext_keep_platform(void);   // platforms/mk66/source/util.cpp
extern const uint8_t ext_modulesCount asm("_ZN9Interface12modulesCountE");   // Interface::modulesCount (private)
#endif
}

namespace
{
// text, data and bss of the module, then its reset surviving tail (ext_abi.h). SDRAM past the statics, the startup never clears it.
#ifndef UI_SHOT_HOST
__attribute__((section(".noinit.$RAM4.ext"), aligned(16)))
#endif
uint8_t s_ext[EXT_SLOT_BYTES];

FileSystem* s_fs;
Timer* s_timer;
uint32_t s_markerMs;
bool s_markerSet;
uint32_t now() { return s_timer ? s_timer->millis() : 0; }

// ext_loader.h's file operations on the card
struct CardFs
{
    FileSystem* fs;
    FileSystem::FileHandle f = nullptr;
    bool exists(const char* p) { return fs->exists(p); }
    bool remove(const char* p) { return fs->remove(p); }
    bool writeText(const char* p, const char* t, int n) { return plug::writeText(p, t, n); }
    bool open(const char* p)
    {
        auto [ok, h] = fs->openFile(p, FileSystem::FileOpenMode::kRead);
        f = h;
        return ok;
    }
    bool read(uint8_t* dst, uint32_t n)
    {
        while (n)
        {
            const uint32_t chunk = n > 256 ? 256 : n;   // through FatFs' own sector buffer
            if (fs->readFile(dst, chunk, f) != int32_t(chunk)) return false;
            dst += chunk;
            n -= chunk;
        }
        return true;
    }
    void close() { fs->closeFile(f); }
};

#ifndef UI_SHOT_HOST
// CRC-32 of this image as the bootloader programmed it: [0, _etext) and the .data load image right after (AT(_etext)).
// About 1 MB through the bitwise CRC, once per develop boot with a module whose file CRC is good.
uint32_t imageCrc()
{
    const uint32_t text = uint32_t(&_etext);
    const uint8_t* flash;
    asm("mov %0, #0" : "=r"(flash));   // address 0 (the vector table), not a null the compiler may assume away
    return plug::crc32(0, flash, text + (uint32_t(&_edata) - uint32_t(&_sdata)));
}

// Firmware functions only the module calls (gc-sections drops them from an image without the console): referenced here so the
// image keeps them, the module links against them (ld --just-symbols). Never called. The SD card and vector table ones are in
// ext_keep_platform (the core build has no platform headers).
volatile uint32_t s_keepSink;
__attribute__((noinline, used)) void keepForModule()
{
    s_keepSink = display.isIdle() + ext_modulesCount;
    asm volatile("" ::"r"(&polysynth::open::loadStats));   // the console's cpu command reads the synth load counters
    asm volatile("" ::"r"(&polysynth::open::loadShots));
    asm volatile("" ::"r"(&polysynth::open::setAdmitPermille));
    asm volatile("" ::"r"(&polysynth::open::admitPermille));
    ext_keep_platform();
}

// the stock fault (3 to 6) and SysTick (15) vectors from before an accepted entry, put back by ext_power_down
void (*s_stockVectors[5])(void);
bool s_entered;
constexpr int kVectors = 16 + 100;   // _VectorsRam / _VectorsFlash: 16 system vectors + NVIC_NUM_INTERRUPTS (MK66)

bool enter(const plug_file_header& h)
{
    asm volatile("" ::"r"(&keepForModule));
    *reinterpret_cast<volatile uint32_t*>(0x4000D000) = 0;   // system MPU: let the core fetch from SDRAM (plug::load does the same)
    plug::syncCode();
    void (*saved[5])(void) = {_VectorsRam[3], _VectorsRam[4], _VectorsRam[5], _VectorsRam[6], _VectorsRam[15]};
    ext_hooks_t hk;
    memset(&hk, 0, sizeof(hk));
    hk.struct_size = sizeof(hk);
    const ext_entry_fn entry = reinterpret_cast<ext_entry_fn>((uintptr_t(s_ext) + h.entry_offset) | 1u);
    const bool ok = entry(&hk, EXT_ABI_VERSION, 1) == 0;
    asm volatile("cpsid i" ::: "memory");
    if (ok)
    {
        ext_hooks = hk;
        for (int i = 0; i < 5; i++) s_stockVectors[i] = saved[i];
        s_entered = true;
    }
    else
    {   // a refusing module leaves the stock handlers
        for (int i = 0; i < 4; i++) _VectorsRam[3 + i] = saved[i];
        _VectorsRam[15] = saved[4];
    }
    asm volatile("cpsie i" ::: "memory");
    return ok;
}
#else
// ui_shot: the rules run, no module code (the host cannot run Arm code). The image id is 0, so a card module is refused.
uint32_t imageCrc() { return 0; }
bool enter(const plug_file_header&) { return false; }
#endif
}   // namespace

namespace
{
ext::Status s_status = ext::Status::kOff;
// decision D4 (ref/features-design.md): the module is card code, so Shift held at power on skips it for the session. It loads
// when the plugin host's key window closes (the same 4 s and the same key path), not in TrackerImpl::init
constexpr uint32_t kKeyWindowMs = 4000;
bool s_pending, s_safe;
uint32_t s_bootMs;
}

bool plugUiShiftHeld();   // plug_ui.cpp

extern "C" const char* ext_status_short(void)
{
    if (s_safe) return "Safe";
    switch (s_status)
    {
    case ext::Status::kOff: return "On";
    case ext::Status::kLoaded: return "Loaded";
    case ext::Status::kNone: return "No module";
    case ext::Status::kCrashed: return "Crashed";
    default: return "Refused";
    }
}

extern "C" void ext_boot(void* platformPtr, int develop)
{
    memset(&ext_hooks, 0, sizeof(ext_hooks));
    if (!develop) return;
    Platform* platform = static_cast<Platform*>(platformPtr);
    s_fs = platform->getFileSystem();
    s_timer = platform->getTimer();
    s_pending = true;   // loaded by ext_update once the key window closed
    s_bootMs = now();
}

static void ext_load()
{
    CardFs card{s_fs};
#ifndef UI_SHOT_HOST
    // RCM_SRS0 / RCM_SRS1: what ended the last boot, for the crash report (they hold until the next reset)
    const uint32_t reset = *reinterpret_cast<volatile uint8_t*>(0x4007F000) | *reinterpret_cast<volatile uint8_t*>(0x4007F001) << 8;
#else
    const uint32_t reset = 0;
#endif
    const ext::Slot slot{s_ext, EXT_LOAD_BYTES, uintptr_t(s_ext), reset};
    s_status = ext::boot(card, true, slot, imageCrc, enter);
    const bool loaded = s_status == ext::Status::kLoaded;
    s_markerMs = now();
    s_markerSet = loaded;
}

extern "C" void ext_update(void)
{
    if (s_pending)
    {
        if (plugUiShiftHeld())
        {
            s_pending = false;
            s_safe = true;   // nothing of the card's code runs this session, the marker stays as it was
        }
        else if (now() - s_bootMs >= kKeyWindowMs)
        {
            s_pending = false;
            ext_load();
        }
    }
    if (s_markerSet)
    {
        CardFs card{s_fs};
        if (ext::markerExpired(card, s_markerMs, now())) s_markerSet = false;
    }
    if (ext_hooks.update) ext_hooks.update();
}

// The end of a power off (PlatformImpl::setPowerMode(false), interrupts off) unpowers the SDRAM and the module's code with it, and
// detachInterrupt (teensy core, called by disableHardware) turns the interrupts on again after that. A SysTick or a fault whose
// handler sits in the dead slot cannot be taken: the core locks up (RCM SRS1 LOCKUP, srs 0200, ref/restart-lockup.md). So before
// the teardown every vector that points into the slot goes back to the image (fault and SysTick: the ones saved at the entry,
// an interrupt vector: the flash one, with the interrupt disabled) and every hook is cleared. The module is gone for the rest of
// this power cycle.
extern "C" void ext_power_down(void)
{
    memset(&ext_hooks, 0, sizeof(ext_hooks));
#ifndef UI_SHOT_HOST
    if (!s_entered) return;
    const uintptr_t lo = uintptr_t(s_ext), hi = lo + sizeof(s_ext);
    for (int i = 0; i < kVectors; i++)
    {
        const uintptr_t v = uintptr_t(_VectorsRam[i]);
        if (v < lo || v >= hi) continue;
        if (i >= 3 && i <= 6) _VectorsRam[i] = s_stockVectors[i - 3];
        else if (i == 15) _VectorsRam[i] = s_stockVectors[4];
        else
        {
            _VectorsRam[i] = _VectorsFlash[i];
            if (i >= 16) reinterpret_cast<volatile uint32_t*>(0xE000E180)[(i - 16) / 32] = 1u << ((i - 16) % 32);   // NVIC_ICER
        }
    }
    s_entered = false;
    asm volatile("dsb" ::: "memory");
#endif
}

#endif
