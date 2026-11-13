// Device heap model for ui_shot (UISHOT_HEAP=device). Host only, NOT firmware.
//
// The unit has a small heap and no safety net: MK66 newlib-nano malloc over about 52 KB (the room between _ebss and the stack margin, see
// the emulator notes for the figure from the linker map), and the core's operator new is `return malloc(size)`, so it returns null when
// the heap is full or in pieces (platforms/mk66/3rdparty/teensy_core/new.cpp). The Mac had a 64 bit system malloc that never runs out, so a
// Model roll that needed a 4.7 KB block next to a fragmented heap passed on the Mac and faulted on the unit.
//
// UISHOT_HEAP=device   serve operator new / delete of the firmware code from a fixed arena of UISHOT_HEAP_BYTES with the allocation policy of
//                      newlib-nano: address ordered free list, first fit, a split hands out the HIGHER part of the chunk, neighbours merged
//                      on free, 8 byte chunk overhead, 8 byte alignment, 16 byte minimum chunk. Returns nullptr on exhaustion.
// UISHOT_HEAP_BYTES    heap size in bytes. Default 53,692 = _estack - 232 (stack in use at the last _sbrk of Profiler::init) - 8192 (STACK_MARGIN)
//                      - _ebss 0x20020d5c of the 1.2.0 console image that faulted on the device (atf7, no arena). With the arena fix
//                      (model-roll-fix, _ebss 0x2002147c) 51,868. tools/heap_size_from_map.py prints it for any map.
// UISHOT_HEAP_ARENA    1 / 0. Default 1 when this tree has the 48 KB SDRAM arena (platforms/mk66/3rdparty/teensy_core/new_arena.h, commit
//                      98f7938): blocks of 1 KB and more come from the arena first, the heap is the fallback, a small block the heap refuses
//                      goes to the arena, and when both are out the unit executes __builtin_trap (here: abort). 0 = the firmware before the fix.
// UISHOT_HEAP_SCALE    percent of the host's requested size that is charged. The host is 64 bit with another libc++: std::string, std::function and
//                      pointers are wider, a `Patch::Metadata::Param` is 56 B on the unit and 72 B here. Default 65, calibrated against the unit: 78 would be the Param ratio
//                      alone (56 B on the unit, 72 B here) but the other blocks do not all shrink that much. At 65 the model heap with the arena has 25.4 KB
//                      free after the first roll (unit, 1.2.0 console image: 26.5 KB after boot) and without the arena operator new returns null with
//                      12.4 KB free in pieces at roll 4 (unit before the fix: 7.9 to 12 KB free at the faults, rolls 3 to 20 of the repro).
// UISHOT_HEAP_TRACE=N  print the first N allocations (size, charged size, caller) to stderr.
//
// What is counted: allocations made by the firmware code of core/ and synth-open/. The host's own stand-ins (host_*.cpp, the FT812 model, the
// console transport, ASan itself) are recognised by their caller and served from the system heap uncounted (UISHOT_HOST_SYMTAB, host_symtab.txt next to
// the binary by the Makefile). malloc() called directly (Profiler.cpp, plug_host.cpp, plug_bench.cpp) is not modelled. Not modelled either:
// libc's own allocations (printf buffers), the FatFs and USB buffers, anything before Profiler::init.
// Not with SAN= builds (ASan replaces operator new, the two do not compose): the gate runs both builds.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <execinfo.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <mutex>
#include <new>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#if defined(__has_include)
#if __has_include("new_arena.h")
#include "new_arena.h"
#define HEAPMODEL_HAVE_ARENA 1
#endif
#endif

namespace
{
constexpr size_t kHdr = 8;
constexpr size_t kMinChunk = 16;
constexpr size_t kMaxHeap = 128 * 1024;
constexpr size_t kArenaBytes = 49152;   // new.cpp kArenaBytes
constexpr size_t kBigBytes = 1024;      // new.cpp kBigBytes

struct Chunk   // in band, the way nano keeps it: size of the whole chunk, then (free chunks only) the offset of the next free chunk
{
    uint32_t size;
    uint32_t next;   // offset in the heap, 0xffffffff = none
};

struct Model
{
    bool on = false, ready = false, arena = false;
    alignas(16) uint8_t heap[kMaxHeap];
    size_t heapBytes = 0;
    uint32_t freeHead = 0xffffffffu;
    size_t used = 0, peak = 0, allocs = 0, nulls = 0, frees = 0;
    size_t minLargest = size_t(-1);
    size_t arenaBigFb = 0, arenaSmallFb = 0, maxReq = 0;
    int scalePct = 100;
    int trace = 0;
    std::mutex mu;
    struct Site { bool host; const char* name; };
    std::unordered_map<uint64_t, Site> hostSite;   // hash of the caller frames -> is the host's, and the name of the caller
    bool haveSyms = false;
#ifdef HEAPMODEL_HAVE_ARENA
    alignas(8) uint8_t arenaMem[kArenaBytes];
    NewArena arenaAlloc;
#endif
};

Model* g_m = nullptr;

// host block -> model block, for the counted blocks (uncounted ones are not in it)
struct Live { void* mod; const char* site; size_t charged; };
std::unordered_map<void*, Live>& sideTable()
{
    static auto* t = new std::unordered_map<void*, Live>();
    return *t;
}
thread_local bool t_in = false;

Chunk* at(Model& m, uint32_t off) { return reinterpret_cast<Chunk*>(m.heap + off); }

uint32_t off(Model& m, const void* p) { return uint32_t(static_cast<const uint8_t*>(p) - m.heap); }

size_t largestFree(Model& m)
{
    size_t best = 0;
    for (uint32_t o = m.freeHead; o != 0xffffffffu; o = at(m, o)->next)
        best = std::max<size_t>(best, at(m, o)->size);
    return best > kHdr ? best - kHdr : 0;
}

size_t freeBytes(Model& m)
{
    size_t f = 0;
    for (uint32_t o = m.freeHead; o != 0xffffffffu; o = at(m, o)->next)
        f += at(m, o)->size;
    return f;
}

void* heapAlloc(Model& m, size_t n)
{
    size_t need = (n + kHdr + 7) & ~size_t(7);
    if (need < kMinChunk)
        need = kMinChunk;
    uint32_t prev = 0xffffffffu;
    for (uint32_t o = m.freeHead; o != 0xffffffffu; prev = o, o = at(m, o)->next)
    {
        Chunk* c = at(m, o);
        if (c->size < need)
            continue;
        const size_t rem = c->size - need;
        Chunk* got;
        if (rem >= kMinChunk)
        {
            c->size = uint32_t(rem);   // the head stays in the list, the tail is handed out (nano)
            got = at(m, o + uint32_t(rem));
            got->size = uint32_t(need);
        }
        else
        {
            if (prev == 0xffffffffu)
                m.freeHead = c->next;
            else
                at(m, prev)->next = c->next;
            got = c;
        }
        m.used += got->size;
        m.peak = std::max(m.peak, m.used);
        return reinterpret_cast<uint8_t*>(got) + kHdr;
    }
    return nullptr;
}

void heapFree(Model& m, void* p)
{
    Chunk* c = reinterpret_cast<Chunk*>(static_cast<uint8_t*>(p) - kHdr);
    const uint32_t co = off(m, c);
    m.used -= c->size;
    uint32_t prev = 0xffffffffu, o = m.freeHead;
    while (o != 0xffffffffu && o < co)
    {
        prev = o;
        o = at(m, o)->next;
    }
    c->next = o;
    if (prev == 0xffffffffu)
        m.freeHead = co;
    else
        at(m, prev)->next = co;
    if (o != 0xffffffffu && co + c->size == o)   // merge with the next chunk
    {
        c->size += at(m, o)->size;
        c->next = at(m, o)->next;
    }
    if (prev != 0xffffffffu && prev + at(m, prev)->size == co)   // and with the previous one
    {
        at(m, prev)->size += c->size;
        at(m, prev)->next = c->next;
    }
}

bool inHeap(Model& m, const void* p)
{
    return static_cast<const uint8_t*>(p) >= m.heap && static_cast<const uint8_t*>(p) < m.heap + m.heapBytes;
}

struct SymEnt { uintptr_t addr; bool host; const char* name; };
std::vector<SymEnt>* g_syms = nullptr;

void loadSyms(Model& m)
{
    const char* path = getenv("UISHOT_HOST_SYMTAB");
    char buf[2048];
    std::string alt;
    if (!path)
    {
        Dl_info di;
        if (dladdr(reinterpret_cast<void*>(&loadSyms), &di) && di.dli_fname)
        {
            alt = di.dli_fname;
            alt = alt.substr(0, alt.rfind('/') + 1) + "host_symtab.txt";
            path = alt.c_str();
        }
    }
    FILE* f = path ? fopen(path, "r") : nullptr;
    if (!f)
        return;
    g_syms = new std::vector<SymEnt>();
    while (fgets(buf, sizeof buf, f))
    {
        buf[strcspn(buf, "\r\n")] = 0;
        char* sp1 = strchr(buf, ' ');
        if (!sp1 || !sp1[1] || sp1[2] != ' ')
            continue;
        g_syms->push_back({uintptr_t(strtoull(buf, nullptr, 16)), sp1[1] == 'H', strdup(sp1 + 3)});
    }
    fclose(f);
    m.haveSyms = !g_syms->empty();
}

const SymEnt* symAt(uintptr_t linkAddr)
{
    if (!g_syms || g_syms->empty())
        return nullptr;
    size_t lo = 0, hi = g_syms->size();
    while (lo < hi)
    {
        const size_t mid = (lo + hi) / 2;
        if ((*g_syms)[mid].addr <= linkAddr)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo ? &(*g_syms)[lo - 1] : nullptr;
}

// The caller is the host's own when the first frame above the allocator plumbing (libc++ containers, operator new) is a host function.
bool hostCaller(Model& m, const char** siteName)
{
    void* fr[10];
    const int n = backtrace(fr, 10);
    uint64_t key = 1469598103934665603ull;
    for (int i = 0; i < n; i++)
        key = (key ^ uint64_t(reinterpret_cast<uintptr_t>(fr[i]))) * 1099511628211ull;
    auto it = m.hostSite.find(key);
    if (it != m.hostSite.end())
    {
        *siteName = it->second.name;
        return it->second.host;
    }
    static const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
    bool host = false;
    const char* name = "?";
    for (int i = 0; i < n; i++)
    {
        Dl_info di;
        if (!dladdr(fr[i], &di))
            continue;
        if (di.dli_fbase != static_cast<const void*>(_dyld_get_image_header(0)))
            continue;   // libc++, libsystem: plumbing
        const SymEnt* e = symAt(uintptr_t(fr[i]) - uintptr_t(slide));
        if (!e)
            continue;
        if (!strncmp(e->name, "_ZN12_GLOBAL__N_110hostCaller", 29) || !strncmp(e->name, "_ZN12_GLOBAL__N_18allocate", 26) || !strncmp(e->name, "_Znwm", 5) ||
            !strncmp(e->name, "_Znam", 5) || !strncmp(e->name, "_ZnwmRKSt9nothrow_t", 19) || !strncmp(e->name, "_ZnamRKSt9nothrow_t", 19) ||
            !strncmp(e->name, "_Z16hostHeapAllocatem", 21) || !strncmp(e->name, "_ZN12_GLOBAL__N_15allocEm", 25))
            continue;   // this file and the fault injection allocator in front of it (host_fault.cpp)
        host = e->host;
        name = e->name;
        break;
    }
    m.hostSite[key] = {host, name};
    *siteName = name;
    return host;
}

Model& model()
{
    if (!g_m)
    {
        const bool was = t_in;
        t_in = true;   // the model's own tables are not the firmware's allocations
        g_m = new (malloc(sizeof(Model))) Model();
        Model& m = *g_m;
        const char* e = getenv("UISHOT_HEAP");
        m.on = e && !strcmp(e, "device");
        if (m.on)
        {
            m.arena = false;
#ifdef HEAPMODEL_HAVE_ARENA
            const char* a = getenv("UISHOT_HEAP_ARENA");
            m.arena = !a || strcmp(a, "0") != 0;
#else
            m.arena = false;
#endif
            const char* b = getenv("UISHOT_HEAP_BYTES");
            m.heapBytes = b ? size_t(atol(b)) : (m.arena ? 51868 : 53692);
            if (m.heapBytes > kMaxHeap)
                m.heapBytes = kMaxHeap;
            const char* s = getenv("UISHOT_HEAP_SCALE");
            m.scalePct = s ? atoi(s) : 65;   // calibrated, see the header
            const char* t = getenv("UISHOT_HEAP_TRACE");
            m.trace = t ? atoi(t) : 0;
            Chunk* c = at(m, 0);
            c->size = uint32_t(m.heapBytes & ~size_t(7));
            c->next = 0xffffffffu;
            m.freeHead = 0;
#ifdef HEAPMODEL_HAVE_ARENA
            if (m.arena)
                m.arenaAlloc.init(m.arenaMem, kArenaBytes);
#endif
            loadSyms(m);
        }
        m.ready = true;
        t_in = was;
    }
    return *g_m;
}

void* allocate(size_t size)
{
    Model& m = model();
    if (!m.on || t_in)
    {
        void* p = malloc(size ? size : 1);
        return p;
    }
    t_in = true;
    void* result = nullptr;
    bool counted = true;
    const char* site = "?";
    if (m.haveSyms && hostCaller(m, &site))
        counted = false;
    if (!counted)
    {
        result = malloc(size ? size : 1);
    }
    else
    {
        std::lock_guard<std::mutex> lock(m.mu);
        const size_t charged = std::max<size_t>(1, size * size_t(m.scalePct) / 100);
        m.allocs++;
        m.maxReq = std::max(m.maxReq, charged);
        if (m.trace > 0)
        {
            m.trace--;
            void* fr[7];
            const int n = backtrace(fr, 7);
            static const intptr_t slide = _dyld_get_image_vmaddr_slide(0);
            std::string who;
            for (int i = 1; i < n; i++)
            {
                const SymEnt* e = symAt(uintptr_t(fr[i]) - uintptr_t(slide));
                who += std::string(e ? (e->host ? "H:" : "F:") : "?:") + (e ? std::string(e->name).substr(0, 60) : std::string("-")) + " < ";
            }
            fprintf(stderr, "heapmodel: new %zu (charged %zu) from %s\n", size, charged, who.c_str());
        }
        void* p = nullptr;
#ifdef HEAPMODEL_HAVE_ARENA
        if (m.arena)
        {
            if (charged >= kBigBytes)
            {
                p = m.arenaAlloc.alloc(charged);
                if (!p)
                    m.arenaBigFb++;
            }
            if (!p)
                p = heapAlloc(m, charged);
            if (!p)
            {
                p = m.arenaAlloc.alloc(charged);
                if (p)
                    m.arenaSmallFb++;
            }
            if (!p)
            {
                fprintf(stderr, "heapmodel: operator new(%zu) found nothing in the heap or the arena: the unit executes __builtin_trap here\n", charged);
                abort();
            }
        }
        else
#endif
        {
            p = heapAlloc(m, charged);
            if (!p)
            {
                m.nulls++;
                fprintf(stderr, "heapmodel: operator new(%zu) returns null: heap %zu B, free %zu B in pieces, largest free block %zu B (the unit has the same bus fault here)\n",
                        charged, m.heapBytes, freeBytes(m), largestFree(m));
            }
        }
        if (p)
        {
            // the real storage: the model only decides whether the request fits. A shadow block of the host's own size backs it
            result = malloc(size ? size : 1);
            sideTable()[result] = {p, site, charged};
            if (m.trace >= 0 && getenv("UISHOT_HEAP_PTRS"))
                fprintf(stderr, "heapmodel: A %p %zu %s\n", result, size, site);
            const size_t lf = largestFree(m);
            m.minLargest = std::min(m.minLargest, lf);
        }
    }
    t_in = false;
    return result;
}
}   // namespace

static void release(void* p)
{
    if (!p)
        return;
    Model& m = model();
    if (m.on && !t_in)
    {
        t_in = true;
        std::lock_guard<std::mutex> lock(m.mu);
        auto& t = sideTable();
        auto it = t.find(p);
        if (it != t.end())
        {
            void* mod = it->second.mod;
            t.erase(it);
            m.frees++;
            if (getenv("UISHOT_HEAP_PTRS"))
                fprintf(stderr, "heapmodel: F %p\n", p);
#ifdef HEAPMODEL_HAVE_ARENA
            if (m.arena && m.arenaAlloc.owns(mod))
                m.arenaAlloc.free(mod);
            else
#endif
                heapFree(m, mod);
            m.minLargest = std::min(m.minLargest, largestFree(m));
        }
        t_in = false;
    }
    free(p);
}

// The global operator new / delete family is defined in host_fault.cpp (allocation fault injection), which forwards here.
void* hostHeapAllocate(size_t size) { return allocate(size); }
void hostHeapRelease(void* p) { release(p); }

// ---- reports -------------------------------------------------------------------------------------------------------------------------
// the arena command of the console (UsbTest.cpp) on the Mac: size, in use, peak, allocations, big blocks that fell back to the heap, small
// blocks that fell back to the arena, largest request, largest free piece, free bytes (new.cpp operatorNewArenaStats)
void operatorNewArenaStats(uint32_t* o)
{
    for (int i = 0; i < 9; i++)
        o[i] = 0;
#ifdef HEAPMODEL_HAVE_ARENA
    Model& m = model();
    if (m.on && m.arena)
    {
        o[0] = uint32_t(kArenaBytes);
        o[1] = uint32_t(m.arenaAlloc.usedBytes());
        o[2] = uint32_t(m.arenaAlloc.peakBytes());
        o[3] = uint32_t(m.allocs);
        o[4] = uint32_t(m.arenaBigFb);
        o[5] = uint32_t(m.arenaSmallFb);
        o[6] = uint32_t(m.maxReq);
        o[7] = uint32_t(m.arenaAlloc.largestFree());
        o[8] = uint32_t(m.arenaAlloc.freeBytes());
    }
#endif
}

// mallinfo().fordblks of the unit: free bytes of the heap (Profiler::freeRamBytes). -1: the model is off
long hostHeapFreeBytes()
{
    Model& m = model();
    if (!m.on)
        return -1;
    std::lock_guard<std::mutex> lock(m.mu);
    return long(freeBytes(m));
}

// one line for the host side console command `_heap` (devtools host_serve.cpp) and for the gate of roll_soak.py
int hostHeapReport(char* out, size_t n)
{
    Model& m = model();
    if (!m.on)
        return snprintf(out, n, "model=off");
    std::lock_guard<std::mutex> lock(m.mu);
    uint32_t a[9];
    operatorNewArenaStats(a);
    return snprintf(out, n,
                    "model=device arena=%d size=%zu used=%zu peak=%zu free=%zu lfree=%zu minlfree=%zu allocs=%zu frees=%zu nulls=%zu maxreq=%zu "
                    "aused=%u apeak=%u alfree=%u afree=%u bigfb=%u smallfb=%u scale=%d",
                    m.arena ? 1 : 0, m.heapBytes, m.used, m.peak, freeBytes(m), largestFree(m),
                    m.minLargest == size_t(-1) ? largestFree(m) : m.minLargest, m.allocs, m.frees, m.nulls, m.maxReq, a[1], a[2], a[7], a[8],
                    a[4], a[5], m.scalePct);
}

// the live counted blocks by caller (top n), for the leak hunt: `_heapsites`
int hostHeapSites(char* out, size_t n)
{
    Model& m = model();
    if (!m.on)
        return snprintf(out, n, "model=off");
    std::lock_guard<std::mutex> lock(m.mu);
    int w = 0;
    t_in = true;
    {
    std::unordered_map<std::string, std::pair<size_t, size_t>> by;
    for (auto& kv : sideTable())
    {
        auto& e = by[kv.second.site];
        e.first++;
        e.second += kv.second.charged;
    }
    std::vector<std::pair<size_t, std::string>> v;
    for (auto& kv : by)
        v.push_back({kv.second.second, kv.first + " n=" + std::to_string(kv.second.first)});
    std::sort(v.rbegin(), v.rend());
    for (size_t i = 0; i < v.size() && i < 14 && size_t(w) + 200 < n; i++)
        w += snprintf(out + w, n - size_t(w), "%zu:%s|", v[i].first, v[i].second.substr(0, 150).c_str());
    }
    t_in = false;
    return w;
}
