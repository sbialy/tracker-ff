// ui_shot BRIDGE=1 only: the libunicorn bridge core of the emu parity plan (T2). See host_ucbridge.h.
#include "host_ucbridge.h"

#include "audio/synth_engine.h"

#include <unicorn/unicorn.h>

#include <atomic>
#include <condition_variable>
#include <cxxabi.h>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>
#ifdef __APPLE__
#include <mach/mach.h>
#include <mach/mach_vm.h>
#include <pthread/qos.h>
#endif

#ifndef UISHOT_DEFAULT_AXF
#define UISHOT_DEFAULT_AXF ""
#endif

using ot::audio::SynthNote;
using ot::audio::SynthParams;
using ot::audio::SynthSlotScratch;
using ot::audio::SynthVoice;

namespace ucb
{
namespace
{
// ------------------------------------------------------------------------------------------------ device layouts (ARM, 32 bit)
// SynthVoice on the unit: slotScratch 0, bend 4, bendStart 8, level 12, slot 16, note 17, gate 18, fading 19, cutoffOct 20, state 24
constexpr uint32_t kDevVoice = 152, kDevVoiceState = 24;
static_assert(sizeof(SynthVoice::state) == 128 && kDevVoiceState + 128 == kDevVoice, "SynthVoice device layout");
// no pointers in these three: the same bytes on both sides
static_assert(sizeof(SynthParams) == 692 && sizeof(SynthNote) == 12 && sizeof(SynthSlotScratch) == 384, "pointer free synth structs");
constexpr uint32_t kBlockFrames = 128;

// guest struct offsets (plug_abi.h on the unit)
namespace D
{
constexpr uint32_t desc_struct_size = 0, desc_kind = 4, desc_name = 8, desc_author = 12, desc_num_params = 16, desc_params = 20,
                   desc_instance_size = 24, desc_init = 28, desc_reset = 32, desc_set_param = 36, desc_process = 40, desc_query = 44,
                   desc_size = 48;
constexpr uint32_t param_size = 32;   // name 0, unit 4, min 8, max 12, def 16, step 20, enum_count 24, flags 26, enum_names 28
constexpr uint32_t sparam_size = 24;   // name 0, def 4, min 8, max 12, fmt 16, curve 17, enum_names 20
constexpr uint32_t section_size = 16;  // name 0, short_name 4, ids 8, count 12
constexpr uint32_t macro_size = 24;    // name 0, bipolar 4, count 5, targets 8 (param 0, amount 4) x 2
constexpr uint32_t synth_size = 108;   // struct_size 0 ... view 104, 4 bytes a field
}   // namespace D

// ------------------------------------------------------------------------------------------------ windows
alignas(16384) uint8_t g_flash[kFlashSize];
alignas(16384) uint8_t g_nvm[kNvmSize];
alignas(16384) uint8_t g_sramL[kSramLSize];
alignas(16384) uint8_t g_sramU[kSramUSize];
alignas(16384) uint8_t g_sdram[kSdramSize];
alignas(16384) uint8_t g_bridge[kBridgeSize];
alignas(16384) uint8_t g_imxSdram[kImxSdramSize];
// The K66 system MPU's CESR (0x4000D000): plug_synth start() of the image clears it before it runs a bank engine (T5 runs the image's
// own start() in host_synthcheck_test). A plain RAM page, no MPU behind it
alignas(16384) uint8_t g_mpu[kMpuSize];

struct Window
{
    uint32_t base, size;
    uint8_t* host;
    uint32_t prot;
};
const Window kWin[] = {
    {kFlash, kFlashSize, g_flash, UC_PROT_READ | UC_PROT_EXEC},
    {kNvm, kNvmSize, g_nvm, UC_PROT_READ | UC_PROT_EXEC},   // a plugin store into its text faults as on the unit
    {kSramL, kSramLSize, g_sramL, UC_PROT_ALL},
    {kSramU, kSramUSize, g_sramU, UC_PROT_ALL},
    {kSdram, kSdramSize, g_sdram, UC_PROT_ALL},
    {kBridge, kBridgeSize, g_bridge, UC_PROT_ALL},
    {kImxSdram, kImxSdramSize, g_imxSdram, UC_PROT_ALL},
    {kMpu, kMpuSize, g_mpu, UC_PROT_READ | UC_PROT_WRITE},
};

// bridge region
constexpr uint32_t kLaneBytes = 0x20000, kStackTop = 0x10000, kBounce = 0x10000, kBounceEnd = 0x1F000, kTrap = 0x1FF00;
constexpr uint32_t kVoiceShadows = kBridge + 0x100000, kVoiceStride = 160, kVoiceMax = 512;
constexpr uint32_t kScratchShadows = kBridge + 0x120000, kScratchMax = 64;
constexpr uint32_t kHeap = kBridge + 0x200000, kHeapEnd = kBridge + kBridgeSize;

// ------------------------------------------------------------------------------------------------ image and symbols
struct Sym
{
    std::string name;
    uint32_t addr, size;
};
std::vector<Sym> g_syms;
std::string g_err = "not initialised", g_axf;
bool g_ready = false;
std::once_flag g_once;

void say(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
void say(const char* fmt, ...)
{
    va_list a;
    va_start(a, fmt);
    fputs("ucb: ", stderr);
    vfprintf(stderr, fmt, a);
    fputc('\n', stderr);
    va_end(a);
}

const Window* winOf(uint32_t g, uint32_t len)
{
    for (const Window& w : kWin)
        if (g >= w.base && uint64_t(g) + len <= uint64_t(w.base) + w.size) return &w;
    return nullptr;
}

bool loadAxf(const char* path)
{
    FILE* f = fopen(path, "rb");
    if (!f)
    {
        g_err = std::string("no release axf at ") + path + " (UISHOT_AXF)";
        return false;
    }
    std::vector<uint8_t> d;
    fseek(f, 0, SEEK_END);
    d.resize(size_t(ftell(f)));
    fseek(f, 0, SEEK_SET);
    const bool ok = fread(d.data(), 1, d.size(), f) == d.size();
    fclose(f);
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &d[o], 2); return v; };
    if (!ok || d.size() < 52 || memcmp(d.data(), "\x7f" "ELF\x01\x01", 6) != 0 || u16(18) != 40)
    {
        g_err = std::string(path) + ": not an ARM ELF32";
        return false;
    }
    const uint32_t phoff = u32(28), shoff = u32(32);
    const uint16_t phnum = u16(44), shnum = u16(48), shentsize = u16(46);
    for (uint32_t i = 0; i < phnum; i++)
    {
        const size_t p = phoff + 32 * i;
        const uint32_t type = u32(p), off = u32(p + 4), va = u32(p + 8), pa = u32(p + 12), fs = u32(p + 16);
        if (type != 1 || !fs || off + uint64_t(fs) > d.size()) continue;
        for (uint32_t at : {va, pa})
            if (const Window* w = winOf(at, fs)) memcpy(w->host + (at - w->base), &d[off], fs);
            else if (at == va) say("axf segment at 0x%08x (%u B) outside the windows", va, fs);
    }
    for (uint32_t i = 0; i < shnum; i++)
    {
        const size_t s = shoff + size_t(shentsize) * i;
        if (u32(s + 4) != 2) continue;   // SHT_SYMTAB
        const uint32_t off = u32(s + 16), size = u32(s + 20), link = u32(s + 24);
        const size_t ls = shoff + size_t(shentsize) * link;
        const uint32_t stroff = u32(ls + 16);
        for (uint32_t k = 0; k < size / 16; k++)
        {
            const size_t e = off + 16 * k;
            const uint8_t info = d[e + 12];
            if ((info & 0xF) > 2) continue;   // NOTYPE OBJECT FUNC
            const char* raw = reinterpret_cast<const char*>(&d[stroff + u32(e)]);
            if (!*raw || raw[0] == '$') continue;
            int st = 0;
            char* dm = abi::__cxa_demangle(raw, nullptr, nullptr, &st);
            g_syms.push_back({st == 0 && dm ? dm : raw, u32(e + 4), u32(e + 8)});
            free(dm);
        }
    }
    if (g_syms.empty())
    {
        g_err = std::string(path) + ": no symbols";
        return false;
    }
    return true;
}

const Sym* findSym(const char* name, uint32_t size)
{
    for (const Sym& s : g_syms)
    {
        if (size && s.size != size) continue;
        // a function's demangled name carries its arguments: "plug::checkDesc(plug_desc const*, ...)" matches "plug::checkDesc"
        const size_t n = strlen(name);
        if (s.name.compare(0, n, name) == 0 && (s.name.size() == n || s.name[n] == '(')) return &s;
    }
    return nullptr;
}

// ------------------------------------------------------------------------------------------------ overrides and services
Services g_svc = {};
struct Ovr
{
    uint32_t addr;
    Override h;
};
std::vector<Ovr> g_ovr;
std::mutex g_ovrM;
uint32_t g_files = 0;   // guest kFiles

struct Shadow
{
    uint32_t guest;
    const void* host;
    uint32_t bytes;
};
std::vector<Shadow> g_shadows;
std::mutex g_shadowM;

// alias windows (aliasMem): live host memory a plugin reads or writes through a pointer the firmware gave it (the sample memory behind a
// .ptfx's in and out), mapped read/write into every lane at a bridge-chosen guest address. Append only, each lane maps the new ones before
// its next call (syncLane)
struct Alias
{
    uint8_t* host;
    uint32_t base, size;
};
std::vector<Alias> g_alias;
std::mutex g_aliasM;
constexpr uint32_t kAliasBase = 0x68000000, kAliasEnd = 0x70000000, kAliasReach = 8u << 20, kAliasMax = 256, kHostPage = 16384;
uint32_t g_aliasNext = kAliasBase;

void ovrHook(uc_engine* uc, uint64_t, uint32_t, void* user)
{
    reinterpret_cast<Override>(user)(uc);
}

uint32_t rdg(uint32_t g);

uint32_t toGuest(const void* p, uint32_t id)
{
    if (!p) return 0;
    if (id == PLUG_EXT_FILES && g_svc.query && p == g_svc.query(PLUG_EXT_FILES))
    {   // the image's kFiles (its functions are overridden: they call the native struct), or, when the native struct is the short one of an
        // ABI 1.2 host without the by path tail (struct_size up to read_wave: host_plugrender.cpp's transcript "files 3"), a guest copy that
        // short with the same three kFiles functions, so the plugin sees what an ABI 1.2 host gives it
        if (!g_files || static_cast<const plug_ext_files*>(p)->struct_size >= sizeof(plug_ext_files)) return g_files;
        static uint32_t shortFiles;
        if (!shortFiles)
            if (uint8_t* q = static_cast<uint8_t*>(heapAlloc(16)))
            {
                const uint32_t w[4] = {16, rdg(g_files + 4), rdg(g_files + 8), rdg(g_files + 12)};
                memcpy(q, w, 16);
                shortFiles = h2gOr0(q, 16);
            }
        return shortFiles;
    }
    uint32_t g = 0;
    if (h2g(p, g)) return g;
    static bool told;
    if (!told) say("query %u answered a host pointer outside every window: NULL to the plugin", id), told = true;
    return 0;
}

void svcAlloc(void* uc)
{
    void* p = g_svc.alloc ? g_svc.alloc(reg(uc, 0), reg(uc, 1)) : nullptr;
    uint32_t g = 0;
    if (p && !h2g(p, g, 1)) say("alloc gave a host pointer outside the windows"), g = 0;
    setReg(uc, 0, g);
}
void svcCycles(void* uc)
{
    setReg(uc, 0, g_svc.cycles ? g_svc.cycles() : 0);
}
void svcQuery(void* uc)
{
    const uint32_t id = reg(uc, 0);
    setReg(uc, 0, g_svc.query ? toGuest(g_svc.query(id), id) : 0);
}
const plug_ext_files* files()
{
    return g_svc.query ? static_cast<const plug_ext_files*>(g_svc.query(PLUG_EXT_FILES)) : nullptr;
}
char* str(uint32_t g)
{
    return reinterpret_cast<char*>(g2h(g, 1));
}
void fCount(void* uc)
{
    const plug_ext_files* f = files();
    setReg(uc, 0, uint32_t(f ? f->count() : 0));
}
void fName(void* uc)
{
    const plug_ext_files* f = files();
    char* n = reinterpret_cast<char*>(g2h(reg(uc, 1), reg(uc, 2)));
    setReg(uc, 0, uint32_t(f && n ? f->name(reg(uc, 0), n, reg(uc, 2)) : PLUG_FILES_E_NONE));
}
void fRead(void* uc)
{
    const plug_ext_files* f = files();
    int16_t* dst = reinterpret_cast<int16_t*>(g2h(reg(uc, 2), reg(uc, 3) * 2));
    setReg(uc, 0, uint32_t(f && dst ? f->read_wave(reg(uc, 0), reg(uc, 1), dst, reg(uc, 3)) : PLUG_FILES_E_NONE));
}
void fWaveRef(void* uc)
{
    const plug_ext_files* f = files();
    char* p = reinterpret_cast<char*>(g2h(reg(uc, 2), reg(uc, 3)));
    setReg(uc, 0, uint32_t(f && p ? f->wave_ref(reg(uc, 0), reg(uc, 1), p, reg(uc, 3)) : PLUG_FILES_E_NONE));
}
void fReadPath(void* uc)
{
    const plug_ext_files* f = files();
    int16_t* dst = reinterpret_cast<int16_t*>(g2h(reg(uc, 2), reg(uc, 3) * 2));
    const char* p = str(reg(uc, 0));
    setReg(uc, 0, uint32_t(f && dst && p ? f->read_path(p, reg(uc, 1), dst, reg(uc, 3)) : PLUG_FILES_E_NONE));
}
void fNextPath(void* uc)
{
    const plug_ext_files* f = files();
    const char* p = str(reg(uc, 0));
    char* o = reinterpret_cast<char*>(g2h(reg(uc, 1), reg(uc, 2)));
    setReg(uc, 0, uint32_t(f && p && o ? f->next_path(p, o, reg(uc, 2)) : PLUG_FILES_E_NONE));
}

uint32_t rdg(uint32_t g)
{
    uint8_t* p = g2h(g, 4);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}

void setupImage()
{
    // the three plug_host services that touch firmware state, through the image's own table (init is running: no sym()/hostTable())
    const Sym* hs = findSym("plug::(anonymous namespace)::s_host", 0);
    const uint32_t host = hs ? hs->addr : 0;
    if (!host)
    {
        g_err = "plug::(anonymous namespace)::s_host not in the axf";
        g_ready = false;
        return;
    }
    override(rdg(host + 12 + 4 * 9), svcAlloc);    // plug_host.alloc
    override(rdg(host + 12 + 4 * 10), svcCycles);  // cycles
    override(rdg(host + 12 + 4 * 11), svcQuery);   // query
    const Sym* kf = findSym("plug::(anonymous namespace)::kFiles", 0);
    g_files = kf ? kf->addr : 0;
    if (g_files)
    {
        const Override fns[] = {fCount, fName, fRead, fWaveRef, fReadPath, fNextPath};
        for (int i = 0; i < 6; i++) override(rdg(g_files + 4 + 4 * i), fns[i]);
    }
}

// ------------------------------------------------------------------------------------------------ heap
std::mutex g_heapM;
std::map<uint32_t, uint32_t> g_free = {{kHeap, kHeapEnd - kHeap}};   // address -> bytes
std::unordered_map<uint32_t, uint32_t> g_used;

// ------------------------------------------------------------------------------------------------ lanes
std::atomic<uint32_t> g_codeGen{1};
std::atomic<uint64_t> g_faults{0}, g_calls{0};
thread_local int t_lane = kMain;
std::atomic<uint64_t> g_timeoutUs{0};
thread_local int t_worker = -1;
bool g_count = false;

struct LaneT
{
    int id = 0;
    uc_engine* uc = nullptr;
    std::thread th;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::pair<void (*)(void*), void*>> q;
    int running = 0;
    uint32_t gen = 0;
    size_t nOvr = 0;
    size_t nAlias = 0;
    uint32_t base = 0;
    std::recursive_mutex use;   // one caller at a time: the bounce area and the registers
    uint64_t insn = 0;
    std::vector<uint32_t> faultPcs;
};
LaneT g_lane[kLanes];
std::once_flag g_laneOnce[kLanes];

void countHook(uc_engine*, uint64_t, uint32_t, void* user)
{
    ++reinterpret_cast<LaneT*>(user)->insn;
}

void openLane(LaneT& L)
{
    uc_engine* uc = nullptr;
    if (uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &uc) != UC_ERR_OK)
    {
        say("uc_open failed on lane %d", L.id);
        return;
    }
    uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15);   // VFPv4 + Thumb-2, the prototype's model (bit identical to emu_render.py)
    for (const Window& w : kWin)
        if (uc_mem_map_ptr(uc, w.base, w.size, w.prot, w.host) != UC_ERR_OK) say("map 0x%08x failed on lane %d", w.base, L.id);
    uint32_t v = 0;
    uc_reg_read(uc, UC_ARM_REG_C1_C0_2, &v);
    v |= 0xFu << 20;   // CPACR: CP10, CP11 full access
    uc_reg_write(uc, UC_ARM_REG_C1_C0_2, &v);
    v = 0x40000000;   // FPEXC.EN
    uc_reg_write(uc, UC_ARM_REG_FPEXC, &v);
    const uint16_t trap = 0xE7FE;
    memcpy(g2h(L.base + kTrap, 2), &trap, 2);
    if (g_count)
    {
        uc_hook h;
        uc_hook_add(uc, &h, UC_HOOK_CODE, reinterpret_cast<void*>(countHook), &L, 1, 0);
    }
    L.uc = uc;
}

void syncLane(LaneT& L)
{
    const uint32_t gen = g_codeGen.load();
    if (L.gen != gen)
    {
        if (L.gen) uc_ctl_flush_tb(L.uc);
        L.gen = gen;
    }
    {
        std::lock_guard<std::mutex> g(g_aliasM);
        for (; L.nAlias < g_alias.size(); L.nAlias++)
        {
            const Alias& a = g_alias[L.nAlias];
            if (uc_mem_map_ptr(L.uc, a.base, a.size, UC_PROT_READ | UC_PROT_WRITE, a.host) != UC_ERR_OK) say("alias map 0x%08x failed on lane %d", a.base, L.id);
        }
    }
    std::lock_guard<std::mutex> g(g_ovrM);
    for (; L.nOvr < g_ovr.size(); L.nOvr++)
    {
        uc_hook h;
        const uint32_t a = g_ovr[L.nOvr].addr & ~1u;
        uc_hook_add(L.uc, &h, UC_HOOK_CODE, reinterpret_cast<void*>(ovrHook), reinterpret_cast<void*>(g_ovr[L.nOvr].h), a, a);
    }
}

void worker(LaneT* L)
{
#ifdef __APPLE__
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    char name[32];
    snprintf(name, sizeof(name), "ucb-lane-%d", L->id);
#ifdef __APPLE__
    pthread_setname_np(name);
#endif
    t_worker = L->id;
    t_lane = L->id;
    openLane(*L);
    std::unique_lock<std::mutex> lk(L->m);
    for (;;)
    {
        L->cv.wait(lk, [&] { return !L->q.empty(); });
        auto job = L->q.front();
        L->q.pop_front();
        lk.unlock();
        job.first(job.second);
        lk.lock();
        L->running--;
        L->cv.notify_all();
    }
}

LaneT& lane(int id)
{
    if (id < 0 || id >= kLanes) id = kMain;
    LaneT& L = g_lane[id];
    std::call_once(g_laneOnce[id], [&] {
        L.id = id;
        L.base = kBridge + uint32_t(id) * kLaneBytes;
        L.th = std::thread(worker, &L);
        L.th.detach();
    });
    return L;
}

// runs job on the lane's thread and waits
void runOn(LaneT& L, void (*job)(void*), void* arg)
{
    if (t_worker == L.id)
    {
        job(arg);
        return;
    }
    std::unique_lock<std::mutex> lk(L.m);
    const int mark = ++L.running;
    (void)mark;
    struct W
    {
        void (*job)(void*);
        void* arg;
        bool done;
        LaneT* L;
    } w{job, arg, false, &L};
    L.q.push_back({[](void* p) {
                       W* w = static_cast<W*>(p);
                       w->job(w->arg);
                       std::lock_guard<std::mutex> g(w->L->m);
                       w->done = true;
                   },
                   &w});
    L.cv.notify_all();
    L.cv.wait(lk, [&] { return w.done; });
}

struct CallArgs
{
    LaneT* L;
    uint32_t fn;
    const uint32_t* ints;
    int nInts;
    const float* floats;
    int nFloats;
    uint32_t r0;
    float s0;
};

void doCall(void* p)
{
    CallArgs& a = *static_cast<CallArgs*>(p);
    LaneT& L = *a.L;
    a.r0 = 0;
    a.s0 = 0;
    if (!L.uc) return;
    syncLane(L);
    {
        std::lock_guard<std::mutex> g(g_shadowM);
        for (const Shadow& s : g_shadows)
            if (uint8_t* h = g2h(s.guest, s.bytes)) memcpy(h, s.host, s.bytes);
    }
    uc_engine* uc = L.uc;
    uint32_t sp = L.base + kStackTop;
    if (a.nInts > 4)
    {
        const uint32_t extra = uint32_t(a.nInts - 4) * 4;
        sp = (sp - extra) & ~7u;
        memcpy(g2h(sp, extra), a.ints + 4, extra);
    }
    const uint32_t lr = (L.base + kTrap) | 1u;
    uc_reg_write(uc, UC_ARM_REG_SP, &sp);
    uc_reg_write(uc, UC_ARM_REG_LR, &lr);
    for (int i = 0; i < a.nInts && i < 4; i++) uc_reg_write(uc, UC_ARM_REG_R0 + i, &a.ints[i]);
    for (int i = 0; i < a.nFloats && i < 16; i++)
    {
        uint32_t b;
        memcpy(&b, &a.floats[i], 4);
        uc_reg_write(uc, UC_ARM_REG_S0 + i, &b);
    }
    g_calls++;
    const uint64_t limit = g_timeoutUs.load();
    uc_err e = uc_emu_start(uc, a.fn | 1u, L.base + kTrap, limit, 0);
    uint32_t pc = 0;
    // a call that ran out of setCallTimeout's time stops elsewhere than at the trap: a fault (a corrupt image that loops)
    if (e == UC_ERR_OK && limit && (uc_reg_read(uc, UC_ARM_REG_PC, &pc), (pc & ~1u) != ((L.base + kTrap) & ~1u))) e = UC_ERR_EXCEPTION;
    if (e != UC_ERR_OK)
    {
        uc_reg_read(uc, UC_ARM_REG_PC, &pc);
        g_faults++;
        bool seen = false;
        for (uint32_t f : L.faultPcs) seen = seen || f == pc;
        if (!seen && L.faultPcs.size() < 32)
        {
            L.faultPcs.push_back(pc);
            say("lane %d: %s at pc 0x%08x in the call of 0x%08x", L.id, uc_strerror(e), pc, a.fn);
        }
        return;
    }
    uc_reg_read(uc, UC_ARM_REG_R0, &a.r0);
    uint32_t b = 0;
    uc_reg_read(uc, UC_ARM_REG_S0, &b);
    memcpy(&a.s0, &b, 4);
}

// ------------------------------------------------------------------------------------------------ bounce: host arguments outside the windows
struct Bounce
{
    LaneT& L;
    uint32_t at;
    struct Back
    {
        void* host;
        uint32_t guest, bytes;
    };
    Back back[16];
    int nBack = 0;
    explicit Bounce(LaneT& l) : L(l), at(l.base + kBounce) {}
    uint32_t room(uint32_t bytes)
    {
        const uint32_t g = (at + 7u) & ~7u;
        if (g + bytes > L.base + kBounceEnd)
        {
            say("lane %d: bounce area full (%u B)", L.id, bytes);
            return 0;
        }
        at = g + bytes;
        return g;
    }
    // const input: its guest address, a copy when the host bytes are no window
    uint32_t in(const void* h, uint32_t bytes)
    {
        if (!h) return 0;
        uint32_t g = 0;
        if (h2g(h, g, bytes)) return g;
        g = room(bytes);
        if (g) memcpy(g2h(g, bytes), h, bytes);
        return g;
    }
    // input and output: copied back after the call
    uint32_t io(void* h, uint32_t bytes)
    {
        if (!h) return 0;
        uint32_t g = 0;
        if (h2g(h, g, bytes)) return g;
        g = room(bytes);
        if (!g) return 0;
        memcpy(g2h(g, bytes), h, bytes);
        if (nBack < 16) back[nBack++] = {h, g, bytes};
        return g;
    }
    ~Bounce()
    {
        for (int i = 0; i < nBack; i++) memcpy(back[i].host, g2h(back[i].guest, back[i].bytes), back[i].bytes);
    }
};

uint32_t callOn(LaneT& L, uint32_t fn, std::initializer_list<uint32_t> ints, std::initializer_list<float> floats = {}, float* s0 = nullptr)
{
    CallArgs a{&L, fn, ints.begin(), int(ints.size()), floats.begin(), int(floats.size()), 0, 0};
    runOn(L, doCall, &a);
    if (s0) *s0 = a.s0;
    return a.r0;
}

// ------------------------------------------------------------------------------------------------ voice and scratch shadows
std::mutex g_shadowTabM;
std::unordered_map<const void*, uint32_t> g_voiceTab, g_scratchTab;

uint32_t shadowSlot(std::unordered_map<const void*, uint32_t>& tab, const void* h, uint32_t base, uint32_t stride, uint32_t max)
{
    std::lock_guard<std::mutex> g(g_shadowTabM);
    auto it = tab.find(h);
    if (it != tab.end()) return it->second;
    if (tab.size() >= max)
    {
        say("shadow table full (%u)", max);
        return 0;
    }
    const uint32_t s = base + uint32_t(tab.size()) * stride;
    tab.emplace(h, s);
    return s;
}

// a native scratch: its window address, or its shadow (copied in now, out by the Bounce)
uint32_t scratchIn(Bounce& b, SynthSlotScratch* sc)
{
    if (!sc) return 0;
    uint32_t g = 0;
    if (h2g(sc, g, sizeof(*sc))) return g;
    g = shadowSlot(g_scratchTab, sc, kScratchShadows, 512, kScratchMax);
    if (!g) return 0;
    memcpy(g2h(g, sizeof(*sc)), sc, sizeof(*sc));
    if (b.nBack < 16) b.back[b.nBack++] = {sc, g, uint32_t(sizeof(*sc))};
    return g;
}

struct VoiceIn
{
    SynthVoice* host;
    uint32_t g;
};
VoiceIn voiceIn(Bounce& b, SynthVoice* v)
{
    if (!v) return {nullptr, 0};
    const uint32_t g = shadowSlot(g_voiceTab, v, kVoiceShadows, kVoiceStride, kVoiceMax);
    if (!g) return {v, 0};
    uint8_t* d = g2h(g, kDevVoice);
    const uint32_t sc = scratchIn(b, v->slotScratch);
    memcpy(d + 0, &sc, 4);
    memcpy(d + 4, &v->bend, 4);
    memcpy(d + 8, &v->bendStart, 4);
    memcpy(d + 12, &v->level, 4);
    d[16] = v->slot;
    d[17] = v->note;
    d[18] = v->gate;
    d[19] = v->fading;
    memcpy(d + 20, &v->cutoffOct, 4);
    memcpy(d + kDevVoiceState, v->state, sizeof(v->state));
    return {v, g};
}
void voiceOut(const VoiceIn& vi)
{
    if (!vi.g) return;
    SynthVoice* v = vi.host;
    const uint8_t* d = g2h(vi.g, kDevVoice);
    memcpy(&v->bend, d + 4, 4);
    memcpy(&v->bendStart, d + 8, 4);
    memcpy(&v->level, d + 12, 4);
    v->slot = d[16];
    v->note = d[17];
    v->gate = d[18] != 0;
    v->fading = d[19] != 0;
    memcpy(&v->cutoffOct, d + 20, 4);
    memcpy(v->state, d + kDevVoiceState, sizeof(v->state));
}

// ------------------------------------------------------------------------------------------------ mirrors
const char* hstr(uint32_t g)
{
    return g ? reinterpret_cast<const char*>(g2h(g, 1)) : nullptr;
}
float rdf(uint32_t g)
{
    float f = 0;
    if (uint8_t* p = g2h(g, 4)) memcpy(&f, p, 4);
    return f;
}
uint8_t rd8(uint32_t g)
{
    uint8_t* p = g2h(g, 1);
    return p ? *p : 0;
}
uint16_t rd16(uint32_t g)
{
    uint16_t v = 0;
    if (uint8_t* p = g2h(g, 2)) memcpy(&v, p, 2);
    return v;
}
// the host struct_size of a guest struct of guest size gs whose fields are 4 bytes on the unit and 8 (pointers) or 4 on the host:
// hostOff[i] = host offset of field i, the size covers every field the guest size covers in full
uint32_t hostSize(uint32_t gs, const uint32_t* hostOff, const uint32_t* hostLen, int n, uint32_t hostSizeof)
{
    if (gs >= uint32_t(n) * 4) return hostSizeof + (gs - uint32_t(n) * 4);
    uint32_t s = 0;
    for (int i = 0; i < n && uint32_t(i + 1) * 4 <= gs; i++) s = hostOff[i] + hostLen[i];
    return s;
}
std::mutex g_mirM;
LaneT& cur()
{
    return lane(t_lane);
}

// ---- plug_desc
constexpr int kDescPool = 64;
struct DescM
{
    plug_desc h{};
    uint32_t g = 0, init = 0, reset = 0, setp = 0, proc = 0, query = 0, inst = 0;
    std::vector<plug_param> params;
    std::vector<std::vector<const char*>> names;
};
DescM g_desc[kDescPool];
int g_nDesc = 0;

int32_t dInit(int i, void* inst, const plug_host*)
{
    DescM& m = g_desc[i];
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    Bounce b(L);
    const uint32_t gi = b.io(inst, m.inst ? m.inst : 8);
    return int32_t(callOn(L, m.init, {gi, hostTable()}));
}
void dReset(int i, void* inst)
{
    DescM& m = g_desc[i];
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    Bounce b(L);
    callOn(L, m.reset, {b.io(inst, m.inst ? m.inst : 8)});
}
void dSet(int i, void* inst, uint32_t index, float v)
{
    DescM& m = g_desc[i];
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    Bounce b(L);
    callOn(L, m.setp, {b.io(inst, m.inst ? m.inst : 8), index}, {v});
}
void dProcess(int i, void* inst, float* const* ch, uint32_t channels, uint32_t frames)
{
    DescM& m = g_desc[i];
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    Bounce b(L);
    const uint32_t gi = b.io(inst, m.inst ? m.inst : 8);
    const uint32_t arr = b.room(4 * (channels ? channels : 1));
    if (!arr) return;
    for (uint32_t c = 0; c < channels; c++)
    {
        const uint32_t g = b.io(ch[c], frames * 4);
        memcpy(g2h(arr + 4 * c, 4), &g, 4);
    }
    callOn(L, m.proc, {gi, arr, channels, frames});
}
const void* dQuery(int i, uint32_t id)
{
    DescM& m = g_desc[i];
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    const uint32_t g = callOn(L, m.query, {id});
    if (!g) return nullptr;
    switch (id)
    {
        case PLUG_EXT_TEMPO: return mirrorTempo(g);
        case PLUG_EXT_TAIL: return mirrorTail(g);
        case PLUG_EXT_SYNTH: return mirrorSynth(g);
        case PLUG_EXT_CHORD: return mirrorChord(g);
        default: return g2h(g, 4);
    }
}
template <int I>
struct DT
{
    static int32_t init(void* a, const plug_host* h) { return dInit(I, a, h); }
    static void reset(void* a) { dReset(I, a); }
    static void set(void* a, uint32_t k, float v) { dSet(I, a, k, v); }
    static void process(void* a, float* const* c, uint32_t n, uint32_t f) { dProcess(I, a, c, n, f); }
    static const void* query(uint32_t id) { return dQuery(I, id); }
};
struct DescFns
{
    int32_t (*init)(void*, const plug_host*);
    void (*reset)(void*);
    void (*set)(void*, uint32_t, float);
    void (*process)(void*, float* const*, uint32_t, uint32_t);
    const void* (*query)(uint32_t);
};
template <size_t... I>
std::vector<DescFns> descFns(std::index_sequence<I...>)
{
    return {DescFns{&DT<I>::init, &DT<I>::reset, &DT<I>::set, &DT<I>::process, &DT<I>::query}...};
}
const std::vector<DescFns> kDescFns = descFns(std::make_index_sequence<kDescPool>());

// ---- plug_ext_tempo, plug_ext_tail
constexpr int kExtPool = 64;
struct TempoM
{
    plug_ext_tempo h{};
    uint32_t g = 0, setTempo = 0, setTransport = 0;
};
TempoM g_tempo[kExtPool];
int g_nTempo = 0;
struct TailM
{
    plug_ext_tail h{};
    uint32_t g = 0, tailFrames = 0, quiet = 0, skip = 0;
};
TailM g_tail[kExtPool];
int g_nTail = 0;
uint32_t instG(Bounce& b, void* inst)
{
    return b.io(inst, 8);   // an insert's instance is in the guest heap (plug_host hostAlloc): its address, else 8 bytes
}
template <int I>
struct TT
{
    static void setTempo(void* inst, float bpm)
    {
        LaneT& L = cur();
        std::lock_guard<std::recursive_mutex> u(L.use);
        Bounce b(L);
        callOn(L, g_tempo[I].setTempo, {instG(b, inst)}, {bpm});
    }
    static void setTransport(void* inst, float beat, uint32_t playing)
    {
        LaneT& L = cur();
        std::lock_guard<std::recursive_mutex> u(L.use);
        Bounce b(L);
        callOn(L, g_tempo[I].setTransport, {instG(b, inst), playing}, {beat});
    }
    static uint32_t tailFrames(void* inst)
    {
        LaneT& L = cur();
        std::lock_guard<std::recursive_mutex> u(L.use);
        Bounce b(L);
        return callOn(L, g_tail[I].tailFrames, {instG(b, inst)});
    }
    static uint32_t quiet(void* inst)
    {
        LaneT& L = cur();
        std::lock_guard<std::recursive_mutex> u(L.use);
        Bounce b(L);
        return callOn(L, g_tail[I].quiet, {instG(b, inst)});
    }
    static void skip(void* inst, uint32_t frames)
    {
        LaneT& L = cur();
        std::lock_guard<std::recursive_mutex> u(L.use);
        Bounce b(L);
        callOn(L, g_tail[I].skip, {instG(b, inst), frames});
    }
};
struct ExtFns
{
    void (*setTempo)(void*, float);
    void (*setTransport)(void*, float, uint32_t);
    uint32_t (*tailFrames)(void*);
    uint32_t (*quiet)(void*);
    void (*skip)(void*, uint32_t);
};
template <size_t... I>
std::vector<ExtFns> extFns(std::index_sequence<I...>)
{
    return {ExtFns{&TT<I>::setTempo, &TT<I>::setTransport, &TT<I>::tailFrames, &TT<I>::quiet, &TT<I>::skip}...};
}
const std::vector<ExtFns> kExtFns = extFns(std::make_index_sequence<kExtPool>());

// ---- plug_ext_synth
constexpr int kSynthPool = 64;
struct SynthM
{
    plug_ext_synth h{};
    uint32_t g = 0;
    uint32_t setWork = 0, prepare = 0, slotReset = 0, setTempo = 0, noteOn = 0, noteOff = 0, render = 0, slotPost = 0, sendTap = 0,
             service = 0, view = 0;
    std::vector<plug_synth_param> params;
    std::vector<std::vector<const char*>> names;
    std::vector<plug_synth_section> sections;
    std::vector<plug_synth_macro> macros;
};
SynthM g_synth[kSynthPool];
int g_nSynth = 0;

#define LANE_LOCK            \
    LaneT& L = cur();        \
    std::lock_guard<std::recursive_mutex> u_(L.use); \
    Bounce b(L)

template <int I>
struct ST
{
    static SynthM& m() { return g_synth[I]; }
    static void setWork(void* work, uint32_t bytes)
    {
        LANE_LOCK;
        uint32_t g = 0;
        if (!h2g(work, g, bytes ? bytes : 1))
        {
            say("set_work: the work memory (%u B) is no guest window, not passed", bytes);
            return;
        }
        callOn(L, m().setWork, {g, bytes});
    }
    static void prepare(void* scratch, const void* params)
    {
        LANE_LOCK;
        const uint32_t sc = scratchIn(b, static_cast<SynthSlotScratch*>(scratch));
        callOn(L, m().prepare, {sc, b.in(params, sizeof(SynthParams))});
    }
    static void slotReset(void* scratch)
    {
        LANE_LOCK;
        callOn(L, m().slotReset, {scratchIn(b, static_cast<SynthSlotScratch*>(scratch))});
    }
    static void setTempo(float bpm)
    {
        LANE_LOCK;
        callOn(L, m().setTempo, {}, {bpm});
    }
    static void noteOn(void* voice, const void* params, const void* note)
    {
        LANE_LOCK;
        const VoiceIn v = voiceIn(b, static_cast<SynthVoice*>(voice));
        const uint32_t gp = b.in(params, sizeof(SynthParams)), gn = b.in(note, sizeof(SynthNote));
        callOn(L, m().noteOn, {v.g, gp, gn});
        voiceOut(v);
    }
    static void noteOff(void* voice, const void* params)
    {
        LANE_LOCK;
        const VoiceIn v = voiceIn(b, static_cast<SynthVoice*>(voice));
        callOn(L, m().noteOff, {v.g, b.in(params, sizeof(SynthParams))});
        voiceOut(v);
    }
    static uint32_t render(void* voice, const void* params, float g0, float gInc, float* l, float* r)
    {
        LANE_LOCK;
        const VoiceIn v = voiceIn(b, static_cast<SynthVoice*>(voice));
        const uint32_t gp = b.in(params, sizeof(SynthParams));
        const uint32_t gl = b.io(l, kBlockFrames * 4), gr = b.io(r, kBlockFrames * 4);
        const uint32_t ret = callOn(L, m().render, {v.g, gp, gl, gr}, {g0, gInc});
        voiceOut(v);
        return ret;
    }
    static uint32_t slotPost(int32_t slot, void* scratch, const void* params, float* l, float* r)
    {
        LANE_LOCK;
        const uint32_t sc = scratchIn(b, static_cast<SynthSlotScratch*>(scratch));
        const uint32_t gp = b.in(params, sizeof(SynthParams));
        const uint32_t gl = b.io(l, kBlockFrames * 4), gr = b.io(r, kBlockFrames * 4);
        return callOn(L, m().slotPost, {uint32_t(slot), sc, gp, gl, gr});
    }
    static uint32_t sendTap(int32_t slot, float* delay, float* reverb, float gd, float gr)
    {
        LANE_LOCK;
        const uint32_t d = b.io(delay, kBlockFrames * 4), rv = b.io(reverb, kBlockFrames * 4);
        return callOn(L, m().sendTap, {uint32_t(slot), d, rv}, {gd, gr});
    }
    static uint32_t service(uint32_t part, const float* values, uint32_t count)
    {
        LANE_LOCK;
        return callOn(L, m().service, {part, b.in(values, count * 4), count});
    }
    static void view(const float* values, uint32_t count, int8_t* out, uint32_t n)
    {
        LANE_LOCK;
        const uint32_t gv = b.in(values, count * 4), go = b.io(out, n);
        callOn(L, m().view, {gv, count, go, n});
    }
};
struct SynthFns
{
    void (*setWork)(void*, uint32_t);
    void (*prepare)(void*, const void*);
    void (*slotReset)(void*);
    void (*setTempo)(float);
    void (*noteOn)(void*, const void*, const void*);
    void (*noteOff)(void*, const void*);
    uint32_t (*render)(void*, const void*, float, float, float*, float*);
    uint32_t (*slotPost)(int32_t, void*, const void*, float*, float*);
    uint32_t (*sendTap)(int32_t, float*, float*, float, float);
    uint32_t (*service)(uint32_t, const float*, uint32_t);
    void (*view)(const float*, uint32_t, int8_t*, uint32_t);
};
template <size_t... I>
std::vector<SynthFns> synthFns(std::index_sequence<I...>)
{
    return {SynthFns{&ST<I>::setWork, &ST<I>::prepare, &ST<I>::slotReset, &ST<I>::setTempo, &ST<I>::noteOn, &ST<I>::noteOff,
                     &ST<I>::render, &ST<I>::slotPost, &ST<I>::sendTap, &ST<I>::service, &ST<I>::view}...};
}
const std::vector<SynthFns> kSynthFns = synthFns(std::make_index_sequence<kSynthPool>());

// ---- .ptfx
constexpr int kPtfxPool = 16;
struct PtfxM
{
    uint32_t init = 0, setp = 0, proc = 0;
};
PtfxM g_ptfx[kPtfxPool];
int g_nPtfx = 0;
template <int I>
struct PT
{
    static void init(void* scratch, uint32_t size, const PtfxHostApi*)
    {
        LANE_LOCK;
        const uint32_t gs = b.io(scratch, size);
        callOn(L, g_ptfx[I].init, {gs, size, sym("s_ptfxHostApi")});
    }
    static void setParam(uint8_t index, float v)
    {
        LANE_LOCK;
        callOn(L, g_ptfx[I].setp, {index}, {v});
    }
    static int32_t process(const int16_t** in, int16_t** out, uint32_t samples, uint8_t channels)
    {
        LANE_LOCK;
        const uint32_t ai = b.room(4 * (channels ? channels : 1)), ao = b.room(4 * (channels ? channels : 1));
        if (!ai || !ao) return 0;
        for (uint32_t c = 0; c < channels; c++)
        {
            // the sample memory is live: an effect reads input it was given blocks ago (Delay) and keeps the pointer, so in and out are
            // aliased, not bounced, where the host can say how far the memory goes (else the bounce below)
            if (in && in[c]) aliasMem(in[c], samples * 2);
            if (out && out[c]) aliasMem(out[c], samples * 2);
            const uint32_t gi = b.in(in ? in[c] : nullptr, samples * 2), go = b.io(out ? out[c] : nullptr, samples * 2);
            memcpy(g2h(ai + 4 * c, 4), &gi, 4);
            memcpy(g2h(ao + 4 * c, 4), &go, 4);
        }
        return int32_t(callOn(L, g_ptfx[I].proc, {ai, ao, samples, channels}));
    }
};
struct PtfxFns
{
    PtfxInitFunc init;
    PtfxSetParamFunc setParam;
    PtfxProcessBlockFunc process;
};
template <size_t... I>
std::vector<PtfxFns> ptfxFns(std::index_sequence<I...>)
{
    return {PtfxFns{&PT<I>::init, &PT<I>::setParam, &PT<I>::process}...};
}
const std::vector<PtfxFns> kPtfxFns = ptfxFns(std::make_index_sequence<kPtfxPool>());

template <class M>
int slotFor(M* pool, int& n, int cap, uint32_t g, const char* what)
{
    for (int i = 0; i < n; i++)
        if (pool[i].g == g) return i;
    if (n >= cap)
    {
        say("%s mirror pool full (%d)", what, cap);
        return -1;
    }
    pool[n].g = g;
    return n++;
}
const char** names(std::vector<std::vector<const char*>>& keep, uint32_t g, uint32_t n)
{
    if (!g || !n) return nullptr;
    keep.emplace_back(n);
    for (uint32_t k = 0; k < n; k++) keep.back()[k] = hstr(rdg(g + 4 * k));
    return keep.back().data();
}

}   // namespace

// ================================================================================================ public
bool init(const char* axf)
{
    std::call_once(g_once, [&] {
        g_count = getenv("UISHOT_BRIDGE_COUNT") && atoi(getenv("UISHOT_BRIDGE_COUNT")) > 0;
        if (getenv("UISHOT_BRIDGE_STATS"))   // one line at exit: guest calls and faults
            atexit([] { say("%llu guest calls, %llu faults", (unsigned long long)g_calls.load(), (unsigned long long)g_faults.load()); });
        const char* p = axf && *axf ? axf : getenv("UISHOT_AXF");
        if (!p || !*p) p = UISHOT_DEFAULT_AXF;
        g_axf = p ? p : "";
        if (g_axf.empty())
        {
            g_err = "no release axf (UISHOT_AXF)";
            return;
        }
        g_ready = loadAxf(g_axf.c_str());
        if (g_ready)
        {
            g_err.clear();
            setupImage();
        }
        if (!g_ready) say("%s: plugins do not run", g_err.c_str());
    });
    return g_ready;
}
bool ready()
{
    return init();
}
const char* error()
{
    init();
    return g_err.c_str();
}
const char* axfPath()
{
    init();
    return g_axf.c_str();
}

uint32_t sym(const char* name, uint32_t size)
{
    if (!init()) return 0;
    const Sym* s = findSym(name, size);
    return s ? s->addr : 0;
}
uint32_t symSize(const char* name, uint32_t size)
{
    if (!init()) return 0;
    const Sym* s = findSym(name, size);
    return s ? s->size : 0;
}

uint8_t* g2h(uint32_t guest, uint32_t len)
{
    const Window* w = winOf(guest, len ? len : 1);
    if (w) return w->host + (guest - w->base);
    if (guest < kAliasBase || guest >= kAliasEnd) return nullptr;
    std::lock_guard<std::mutex> g(g_aliasM);
    for (const Alias& a : g_alias)
        if (guest >= a.base && uint64_t(guest) + (len ? len : 1) <= uint64_t(a.base) + a.size) return a.host + (guest - a.base);
    return nullptr;
}
bool h2g(const void* host, uint32_t& guest, uint32_t len)
{
    const uint8_t* p = static_cast<const uint8_t*>(host);
    for (const Window& w : kWin)
        if (p >= w.host && p + (len ? len : 1) <= w.host + w.size)
        {
            guest = w.base + uint32_t(p - w.host);
            return true;
        }
    std::lock_guard<std::mutex> g(g_shadowM);
    for (const Shadow& s : g_shadows)
        if (p >= static_cast<const uint8_t*>(s.host) && p + (len ? len : 1) <= static_cast<const uint8_t*>(s.host) + s.bytes)
        {
            guest = s.guest + uint32_t(p - static_cast<const uint8_t*>(s.host));
            return true;
        }
    std::lock_guard<std::mutex> ga(g_aliasM);
    for (const Alias& a : g_alias)
        if (p >= a.host && p + (len ? len : 1) <= a.host + a.size)
        {
            guest = a.base + uint32_t(p - a.host);
            return true;
        }
    return false;
}

#ifdef __APPLE__
namespace
{
// the contiguous run of readable and writable host regions that holds p, at most kAliasReach each side of it
bool hostRun(uintptr_t p, uintptr_t& lo, uintptr_t& hi)
{
    auto region = [](mach_vm_address_t at, mach_vm_address_t& start, mach_vm_size_t& size) {
        vm_region_basic_info_data_64_t info;
        mach_msg_type_number_t cnt = VM_REGION_BASIC_INFO_COUNT_64;
        mach_port_t obj = MACH_PORT_NULL;
        start = at;
        if (mach_vm_region(mach_task_self(), &start, &size, VM_REGION_BASIC_INFO_64, reinterpret_cast<vm_region_info_t>(&info), &cnt, &obj) != KERN_SUCCESS)
            return false;
        return (info.protection & (VM_PROT_READ | VM_PROT_WRITE)) == (VM_PROT_READ | VM_PROT_WRITE);
    };
    mach_vm_address_t a;
    mach_vm_size_t n;
    if (!region(p, a, n) || a > p) return false;
    lo = a;
    hi = a + n;
    while (hi - p < kAliasReach && region(hi, a, n) && a == hi) hi = a + n;
    while (p - lo < kAliasReach && lo > kHostPage)
    {
        mach_vm_address_t b;
        mach_vm_size_t m;
        if (!region(lo - 1, b, m) || b + m != lo) break;   // the region at or above lo - 1 must end exactly at lo
        lo = b;
    }
    return true;
}
}   // namespace
#endif

uint32_t aliasMem(const void* host, uint32_t bytes)
{
    if (!host) return 0;
    uint32_t g = 0;
    if (h2g(host, g, bytes)) return g;
#ifdef __APPLE__
    uintptr_t lo = 0, hi = 0;
    const uintptr_t p = uintptr_t(host);
    if (!hostRun(p, lo, hi) || p + bytes > hi) return 0;
    if (p - lo > kAliasReach) lo = (p - kAliasReach) & ~uintptr_t(kHostPage - 1);
    if (hi - p > kAliasReach) hi = p + kAliasReach;
    lo = (lo + kHostPage - 1) & ~uintptr_t(kHostPage - 1);   // whole host pages
    hi &= ~uintptr_t(kHostPage - 1);
    if (lo > p || hi < p + bytes) return 0;
    std::lock_guard<std::mutex> ga(g_aliasM);
    if (g_alias.size() >= kAliasMax || uint64_t(g_aliasNext) + (hi - lo) > kAliasEnd)
    {
        say("alias windows exhausted");
        return 0;
    }
    const uint32_t base = g_aliasNext;
    g_aliasNext += uint32_t(hi - lo);
    g_alias.push_back({reinterpret_cast<uint8_t*>(lo), base, uint32_t(hi - lo)});
    return base + uint32_t(p - lo);
#else
    return 0;
#endif
}
uint32_t h2gOr0(const void* host, uint32_t len)
{
    uint32_t g = 0;
    return h2g(host, g, len) ? g : 0;
}

uint8_t* symWindow(const char* name, uint32_t bytes)
{
    const uint32_t a = sym(name, bytes);
    if (a)
        if (uint8_t* h = g2h(a, bytes)) return h;
    say("no window for %s (%u B): a private buffer, plugins cannot reach it", name, bytes);
    return static_cast<uint8_t*>(calloc(1, bytes ? bytes : 1));
}

void shadow(uint32_t guest, const void* host, uint32_t bytes)
{
    if (!g2h(guest, bytes)) return;
    std::lock_guard<std::mutex> g(g_shadowM);
    g_shadows.push_back({guest, host, bytes});
}

void* heapAlloc(uint32_t bytes)
{
    bytes = (bytes + 7u) & ~7u;
    if (!bytes) bytes = 8;
    std::lock_guard<std::mutex> g(g_heapM);
    for (auto it = g_free.begin(); it != g_free.end(); ++it)
        if (it->second >= bytes)
        {
            const uint32_t a = it->first, left = it->second - bytes;
            g_free.erase(it);
            if (left) g_free.emplace(a + bytes, left);
            g_used[a] = bytes;
            uint8_t* h = g2h(a, bytes);
            memset(h, 0, bytes);
            return h;
        }
    return nullptr;
}
void heapFree(void* p)
{
    uint32_t a = 0;
    if (!p || !h2g(p, a)) return;
    std::lock_guard<std::mutex> g(g_heapM);
    auto u = g_used.find(a);
    if (u == g_used.end()) return;
    uint32_t n = u->second;
    g_used.erase(u);
    auto next = g_free.lower_bound(a);
    if (next != g_free.end() && a + n == next->first)
    {
        n += next->second;
        next = g_free.erase(next);
    }
    if (next != g_free.begin())
    {
        auto prev = std::prev(next);
        if (prev->first + prev->second == a)
        {
            prev->second += n;
            return;
        }
    }
    g_free.emplace(a, n);
}

void setServices(const Services& s)
{
    g_svc = s;
}
const Services& services()
{
    return g_svc;
}
bool override(uint32_t guestFn, Override h)
{
    const uint32_t a = guestFn & ~1u;
    uint8_t* p = g2h(a, 2);
    if (!a || !p || !h) return false;
    const uint16_t bxlr = 0x4770;
    memcpy(p, &bxlr, 2);
    std::lock_guard<std::mutex> g(g_ovrM);
    g_ovr.push_back({a, h});
    g_codeGen++;
    return true;
}
bool hookAt(uint32_t guestPc, Override h)
{
    const uint32_t a = guestPc & ~1u;
    if (!a || !g2h(a, 2) || !h) return false;
    std::lock_guard<std::mutex> g(g_ovrM);
    g_ovr.push_back({a, h});
    g_codeGen++;
    return true;
}
uint32_t reg(void* uc, int n)
{
    uint32_t v = 0;
    const int r = n <= 12 ? UC_ARM_REG_R0 + n : n == 13 ? UC_ARM_REG_SP : n == 14 ? UC_ARM_REG_LR : UC_ARM_REG_PC;
    uc_reg_read(static_cast<uc_engine*>(uc), r, &v);
    return v;
}
void setReg(void* uc, int n, uint32_t v)
{
    const int r = n <= 12 ? UC_ARM_REG_R0 + n : n == 13 ? UC_ARM_REG_SP : n == 14 ? UC_ARM_REG_LR : UC_ARM_REG_PC;
    uc_reg_write(static_cast<uc_engine*>(uc), r, &v);
}
float sreg(void* uc, int n)
{
    uint32_t v = 0;
    uc_reg_read(static_cast<uc_engine*>(uc), UC_ARM_REG_S0 + n, &v);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
void setSreg(void* uc, int n, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    uc_reg_write(static_cast<uc_engine*>(uc), UC_ARM_REG_S0 + n, &v);
}

LaneScope::LaneScope(int l) : prev(t_lane)
{
    t_lane = l;
}
LaneScope::~LaneScope()
{
    t_lane = prev;
}
int currentLane()
{
    return t_lane;
}

uint32_t call(uint32_t fn, const uint32_t* ints, int nInts, const float* floats, int nFloats, float* s0)
{
    if (!init() || !fn) return 0;
    LaneT& L = cur();
    std::lock_guard<std::recursive_mutex> u(L.use);
    CallArgs a{&L, fn, ints, nInts, floats, nFloats, 0, 0};
    runOn(L, doCall, &a);
    if (s0) *s0 = a.s0;
    return a.r0;
}
uint32_t call(uint32_t fn, uint32_t a, uint32_t b, uint32_t c, uint32_t d)
{
    const uint32_t r[4] = {a, b, c, d};
    return call(fn, r, 4);
}
void submit(int id, void (*job)(void*), void* arg)
{
    LaneT& L = lane(id);
    std::lock_guard<std::mutex> g(L.m);
    L.running++;
    L.q.push_back({job, arg});
    L.cv.notify_all();
}
void join(int id)
{
    LaneT& L = lane(id);
    std::unique_lock<std::mutex> lk(L.m);
    L.cv.wait(lk, [&] { return L.running == 0 && L.q.empty(); });
}
uint64_t faults()
{
    return g_faults.load();
}
uint64_t calls()
{
    return g_calls.load();
}
uint64_t instructions(int id)
{
    return id >= 0 && id < kLanes ? g_lane[id].insn : 0;
}
void setCallTimeout(uint32_t us)
{
    g_timeoutUs = us;
}
void codeChanged()
{
    g_codeGen++;
}

uint32_t hostTable()
{
    static const uint32_t a = sym("plug::(anonymous namespace)::s_host");
    return a;
}
uint32_t entry(uint32_t entryRun)
{
    if (!init()) return 0;
    return call(entryRun, hostTable(), (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR);
}
int checkDesc(uint32_t gdesc, uint32_t base, uint32_t size, uint32_t wantKind)
{
    static const uint32_t f = sym("plug::checkDesc");
    if (!f) return -1000;
    return int(int32_t(call(f, gdesc, base, size, wantKind)));
}
uint32_t tempoExt(uint32_t gdesc, uint32_t base, uint32_t size)
{
    static const uint32_t f = sym("plug::tempoExt");
    return f ? call(f, gdesc, base, size) : 0;
}
uint32_t tailExt(uint32_t gdesc, uint32_t base, uint32_t size)
{
    static const uint32_t f = sym("plug::tailExt");
    return f ? call(f, gdesc, base, size) : 0;
}
bool fn(uint32_t f, uint32_t lo, uint32_t n, bool need)
{
    static const uint32_t a = sym("plug::(anonymous namespace)::fn");
    return a && (call(a, f, lo, n, need ? 1u : 0u) & 0xFF) != 0;
}

const plug_desc* mirrorDesc(uint32_t g)
{
    if (!g || !g2h(g, D::desc_size)) return nullptr;
    std::lock_guard<std::mutex> lk(g_mirM);
    const int i = slotFor(g_desc, g_nDesc, kDescPool, g, "plug_desc");
    if (i < 0) return nullptr;
    DescM& m = g_desc[i];
    const uint32_t gs = rdg(g + D::desc_struct_size);
    auto field = [&](uint32_t off) { return off + 4 <= gs ? rdg(g + off) : 0u; };
    static const uint32_t ho[] = {offsetof(plug_desc, struct_size), offsetof(plug_desc, kind), offsetof(plug_desc, name),
                                  offsetof(plug_desc, author), offsetof(plug_desc, num_params), offsetof(plug_desc, params),
                                  offsetof(plug_desc, instance_size), offsetof(plug_desc, init), offsetof(plug_desc, reset),
                                  offsetof(plug_desc, set_param), offsetof(plug_desc, process), offsetof(plug_desc, query)};
    static const uint32_t hl[] = {4, 4, 8, 8, 4, 8, 4, 8, 8, 8, 8, 8};
    m.h = plug_desc{};
    m.h.struct_size = hostSize(gs, ho, hl, 12, sizeof(plug_desc));
    m.h.kind = field(D::desc_kind);
    m.h.name = hstr(field(D::desc_name));
    m.h.author = hstr(field(D::desc_author));
    m.h.num_params = field(D::desc_num_params);
    m.h.instance_size = field(D::desc_instance_size);
    m.inst = m.h.instance_size;
    m.init = field(D::desc_init);
    m.reset = field(D::desc_reset);
    m.setp = field(D::desc_set_param);
    m.proc = field(D::desc_process);
    m.query = field(D::desc_query);
    const DescFns& f = kDescFns[size_t(i)];
    m.h.init = m.init ? f.init : nullptr;
    m.h.reset = m.reset ? f.reset : nullptr;
    m.h.set_param = m.setp ? f.set : nullptr;
    m.h.process = m.proc ? f.process : nullptr;
    m.h.query = m.query ? f.query : nullptr;
    m.names.clear();
    m.params.clear();
    const uint32_t gp = field(D::desc_params), np = m.h.num_params <= 256 ? m.h.num_params : 0;
    if (gp && np && g2h(gp, np * D::param_size))
    {
        m.params.resize(np);
        for (uint32_t k = 0; k < np; k++)
        {
            const uint32_t p = gp + k * D::param_size;
            plug_param& q = m.params[k];
            q.name = hstr(rdg(p));
            q.unit = hstr(rdg(p + 4));
            q.min = rdf(p + 8);
            q.max = rdf(p + 12);
            q.def = rdf(p + 16);
            q.step = rdf(p + 20);
            q.enum_count = rd16(p + 24);
            q.flags = rd16(p + 26);
            const uint32_t nn = q.enum_count ? q.enum_count : plug_param_name_count(&q);
            q.enum_names = names(m.names, rdg(p + 28), nn);
        }
        m.h.params = m.params.data();
    }
    return &m.h;
}

const plug_ext_tempo* mirrorTempo(uint32_t g)
{
    if (!g || !g2h(g, 4)) return nullptr;
    std::lock_guard<std::mutex> lk(g_mirM);
    const int i = slotFor(g_tempo, g_nTempo, kExtPool, g, "plug_ext_tempo");
    if (i < 0) return nullptr;
    TempoM& m = g_tempo[i];
    const uint32_t gs = rdg(g);
    static const uint32_t ho[] = {offsetof(plug_ext_tempo, struct_size), offsetof(plug_ext_tempo, set_tempo),
                                  offsetof(plug_ext_tempo, set_transport)};
    static const uint32_t hl[] = {4, 8, 8};
    m.h = plug_ext_tempo{};
    m.h.struct_size = hostSize(gs, ho, hl, 3, sizeof(plug_ext_tempo));
    m.setTempo = gs >= 8 ? rdg(g + 4) : 0;
    m.setTransport = gs >= 12 ? rdg(g + 8) : 0;
    m.h.set_tempo = m.setTempo ? kExtFns[size_t(i)].setTempo : nullptr;
    m.h.set_transport = m.setTransport ? kExtFns[size_t(i)].setTransport : nullptr;
    return &m.h;
}

const plug_ext_tail* mirrorTail(uint32_t g)
{
    if (!g || !g2h(g, 4)) return nullptr;
    std::lock_guard<std::mutex> lk(g_mirM);
    const int i = slotFor(g_tail, g_nTail, kExtPool, g, "plug_ext_tail");
    if (i < 0) return nullptr;
    TailM& m = g_tail[i];
    const uint32_t gs = rdg(g);
    static const uint32_t ho[] = {offsetof(plug_ext_tail, struct_size), offsetof(plug_ext_tail, tail_frames),
                                  offsetof(plug_ext_tail, quiet), offsetof(plug_ext_tail, skip)};
    static const uint32_t hl[] = {4, 8, 8, 8};
    m.h = plug_ext_tail{};
    m.h.struct_size = hostSize(gs, ho, hl, 4, sizeof(plug_ext_tail));
    m.tailFrames = gs >= 8 ? rdg(g + 4) : 0;
    m.quiet = gs >= 12 ? rdg(g + 8) : 0;
    m.skip = gs >= 16 ? rdg(g + 12) : 0;
    m.h.tail_frames = m.tailFrames ? kExtFns[size_t(i)].tailFrames : nullptr;
    m.h.quiet = m.quiet ? kExtFns[size_t(i)].quiet : nullptr;
    m.h.skip = m.skip ? kExtFns[size_t(i)].skip : nullptr;
    return &m.h;
}

uint32_t deviceSynthLayout()
{
    return PLUG_SYNTH_LAYOUT(kDevVoice, sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch));
}
uint32_t hostSynthLayout()
{
    return PLUG_SYNTH_LAYOUT(sizeof(SynthVoice), sizeof(SynthParams), sizeof(SynthNote), sizeof(SynthSlotScratch));
}

const plug_ext_synth* mirrorSynth(uint32_t g)
{
    if (!g || !g2h(g, 8)) return nullptr;
    std::lock_guard<std::mutex> lk(g_mirM);
    const int i = slotFor(g_synth, g_nSynth, kSynthPool, g, "plug_ext_synth");
    if (i < 0) return nullptr;
    SynthM& m = g_synth[i];
    const uint32_t gs = rdg(g);
    auto field = [&](uint32_t idx) { return (idx + 1) * 4 <= gs ? rdg(g + idx * 4) : 0u; };
    static const uint32_t ho[] = {
        offsetof(plug_ext_synth, struct_size), offsetof(plug_ext_synth, layout), offsetof(plug_ext_synth, stored),
        offsetof(plug_ext_synth, patch_engine), offsetof(plug_ext_synth, type_id), offsetof(plug_ext_synth, name),
        offsetof(plug_ext_synth, params), offsetof(plug_ext_synth, param_count), offsetof(plug_ext_synth, sections),
        offsetof(plug_ext_synth, section_count), offsetof(plug_ext_synth, macros), offsetof(plug_ext_synth, macro_count),
        offsetof(plug_ext_synth, volume_param), offsetof(plug_ext_synth, max_voices), offsetof(plug_ext_synth, flags),
        offsetof(plug_ext_synth, work_bytes), offsetof(plug_ext_synth, set_work), offsetof(plug_ext_synth, prepare),
        offsetof(plug_ext_synth, slot_reset), offsetof(plug_ext_synth, set_tempo), offsetof(plug_ext_synth, note_on),
        offsetof(plug_ext_synth, note_off), offsetof(plug_ext_synth, render), offsetof(plug_ext_synth, slot_post),
        offsetof(plug_ext_synth, set_send_tap), offsetof(plug_ext_synth, service), offsetof(plug_ext_synth, view)};
    static const uint32_t hl[] = {4, 4, 4, 4, 4, 8, 8, 4, 8, 4, 8, 4, 4, 4, 4, 4, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8, 8};
    static_assert(sizeof(ho) / 4 == D::synth_size / 4, "27 fields");
    m.h = plug_ext_synth{};
    m.h.struct_size = hostSize(gs, ho, hl, 27, sizeof(plug_ext_synth));
    const uint32_t layout = field(1);
    m.h.layout = layout == deviceSynthLayout() ? hostSynthLayout() : layout;
    m.h.stored = int32_t(field(2));
    m.h.patch_engine = int32_t(field(3));
    m.h.type_id = int32_t(field(4));
    m.h.name = hstr(field(5));
    m.h.param_count = field(7);
    m.h.section_count = field(9);
    m.h.macro_count = field(11);
    m.h.volume_param = int32_t(field(12));
    m.h.max_voices = field(13);
    m.h.flags = field(14);
    m.h.work_bytes = field(15);
    uint32_t* const fns[] = {&m.setWork, &m.prepare, &m.slotReset, &m.setTempo, &m.noteOn, &m.noteOff,
                             &m.render,  &m.slotPost, &m.sendTap, &m.service, &m.view};
    for (int k = 0; k < 11; k++) *fns[k] = field(uint32_t(16 + k));
    const SynthFns& f = kSynthFns[size_t(i)];
    m.h.set_work = m.setWork ? f.setWork : nullptr;
    m.h.prepare = m.prepare ? f.prepare : nullptr;
    m.h.slot_reset = m.slotReset ? f.slotReset : nullptr;
    m.h.set_tempo = m.setTempo ? f.setTempo : nullptr;
    m.h.note_on = m.noteOn ? f.noteOn : nullptr;
    m.h.note_off = m.noteOff ? f.noteOff : nullptr;
    m.h.render = m.render ? f.render : nullptr;
    m.h.slot_post = m.slotPost ? f.slotPost : nullptr;
    m.h.set_send_tap = m.sendTap ? f.sendTap : nullptr;
    m.h.service = m.service ? f.service : nullptr;
    m.h.view = m.view ? f.view : nullptr;
    m.params.clear();
    m.names.clear();
    m.sections.clear();
    m.macros.clear();
    const uint32_t gp = field(6), np = m.h.param_count <= 256 ? m.h.param_count : 0;
    if (gp && np && g2h(gp, np * D::sparam_size))
    {
        m.params.resize(np);
        for (uint32_t k = 0; k < np; k++)
        {
            const uint32_t p = gp + k * D::sparam_size;
            plug_synth_param& q = m.params[k];
            q.name = hstr(rdg(p));
            q.def = rdf(p + 4);
            q.min = rdf(p + 8);
            q.max = rdf(p + 12);
            q.fmt = rd8(p + 16);
            q.curve = rd8(p + 17);
            const uint32_t ne = q.fmt == PLUG_FMT_ENUM && q.max >= 0.0f && q.max <= 255.0f ? uint32_t(q.max) + 1u : 0u;
            q.enum_names = names(m.names, rdg(p + 20), ne);
        }
        m.h.params = m.params.data();
    }
    const uint32_t gsec = field(8), ns = m.h.section_count <= 64 ? m.h.section_count : 0;
    if (gsec && ns && g2h(gsec, ns * D::section_size))
    {
        m.sections.resize(ns);
        for (uint32_t k = 0; k < ns; k++)
        {
            const uint32_t p = gsec + k * D::section_size;
            m.sections[k].name = hstr(rdg(p));
            m.sections[k].short_name = hstr(rdg(p + 4));
            m.sections[k].ids = rdg(p + 8) ? g2h(rdg(p + 8), 1) : nullptr;
            m.sections[k].count = rd8(p + 12);
        }
        m.h.sections = m.sections.data();
    }
    const uint32_t gm = field(10), nm = m.h.macro_count <= 64 ? m.h.macro_count : 0;
    if (gm && nm && g2h(gm, nm * D::macro_size))
    {
        m.macros.resize(nm);
        for (uint32_t k = 0; k < nm; k++)
        {
            const uint32_t p = gm + k * D::macro_size;
            plug_synth_macro& q = m.macros[k];
            q.name = hstr(rdg(p));
            q.bipolar = rd8(p + 4);
            q.count = rd8(p + 5);
            for (int t = 0; t < 2; t++)
            {
                q.targets[t].param = rd8(p + 8 + 8 * uint32_t(t));
                q.targets[t].amount = rdf(p + 12 + 8 * uint32_t(t));
            }
        }
        m.h.macros = m.macros.data();
    }
    return &m.h;
}

const plug_ext_chord* mirrorChord(uint32_t g)
{
    return reinterpret_cast<const plug_ext_chord*>(g2h(g, sizeof(plug_ext_chord)));
}

uint32_t guestOf(const plug_desc* p)
{
    std::lock_guard<std::mutex> lk(g_mirM);
    for (int i = 0; i < g_nDesc; i++)
        if (&g_desc[i].h == p) return g_desc[i].g;
    return 0;
}
uint32_t guestProcess(const plug_desc* p)
{
    std::lock_guard<std::mutex> lk(g_mirM);
    for (int i = 0; i < g_nDesc; i++)
        if (&g_desc[i].h == p) return g_desc[i].proc;
    return 0;
}
uint32_t guestOf(const plug_ext_synth* p)
{
    std::lock_guard<std::mutex> lk(g_mirM);
    for (int i = 0; i < g_nSynth; i++)
        if (&g_synth[i].h == p) return g_synth[i].g;
    return 0;
}

void mirrorPtfx(uint32_t gInit, uint32_t gSetParam, uint32_t gProcess, PtfxInitFunc& init, PtfxSetParamFunc& setParam,
                PtfxProcessBlockFunc& process)
{
    std::lock_guard<std::mutex> lk(g_mirM);
    int i = -1;
    for (int k = 0; k < g_nPtfx && i < 0; k++)
        if (g_ptfx[k].init == gInit && g_ptfx[k].setp == gSetParam && g_ptfx[k].proc == gProcess) i = k;
    if (i < 0 && g_nPtfx < kPtfxPool)
    {
        i = g_nPtfx++;
        g_ptfx[i] = {gInit, gSetParam, gProcess};
    }
    if (i < 0)
    {
        say("ptfx mirror pool full");
        init = nullptr, setParam = nullptr, process = nullptr;
        return;
    }
    init = kPtfxFns[size_t(i)].init;
    setParam = kPtfxFns[size_t(i)].setParam;
    process = kPtfxFns[size_t(i)].process;
}

uint32_t voiceShadow(const void* hostVoice)
{
    std::lock_guard<std::mutex> g(g_shadowTabM);
    auto it = g_voiceTab.find(hostVoice);
    return it == g_voiceTab.end() ? 0 : it->second;
}

}   // namespace ucb
