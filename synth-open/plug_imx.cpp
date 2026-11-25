// The IMX plugin host core, see plug_imx.h. The whole file is PLATFORM_IMX only: the MK66 build (SYNTH_OPEN_SRCS takes every
// synth-open/*.cpp) compiles it empty.
#if defined(PLATFORM_IMX)

#include "plug_imx.h"
#include "cr_section_macros.h"

#include <math.h>
#include <string.h>

namespace plug
{
namespace imx
{
namespace
{
// 32 B aligned: a D-cache line of the M7, so a clean never writes back a neighbour's line. NOINIT: the loader writes every byte
// it runs (image and bss), the rest of a slot is never read.
// Each slot array has its own input section (.noinit.$EXTERNAL_RAM.<name>, linkerScript.ld takes .noinit.$EXTERNAL_RAM.*): with one shared
// __NOINIT section --gc-sections kept the three synth slots (98,304 B) as soon as the insert host referenced the insert slot (imx-p6 round 3)
#if defined(UI_SHOT_HOST)
#define PLUG_IMX_SLOT_SECTION(name) __NOINIT(EXTERNAL_RAM)
#else
#define PLUG_IMX_SLOT_SECTION(name) __attribute__((section(".noinit.$EXTERNAL_RAM." name)))
#endif
PLUG_IMX_SLOT_SECTION("plug_synthslot") __attribute__((aligned(32))) uint8_t s_synthSlot[kImxSynthSlots][kImxSynthSlotBytes];
PLUG_IMX_SLOT_SECTION("plug_insertslot") __attribute__((aligned(32))) uint8_t s_insertSlot[kImxInsertSlotBytes];

uint8_t* s_allocAt;
uint32_t s_allocLeft;

void* hostAlloc(uint32_t bytes, uint32_t /*flags*/)
{
    bytes = (bytes + 7u) & ~7u;
    if (!s_allocAt || bytes > s_allocLeft) return nullptr;   // init only, never an exception
    void* p = s_allocAt;
    s_allocAt += bytes;
    s_allocLeft -= bytes;
    memset(p, 0, bytes);
    return p;
}

uint32_t hostCycles()
{
#if defined(UI_SHOT_HOST)
    return 0;
#else
    return *reinterpret_cast<volatile uint32_t*>(0xE0001004u);   // DWT_CYCCNT, 600 MHz (kCoreHz)
#endif
}

// host level 2: PLUG_EXT_FILES needs the FatFs wave list of plug_synth.cpp (P6), PLUG_EXT_STRUM the project's ramp depth (P6).
// Until then every id answers NULL, which a plugin reads as "not offered" (plug_abi.h).
const void* hostQuery(uint32_t)
{
    return nullptr;
}

float hSin(float x) { return sinf(x); }
float hCos(float x) { return cosf(x); }
float hTan(float x) { return tanf(x); }
float hTanh(float x) { return tanhf(x); }
float hExp(float x) { return expf(x); }
float hLog(float x) { return logf(x); }
float hPow(float x, float y) { return powf(x, y); }
float hExp2(float x) { return exp2f(x); }
float hLog2(float x) { return log2f(x); }

// the IMX newlib libm (not bit identical to the MK66 build, golden renders are per platform, R2.1d)
const plug_host s_host = {
    sizeof(plug_host), PLUG_HOST_LEVEL, kSampleRate, hSin, hCos, hTan, hTanh, hExp, hLog, hPow, memcpy, memset,
    hostAlloc,         hostCycles,      hostQuery,   hExp2, hLog2,
};

CacheOps s_ops;

#if defined(UI_SHOT_HOST)
// The plus BRIDGE sim (H8): the slots live in a host buffer that Unicorn maps at an IMX SDRAM address, so the loader rebases for
// the guest address and the guest runs the very bytes it placed (simSlots). Null: the static slots, host addresses (host tests)
uint8_t* s_simSlots;
uint32_t s_simGuest;
#endif
}  // namespace

void CacheOps::cleanDCache(const void* p, uint32_t n)
{
    cleaned++;
#if !defined(UI_SHOT_HOST)
    // SCB_CleanDCache_by_Addr (core_cm7.h, what fsl_cache.h DCACHE_CleanByRange calls): DCCMVAC per 32 B line, then DSB
    uintptr_t a = uintptr_t(p) & ~uintptr_t(31);
    const uintptr_t end = uintptr_t(p) + n;
    __asm volatile("dsb 0xF" ::: "memory");
    for (; a < end; a += 32) *reinterpret_cast<volatile uint32_t*>(0xE000EF68u) = uint32_t(a);
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
#else
    (void)p;
    (void)n;
#endif
}

void CacheOps::invalidateICache()
{
    invalidated++;
#if !defined(UI_SHOT_HOST)
    // SCB_InvalidateICache: ICIALLU, then DSB and ISB
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
    *reinterpret_cast<volatile uint32_t*>(0xE000EF50u) = 0u;
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
#endif
}

void CacheOps::barrier()
{
    barriers++;
#if !defined(UI_SHOT_HOST)
    __asm volatile("dsb 0xF" ::: "memory");
    __asm volatile("isb 0xF" ::: "memory");
#endif
}

uint8_t* synthSlot(int part)
{
    if (part < 0 || part >= kImxSynthSlots) return nullptr;
#if defined(UI_SHOT_HOST)
    if (s_simSlots) return s_simSlots + uint32_t(part) * kImxSynthSlotBytes;
#endif
    return s_synthSlot[part];
}

uint8_t* insertSlot()
{
#if defined(UI_SHOT_HOST)
    if (s_simSlots) return s_simSlots + uint32_t(kImxSynthSlots) * kImxSynthSlotBytes;
#endif
    return s_insertSlot;
}

uint32_t slotAddr(const uint8_t* slot)
{
#if defined(UI_SHOT_HOST)
    if (s_simSlots) return s_simGuest + uint32_t(slot - s_simSlots);
#endif
    return uint32_t(uintptr_t(slot));
}

#if defined(UI_SHOT_HOST)
void simSlots(uint8_t* host, uint32_t guest)
{
    s_simSlots = host;
    s_simGuest = guest;
}

const CacheOps& simOps()
{
    return s_ops;
}
#endif

Err load(const uint8_t* file, uint32_t len, uint32_t kind, int part, uint32_t& entryAddr)
{
    entryAddr = 0;
    uint8_t* slot = part < 0 ? insertSlot() : synthSlot(part);
    if (!slot || (part >= 0 && kind != PLUG_KIND_SYNTH)) return kAbi;
    const uint32_t slotLen = part < 0 ? kImxInsertSlotBytes : kImxSynthSlotBytes;
    return loadImage(file, len, kind, slot, slotLen, slotAddr(slot), entryAddr, s_ops);
}

void syncCode(const void* p, uint32_t n)
{
    s_ops.cleanDCache(p, n);
    s_ops.invalidateICache();
    s_ops.barrier();
}

const plug_host* host()
{
    return &s_host;
}

void beginAlloc(uint8_t* arena, uint32_t bytes)
{
    s_allocAt = arena;
    s_allocLeft = arena ? bytes : 0;
}

void endAlloc()
{
    beginAlloc(nullptr, 0);
}

uint32_t cyclesToMicros(uint32_t cycles)
{
    return uint32_t(uint64_t(cycles) * 1000000u / kCoreHz);
}

}  // namespace imx
}  // namespace plug

#endif  // PLATFORM_IMX
