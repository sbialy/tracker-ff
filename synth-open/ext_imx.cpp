// The extension module loader of Tracker+ and Mini (ref/ff-imx-port-plan.md R2.1g, P10 H7, synth-open/ext_imx.h). In develop mode
// (/.develop at the card root) /Plugins/Ext/module.plg goes into an SDRAM slot through the MK66 file rules (ext_loader.h: crash
// marker, header, layout, run address, file CRC, image id, status.txt), then the IMX host's cache steps (plug_imx.h CacheOps: clean
// the D-cache over image and bss, invalidate the I-cache, DSB + ISB), then the entry with ext_imx_hooks_t (ext_imx_abi.h). What the
// MK66 loader does and this one does not (yet): the RAM vector table (a module's own fault and SysTick handlers, VTOR), the USB
// recovery console (needs the CDC function on the IMX composite USB, a unit), modit.plg and Shift held at power on (plug_ui).
// The whole file is IMX only: the MK66 build (every synth-open/*.cpp) compiles it empty.
#include "ext_imx.h"

#if defined(EXT_IMX)

#include "ext_loader.h"
#include "plug_imx.h"

#include "Platform.h"

#include <string.h>

#if defined(UI_SHOT_HOST)
const ExtImxSim* ext_imx_sim;
#endif

namespace
{
// text, data and bss of the module, then its reset surviving tail (ext_abi.h EXT_SLOT_BYTES, the MK66 size: the console module is
// 43,112 B of load part on the MK66). Not a static: in develop mode only, TrackerImpl::init borrows the sample pool's last EXT_SLOT_BYTES
// (core/source/Tracker.cpp borrowExtSlot, user decision 2026-11-20) and hands them here, 32 B aligned (an M7 D-cache line). The loader
// writes every byte the module runs.
uint8_t* s_ext;

FileSystem* s_fs;
Timer* s_timer;
Platform* s_platform;
ext::Status s_status = ext::Status::kOff;
bool s_pending, s_markerSet;
bool s_safe;   // Shift held in the key window (decision D4, as the MK66): no card code runs this session, the marker stays as it was
bool s_down;   // ext_imx_power_down ran: the module is never called again this power cycle
uint32_t s_bootMs, s_markerMs;
plug::imx::CacheOps s_ops;
constexpr uint32_t kKeyWindowMs = 4000;   // the MK66 loads the module when the plugin host's key window closes, the same 4 s here

uint32_t now() { return s_timer ? s_timer->millis() : 0; }

struct CardFs
{
    FileSystem* fs;
    FileSystem::FileHandle f = nullptr;
    bool exists(const char* p) { return fs->exists(p); }
    bool remove(const char* p) { return fs->remove(p); }
    bool writeText(const char* p, const char* t, int n)
    {
        auto [ok, h] = fs->openFile(p, FileSystem::FileOpenMode::kWriteCreateNew);
        if (!ok) return false;
        const bool done = n <= 0 || fs->writeFile(t, uint32_t(n), h) == n;
        fs->closeFile(h);
        return done;
    }
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
            const uint32_t chunk = n > 256 ? 256 : n;
            if (fs->readFile(dst, chunk, f) != int32_t(chunk)) return false;
            dst += chunk;
            n -= chunk;
        }
        return true;
    }
    void close() { fs->closeFile(f); }
};
}  // namespace

bool plugUiShiftHeld();   // plug_ui.cpp (the plugin host's key path, FEATURE_PLUG_HOST is on for IMX since P6)

// The services of ext_imx_hooks_t (the image hands their addresses to the module, the sim bridge calls them from its stubs)
extern "C" void ext_imx_svc_log(const char*) {}   // no log sink on the IMX image yet (platforms/imx DevConsole.cpp owns the serial port)
extern "C" uint32_t ext_imx_svc_millis(void) { return now(); }
extern "C" int ext_imx_svc_exists(const char* p) { return s_fs && s_fs->exists(p) ? 1 : 0; }
extern "C" int ext_imx_svc_write_text(const char* p, const char* t, int n) { return s_fs && CardFs{s_fs}.writeText(p, t, n) ? 1 : 0; }
extern "C" const char* ext_imx_svc_build_id(void)
{
    const char* b = s_platform ? s_platform->getSystemInfo()->buildId : nullptr;
    return b ? b : "";
}

namespace
{
#if !defined(UI_SHOT_HOST)
extern "C" uint8_t _image_start, _image_end;   // platforms/imx/linker/linkerScript.ld: the QSPI image, text then the .data load image
uint32_t imageId() { return plug::crc32(0, &_image_start, uint32_t(&_image_end - &_image_start)); }
uintptr_t runAddr() { return uintptr_t(s_ext); }

ext_imx_hooks_t s_hooks;
int32_t enterModule(uint32_t entryOffset)
{
    memset(&s_hooks, 0, sizeof(s_hooks));
    s_hooks.struct_size = sizeof(s_hooks);
    s_hooks.svc_log = uint32_t(uintptr_t(&ext_imx_svc_log));
    s_hooks.svc_millis = uint32_t(uintptr_t(&ext_imx_svc_millis));
    s_hooks.svc_exists = uint32_t(uintptr_t(&ext_imx_svc_exists));
    s_hooks.svc_write_text = uint32_t(uintptr_t(&ext_imx_svc_write_text));
    s_hooks.svc_build_id = uint32_t(uintptr_t(&ext_imx_svc_build_id));
    typedef int32_t (*Entry)(ext_imx_hooks_t*, uint32_t, uint32_t);
    const Entry entry = reinterpret_cast<Entry>((uintptr_t(s_ext) + entryOffset) | 1u);
    return entry(&s_hooks, EXT_IMX_ABI_VERSION, 1);
}
void callUpdate()
{
    if (s_hooks.update) reinterpret_cast<void (*)(void)>(uintptr_t(s_hooks.update))();
}
int32_t callCommand(const char* line, char* out, uint32_t cap)
{
    if (!s_hooks.command) return -1;
    return reinterpret_cast<int32_t (*)(const char*, char*, uint32_t)>(uintptr_t(s_hooks.command))(line, out, cap);
}
#else
uint32_t imageId() { return ext_imx_sim ? ext_imx_sim->imageId : 0; }
uintptr_t runAddr() { return ext_imx_sim ? ext_imx_sim->run : uintptr_t(s_ext); }
uint32_t s_loadBytes;   // what the bridge copies to the guest: image and bss
int32_t enterModule(uint32_t entryOffset) { return ext_imx_sim ? ext_imx_sim->enter(s_ext, s_loadBytes, entryOffset) : -1; }
void callUpdate()
{
    if (ext_imx_sim) ext_imx_sim->update();
}
int32_t callCommand(const char* line, char* out, uint32_t cap) { return ext_imx_sim ? ext_imx_sim->command(line, out, cap) : -1; }
#endif

bool enter(const plug_file_header& h)
{
    // ext::load copied the image and cleared the bss: the R2.1b steps before the first fetch from the slot
    s_ops.cleanDCache(s_ext, h.image_size + h.bss_size);
    s_ops.invalidateICache();
    s_ops.barrier();
#if defined(UI_SHOT_HOST)
    s_loadBytes = h.image_size + h.bss_size;
#endif
    return enterModule(h.entry_offset) == 0;
}

void load()
{
    CardFs card{s_fs};
    const ext::Slot slot{s_ext, EXT_LOAD_BYTES, runAddr()};
    s_status = ext::boot(card, true, slot, imageId, enter);
    s_markerMs = now();
    s_markerSet = s_status == ext::Status::kLoaded;
}
}  // namespace

extern "C" void ext_imx_boot(void* platformPtr, uint8_t* slot)
{
    s_platform = static_cast<Platform*>(platformPtr);
    s_fs = s_platform->getFileSystem();
    s_timer = s_platform->getTimer();
    s_ext = slot;
    if (!slot) return;
    s_pending = true;
    s_bootMs = now();
}

extern "C" void ext_imx_update(void)
{
    if (s_down) return;
    if (s_pending)
    {
        if (plugUiShiftHeld())
        {
            s_pending = false;
            s_safe = true;
        }
        else if (now() - s_bootMs >= kKeyWindowMs)
        {
            s_pending = false;
            load();
        }
    }
    if (s_markerSet)
    {
        CardFs card{s_fs};
        if (ext::markerExpired(card, s_markerMs, now())) s_markerSet = false;
    }
    if (s_status == ext::Status::kLoaded) callUpdate();
}

extern "C" const char* ext_imx_status_short(void)
{
    if (s_down) return "Off";
    if (s_safe) return "Safe";
    if (s_pending) return "Pending";
    switch (s_status)
    {
    case ext::Status::kOff: return "On";
    case ext::Status::kLoaded: return "Loaded";
    case ext::Status::kNone: return "No module";
    case ext::Status::kCrashed: return "Crashed";
    case ext::Status::kOtherImage: return "Other img";
    case ext::Status::kCrc:
    case ext::Status::kRead: return "Bad file";
    default: return "Refused";
    }
}

extern "C" int32_t ext_imx_command(const char* line, char* out, uint32_t cap)
{
    if (cap) out[0] = 0;
    if (s_down || s_status != ext::Status::kLoaded) return -2;
    return callCommand(line, out, cap);
}

// The power off (mtSleep::powerOff, before PlatformImpl::setPowerMode(false)): the module is never called again. On the MK66
// ext_power_down also puts the stock fault and SysTick vectors back (the SDRAM goes unpowered under them). The IMX loader installs no
// vector of the module (no RAM vector table / VTOR yet), and turnPowerOff cuts the supply, so dropping the hooks and the pending
// load is the whole teardown here.
extern "C" void ext_imx_power_down(void)
{
    s_down = true;
    s_pending = false;
#if !defined(UI_SHOT_HOST)
    memset(&s_hooks, 0, sizeof(s_hooks));
#endif
}

extern "C" const uint8_t* ext_imx_slot(void) { return s_ext; }

#if defined(UI_SHOT_HOST)
// the cache steps of the loads so far (the sim has no caches, its rows check the order ran: clean, invalidate, barrier per load)
extern "C" void ext_imx_sim_ops(uint32_t* cleaned, uint32_t* invalidated, uint32_t* barriers)
{
    *cleaned = s_ops.cleaned;
    *invalidated = s_ops.invalidated;
    *barriers = s_ops.barriers;
}
#endif

#endif
