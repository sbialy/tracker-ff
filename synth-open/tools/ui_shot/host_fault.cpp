// Allocation fault injection for ui_shot. Host only, NOT firmware. Replaces the global operator new family (malloc/free underneath)
// and returns nullptr like the MK66 (its operator new is a plain malloc and returns null on a short heap, so code that does not test
// the result writes through null there).
//
//   UISHOT_FAIL=size=2048[,skip=K][,count=C]   fail the allocations of exactly this size: skip the first K, fail the next C (0 = all)
//   UISHOT_FAIL_ARM=PATH                       injection only while the file PATH exists (the scenario creates it just before the action)
//   UISHOT_FAIL_LOG=PATH                       append "size" of every allocation >= 256 B made while armed (to find the size to target)
//   UISHOT_FAIL_LOGMIN=N                       log from N bytes instead of 256 (the editor list strings are a few hundred bytes)
//   UISHOT_FAIL_REPORT=PATH                    append one line per injected failure
// With none of them set this is a plain forwarding allocator.
// Allocations of the simulator itself (the FT812 model, eve_model.cpp) are never failed: they hold a HostFaultSuspend while they
// run, so an injected null lands in firmware code only (the MK66 has no such model, a null there would be a host artefact).
#include "eve_model.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <sys/stat.h>

#define NOOPT __attribute__((noinline, optnone, no_sanitize("returns-nonnull-attribute")))

void* hostHeapAllocate(size_t size); // host_heapmodel.cpp: the device heap model (UISHOT_HEAP=device), plain malloc without it
void hostHeapRelease(void* p);

namespace
{
struct Rule
{
    bool init = false, on = false;
    size_t size = 0;
    long skip = 0, count = 1, seen = 0, failed = 0;
    size_t logMin = 256;
    const char *arm = nullptr, *logPath = nullptr, *reportPath = nullptr;
};
Rule g;
int g_suspend;

NOOPT bool armed()
{
    if (!g.arm)
        return true;
    struct stat st;
    return stat(g.arm, &st) == 0;
}

NOOPT void setup()
{
    g.init = true;
    g.arm = getenv("UISHOT_FAIL_ARM");
    g.logPath = getenv("UISHOT_FAIL_LOG");
    g.reportPath = getenv("UISHOT_FAIL_REPORT");
    if (const char* v = getenv("UISHOT_FAIL_LOGMIN"))
        g.logMin = strtoul(v, nullptr, 0);
    if (const char* v = getenv("UISHOT_FAIL"))
    {
        g.on = true;
        for (const char* p = v; p && *p;)
        {
            if (!strncmp(p, "size=", 5))
                g.size = strtoul(p + 5, nullptr, 0);
            else if (!strncmp(p, "skip=", 5))
                g.skip = atol(p + 5);
            else if (!strncmp(p, "count=", 6))
                g.count = atol(p + 6);
            p = strchr(p, ',');
            if (p)
                p++;
        }
    }
}

NOOPT void* alloc(size_t n)
{
    if (!g.init)
        setup();
    if (g.logPath && n >= g.logMin && g_suspend == 0 && armed())
    {
        if (FILE* f = fopen(g.logPath, "a"))
        {
            fprintf(f, "%zu\n", n);
            fclose(f);
        }
    }
    if (g.on && n == g.size && g_suspend == 0 && armed())
    {
        g.seen++;
        if (g.seen > g.skip && (g.count == 0 || g.failed < g.count))
        {
            g.failed++;
            if (g.reportPath)
                if (FILE* f = fopen(g.reportPath, "a"))
                {
                    fprintf(f, "failed alloc of %zu B (match %ld)\n", n, g.seen);
                    fclose(f);
                }
            return nullptr;
        }
    }
    return hostHeapAllocate(n);
}
} // namespace

HostFaultSuspend::HostFaultSuspend() { g_suspend++; }
HostFaultSuspend::~HostFaultSuspend() { g_suspend--; }

NOOPT void* operator new(size_t n) { return alloc(n); }
NOOPT void* operator new[](size_t n) { return alloc(n); }
NOOPT void* operator new(size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
NOOPT void* operator new[](size_t n, const std::nothrow_t&) noexcept { return alloc(n); }
void operator delete(void* p) noexcept { hostHeapRelease(p); }
void operator delete[](void* p) noexcept { hostHeapRelease(p); }
void operator delete(void* p, size_t) noexcept { hostHeapRelease(p); }
void operator delete[](void* p, size_t) noexcept { hostHeapRelease(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { hostHeapRelease(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { hostHeapRelease(p); }
