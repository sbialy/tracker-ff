// Plugin runtime of a .wasm module (plug_wasm.h), the twin of plug_rt.c: libm and the memory functions the compiler may call,
// forwarded to the host table that plug_entry() stored. The module links no libc, so it has no function import and calls the
// very same libm the host calls (bit identical to the host's linked-in code built with the same flags).
// Build it with -fno-builtin so these bodies stay plain calls.
#include "plug_wasm.h"

const plug_host* plug_rt_host;

// PLUG_RT_WASM_ABI: only the refusal test of a host builds a module that claims another module ABI
#ifndef PLUG_RT_WASM_ABI
#define PLUG_RT_WASM_ABI PLUG_WASM_ABI
#endif
PLUG_WASM_EXPORT uint32_t plug_wasm_abi(void) { return PLUG_RT_WASM_ABI; }

float sinf(float x) { return plug_rt_host->sinf(x); }
float cosf(float x) { return plug_rt_host->cosf(x); }
float tanf(float x) { return plug_rt_host->tanf(x); }
float tanhf(float x) { return plug_rt_host->tanhf(x); }
float expf(float x) { return plug_rt_host->expf(x); }
float logf(float x) { return plug_rt_host->logf(x); }
float powf(float x, float y) { return plug_rt_host->powf(x, y); }
// ABI 1.1 host table, as plug_rt.c with PLUG_RT_LIBM11 (a host of level 1 without them gets the powf form)
float exp2f(float x) { return PLUG_HAS(plug_rt_host, plug_host, exp2f) ? plug_rt_host->exp2f(x) : plug_rt_host->powf(2.0f, x); }
float log2f(float x) { return PLUG_HAS(plug_rt_host, plug_host, log2f) ? plug_rt_host->log2f(x) : plug_rt_host->logf(x) * 1.44269504f; }
// One wasm instruction each
float sqrtf(float x) { return __builtin_sqrtf(x); }
float fabsf(float x) { return __builtin_fabsf(x); }
float floorf(float x) { return __builtin_floorf(x); }
float ceilf(float x) { return __builtin_ceilf(x); }
float truncf(float x) { return __builtin_truncf(x); }

// Local loops as on the device (bulk memory instructions when the compiler picks them)
void* memcpy(void* d, const void* s, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    const unsigned char* sp = (const unsigned char*)s;
    while (n--) *dp++ = *sp++;
    return d;
}

void* memmove(void* d, const void* s, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    const unsigned char* sp = (const unsigned char*)s;
    if (dp < sp)
        while (n--) *dp++ = *sp++;
    else
        while (n--) dp[n] = sp[n];
    return d;
}

void* memset(void* d, int c, size_t n)
{
    unsigned char* dp = (unsigned char*)d;
    while (n--) *dp++ = (unsigned char)c;
    return d;
}

// C++ support a static engine object may pull in (nothing is ever destroyed, no exceptions)
int __cxa_atexit(void (*f)(void*), void* p, void* d)
{
    (void)f;
    (void)p;
    (void)d;
    return 0;
}
void __cxa_pure_virtual(void) { __builtin_trap(); }
// operator delete(void*, size_t): synth_plug.h defines the ARM pair (size_t is unsigned int there, unsigned long on wasm32)
void _ZdlPvm(void* p, unsigned long n)
{
    (void)p;
    (void)n;
}
void* __dso_handle;
