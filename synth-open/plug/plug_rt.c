// Plugin runtime (linked into every plugin, never into the firmware): libm and the memory functions the compiler may call,
// forwarded to the host table that plug_entry() stored. No newlib in a plugin, so no libm code is duplicated in the file.
#include "plug_abi.h"

const plug_host* plug_rt_host;

float sinf(float x) { return plug_rt_host->sinf(x); }
float cosf(float x) { return plug_rt_host->cosf(x); }
float tanf(float x) { return plug_rt_host->tanf(x); }
float tanhf(float x) { return plug_rt_host->tanhf(x); }
float expf(float x) { return plug_rt_host->expf(x); }
float logf(float x) { return plug_rt_host->logf(x); }
float powf(float x, float y) { return plug_rt_host->powf(x, y); }
#ifdef PLUG_RT_LIBM11
// Synth plugins (host ABI 1.1, build_plug.py --kind synth): the firmware's own exp2f and log2f, so a port of a built-in engine
// calls the same libm the built-in one did (bit identical output)
float exp2f(float x) { return plug_rt_host->exp2f(x); }
float log2f(float x) { return plug_rt_host->log2f(x); }
#else
float exp2f(float x) { return plug_rt_host->powf(2.0f, x); }
#endif
// One FPU instruction, no host call (the compiler calls sqrtf for the errno path without -fno-math-errno).
float sqrtf(float x)
{
    float r;
    __asm__("vsqrt.f32 %0, %1" : "=t"(r) : "t"(x));
    return r;
}

// Local loops, not host calls: the compiler emits these for struct copies and zeroing, often in audio code (the engines
// clear their per-chunk sum buffers with memset: as a byte loop that was 12 to 15k cycles per block for CS-Type, B-Type
// and 8-Type at 4 voices). Word stores when the pointers are 4 B aligned, bytes for the rest. no-tree-loop-distribute-
// patterns: GCC must not turn these loops back into calls to themselves.
#define PLUG_RT_NO_IDIOM __attribute__((optimize("no-tree-loop-distribute-patterns")))
typedef uint32_t __attribute__((may_alias)) plug_rt_word;   // the bytes may belong to any type
PLUG_RT_NO_IDIOM void* memcpy(void* d, const void* s, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    const unsigned char* sp = (const unsigned char*)s;
    if ((((uintptr_t)dp | (uintptr_t)sp) & 3u) == 0)
    {
        plug_rt_word* wd = (plug_rt_word*)dp;
        const plug_rt_word* ws = (const plug_rt_word*)sp;
        for (; n >= 4; n -= 4) *wd++ = *ws++;
        dp = (unsigned char*)wd;
        sp = (const unsigned char*)ws;
    }
    while (n--) *dp++ = *sp++;
    return d;
}

PLUG_RT_NO_IDIOM void* memset(void* d, int c, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    if ((((uintptr_t)dp) & 3u) == 0)
    {
        const uint32_t w = (uint32_t)(unsigned char)c * 0x01010101u;
        plug_rt_word* wd = (plug_rt_word*)dp;
        for (; n >= 4; n -= 4) *wd++ = w;
        dp = (unsigned char*)wd;
    }
    while (n--) *dp++ = (unsigned char)c;
    return d;
}
