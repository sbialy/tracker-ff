// plug_load.h on the Mac (ref/ff-imx-port-plan.md P5 H1/H3): a real .plg built for the IMX (build_plug.py --cpu cm7) and the same
// source built cm4f, loaded at an IMX SDRAM guest address. Built twice by run_tests.sh: with -DPLATFORM_IMX (the IMX host takes
// both files) and without (the MK66 host refuses cm7 with kAbi and still takes cm4f).
//   plug_load_test <cm7.plg> <cm4f.plg> [slot-dump]
#include "plug_load.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

static int fails = 0;
#define CHECK(c, ...)                                                                                                                 \
    do                                                                                                                                \
    {                                                                                                                                 \
        if (!(c)) { printf("FAIL %s:%d ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); fails++; }                         \
    } while (0)

struct RecOps   // records the cache steps: the order and the range are part of the contract (Unicorn has no caches)
{
    std::vector<int> calls;
    const void* p = nullptr;
    uint32_t n = 0;
    void cleanDCache(const void* q, uint32_t m) { calls.push_back(1); p = q; n = m; }
    void invalidateICache() { calls.push_back(2); }
    void barrier() { calls.push_back(3); }
};

static std::vector<uint8_t> readAll(const char* path)
{
    std::vector<uint8_t> v;
    FILE* f = fopen(path, "rb");
    if (!f) { printf("FAIL cannot open %s\n", path); exit(2); }
    uint8_t b[4096];
    size_t got;
    while ((got = fread(b, 1, sizeof(b), f)) > 0) v.insert(v.end(), b, b + got);
    fclose(f);
    return v;
}

static const uint32_t kBase = 0x80100000u;   // an IMX SDRAM guest address (emu_plug.py --imx maps 0x80000000)

static void loadOne(const char* path, uint32_t cpu, bool wantOk, const char* dump)
{
    std::vector<uint8_t> f = readAll(path);
    plug_file_header h;
    memcpy(&h, f.data(), sizeof(h));
    CHECK(h.cpu == cpu, "%s: header cpu %u, want %u", path, unsigned(h.cpu), unsigned(cpu));
    static uint8_t slot[plug::kImxSynthSlotBytes] __attribute__((aligned(8)));
    memset(slot, 0xA5, sizeof(slot));
    RecOps ops;
    uint32_t entry = 0;
    plug::Err e = plug::loadImage(f.data(), uint32_t(f.size()), h.kind, slot, sizeof(slot), kBase, entry, ops);
    const uint32_t entryOk = entry;
    if (!wantOk)
    {
        CHECK(e == plug::kAbi, "%s: err %d, want kAbi on this host", path, int(e));
        CHECK(ops.calls.empty() && entry == 0, "%s: a refused file touched the caches", path);
        return;
    }
    CHECK(e == plug::kOk, "%s: err %d", path, int(e));
    if (e != plug::kOk) return;
    CHECK(entry == ((kBase + h.entry_offset) | 1u), "%s: entry %08x", path, entry);
    CHECK(ops.calls == std::vector<int>({1, 2, 3}), "%s: cache steps not clean, invalidate, barrier", path);
    CHECK(ops.p == slot && ops.n == h.image_size + h.bss_size, "%s: D-cache clean range %u, want %u", path, ops.n,
          h.image_size + h.bss_size);
    for (uint32_t i = 0; i < h.bss_size; i++)
        if (slot[h.image_size + i]) { CHECK(false, "%s: bss byte %u not zero", path, i); break; }
    const uint8_t* img = f.data() + h.header_size;
    const uint8_t* tab = img + h.image_size;
    for (uint32_t k = 0; k < h.rebase_count; k++)
    {
        uint32_t off, was, now;
        memcpy(&off, tab + 4 * k, 4);
        memcpy(&was, img + off, 4);
        memcpy(&now, slot + off, 4);
        CHECK(now == was + kBase, "%s: rebase word %u at %x: %08x, want %08x", path, k, off, now, was + kBase);
    }
    // the bytes past image + bss stay untouched
    CHECK(slot[h.image_size + h.bss_size] == 0xA5, "%s: wrote past image + bss", path);
    // a damaged byte inside the CRC range is refused before any copy, a slot one word short is kTooBig
    std::vector<uint8_t> bad = f;
    bad[h.header_size + h.image_size / 2] ^= 1;
    RecOps o2;
    CHECK(plug::loadImage(bad.data(), uint32_t(bad.size()), h.kind, slot, sizeof(slot), kBase, entry, o2) == plug::kCrc,
          "%s: damaged file not kCrc", path);
    CHECK(plug::loadImage(f.data(), uint32_t(f.size()), h.kind, slot, h.image_size + h.bss_size - 4, kBase, entry, o2) ==
              plug::kTooBig, "%s: short slot not kTooBig", path);
    CHECK(plug::loadImage(f.data(), uint32_t(f.size()) - 4, h.kind, slot, sizeof(slot), kBase, entry, o2) == plug::kRead,
          "%s: cut file not kRead", path);
    CHECK(o2.calls.empty(), "%s: a refused load touched the caches", path);
    if (dump)
    {
        FILE* d = fopen(dump, "wb");
        if (d) { fwrite(slot, 1, h.image_size + h.bss_size, d); fclose(d); }
    }
    printf("ok    %s cpu %u image %u bss %u rebase %u entry %08x\n", path, unsigned(cpu), h.image_size, h.bss_size,
           h.rebase_count, entryOk);
}

int main(int argc, char** argv)
{
    if (argc < 3) { printf("usage: plug_load_test <cm7.plg> <cm4f.plg> [slot-dump]\n"); return 2; }
    printf("host %s: cm7 %s\n", plug::kHostRunsCm7 ? "imx" : "mk66", plug::kHostRunsCm7 ? "accepted" : "refused");
    CHECK(plug::hostCpuOk(PLUG_CPU_CM4F) && !plug::hostCpuOk(0) && !plug::hostCpuOk(3), "hostCpuOk");
    loadOne(argv[1], PLUG_CPU_CM7, plug::kHostRunsCm7, argc > 3 ? argv[3] : nullptr);
    loadOne(argv[2], PLUG_CPU_CM4F, true, nullptr);
    // the synth slot size is one constant (plug_load.h kImxSynthSlotBytes, IMX port P7 slot options), the sum follows it
    static_assert(plug::kImxCodeSlotsBytes == 3 * plug::kImxSynthSlotBytes + 9728 && plug::kImxSynthSlotBytes % 4096 == 0, "IMX code slots");
    printf(fails ? "RESULT: RED %d\n" : "RESULT: GREEN\n", fails);
    return fails ? 1 : 0;
}
