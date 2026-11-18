// emu-parity prototype (tools only, never in the image): run a REAL synth .plg from the release card under libunicorn from
// native C++, the way a ui_shot bridge would. The engine text is rebased for and written into a host array that is mapped at
// the FlexNVM bank address 0x10000000 (uc_mem_map_ptr: the host writes the "flash", the emulated core runs from it), rw and bss
// in SRAM, the work arena in SDRAM. The driver is poly-engines tools/render_harness.cpp (a .plg of its own), the same as
// emu_render.py, so the output can be compared byte for byte with the Python reference at another base.
//   ucbridge_proto ENGINE.plg HARNESS.plg OUT.f32 BLOCKS VOICES [percall]
#include <unicorn/unicorn.h>

#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <zlib.h>

namespace
{
constexpr uint32_t SRAM = 0x1FFF0000, SRAM_SIZE = 0x40000, SDRAM = 0x70000000, SDRAM_SIZE = 0x800000;
constexpr uint32_t WORK = 0x20020000, TRAP = WORK + 0xF000, HARNESS = 0x1FFF4000, WORK_MEM = SDRAM + 0x600000, CFG = SDRAM + 0x7F0000;
constexpr uint32_t NVM = 0x10000000, NVM_SIZE = 0x20000, ENGINE_RW = 0x1FFFA000;
constexpr uint32_t N = 128;
const char* kNames[] = {"sinf", "cosf", "tanf", "tanhf", "expf", "logf", "powf", "memcpy", "memset", "alloc", "cycles", "query", "exp2f", "log2f"};
constexpr uint32_t kN = sizeof(kNames) / sizeof(kNames[0]);
constexpr uint32_t STUBS = TRAP + 0x100;
alignas(4096) uint8_t g_nvm[NVM_SIZE];   // the "FlexNVM bank": host memory the core executes from
uint64_t g_svc = 0;

void die(const char* m, uc_err e = UC_ERR_OK)
{
    fprintf(stderr, "%s %s\n", m, e ? uc_strerror(e) : "");
    exit(1);
}
#define UC(x) do { uc_err e_ = (x); if (e_) die(#x, e_); } while (0)

uint32_t rd32(uc_engine* uc, uint32_t a)
{
    uint32_t v;
    UC(uc_mem_read(uc, a, &v, 4));
    return v;
}
float sf(uc_engine* uc)
{
    uint32_t v;
    uc_reg_read(uc, UC_ARM_REG_S0, &v);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
void wfl(uc_engine* uc, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    uc_reg_write(uc, UC_ARM_REG_S0, &v);
}
uint32_t reg(uc_engine* uc, int r)
{
    uint32_t v;
    uc_reg_read(uc, r, &v);
    return v;
}
void setreg(uc_engine* uc, int r, uint32_t v) { uc_reg_write(uc, r, &v); }

// host services: libm as emu_render.py does it (double, rounded to float once), memcpy/memset on guest memory
void service(uc_engine* uc, uint64_t addr, uint32_t, void*)
{
    g_svc++;
    const uint32_t k = uint32_t(addr - STUBS) / 4;
    const char* n = kNames[k];
    if (!strcmp(n, "sinf")) wfl(uc, float(sin(double(sf(uc)))));
    else if (!strcmp(n, "cosf")) wfl(uc, float(cos(double(sf(uc)))));
    else if (!strcmp(n, "memset"))
    {
        std::vector<uint8_t> b(reg(uc, UC_ARM_REG_R2), uint8_t(reg(uc, UC_ARM_REG_R1)));
        if (!b.empty()) uc_mem_write(uc, reg(uc, UC_ARM_REG_R0), b.data(), b.size());
    }
    else if (!strcmp(n, "memcpy"))
    {
        std::vector<uint8_t> b(reg(uc, UC_ARM_REG_R2));
        if (!b.empty())
        {
            uc_mem_read(uc, reg(uc, UC_ARM_REG_R1), b.data(), b.size());
            uc_mem_write(uc, reg(uc, UC_ARM_REG_R0), b.data(), b.size());
        }
    }
    else setreg(uc, UC_ARM_REG_R0, 0);   // query (no extensions), cycles 0, alloc NULL (as emu_render.py without a counter)
}

uint32_t call(uc_engine* uc, uint32_t fn, std::initializer_list<uint32_t> args)
{
    setreg(uc, UC_ARM_REG_SP, WORK + 0xE000);
    setreg(uc, UC_ARM_REG_LR, TRAP | 1);
    const int rr[] = {UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3};
    int i = 0;
    for (uint32_t a : args) setreg(uc, rr[i++], a);
    UC(uc_emu_start(uc, fn | 1, TRAP, 0, 0));
    return reg(uc, UC_ARM_REG_R0);
}

struct Img
{
    uint32_t entry, lo, hi;
};
// The plug::load steps the device does for a synth bank install: header, CRC (logo trailer outside it), rebase for the run
// addresses, text to `textDst` (host pointer of the run address), rw + bss to the rw run address.
Img load(uc_engine* uc, const char* path, uint32_t base, uint8_t* textDst, uint32_t rwbase)
{
    FILE* f = fopen(path, "rb");
    if (!f) die(path);
    std::vector<uint8_t> d;
    uint8_t buf[65536];
    size_t n;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) d.insert(d.end(), buf, buf + n);
    fclose(f);
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &d[o], 2); return v; };
    if (u32(0) != 0x31474C50) die("magic");
    const uint32_t hsize = u16(4), img = u32(24), bss = u32(28), nreb = u32(32), entry = u32(36), crc = u32(48);
    uint32_t rwoff = u32(52);
    if (!rwoff) rwoff = img;
    if (!rwbase) rwbase = base + rwoff;
    const uint32_t logo = hsize >= 64 + 96 ? u32(64 + 72) : 0;
    if (logo && logo == hsize + img + 4 * nreb && d.size() == logo + 1676) d.resize(logo);
    if ((uint32_t(crc32(0, d.data() + 64, uInt(d.size() - 64))) != crc) || d.size() != hsize + img + 4 * nreb) die("crc/size");
    std::vector<uint8_t> image(d.begin() + hsize, d.begin() + hsize + img);
    for (uint32_t k = 0; k < nreb; k++)
    {
        const uint32_t off = u32(hsize + img + 4 * k);
        uint32_t v;
        memcpy(&v, &image[off], 4);
        v = v < rwoff ? base + v : rwbase + v - rwoff;
        memcpy(&image[off], &v, 4);
    }
    if (textDst) memcpy(textDst, image.data(), rwoff);
    else UC(uc_mem_write(uc, base, image.data(), rwoff));
    std::vector<uint8_t> rw(image.begin() + rwoff, image.end());
    rw.resize(rw.size() + bss);
    if (!rw.empty()) UC(uc_mem_write(uc, rwbase, rw.data(), rw.size()));
    return {base + entry, base, base + rwoff};
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 6) die("usage: ucbridge_proto ENGINE.plg HARNESS.plg OUT.f32 BLOCKS VOICES [percall]");
    const uint32_t blocks = uint32_t(atoi(argv[4])), voices = uint32_t(atoi(argv[5]));
    const bool percall = argc > 6;
    uc_engine* uc;
    UC(uc_open(UC_ARCH_ARM, UC_MODE_THUMB, &uc));
    UC(uc_ctl_set_cpu_model(uc, UC_CPU_ARM_CORTEX_A15));
    UC(uc_mem_map(uc, SRAM, SRAM_SIZE, UC_PROT_ALL));
    UC(uc_mem_map(uc, SDRAM, SDRAM_SIZE, UC_PROT_ALL));
    memset(g_nvm, 0xFF, sizeof g_nvm);   // erased flash
    UC(uc_mem_map_ptr(uc, NVM, NVM_SIZE, UC_PROT_READ | UC_PROT_EXEC, g_nvm));   // read + execute: a plugin store into its text faults as on the device
    uint32_t v = reg(uc, UC_ARM_REG_C1_C0_2) | (0xF << 20);
    setreg(uc, UC_ARM_REG_C1_C0_2, v);
    setreg(uc, UC_ARM_REG_FPEXC, 0x40000000);
    const Img eng = load(uc, argv[1], NVM, g_nvm, ENGINE_RW);
    const Img har = load(uc, argv[2], HARNESS, nullptr, 0);   // harness rw right after its text (rwbase 0 = see below)
    (void)har;
    const uint16_t trap = 0xE7FE;
    UC(uc_mem_write(uc, TRAP, &trap, 2));
    for (uint32_t i = 0; i < kN; i++)
    {
        const uint8_t bxlr[4] = {0x70, 0x47, 0x00, 0xBF};
        UC(uc_mem_write(uc, STUBS + 4 * i, bxlr, 4));
    }
    uc_hook h;
    UC(uc_hook_add(uc, &h, UC_HOOK_CODE, reinterpret_cast<void*>(service), nullptr, STUBS, STUBS + 4 * kN - 1));
    std::vector<uint32_t> host = {(15 + 2) * 4, 1};
    float sr = 44117.64706f;
    uint32_t srb;
    memcpy(&srb, &sr, 4);
    host.push_back(srb);
    for (uint32_t i = 0; i < kN; i++) host.push_back((STUBS + 4 * i) | 1);
    UC(uc_mem_write(uc, WORK, host.data(), host.size() * 4));
    const uint32_t desc = call(uc, eng.entry, {WORK, (1u << 16) | 1});
    if (call(uc, rd32(uc, desc + 28), {WORK + 0x1000, WORK}) != 0) die("engine init");
    const uint32_t ext = call(uc, rd32(uc, desc + 44), {3});
    if (!ext) die("no PLUG_EXT_SYNTH");
    const uint32_t work = rd32(uc, ext + 60);
    if (work) call(uc, rd32(uc, ext + 64), {WORK_MEM, work});
    const uint32_t hdesc = call(uc, har.entry, {WORK, (1u << 16) | 1});
    call(uc, rd32(uc, hdesc + 28), {WORK + 0x1100, WORK});
    const uint32_t runfn = call(uc, rd32(uc, hdesc + 44), {0x7E});
    // config block as emu_render.Run.run: voices, offBlock, nover = 0, notes, zeros
    std::vector<uint8_t> cfg(4096, 0);
    const uint32_t notes[4] = {48, 55, 60, 64};
    uint32_t hdr[7] = {voices, 1000000u, 0, 0, 0, 0, 0};
    for (uint32_t i = 0; i < voices && i < 4; i++) hdr[3 + i] = notes[i];
    memcpy(cfg.data(), hdr, sizeof hdr);
    UC(uc_mem_write(uc, CFG, cfg.data(), cfg.size()));
    std::vector<uint8_t> zero(size_t(blocks) * (2 * N + 2) * 4, 0);
    UC(uc_mem_write(uc, SDRAM, zero.data(), zero.size()));
    const auto t0 = std::chrono::steady_clock::now();
    if (!percall) call(uc, runfn, {ext, SDRAM, blocks, CFG});
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count();
    std::vector<uint8_t> out(size_t(blocks) * 2 * N * 4);
    UC(uc_mem_read(uc, SDRAM, out.data(), out.size()));
    FILE* o = fopen(argv[3], "wb");
    fwrite(out.data(), 1, out.size(), o);
    fclose(o);
    printf("blocks %u voices %u: %.1f us/block (%.2fx real time), %llu service traps, text %u B at 0x%08x (host-backed FlexNVM)\n",
           blocks, voices, us / blocks, 2902.5 / (us / blocks), (unsigned long long)g_svc, eng.hi - eng.lo, eng.lo);
    // per-call overhead: an empty emu_start (straight to the trap) from C
    const auto t2 = std::chrono::steady_clock::now();
    for (int i = 0; i < 100000; i++) call(uc, TRAP, {});
    const auto t3 = std::chrono::steady_clock::now();
    printf("empty uc_emu_start round trip: %.2f us\n", std::chrono::duration<double, std::micro>(t3 - t2).count() / 100000);
    uc_close(uc);
    return 0;
}
