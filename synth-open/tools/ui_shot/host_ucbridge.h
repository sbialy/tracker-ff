// ui_shot BRIDGE=1 only (emu parity plan T2, ref/emu-parity-plan.md): the real plugin bytes of the card run under libunicorn, the
// firmware stays native. Never part of the image, never part of a plain ui_shot build (the Makefile leaves this pair out without BRIDGE=1).
//
// Memory. Every guest address range the bridge knows is a host buffer mapped into each Unicorn instance with uc_mem_map_ptr at the
// device address (the "windows"): the release image (flash, read and execute only), the FlexNVM bank, SRAM_L, SRAM_U, the SDRAM, and
// a bridge region at 0x60000000 (unused on the MK66) for the guest heap, the voice shadows and the lanes' stacks. arm64 macOS refuses
// fixed maps at the device addresses, so native code holds host pointers and translates at the bridge edge only: g2h() and h2g().
// A firmware global that a plugin must reach (the insert slot s_slot, the arenas, the pool) becomes a reference into its window at the
// address of the release axf's own symbol (symWindow), so native code and the guest see the same bytes.
//
// Image calls. The release axf (UISHOT_AXF, else the build's default) is mapped at its link addresses, .data included, and its own
// functions run as Thumb code: the plug_host service table plug::s_host (newlib sinf ... log2f, memcpy, memset: the unit's libm, not the
// host's) and the pure loader checks (plug::checkDesc, tempoExt, tailExt, plug_synth's fn). The services that touch firmware state
// (hostAlloc, hostCycles, hostQuery, the PLUG_EXT_FILES functions of kFiles) are patched to `bx lr` in the mapped copy and trapped to
// native code (setServices). A host global that a plugin reads through a pointer the host handed out (strumramp::g_depth) is a shadow:
// copied to its guest address before every call.
//
// Lanes. One Unicorn instance per lane (main, part 1..3, insert chain, spare), each on its own worker thread at
// QOS_CLASS_USER_INTERACTIVE. A call runs on the lane of the calling thread (LaneScope, default main) and waits for it, so the caller
// sees a plain function call. submit()/join() run a job on a lane without waiting (one part per lane in parallel).
//
// Thunks. A guest descriptor or extension is deep copied into a host mirror whose function pointers are native thunks (template pools,
// one entry per guest struct address): plug_desc, plug_ext_tempo, plug_ext_tail, plug_ext_synth (SynthVoice 152 B on the unit, 160 B
// here: each native voice has a stable guest shadow in the device layout, copied in and out around every call), plug_ext_chord (no
// pointers: a view of the guest bytes) and the three .ptfx entry points. Pointer arguments in a window pass as their guest address,
// the rest are bounced through the lane's scratch (copied in, and out where the callee writes).
#pragma once

#include "plug/plug_abi.h"   // the path plug_host.h takes, so a firmware source that includes both sees one copy
#include "tracker_plugin.h"
#include <stddef.h>
#include <stdint.h>

namespace ucb
{
// ---- device memory map (MK66FX1M0, mk66fx1m0.ld)
constexpr uint32_t kFlash = 0x00000000, kFlashSize = 0x100000;
constexpr uint32_t kNvm = 0x10000000, kNvmSize = 0x20000;
constexpr uint32_t kSramL = 0x1FFF0000, kSramLSize = 0x10000;
constexpr uint32_t kSramU = 0x20000000, kSramUSize = 0x30000;
constexpr uint32_t kSdram = 0x70000000, kSdramSize = 0x1000000;
constexpr uint32_t kBridge = 0x60000000, kBridgeSize = 0x400000;   // not a device range: guest heap, shadows, lane stacks
// The IMX SDRAM (0x80000000 on the RT1050) where plug::imx places the code slots (plug_imx.h simSlots, H8 of the IMX port): unused by
// the MK66 image, mapped so an IMX load runs the bytes at the address it rebased for. 256 KiB: the code slots at any synth slot size
// under review (3 x 40 KiB + the insert slot = 132,608 B, IMX port P7) plus the IMX services table copy
constexpr uint32_t kImxSdram = 0x80000000, kImxSdramSize = 0x40000;
constexpr uint32_t kMpu = 0x4000D000, kMpuSize = 0x1000;           // the system MPU's CESR page (plain RAM, the image's start() writes it)

enum Lane : int
{
    kMain = 0,
    kPart0 = 1,   // parts 1..3: kPart0 + part
    kInsert = 4,
    kSpare = 5,
    kLanes = 8
};

// ---- setup
// Loads the release axf (path, or UISHOT_AXF, or the build default) once. False: no image, every plugin call fails (entry() gives 0),
// the reason is in error(). Any call below runs it first.
bool init(const char* axf = nullptr);
bool ready();
const char* error();
const char* axfPath();

// ---- symbols of the release axf (demangled names, "plug::(anonymous namespace)::s_slot"), size 0 = any. 0: not found
uint32_t sym(const char* name, uint32_t size = 0);
uint32_t symSize(const char* name, uint32_t size = 0);

// ---- address translation
uint8_t* g2h(uint32_t guest, uint32_t len = 1);    // host pointer of a guest range inside one window, nullptr otherwise
bool h2g(const void* host, uint32_t& guest, uint32_t len = 1);
uint32_t h2gOr0(const void* host, uint32_t len = 1);
// The host bytes of the axf symbol `name` (its size must be `bytes`): what a firmware global becomes so the guest sees it at its device
// address. Without an image (or no such symbol) a private zeroed buffer of that size: the firmware still runs, plugins do not.
uint8_t* symWindow(const char* name, uint32_t bytes);
// A host global a plugin reads through a pointer the host gave it: copied to `guest` before every call, h2g(host) answers `guest`
void shadow(uint32_t guest, const void* host, uint32_t bytes);

// Host memory a plugin reaches through a pointer the firmware passes (the .ptfx sample buffers): mapped read/write into every lane at a
// guest address of its own, together with the contiguous host run around it (a plugin reads earlier input through the pointer it got
// blocks ago). 0 when the pointer is no mapped read/write host memory. Pointers already in a window (or alias) answer at once.
uint32_t aliasMem(const void* host, uint32_t bytes);

// ---- guest heap (the device malloc of plug_host's hostAlloc): 8 aligned, zeroed, nullptr when full
void* heapAlloc(uint32_t bytes);
void heapFree(void* p);

// ---- native services behind the image's patched functions
struct Services
{
    void* (*alloc)(uint32_t bytes, uint32_t flags);   // plug_host hostAlloc
    uint32_t (*cycles)();                             // hostCycles
    const void* (*query)(uint32_t id);                // hostQuery: host pointers, translated (PLUG_EXT_FILES -> the image's kFiles)
};
void setServices(const Services& s);
const Services& services();   // what setServices was given last (a command that answers query itself for a while restores it)
// Any other image function answered natively: the handler reads and writes the registers (reg/sreg), the function returns to its caller
typedef void (*Override)(void* uc);
bool override(uint32_t guestFn, Override h);
// A handler that runs every time the guest is about to execute the instruction at guestPc, which then executes as it is (no patch)
bool hookAt(uint32_t guestPc, Override h);
uint32_t reg(void* uc, int n);   // r0..r3 (n 0..3), sp (13), lr (14)
void setReg(void* uc, int n, uint32_t v);
float sreg(void* uc, int n);     // s0..s15
void setSreg(void* uc, int n, float v);

// ---- lanes and calls
struct LaneScope
{
    explicit LaneScope(int lane);
    ~LaneScope();
    int prev;
};
int currentLane();
// Calls guest function fn (Thumb bit added) with r0..r3 = ints, s0..s3 = floats on the current lane. Returns r0, *s0 = s0.
// A guest fault returns 0 and counts in faults() (logged once per lane and pc).
uint32_t call(uint32_t fn, const uint32_t* ints, int nInts, const float* floats = nullptr, int nFloats = 0, float* s0 = nullptr);
uint32_t call(uint32_t fn, uint32_t a = 0, uint32_t b = 0, uint32_t c = 0, uint32_t d = 0);
void submit(int lane, void (*job)(void*), void* arg);   // runs job(arg) on the lane's thread, returns at once
void join(int lane);                                    // waits for the lane's submitted jobs
uint64_t faults();
// Wall time limit of every later guest call in us, 0 none (the default): a call over it counts as a fault and returns 0 (tests that run
// corrupt images, which may loop)
void setCallTimeout(uint32_t us);
uint64_t calls();
uint64_t instructions(int lane);   // 0 unless UISHOT_BRIDGE_COUNT=1 (a per instruction hook, slow)
// The text of guest code changed (plug::syncCode, an install): every lane drops its translated blocks before its next call
void codeChanged();

// ---- image calls (the release image's own functions)
uint32_t hostTable();   // guest address of plug::s_host
uint32_t entry(uint32_t entryRun);   // plug_entry(&s_host, ABI) of a placed image: the guest plug_desc, 0 for none or a fault
int checkDesc(uint32_t gdesc, uint32_t base, uint32_t size, uint32_t wantKind);   // plug::checkDesc, a plug::Err
uint32_t tempoExt(uint32_t gdesc, uint32_t base, uint32_t size);   // plug::tempoExt: the guest plug_ext_tempo or 0
uint32_t tailExt(uint32_t gdesc, uint32_t base, uint32_t size);
bool fn(uint32_t f, uint32_t lo, uint32_t n, bool need);   // plug_synth.cpp's fn() (anonymous namespace in the image)

// ---- host mirrors of guest structs (stable for the life of the process per guest address: a reload refreshes the same mirror)
const plug_desc* mirrorDesc(uint32_t gdesc);
const plug_ext_tempo* mirrorTempo(uint32_t g);
const plug_ext_tail* mirrorTail(uint32_t g);
const plug_ext_synth* mirrorSynth(uint32_t g);
const plug_ext_chord* mirrorChord(uint32_t g);   // the guest bytes (the struct has no pointers)
// the guest address behind a mirror (0 when p is no mirror of that kind)
uint32_t guestOf(const plug_desc* p);
uint32_t guestOf(const plug_ext_synth* p);
uint32_t guestProcess(const plug_desc* p);   // the guest process() of a mirror (Thumb bit set), what host.txt prints on the unit
// .ptfx: the three entry points of a placed .ptfx (guest addresses with the Thumb bit) as native function pointers
void mirrorPtfx(uint32_t gInit, uint32_t gSetParam, uint32_t gProcess, PtfxInitFunc& init, PtfxSetParamFunc& setParam,
                PtfxProcessBlockFunc& process);
// The SynthVoice of the unit (152 B, plug_synth.cpp kLayout) as the host build computes it (160 B): the layout word a guest engine
// carries is translated in mirrorSynth when it is the device's, so a native layout check sees the host's word
uint32_t deviceSynthLayout();
uint32_t hostSynthLayout();
// The guest shadow of a native SynthVoice (152 B device layout), 0 if none yet
uint32_t voiceShadow(const void* hostVoice);
}   // namespace ucb
