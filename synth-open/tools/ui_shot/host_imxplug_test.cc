// ui_shot PLATFORM=IMX BRIDGE=1 only (`make PLATFORM=IMX BRIDGE=1 imx-plug-test`, IMX port P5 H8, ref/ff-imx-port-plan.md R2.1h): every
// synth .plg (CM4F) of a card folder is loaded by the IMX host's own loader, plug::imx::load (plug_imx.cpp compiled for the plus sim), into
// an IMX synth slot that Unicorn maps at the IMX SDRAM address 0x80000000 (host_ucbridge kImxSdram, plug::imx::simSlots): copy, rebase for
// the slot's guest address, bss, then the cache steps (counted here, Unicorn has no caches). The engine then starts through the image's
// plug_entry protocol and checkDesc and is driven from native code with the render of host_ucbridge_test.cc (same notes, voices, blocks).
// Two references per engine:
//   1. the same engine placed the MK66 way (text in the FlexNVM window, rw in s_pool) in this process: must be byte equal (the gate),
//   2. REF_DIR/uc_<name>.f32 (emu_parity/run.sh, the renders validate_synth.py and emu_render.py agree with), reported when present.
// Then the IMX services table rate: a copy of the image's plug::s_host with sample_rate = plug::imx::host()->sample_rate (44,100, the
// IMX codec) in the IMX window, the engine restarted with it. Reported only (an engine that reads the host rate renders differently).
// An engine whose image + bss is over the IMX synth slot (plug_load.h kImxSynthSlotBytes) fails the load with kTooBig and is reported as such, not as a failure.
//   host_imxplug_test AXF SYNTH_DIR [REF_DIR] [BLOCKS VOICES]   exit 0 only when every engine that fits the slot is byte equal to 1.
#include "host_ucbridge.h"
#include "plug_imx.h"

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

uint32_t u32(const std::vector<uint8_t>& d, size_t o)
{
    uint32_t v = 0;
    if (o + 4 <= d.size()) memcpy(&v, &d[o], 4);
    return v;
}

// plug_synth start(): entry, checkDesc, init, PLUG_EXT_SYNTH, fn, layout, work. nullptr and a reason on failure
const plug_ext_synth* start(uint32_t entryRun, uint32_t gHost, uint32_t base, uint32_t text, uint32_t work, uint32_t workN, std::string& why)
{
    const uint32_t gd = gHost ? ucb::call(entryRun, gHost, (PLUG_ABI_MAJOR << 16) | PLUG_ABI_MINOR) : ucb::entry(entryRun);
    const int ck = gd ? ucb::checkDesc(gd, base, text, PLUG_KIND_SYNTH) : -1;
    const plug_desc* d = ck == 0 ? ucb::mirrorDesc(gd) : nullptr;
    char b[160];
    if (!d) return snprintf(b, sizeof b, "entry 0x%08x checkDesc %d", gd, ck), why = b, nullptr;
    uint64_t inst = 0;
    const int32_t ir = d->init(&inst, nullptr);
    const plug_ext_synth* x = ir == 0 && d->query ? static_cast<const plug_ext_synth*>(d->query(PLUG_EXT_SYNTH)) : nullptr;
    const uint32_t gx = ucb::guestOf(x);
    const uint32_t gNoteOn = gx ? *reinterpret_cast<const uint32_t*>(ucb::g2h(gx + 80, 4)) : 0;
    const bool fnOk = ucb::fn(gNoteOn, base, text, true) && !ucb::fn(gNoteOn & ~1u, base, text, true);
    if (!x || !fnOk || x->layout != ucb::hostSynthLayout())
        return snprintf(b, sizeof b, "init %d ext %p fn %d layout 0x%08x", ir, (const void*)x, int(fnOk), x ? x->layout : 0), why = b, nullptr;
    if (x->work_bytes)
    {
        if (x->work_bytes > workN) return snprintf(b, sizeof b, "work %u B > s_work %u B", x->work_bytes, workN), why = b, nullptr;
        memset(ucb::g2h(work, x->work_bytes), 0, x->work_bytes);
        x->set_work(ucb::g2h(work, x->work_bytes), x->work_bytes);
    }
    return x;
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: host_imxplug_test AXF SYNTH_DIR [REF_DIR] [BLOCKS VOICES]\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    const char* refDir = argc > 3 ? argv[3] : "";
    const uint32_t blocks = argc > 4 ? uint32_t(atoi(argv[4])) : 100, voices = argc > 5 ? uint32_t(atoi(argv[5])) : 4;
    if (!ucb::init(argv[1])) return fprintf(stderr, "FAIL: %s\n", ucb::error()), 1;
    ucb::setServices({svcAlloc, svcCycles, svcQuery});
    const uint32_t pool = ucb::sym("plug::(anonymous namespace)::s_pool"), poolN = ucb::symSize("plug::(anonymous namespace)::s_pool");
    const uint32_t work = ucb::sym("plug::(anonymous namespace)::s_work"), workN = ucb::symSize("plug::(anonymous namespace)::s_work");
    if (!pool || !work) return printf("FAIL: s_pool or s_work not in the axf\n"), 1;

    // the IMX code slots inside the IMX SDRAM window, then a copy of the image's services table with the IMX codec rate
    static_assert(((plug::kImxCodeSlotsBytes + 63u) & ~63u) + 4096u <= ucb::kImxSdramSize, "IMX code slots plus the host table copy fit the window");
    uint8_t* slots = ucb::g2h(ucb::kImxSdram, plug::kImxCodeSlotsBytes);
    const uint32_t gImxHost = ucb::kImxSdram + ((plug::kImxCodeSlotsBytes + 63u) & ~63u);
    const uint32_t hostN = *reinterpret_cast<const uint32_t*>(ucb::g2h(ucb::hostTable(), 4));
    uint8_t* imxHost = ucb::g2h(gImxHost, hostN);
    if (!slots || !imxHost || hostN < 12) return printf("FAIL: IMX window (slots %p, host table %p, %u B)\n", (void*)slots, (void*)imxHost, hostN), 1;
    plug::imx::simSlots(slots, ucb::kImxSdram);
    memcpy(imxHost, ucb::g2h(ucb::hostTable(), hostN), hostN);
    const float imxRate = plug::imx::host()->sample_rate;
    memcpy(imxHost + 8, &imxRate, 4);   // struct plug_host: size, level, sample_rate
    printf("axf %s, IMX slots at 0x%08x (%u B, synth slot %u B), IMX host table at 0x%08x (%u B, rate %.1f)\n", ucb::axfPath(), ucb::kImxSdram,
           plug::kImxCodeSlotsBytes, plug::kImxSynthSlotBytes, gImxHost, hostN, double(imxRate));

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
    int same = 0, bad = 0, tooBig = 0, refSame = 0, refRuns = 0, rateSame = 0;
    std::vector<float> mk(size_t(blocks) * 2 * kN), imx(mk.size()), rate(mk.size());
    const size_t bytes = mk.size() * 4;
    int part = 0;
    for (const std::string& n : names)
    {
        ucb::LaneScope lane(ucb::kPart0 + part);
        const std::vector<uint8_t> file = readFile(std::string(argv[2]) + "/" + n + ".plg");
        // 1. the MK66 placement, as host_ucbridge_test
        const Placed pl = place(file, ucb::kNvm, pool, poolN);
        std::string why;
        const plug_ext_synth* x = pl.err.empty() ? start(pl.entry, 0, pl.base, pl.text, work, workN, why) : nullptr;
        if (!x)
        {
            printf("  %-7s FAIL mk66 place: %s%s\n", n.c_str(), pl.err.c_str(), why.c_str());
            bad++;
            continue;
        }
        render(x, mk.data(), blocks, voices, notes);
        // 2. the IMX load into synth slot `part`
        const uint32_t hsize = file.size() > 6 ? uint32_t(file[4] | file[5] << 8) : 0, img = u32(file, 24), bss = u32(file, 28);
        const uint32_t text = u32(file, 52) ? u32(file, 52) : img;
        const plug::imx::CacheOps c0 = plug::imx::simOps();
        uint32_t entry = 0;
        const plug::Err e = plug::imx::load(file.data(), uint32_t(file.size()), PLUG_KIND_SYNTH, part, entry);
        const plug::imx::CacheOps& c1 = plug::imx::simOps();
        const uint32_t base = plug::imx::slotAddr(plug::imx::synthSlot(part));
        if (e == plug::kTooBig)
        {
            printf("  %-7s TOO BIG for the IMX synth slot: image %u + bss %u = %u B > %u B (text %u + rw %u, kTooBig, header %u B)\n", n.c_str(), img,
                   bss, img + bss, plug::kImxSynthSlotBytes, text, img - text + bss, hsize);
            tooBig++;
            continue;
        }
        if (e != plug::kOk || c1.cleaned != c0.cleaned + 1 || c1.invalidated != c0.invalidated + 1 || c1.barriers != c0.barriers + 1)
        {
            printf("  %-7s FAIL imx::load err %d, cache steps %u %u %u\n", n.c_str(), int(e), c1.cleaned - c0.cleaned, c1.invalidated - c0.invalidated,
                   c1.barriers - c0.barriers);
            bad++;
            continue;
        }
        ucb::codeChanged();
        const plug_ext_synth* xi = start(entry, 0, base, text, work, workN, why);
        if (!xi)
        {
            printf("  %-7s FAIL imx start at 0x%08x: %s\n", n.c_str(), base, why.c_str());
            bad++;
            continue;
        }
        const uint64_t f0 = ucb::faults();
        render(xi, imx.data(), blocks, voices, notes);
        const bool eq = memcmp(mk.data(), imx.data(), bytes) == 0 && ucb::faults() == f0;
        // REF_DIR
        std::string refTxt = "no ref";
        const std::vector<uint8_t> ref = *refDir ? readFile(std::string(refDir) + "/uc_" + n + ".f32") : std::vector<uint8_t>();
        if (!ref.empty())
        {
            refRuns++;
            const bool r = ref.size() == bytes && memcmp(ref.data(), imx.data(), bytes) == 0;
            refSame += r;
            refTxt = r ? "= uc ref" : "!= uc ref";
        }
        // 3. the IMX services table (44,100 Hz)
        plug::imx::load(file.data(), uint32_t(file.size()), PLUG_KIND_SYNTH, part, entry);
        ucb::codeChanged();
        const plug_ext_synth* xr = start(entry, gImxHost, base, text, work, workN, why);
        std::string rateTxt = "imx rate: start failed " + why;
        if (xr)
        {
            render(xr, rate.data(), blocks, voices, notes);
            const bool r = memcmp(rate.data(), imx.data(), bytes) == 0;
            rateSame += r;
            rateTxt = r ? "imx rate: same" : "imx rate: differs (reads sample_rate)";
        }
        printf("  %-7s %s (slot %d at 0x%08x, image+bss %u B = text %u + rw %u, %s, %s)\n", n.c_str(),
               eq ? "BIT IDENTICAL to mk66 placement" : "DIFFERS from mk66 placement", part, base, img + bss, text, img - text + bss, refTxt.c_str(),
               rateTxt.c_str());
        eq ? same++ : bad++;
        part = (part + 1) % plug::kImxSynthSlots;
    }
    printf("%d of %zu synth .plg loaded by plug::imx::load bit identical to the mk66 placement, %d too big for the slot, %d failed, uc ref %d of %d, "
           "imx rate same %d, faults %llu\n",
           same, names.size(), tooBig, bad, refSame, refRuns, rateSame, (unsigned long long)ucb::faults());
    return bad == 0 && same > 0 ? 0 : 1;
}
