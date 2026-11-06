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

// Local loops, not host calls: the compiler emits these for struct copies and zeroing, often in audio code.
void* memcpy(void* d, const void* s, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    const unsigned char* sp = (const unsigned char*)s;
    while (n--) *dp++ = *sp++;
    return d;
}

void* memset(void* d, int c, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}
