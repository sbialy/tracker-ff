// ui_shot BRIDGE=1 only (`make BRIDGE=1 bridge-fx-test`): the bridge proof of emu parity T6. Every master insert .plg and every Sample Editor
// .ptfx of a card folder is run from NATIVE code through host_ucbridge exactly as the firmware does (plug_host.cpp / plug_insert.cpp and
// ptfx_loader.cpp / mtEffectModular.cpp under the BRIDGE overlay rules): insert text at the SRAM_L slot address, entry, plug::checkDesc,
// the descriptor mirror, init, then set_param and process(io pair) per block on the insert lane. A .ptfx: its code in the window of
// externalRamScratchBuff (the 128 KB code buffer, the scratch right after it), the three entry points as native thunks, the image's own
// PtfxHostApi (newlib libm as Thumb code) with set_extra_samples answered natively, in <= 8192 sample pieces over the real int16 buffers
// of the host. The output must be byte equal to synth-open/tools/emu_parity/fx_ref.py's, which loads the same bytes into a Python Unicorn
// machine by itself (other loader, other addresses). Same transcript, see that file's header.
//   host_ucbridge_fx_test AXF CARD_DIR REF_DIR [OUT_DIR]     REF_DIR holds fx_in_*.* and ref_*.* (fx_ref.py)
// Exit 0 only when every plugin is byte equal and no guest fault happened.
#include "host_ucbridge.h"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{
constexpr uint32_t kN = 128, kBlocks = 400, kPtIn = 20000, kPtChunk = 8192;
constexpr uint32_t kScratchOff = 128 * 1024, kScratchSize = (768 - 128) * 1024;

std::vector<uint8_t> readFile(const std::string& p)
{
    std::vector<uint8_t> d;
    FILE* f = fopen(p.c_str(), "rb");
    if (!f) return d;
    uint8_t b[65536];
    size_t n;
    while ((n = fread(b, 1, sizeof b, f)) > 0) d.insert(d.end(), b, b + n);
    fclose(f);
    return d;
}

std::vector<std::string> listFiles(const std::string& dir, const char* ext)
{
    std::vector<std::string> v;
    const size_t el = strlen(ext);
    if (DIR* dd = opendir(dir.c_str()))
    {
        while (dirent* e = readdir(dd))
        {
            const std::string n = e->d_name;
            if (n.size() > el && n.compare(n.size() - el, el, ext) == 0) v.push_back(n.substr(0, n.size() - el));
        }
        closedir(dd);
    }
    std::sort(v.begin(), v.end());
    return v;
}

uint32_t fnv(const void* p, size_t n)
{
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < n; i++) h = (h ^ static_cast<const uint8_t*>(p)[i]) * 16777619u;
    return h;
}

float spread(float mn, float mx, uint32_t i)
{
    const float f = float((i * 37) % 100) / 100.0f;
    return mn + (mx - mn) * f;
}

void* svcAlloc(uint32_t bytes, uint32_t) { return ucb::heapAlloc(bytes); }
uint32_t svcCycles() { return 0; }
const void* svcQuery(uint32_t) { return nullptr; }

std::vector<std::pair<uint32_t, uint32_t>> g_extra;   // set_extra_samples(prepend, append) of the .ptfx under test
void ovrExtra(void* uc) { g_extra.emplace_back(ucb::reg(uc, 0), ucb::reg(uc, 1)); }

struct Placed
{
    uint32_t entry = 0, base = 0, rwoff = 0;
    std::string err;
};
// plug::load's steps for a master insert (plug_host.cpp): header, CRC (the logo trailer is outside it), rebase, text at the slot,
// rw + bss right behind it
Placed placeInsert(const std::vector<uint8_t>& file, uint32_t slot, uint32_t room)
{
    Placed out;
    std::vector<uint8_t> d = file;
    auto u32 = [&](size_t o) { uint32_t v; memcpy(&v, &d[o], 4); return v; };
    auto u16 = [&](size_t o) { uint16_t v; memcpy(&v, &d[o], 2); return v; };
    if (d.size() < 64 || u32(0) != PLUG_MAGIC) return out.err = "magic", out;
    const uint32_t hsize = u16(4), img = u32(24), bss = u32(28), nreb = u32(32), entry = u32(36), crc = u32(48);
    const uint32_t rwoff = u32(52) ? u32(52) : img;
    const uint32_t logo = hsize >= 64 + 96 ? u32(64 + 72) : 0;
    if (logo && logo == hsize + img + 4 * nreb && d.size() == logo + 1676) d.resize(logo);
    if (d.size() != hsize + img + 4 * nreb || uint32_t(crc32(0, d.data() + 64, uInt(d.size() - 64))) != crc) return out.err = "crc/size", out;
    std::vector<uint8_t> image(d.begin() + hsize, d.begin() + hsize + img);
    for (uint32_t k = 0; k < nreb; k++)
    {
        const uint32_t off = u32(hsize + img + 4 * k);
        uint32_t v;
        memcpy(&v, &image[off], 4);
        v = v < rwoff ? slot + v : slot + rwoff + (v - rwoff);
        memcpy(&image[off], &v, 4);
    }
    const uint32_t total = img + bss;
    if (total > room) return out.err = "image " + std::to_string(total) + " B > slot " + std::to_string(room) + " B", out;
    uint8_t* w = ucb::g2h(slot, total);
    if (!w) return out.err = "no window", out;
    memset(w, 0, total);
    memcpy(w, image.data(), img);
    ucb::codeChanged();   // plug::syncCode
    out.entry = slot + entry;
    out.base = slot;
    out.rwoff = rwoff;
    return out;
}

bool g_ok = true;

int insertLeg(const std::string& card, const std::string& ref, const std::string& outDir)
{
    const std::vector<uint8_t> in = readFile(ref + "/fx_in_ins.f32");
    if (in.size() != size_t(kBlocks) * 2 * kN * 4)
    {
        printf("FAIL: %s/fx_in_ins.f32 missing or wrong size (%zu), run fx_ref.py\n", ref.c_str(), in.size());
        return 1;
    }
    const uint32_t slot = ucb::sym("plug::(anonymous namespace)::s_slot"), slotN = ucb::symSize("plug::(anonymous namespace)::s_slot");
    if (!slot) return printf("FAIL: s_slot not in the axf\n"), 1;
    int same = 0, bad = 0;
    const std::vector<std::string> names = listFiles(card + "/Plugins/Master", ".plg");
    for (const std::string& n : names)
    {
        ucb::LaneScope lane(ucb::kInsert);   // the insert chain's Unicorn instance and thread (AudioEffectPlug::update)
        const Placed pl = placeInsert(readFile(card + "/Plugins/Master/" + n + ".plg"), slot, slotN);
        if (!pl.err.empty()) { printf("  ins %-8s FAIL place: %s\n", n.c_str(), pl.err.c_str()), bad++; continue; }
        const uint32_t gd = ucb::entry(pl.entry);
        const int ck = gd ? ucb::checkDesc(gd, pl.base, pl.rwoff, PLUG_KIND_MASTER_INSERT) : -1;
        const plug_desc* d = ck == 0 ? ucb::mirrorDesc(gd) : nullptr;
        if (!d) { printf("  ins %-8s FAIL entry 0x%08x checkDesc %d\n", n.c_str(), gd, ck), bad++; continue; }
        void* inst = ucb::heapAlloc(d->instance_size ? d->instance_size : 8);
        const int32_t ir = d->init(inst, nullptr);
        if (ir != 0) { printf("  ins %-8s FAIL init %d\n", n.c_str(), ir), bad++; continue; }
        const plug_ext_tempo* tempo = ucb::mirrorTempo(ucb::tempoExt(gd, pl.base, pl.rwoff));
        const plug_ext_tail* tail = ucb::mirrorTail(ucb::tailExt(gd, pl.base, pl.rwoff));
        std::vector<float> out(size_t(kBlocks) * 2 * kN);
        std::vector<uint32_t> quiet;
        const uint64_t f0 = ucb::faults(), c0 = ucb::calls();
        double procUs = 0;
        for (uint32_t b = 0; b < kBlocks; b++)
        {
            const bool all = b == 0 || b == 40 || b == 80 || b == 120 || b == 200;
            if (tempo)
            {
                if (tempo->set_tempo) tempo->set_tempo(inst, 120.0f);
                if (tempo->struct_size >= 12 && tempo->set_transport) tempo->set_transport(inst, float(b) * 0.25f, (b / 16) % 2 == 0 ? 1u : 0u);
            }
            if (all)
                for (uint32_t i = 0; i < d->num_params; i++)
                {
                    const plug_param& p = d->params[i];
                    const float v = b == 40 ? p.max : b == 80 ? p.min : b == 120 ? spread(p.min, p.max, i) : p.def;
                    d->set_param(inst, i, v);
                }
            if (tail && tail->struct_size >= 12 && tail->quiet) quiet.push_back(tail->quiet(inst));
            float* o = out.data() + size_t(b) * 2 * kN;
            memcpy(o, in.data() + size_t(b) * 2 * kN * 4, 2 * kN * 4);
            float* io[2] = {o, o + kN};
            const auto t0 = std::chrono::steady_clock::now();
            d->process(inst, io, 2, kN);
            procUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
        }
        const std::vector<uint8_t> r = readFile(ref + "/ref_ins_" + n + ".f32"), rq = readFile(ref + "/ref_ins_" + n + ".q");
        const size_t bytes = out.size() * 4;
        const bool eq = r.size() == bytes && memcmp(r.data(), out.data(), bytes) == 0;
        const bool qeq = rq.size() == quiet.size() * 4 && (quiet.empty() || memcmp(rq.data(), quiet.data(), rq.size()) == 0);
        long first = -1;
        if (!eq && r.size() == bytes)
            for (size_t i = 0; i < out.size() && first < 0; i++)
                if (memcmp(&r[i * 4], &out[i], 4) != 0) first = long(i);
        double peak = 0;
        for (float f : out) peak = std::max(peak, double(fabsf(f)));
        if (!outDir.empty())
        {
            FILE* f = fopen((outDir + "/br_ins_" + n + ".f32").c_str(), "wb");
            if (f) fwrite(out.data(), 4, out.size(), f), fclose(f);
        }
        printf("  ins %-8s %s (%s, %u params, tempo %d tail %d, fnv %08x, peak %.4f, %.1f us per block process, %llu guest calls%s)\n", n.c_str(),
               eq && qeq ? "BYTE EQUAL" : "DIFFERS", d->name, d->num_params, tempo != nullptr, tail != nullptr, fnv(out.data(), bytes), peak,
               procUs / kBlocks, (unsigned long long)(ucb::calls() - c0), ucb::faults() != f0 ? ", FAULTS" : "");
        if (!eq) printf("          ref %zu B, out %zu B, first differing float %ld (block %ld)\n", r.size(), bytes, first, first < 0 ? -1L : first / long(2 * kN));
        eq&& qeq&& ucb::faults() == f0 ? same++ : bad++;
    }
    printf("inserts: %d of %zu byte equal to %s, %d failed\n", same, names.size(), ref.c_str(), bad);
    return bad == 0 && same > 0 ? 0 : 1;
}

int ptfxLeg(const std::string& card, const std::string& ref, const std::string& outDir)
{
    const std::vector<uint8_t> inb = readFile(ref + "/fx_in_ptfx.i16");
    if (inb.size() != size_t(kPtIn) * 2)
    {
        printf("FAIL: %s/fx_in_ptfx.i16 missing or wrong size, run fx_ref.py\n", ref.c_str());
        return 1;
    }
    std::vector<int16_t> pin(kPtIn);
    memcpy(pin.data(), inb.data(), inb.size());
    const uint32_t bufG = ucb::sym("externalRamScratchBuff"), bufN = ucb::symSize("externalRamScratchBuff");
    if (!bufG || bufN < kScratchOff + kScratchSize) return printf("FAIL: externalRamScratchBuff not in the axf (0x%x, %u B)\n", bufG, bufN), 1;
    uint8_t* const buf = ucb::symWindow("externalRamScratchBuff", bufN);   // what the ptfx_loader overlay does: the code buffer and the scratch
    const uint32_t hsa = ucb::sym("hostSetExtraSamples");
    if (!hsa || !ucb::override(hsa, ovrExtra)) return printf("FAIL: hostSetExtraSamples not in the axf\n"), 1;
    int same = 0, bad = 0;
    constexpr uint32_t kOutCap = 4u << 20;   // samples: one output buffer for every run, as the sample memory is one SDRAM area on the unit
    std::vector<int16_t> out(kOutCap, 0);
    const std::vector<std::string> names = listFiles(card + "/Effects", ".ptfx");
    for (const std::string& n : names)   // one machine, sorted order: the image's rand state is shared, as in fx_ref.py
    {
        const std::vector<uint8_t> d = readFile(card + "/Effects/" + n + ".ptfx");
        PtfxHeader h;
        if (d.size() < sizeof h) { printf("  ptfx %-14s FAIL short file\n", n.c_str()), bad++; continue; }
        memcpy(&h, d.data(), sizeof h);
        if (h.magic != PTFX_MAGIC || h.format_version != 1 || h.code_offset + h.code_size > d.size() || h.code_size + h.bss_size > kScratchOff)
        {
            printf("  ptfx %-14s FAIL header\n", n.c_str());
            bad++;
            continue;
        }
        memcpy(buf, d.data() + h.code_offset, h.code_size);
        memset(buf + h.code_size, 0, h.bss_size);
        ucb::codeChanged();   // PtfxLoader::syncInstructionCache
        PtfxInitFunc init;
        PtfxSetParamFunc setp;
        PtfxProcessBlockFunc proc;
        ucb::mirrorPtfx((bufG + h.init_offset) | 1, (bufG + h.set_param_offset) | 1, (bufG + h.process_block_offset) | 1, init, setp, proc);
        if (!init || !setp || !proc) { printf("  ptfx %-14s FAIL mirror\n", n.c_str()), bad++; continue; }
        std::vector<uint8_t> res;
        std::string log;
        const uint64_t f0 = ucb::faults(), c0 = ucb::calls();
        double procUs = 0;
        uint32_t procCalls = 0;
        for (int cfg = 0; cfg < 4; cfg++)
        {
            g_extra.clear();
            memset(buf + kScratchOff, 0, kScratchSize);
            init(buf + kScratchOff, kScratchSize, nullptr);
            for (uint32_t i = 0; i < 12; i++)
            {
                float v = 0.0f;
                if (i < h.num_params)
                {
                    const PtfxParamDef& p = h.params[i];
                    v = cfg == 0 ? p.default_val : cfg == 1 ? p.max_val : cfg == 2 ? p.min_val : spread(p.min_val, p.max_val, i);
                }
                setp(uint8_t(i), v);
            }
            uint32_t pre = 0, app = 0;
            for (const auto& e : g_extra) pre += e.first, app += e.second;
            const uint32_t total = pre + kPtIn + app;
            std::fill(out.begin(), out.end(), int16_t(0));
            uint32_t p = 0;
            while (p < total)
            {
                uint32_t bs = std::min(kPtChunk, total - p);
                if (p + bs > kOutCap) { printf("  ptfx %-14s FAIL output buffer\n", n.c_str()), g_ok = false; break; }
                const int16_t* inp[1] = {nullptr};
                int16_t* outp[1] = {out.data() + p};
                if (p < pre)
                    bs = std::min(bs, pre - p);
                else if (p < pre + kPtIn)
                {
                    bs = std::min(bs, pre + kPtIn - p);
                    inp[0] = pin.data() + (p - pre);
                }
                const auto t0 = std::chrono::steady_clock::now();
                const int32_t cnt = proc(inp, outp, bs, 1);
                procUs += std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                procCalls++;
                if (cnt <= 0) break;
                p += uint32_t(cnt);
            }
            res.insert(res.end(), reinterpret_cast<uint8_t*>(out.data()), reinterpret_cast<uint8_t*>(out.data() + p));
            char t[96];
            snprintf(t, sizeof t, " run%d pre=%u app=%u done=%u", cfg, pre, app, p);
            log += t;
        }
        const std::vector<uint8_t> r = readFile(ref + "/ref_ptfx_" + n.substr(0) + ".i16");
        const bool eq = r.size() == res.size() && memcmp(r.data(), res.data(), res.size()) == 0;
        long first = -1;
        if (!eq)
            for (size_t i = 0; i < std::min(r.size(), res.size()) / 2 && first < 0; i++)
                if (memcmp(&r[i * 2], &res[i * 2], 2) != 0) first = long(i);
        if (!outDir.empty())
        {
            FILE* f = fopen((outDir + "/br_ptfx_" + n + ".i16").c_str(), "wb");
            if (f) fwrite(res.data(), 1, res.size(), f), fclose(f);
        }
        printf("  ptfx %-14s %s (%.12s, %zu B, fnv %08x, %.0f us per process_block call, %llu guest calls%s)%s\n", n.c_str(), eq ? "BYTE EQUAL" : "DIFFERS", h.display_name,
               res.size(), fnv(res.data(), res.size()), procUs / std::max(procCalls, 1u), (unsigned long long)(ucb::calls() - c0),
               ucb::faults() != f0 ? ", FAULTS" : "", log.c_str());
        if (!eq) printf("          ref %zu B, out %zu B, first differing sample %ld\n", r.size(), res.size(), first);
        eq&& ucb::faults() == f0 ? same++ : bad++;
    }
    printf(".ptfx: %d of %zu byte equal to %s, %d failed\n", same, names.size(), ref.c_str(), bad);
    return bad == 0 && same > 0 ? 0 : 1;
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: host_ucbridge_fx_test AXF CARD_DIR REF_DIR [OUT_DIR]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!ucb::init(argv[1]))
    {
        fprintf(stderr, "FAIL: %s\n", ucb::error());
        return 1;
    }
    ucb::setServices({svcAlloc, svcCycles, svcQuery});   // as the plug_host.cpp overlay does (hostAlloc, hostCycles, hostQuery)
    const std::string out = argc > 4 ? argv[4] : "";
    printf("axf %s: s_host 0x%08x checkDesc 0x%08x s_slot 0x%08x externalRamScratchBuff 0x%08x\n", ucb::axfPath(), ucb::hostTable(),
           ucb::sym("plug::checkDesc"), ucb::sym("plug::(anonymous namespace)::s_slot"), ucb::sym("externalRamScratchBuff"));
    const int a = insertLeg(argv[2], argv[3], out);
    const int b = ptfxLeg(argv[2], argv[3], out);
    printf("%s: inserts %s, .ptfx %s, guest faults %llu, guest calls %llu\n", a == 0 && b == 0 && ucb::faults() == 0 ? "RESULT: GREEN" : "RESULT: RED", a ? "RED" : "GREEN",
           b ? "RED" : "GREEN", (unsigned long long)ucb::faults(), (unsigned long long)ucb::calls());
    return a == 0 && b == 0 && ucb::faults() == 0 ? 0 : 1;
}
