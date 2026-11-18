// ui_shot BRIDGE=1 only (`make BRIDGE=1 bridge-test`): the bridge unit test of emu parity T2. Every synth .plg of a card folder is
// placed the way the synth bank places it (text at the FlexNVM run address 0x10000000, rw and bss in plug_synth's s_pool, the work
// arena in s_work: the release axf's own addresses), started through the image (plug_entry with the image's plug::s_host, the image's
// plug::checkDesc and plug_synth fn), mirrored (plug_desc, plug_ext_synth thunks) and driven from NATIVE code with native SynthVoice
// records (160 B here, 152 B in the guest), exactly as poly-engines tools/render_harness.cpp drives it under emu_render.py: same
// notes, voices, gate, block order. The output must be byte equal to the prototype's (ucbridge_proto) and emu_render.py's renders.
//   host_ucbridge_test AXF SYNTH_DIR REF_DIR [BLOCKS VOICES]   REF_DIR holds uc_<name>.f32 (emu_parity/run.sh)
// Exit 0 only when every engine is byte equal.
#include "host_ucbridge.h"

#include "audio/synth_engine.h"

#include <algorithm>
#include <chrono>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

using namespace ot::audio;

namespace
{
constexpr uint32_t kN = 128;

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

struct Placed
{
    uint32_t entry, base, text, rw, rwLen;
    std::string err;
};
// plug::load's steps for a bank install: header, CRC (the logo trailer is outside it), rebase for the run addresses, text at the
// FlexNVM run address, rw + bss at rwRun
Placed place(const std::vector<uint8_t>& file, uint32_t base, uint32_t rwRun, uint32_t rwRoom)
{
    Placed out{};
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
        v = v < rwoff ? base + v : rwRun + v - rwoff;
        memcpy(&image[off], &v, 4);
    }
    const uint32_t rwLen = img - rwoff + bss;
    if (rwLen > rwRoom) return out.err = "rw " + std::to_string(rwLen) + " B > room " + std::to_string(rwRoom), out;
    uint8_t* t = ucb::g2h(base, rwoff);
    uint8_t* rw = ucb::g2h(rwRun, rwLen ? rwLen : 1);
    if (!t || !rw) return out.err = "no window", out;
    memset(t, 0xFF, ucb::kNvmSize - (base - ucb::kNvm));   // erased flash
    memcpy(t, image.data(), rwoff);
    memset(rw, 0, rwLen);
    memcpy(rw, image.data() + rwoff, img - rwoff);
    ucb::codeChanged();   // plug::syncCode on the unit
    out.entry = base + entry;
    out.base = base;
    out.text = rwoff;
    out.rw = rwRun;
    out.rwLen = rwLen;
    return out;
}

// tools/render_harness.cpp run(), natively, the defaults emu_render.Run.run gives it (no overrides, no ramp, no glide, slot 0, vel 0)
void render(const plug_ext_synth* x, float* out, uint32_t blocks, uint32_t voices, const uint32_t* notes)
{
    alignas(8) static SynthParams p;
    alignas(8) static SynthSlotScratch sc;
    static SynthVoice v[4];
    bool on[4] = {};
    float gain[4] = {};
    memset(&p, 0, sizeof(p));
    p.engine = x->type_id;
    p.count = int(x->param_count);
    for (uint32_t i = 0; i < x->param_count; i++) p.start[i] = p.end[i] = x->params[i].def;
    memset(&sc, 0, sizeof(sc));
    if (x->slot_reset) x->slot_reset(&sc);
    if (x->set_tempo) x->set_tempo(120.0f);
    const uint32_t offBlock = 1000000;
    for (uint32_t b = 0; b < blocks; b++)
    {
        p.moving = false;
        p.serial++;
        for (uint32_t k = 0; k < voices; k++)
        {
            SynthVoice& vv = v[k];
            if (b == 0)
            {
                SynthNote n;
                n.note = uint8_t(notes[k]);
                n.pitch = float(notes[k]);
                n.velocity = x->volume_param < 0 ? 100.0f / 127.0f : 1.0f;
                vv = SynthVoice{};
                memset(vv.state, 0, sizeof(vv.state));
                vv.slotScratch = &sc;
                vv.slot = 0;
                vv.note = n.note;
                vv.gate = true;
                on[k] = true;
                gain[k] = 0.0f;
                x->note_on(&vv, &p, &n);
            }
            else if (b == offBlock && on[k])
            {
                vv.gate = false;
                x->note_off(&vv, &p);
            }
        }
        float* L = out + b * 2 * kN;
        float* R = L + kN;
        memset(L, 0, 2 * kN * sizeof(float));
        if (x->prepare) x->prepare(&sc, &p);
        for (uint32_t k = 0; k < voices; k++)
        {
            if (!on[k]) continue;
            const float vel = 1.0f;
            const float target = x->volume_param >= 0 ? p.end[x->volume_param] * vel : 1.0f;
            const float g0 = gain[k];
            gain[k] = target;
            if (!x->render(&v[k], &p, g0, (target - g0) * (1.0f / float(kN)), L, R)) on[k] = false;
        }
        memcpy(p.start, p.end, sizeof(p.start));
    }
}

void* svcAlloc(uint32_t bytes, uint32_t) { return ucb::heapAlloc(bytes); }
uint32_t svcCycles() { return 0; }
const void* svcQuery(uint32_t) { return nullptr; }
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 4)
    {
        fprintf(stderr, "usage: host_ucbridge_test AXF SYNTH_DIR REF_DIR [BLOCKS VOICES]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const uint32_t blocks = argc > 4 ? uint32_t(atoi(argv[4])) : 100, voices = argc > 5 ? uint32_t(atoi(argv[5])) : 4;
    if (!ucb::init(argv[1]))
    {
        fprintf(stderr, "FAIL: %s\n", ucb::error());
        return 1;
    }
    ucb::setServices({svcAlloc, svcCycles, svcQuery});   // as the plug_host.cpp overlay does (hostAlloc, hostCycles, hostQuery)
    const uint32_t pool = ucb::sym("plug::(anonymous namespace)::s_pool"), poolN = ucb::symSize("plug::(anonymous namespace)::s_pool");
    const uint32_t work = ucb::sym("plug::(anonymous namespace)::s_work"), workN = ucb::symSize("plug::(anonymous namespace)::s_work");
    printf("axf %s: s_host 0x%08x checkDesc 0x%08x fn 0x%08x s_pool 0x%08x (%u B) s_work 0x%08x (%u B) s_slot 0x%08x\n", ucb::axfPath(),
           ucb::hostTable(), ucb::sym("plug::checkDesc"), ucb::sym("plug::(anonymous namespace)::fn"), pool, poolN, work, workN,
           ucb::sym("plug::(anonymous namespace)::s_slot"));
    if (!pool || !work) return printf("FAIL: s_pool or s_work not in the axf\n"), 1;
    std::vector<std::string> names;
    if (DIR* dd = opendir(argv[2]))
    {
        while (dirent* e = readdir(dd))
        {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".plg") == 0) names.push_back(n.substr(0, n.size() - 4));
        }
        closedir(dd);
    }
    std::sort(names.begin(), names.end());
    const uint32_t notes[4] = {48, 55, 60, 64};
    int same = 0, bad = 0;
    std::vector<float> out(size_t(blocks) * 2 * kN);
    for (const std::string& n : names)
    {
        ucb::LaneScope lane(ucb::kPart0);   // part 1's Unicorn instance and thread
        const Placed pl = place(readFile(std::string(argv[2]) + "/" + n + ".plg"), ucb::kNvm, pool, poolN);
        if (!pl.err.empty())
        {
            printf("  %-7s FAIL place: %s\n", n.c_str(), pl.err.c_str());
            bad++;
            continue;
        }
        const uint32_t gd = ucb::entry(pl.entry);
        const int ck = gd ? ucb::checkDesc(gd, pl.base, pl.text, PLUG_KIND_SYNTH) : -1;
        const plug_desc* d = ck == 0 ? ucb::mirrorDesc(gd) : nullptr;
        if (!d)
        {
            printf("  %-7s FAIL entry 0x%08x checkDesc %d\n", n.c_str(), gd, ck);
            bad++;
            continue;
        }
        uint64_t inst = 0;   // plug_synth start(): a synth engine has no instance, init gets 8 bytes
        const int32_t ir = d->init(&inst, nullptr);
        const plug_ext_synth* x = ir == 0 && d->query ? static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH)) : nullptr;
        const uint32_t gx = ucb::guestOf(x);
        const uint32_t gNoteOn = gx ? *reinterpret_cast<const uint32_t*>(ucb::g2h(gx + 80, 4)) : 0;
        const bool fnOk = ucb::fn(gNoteOn, pl.base, pl.text, true) && !ucb::fn(gNoteOn & ~1u, pl.base, pl.text, true);
        if (!x || !fnOk || x->layout != ucb::hostSynthLayout())
        {
            printf("  %-7s FAIL init %d ext %p fn %d layout 0x%08x\n", n.c_str(), ir, (const void*)x, int(fnOk), x ? x->layout : 0);
            bad++;
            continue;
        }
        if (x->work_bytes)
        {
            if (x->work_bytes > workN)
            {
                printf("  %-7s FAIL work %u B > s_work %u B\n", n.c_str(), x->work_bytes, workN);
                bad++;
                continue;
            }
            memset(ucb::g2h(work, x->work_bytes), 0, x->work_bytes);
            x->set_work(ucb::g2h(work, x->work_bytes), x->work_bytes);
        }
        const uint64_t f0 = ucb::faults(), c0 = ucb::calls();
        const auto t0 = std::chrono::steady_clock::now();
        render(x, out.data(), blocks, voices, notes);
        const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / blocks;
        const std::vector<uint8_t> ref = readFile(std::string(argv[3]) + "/uc_" + n + ".f32");
        const size_t bytes = out.size() * 4;
        const bool eq = ref.size() == bytes && memcmp(ref.data(), out.data(), bytes) == 0;
        long firstDiff = -1;
        if (!eq && ref.size() == bytes)
            for (size_t i = 0; i < out.size() && firstDiff < 0; i++)
                if (memcmp(&ref[i * 4], &out[i], 4) != 0) firstDiff = long(i);
        double peak = 0;
        for (float f : out) peak = std::max(peak, double(f < 0 ? -f : f));
        printf("  %-7s %s (%s, %u params, %u voices max, work %u B, layout 0x%08x -> 0x%08x, %.0f us/block, %llu calls, peak %.4f%s)\n",
               n.c_str(), eq ? "BIT IDENTICAL" : "DIFFERS", x->name, x->param_count, x->max_voices, x->work_bytes, ucb::deviceSynthLayout(),
               x->layout, us, (unsigned long long)(ucb::calls() - c0), peak, ucb::faults() != f0 ? ", FAULTS" : "");
        if (!eq)
            printf("          ref %zu B, out %zu B, first differing float %ld (block %ld)\n", ref.size(), bytes, firstDiff,
                   firstDiff < 0 ? -1L : long(firstDiff / long(2 * kN)));
        eq && ucb::faults() == f0 ? same++ : bad++;
    }
    // the cost of one lane hand-off (another thread's Unicorn instance) for an empty guest function: a `bx lr` of the image
    {
        const uint32_t bxlr = ucb::sym("plug::(anonymous namespace)::hostCycles");   // patched to bx lr + trap
        const auto t0 = std::chrono::steady_clock::now();
        for (int i = 0; i < 20000; i++) ucb::call(bxlr);
        printf("main lane round trip (hand-off + uc_emu_start + trap): %.2f us\n",
               std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count() / 20000);
    }
    printf("%d of %zu synth .plg bit identical to %s, %d failed, faults %llu\n", same, names.size(), argv[3], bad,
           (unsigned long long)ucb::faults());
    return bad == 0 && same > 0 ? 0 : 1;
}
