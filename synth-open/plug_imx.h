// The IMX plugin host core (Tracker+ / Mini, ref/ff-imx-port-plan.md R2.1b-d, P5 H1 H2 H3): the SDRAM code slots, the cache Ops
// of plug_load.h and the plug_host services table. No FlexNVM, no bank: plug_nvm.cpp, plug_fccob.h and plug_bank.h stay MK66 only.
// The IMX core build compiles plug_imx.cpp (core/makefile, PLATFORM=IMX), the MK66 build compiles it empty. P6 (engines) and P7
// (inserts) are the first callers, until then the linker drops every symbol of it (--gc-sections) and the image is unchanged.
#pragma once

#if defined(PLATFORM_IMX)

#include "plug_load.h"

namespace plug
{
namespace imx
{

// The codec rate of the IMX image (R2.1c last bullet). AUDIO PLL = 24 MHz * (37 + 15168 / 24000) = 903.168 MHz, post divider 2,
// SAI1 pre 4, podf 10: MCLK 11.2896 MHz = 256 * 44,100 (platforms/imx/board/clock_config.c audioPllConfig_BOARD_BootClockRUN and the
// SAI1 lines, peripherals.c kSAI_SampleRate44100Hz). So the IMX runs at 44,100 Hz exactly, not the MK66 44,117.647 Hz that
// core/libs/Audio/AudioStream.h AUDIO_SAMPLE_RATE_EXACT names for both platforms. The host passes the real rate.
constexpr unsigned long long kPllInHz = 24000000ull;
constexpr unsigned long long kPllNum = 15168ull, kPllDen = 24000ull, kPllLoop = 37ull;
constexpr unsigned long long kMclkHz = kPllInHz * (kPllLoop * kPllDen + kPllNum) / kPllDen / 2ull / 4ull / 10ull;
static_assert(kMclkHz == 11289600ull, "IMX SAI1 MCLK is 11.2896 MHz");
constexpr uint32_t kCodecRateHz = uint32_t(kMclkHz / 256ull);
static_assert(kCodecRateHz == 44100u, "IMX codec rate");
constexpr float kSampleRate = float(kCodecRateHz);

// DWT CYCCNT runs at the core clock: 600 MHz on the RT1050 (BOARD_BootClockRUN). Every cycles reader scales by this, not by the
// MK66 180 MHz that plug_nvm.cpp hardcodes.
constexpr uint32_t kCoreHz = 600000000u;

// Plugin engines and stale code lines would mix if a load skipped a step: the order is copy, rebase, bss, clean, invalidate,
// barrier (plug_load.h). On the target these are the core_cm7.h register writes that fsl_cache.h DCACHE_CleanByRange and
// SCB_InvalidateICache make (the core build has no platforms/imx include path). The sim counts the calls.
struct CacheOps
{
    void cleanDCache(const void* p, uint32_t n);
    void invalidateICache();
    void barrier();
    uint32_t cleaned = 0, invalidated = 0, barriers = 0;   // calls, read by the host tests
};

// The code slots in SDRAM (R3: no ITCM or OCRAM room). A synth part that plays a plugin engine owns one synth slot, both inserts
// share the insert slot as on the MK66 (insert 1 from the bottom, insert 2 from the top).
uint8_t* synthSlot(int part);   // nullptr outside 0..kImxSynthSlots-1
uint8_t* insertSlot();
uint32_t slotAddr(const uint8_t* slot);   // the run address the loader rebases for (the slot's own address on the unit)

#if defined(UI_SHOT_HOST)
// The plus BRIDGE sim (H8, host_imxplug_test.cc): the 3 synth slots then the insert slot (kImxCodeSlotsBytes) are taken from host
// memory that Unicorn maps at guest, so a load rebases for the guest address. simOps: the cache step counts of the loads so far
void simSlots(uint8_t* host, uint32_t guest);
const CacheOps& simOps();
#endif

// The cache steps of a load that placed code itself (the insert host streams a .plg into insertSlot() at the pair rule's offset,
// plug_host.cpp): clean the D-cache over [p, p + n), invalidate the I-cache, DSB and ISB. Counted in simOps() as a load is
void syncCode(const void* p, uint32_t n);

// Loads a .plg held whole in file[0..len) into synth slot part (kind PLUG_KIND_SYNTH) or the insert slot (part -1, any insert kind).
Err load(const uint8_t* file, uint32_t len, uint32_t kind, int part, uint32_t& entryAddr);

// The services table (plug_abi.h struct plug_host). alloc takes from the arena the caller sets around a plugin's init (beginAlloc
// to endAlloc), flags 0 and 1 both come from it (the IMX has no spare SRAM heap, R3.2).
const plug_host* host();
void beginAlloc(uint8_t* arena, uint32_t bytes);
void endAlloc();
uint32_t cyclesToMicros(uint32_t cycles);

}  // namespace imx
}  // namespace plug

#endif  // PLATFORM_IMX
