#pragma once
// Host seams of the FF layer. The K66 core registers and barriers the plugin host touches (DWT_CYCCNT, LMEM_PCCCR, MPU_CESR, dsb, isb), as one seam. The image compiles
// the register access and the instruction inline, the same code as a raw access in place. The host simulator (UI_SHOT_HOST, ui_shot) has no
// such registers: every register is one plain variable of the simulator (uishot_fake_reg) and a barrier orders nothing, so the simulator
// compiles the shipped source instead of a regex-edited copy (synth-open/tools/ui_shot/gen_overlay.py).
#include <stdint.h>

#ifdef UI_SHOT_HOST
extern "C" volatile uint32_t* uishot_fake_reg();
extern "C" void uishotHashBlock(int part, const float* l, const float* r);
#endif

// A synth part rendered its block into l and r (open_synth.cpp): the simulator hashes it (`poke renderhash`, the bit exact reference of
// synth_nna_sim.sh). Nothing in the image, the arguments are not evaluated
#ifdef UI_SHOT_HOST
#define FF_HOST_PART_RENDERED(part, l, r) uishotHashBlock((part), (l), (r))
#else
#define FF_HOST_PART_RENDERED(part, l, r) ((void)0)
#endif

namespace ffhw
{
__attribute__((always_inline)) inline volatile uint32_t* reg(uintptr_t addr)
{
#ifndef UI_SHOT_HOST
    return reinterpret_cast<volatile uint32_t*>(addr);
#else
    (void)addr;
    return uishot_fake_reg();
#endif
}

__attribute__((always_inline)) inline void dsb()
{
#ifndef UI_SHOT_HOST
    asm volatile("dsb 0xf" ::: "memory");
#endif
}

__attribute__((always_inline)) inline void isb()
{
#ifndef UI_SHOT_HOST
    asm volatile("isb 0xf" ::: "memory");
#endif
}
}   // namespace ffhw
