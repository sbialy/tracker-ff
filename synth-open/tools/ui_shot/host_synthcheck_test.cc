// ui_shot BRIDGE=1 only (`make BRIDGE=1 synth-start-test`): the differential test of emu parity T5. plug_synth.cpp start() is a clone in
// the release image (checkSynth, checkChord and chordStrum inlined into it), the BRIDGE=1 overlay runs host_synthcheck.cpp's ucbs::start
// instead: the image's own calls plus ports of those checks to guest reads. This test runs BOTH on the same bank bytes and compares them.
//
// For every synth .plg of a card folder: the file is placed as the bank places it (text + rw initial bytes at the FlexNVM run address
// 0x10000000, rebased for that address and for rw in the image's s_pool), the image's guest tables are set as plug_synth keeps them for one
// installed engine (s_bank one entry, s_sidx[0] the manifest's uid, crc and numbers, s_lib[0] its name), then
//   image: plug::(anonymous namespace)::start(0, &out) of the release image, run under the bridge (code, *out, s_lib[0].chord/mode/strum)
//   port:  ucbs::start on the same StartIn (code, the extension, chord/mode/strum when it set them)
// must agree: the same code, the same extension, the same three chord bytes (the image's are compared from a sentinel, so "not written" is
// compared too), a guest fault in both or in neither. Variants of each engine, all on the bank bytes start() reads:
//   shipped        the card file as it is (must start: code 0 on both)
//   corrupt        fw_engine_mismatch_sim's payload_flip (file byte 200) and further single byte flips (entry, text, rw, descriptor), placed
//                  without the install's CRC check (a bank that went bad after the install)
//   ABI mismatch   a plugin that refuses the host's ABI (entry returns NULL), another kind, init failing, no extension, an ABI 1.0 / 1.1
//                  extension size, the SynthVoice layout of another interface (the host build's 160 B voice), every field checkSynth reads
//                  out of range in turn, checkChord's and chordStrum's rules, manifest numbers and name that are not the extension's
// The header only rows of fw_engine_mismatch_sim's A3 (magic, abi, cpu, kind, flags, header and image sizes, min_host, version, crc) are
// decided by the scan and the install from the file header: the bank bytes start() runs are the shipped ones, so they are not repeated here.
//   host_synthcheck_test AXF SYNTH_DIR      exit 0 only when every variant agrees, every shipped engine starts, and the codes 0 1 2 3 4 8
//                                           and both chord outcomes were seen. VERBOSE=1 prints every variant.
#include "host_synthcheck.h"
#include "host_ucbridge.h"

#include "plug_bank.h"
#include "plug_index.h"
#include "plug/plug_abi.h"
#include "plug/plug_chord.h"

#include <algorithm>
#include <dirent.h>
#include <map>
#include <math.h>
#include <set>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <zlib.h>

namespace
{
// the device layouts of the guest tables (arm-none-eabi-gdb ptype /o on the release axf): pointer free, so the host compiles them alike
static_assert(sizeof(plug::BankEntry) == 68 && offsetof(plug::BankEntry, rwRun) == 24 && offsetof(plug::BankEntry, file) == 41, "BankEntry");
static_assert(sizeof(plug::Bank) == 948 && offsetof(plug::Bank, e) == 4, "Bank");
static_assert(sizeof(plug::IndexEntry) == 104 && offsetof(plug::IndexEntry, synthStored) == 92 && offsetof(plug::IndexEntry, crc) == 80,
              "IndexEntry");
// plug::(anonymous namespace)::Lib, 24 B: name[9] 0, listed 9, err 10, attached 11, logo 12..14, chord 15, mode 16, strum 17, used 20
constexpr uint32_t kLibSize = 24, kLibName = 0, kLibChord = 15, kLibMode = 16, kLibStrum = 17;
constexpr uint8_t kSentinel = 0x55;
constexpr uint32_t kLibMax = 256;   // plug_host.h
// ui_shot compiles with static_assert defined away: the layouts are checked at run time too
bool layoutsOk()
{
    return sizeof(plug::BankEntry) == 68 && offsetof(plug::BankEntry, rwRun) == 24 && offsetof(plug::BankEntry, file) == 41 &&
           sizeof(plug::Bank) == 948 && offsetof(plug::Bank, e) == 4 && sizeof(plug::IndexEntry) == 104 &&
           offsetof(plug::IndexEntry, synthStored) == 92 && offsetof(plug::IndexEntry, crc) == 80;
}

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
uint32_t u32(const std::vector<uint8_t>& d, size_t o)
{
    uint32_t v = 0;
    if (o + 4 <= d.size()) memcpy(&v, &d[o], 4);
    return v;
}
uint32_t g32(uint32_t g)
{
    const uint8_t* p = ucb::g2h(g, 4);
    uint32_t v = 0;
    if (p) memcpy(&v, p, 4);
    return v;
}
float gf(uint32_t g)
{
    const uint32_t v = g32(g);
    float f;
    memcpy(&f, &v, 4);
    return f;
}
uint8_t g8(uint32_t g)
{
    const uint8_t* p = ucb::g2h(g, 1);
    return p ? *p : 0;
}

// one engine file, rebased for the bank as SdIo::image hands it to bankInstall
struct Engine
{
    std::string file, err;
    uint32_t uid = 0, version = 0, crc = 0, entry = 0, img = 0, text = 0, bss = 0, hs = 0;
    int32_t stored = -1, patch = -1, type = -1;
    char name[16] = {};
    std::vector<uint8_t> image;   // text + rw initial bytes, rebased
};

Engine readEngine(const std::string& path, uint32_t base, uint32_t rwRun)
{
    Engine e;
    e.file = path.substr(path.rfind('/') + 1);
    std::vector<uint8_t> d = readFile(path);
    if (d.size() < 160 || u32(d, 0) != PLUG_MAGIC) return e.err = "not a plugin", e;
    uint16_t hs;
    memcpy(&hs, &d[4], 2);
    const uint32_t img = u32(d, 24), bss = u32(d, 28), nreb = u32(d, 32);
    const uint32_t rwoff = u32(d, 52) ? u32(d, 52) : img;
    const uint32_t logo = hs >= 64 + 96 ? u32(d, 64 + 72) : 0;
    if (logo && logo == hs + img + 4 * nreb && d.size() == logo + 1676) d.resize(logo);
    if (d.size() != hs + img + 4 * nreb || uint32_t(crc32(0, d.data() + 64, uInt(d.size() - 64))) != u32(d, 48)) return e.err = "crc/size", e;
    e.uid = u32(d, 12);
    e.version = u32(d, 16);
    e.crc = u32(d, 48);
    e.entry = u32(d, 36);
    e.img = img;
    e.text = rwoff;
    e.bss = bss;
    e.hs = hs;
    memcpy(e.name, &d[68], 15);
    if (u32(d, 136 + 16) == PLUG_SYNTH_MANIFEST)
    {
        e.stored = int32_t(u32(d, 140));
        e.patch = int32_t(u32(d, 144));
        e.type = int32_t(u32(d, 148));
    }
    e.image.assign(d.begin() + hs, d.begin() + hs + img);
    for (uint32_t k = 0; k < nreb; k++)
    {
        const uint32_t off = u32(d, hs + img + 4 * k);
        uint32_t v;
        memcpy(&v, &e.image[off], 4);
        v = v < rwoff ? base + v : rwRun + v - rwoff;
        memcpy(&e.image[off], &v, 4);
    }
    return e;
}

// the test's hostAlloc: the guest heap, every block given back after each start
std::vector<void*> g_allocs;
void* svcAlloc(uint32_t bytes, uint32_t)
{
    void* p = ucb::heapAlloc(bytes);
    if (p) g_allocs.push_back(p);
    return p;
}
uint32_t svcCycles() { return 0; }
const void* svcQuery(uint32_t) { return nullptr; }
void freeAllocs()
{
    for (void* p : g_allocs) ucb::heapFree(p);
    g_allocs.clear();
}

struct Poke
{
    uint32_t guest;   // where start() sees it (text, or rw: poked into its initial bytes in the flash)
    std::vector<uint8_t> bytes;
};
Poke pk32(uint32_t g, uint32_t v)
{
    Poke p{g, std::vector<uint8_t>(4)};
    memcpy(p.bytes.data(), &v, 4);
    return p;
}
Poke pkf(uint32_t g, float f)
{
    uint32_t v;
    memcpy(&v, &f, 4);
    return pk32(g, v);
}
Poke pk8(uint32_t g, uint8_t v)
{
    return Poke{g, {v}};
}
Poke pkCode(uint32_t fnThumb, uint16_t a, uint16_t b)   // two Thumb instructions at a function
{
    Poke p{fnThumb & ~1u, std::vector<uint8_t>(4)};
    memcpy(&p.bytes[0], &a, 2);
    memcpy(&p.bytes[2], &b, 2);
    return p;
}

// Pokes into what init builds (an extension or chord answer in the engine's bss): applied right after each query returns, in both runs
// (the port through ucbs::setQueryHook, the image through code hooks after its two query calls in start())
std::vector<Poke> g_late;
void applyLate()
{
    for (const Poke& p : g_late)
        if (uint8_t* h = ucb::g2h(p.guest, uint32_t(p.bytes.size()))) memcpy(h, p.bytes.data(), p.bytes.size());
}
void portHook(uint32_t, uint32_t)
{
    applyLate();
}
void imageHook(void*)
{
    applyLate();
}

struct Variant
{
    std::string name;
    std::vector<Poke> pokes;
    int32_t dStored = 0, dPatch = 0, dType = 0;   // the manifest numbers s_sidx carries, off by these
    std::string libName;                          // non-empty: s_lib[0].name instead of the manifest's
};

struct Result
{
    int code = 0;
    bool fault = false;
    uint32_t ext = 0;
    uint8_t chord = kSentinel, mode = kSentinel, strum = kSentinel;
};

std::string chordTxt(const Result& r)
{
    char b[32];
    if (r.chord == kSentinel && r.mode == kSentinel && r.strum == kSentinel)
        snprintf(b, sizeof b, "-");
    else
        snprintf(b, sizeof b, "%d/%d/%d", int8_t(r.chord), int8_t(r.mode), int8_t(r.strum));
    return b;
}
}   // namespace

int main(int argc, char** argv)
{
    if (argc < 3)
    {
        fprintf(stderr, "usage: host_synthcheck_test AXF SYNTH_DIR\n");
        return 2;
    }
    setvbuf(stdout, nullptr, _IONBF, 0);
    if (!ucb::init(argv[1]))
    {
        fprintf(stderr, "FAIL: %s\n", ucb::error());
        return 1;
    }
    if (!layoutsOk()) return printf("FAIL: BankEntry, Bank or IndexEntry is not the device layout on this host\n"), 1;
    ucb::setServices({svcAlloc, svcCycles, svcQuery});
    ucb::setCallTimeout(3000000);   // a corrupt image may loop: over 3 s is a fault
    const uint32_t pool = ucb::sym("plug::(anonymous namespace)::s_pool"), poolN = ucb::symSize("plug::(anonymous namespace)::s_pool");
    const uint32_t bankG = ucb::sym("plug::(anonymous namespace)::s_bank", sizeof(plug::Bank));
    const uint32_t sidxG = ucb::sym("plug::(anonymous namespace)::s_sidx", sizeof(plug::IndexEntry) * kLibMax);
    const uint32_t libG = ucb::sym("plug::(anonymous namespace)::s_lib", kLibSize * kLibMax);
    uint32_t startFn = 0;
    for (const char* n : {"plug::(anonymous namespace)::start(int, plug_ext_synth const**) (.constprop.0)",
                          "plug::(anonymous namespace)::start(int, plug_ext_synth const**)"})
        if (!startFn) startFn = ucb::sym(n);
    const uint32_t outG = ucb::h2gOr0(ucb::heapAlloc(8), 8);
    // the instructions after start()'s two query calls: "movs r0, #3" (PLUG_EXT_SYNTH) or "#6" (PLUG_EXT_CHORD), then "blx rN"
    int hooks = 0;
    if (startFn)
    {
        const uint32_t a = startFn & ~1u, n = ucb::symSize("plug::(anonymous namespace)::start(int, plug_ext_synth const**) (.constprop.0)");
        const uint8_t* code = ucb::g2h(a, n ? n : 2);
        for (uint32_t i = 0; code && i + 4 <= n; i += 2)
        {
            uint16_t h0, h1;
            memcpy(&h0, code + i, 2);
            memcpy(&h1, code + i + 2, 2);
            if ((h0 == 0x2000 + PLUG_EXT_SYNTH || h0 == 0x2000 + PLUG_EXT_CHORD) && (h1 & 0xFF87) == 0x4780)
            {
                printf("image start(): query(%u) returns at 0x%08x\n", unsigned(h0 & 0xFF), a + i + 4);
                ucb::hookAt(a + i + 4, imageHook);
                hooks++;
            }
        }
    }
    ucbs::setQueryHook(portHook);
    printf("axf %s: start 0x%08x s_pool 0x%08x (%u B) s_bank 0x%08x s_sidx 0x%08x s_lib 0x%08x\n", ucb::axfPath(), startFn, pool, poolN, bankG,
           sidxG, libG);
    if (hooks != 2) return printf("FAIL: %d query calls found in the image's start(), 2 expected\n", hooks), 1;
    if (!startFn || !pool || !bankG || !sidxG || !libG || !outG) return printf("FAIL: a symbol of plug_synth.cpp is not in the axf\n"), 1;

    std::vector<std::string> files;
    if (DIR* dd = opendir(argv[2]))
    {
        while (dirent* e = readdir(dd))
        {
            const std::string n = e->d_name;
            if (n.size() > 4 && n.compare(n.size() - 4, 4, ".plg") == 0) files.push_back(n);
        }
        closedir(dd);
    }
    std::sort(files.begin(), files.end());
    if (files.empty()) return printf("FAIL: no .plg in %s\n", argv[2]), 1;

    const uint32_t t = ucb::kNvm;   // bank offset 0
    int agree = 0, differ = 0, engines = 0, shippedOk = 0, total = 0;
    std::map<std::string, int> codes;   // image code (= port code) -> variants
    std::set<std::string> chordSeen;
    for (const std::string& f : files)
    {
        const Engine e = readEngine(std::string(argv[2]) + "/" + f, t, pool);
        if (!e.err.empty())
        {
            printf("%-11s FAIL read: %s\n", f.c_str(), e.err.c_str());
            differ++;
            continue;
        }
        const uint32_t rwLen = e.img - e.text + e.bss;
        if (rwLen > poolN)
        {
            printf("%-11s FAIL rw %u B > s_pool %u B\n", f.c_str(), rwLen, poolN);
            differ++;
            continue;
        }
        engines++;
        char libName[9];
        snprintf(libName, sizeof libName, "%s", e.name);   // synthBoot: snprintf(l.name, sizeof(l.name), "%s", x.name)

        // the flash address of a guest address start() reads: text as is, rw through its initial bytes after the text
        auto flashOf = [&](uint32_t g) -> uint32_t {
            if (g - t < e.text) return g;
            if (g - pool < e.img - e.text) return t + e.text + (g - pool);
            return 0;
        };
        auto place = [&](const std::vector<Poke>& pokes) -> bool {
            uint8_t* b = ucb::g2h(ucb::kNvm, ucb::kNvmSize);
            memset(b, 0xFF, ucb::kNvmSize);
            memcpy(b, e.image.data(), e.image.size());
            g_late.clear();
            for (const Poke& p : pokes)
            {
                const uint32_t fa = flashOf(p.guest);
                if (fa && flashOf(p.guest + uint32_t(p.bytes.size()) - 1))
                    memcpy(ucb::g2h(fa, uint32_t(p.bytes.size())), p.bytes.data(), p.bytes.size());
                else if (p.guest - pool < rwLen && p.guest + uint32_t(p.bytes.size()) - pool <= rwLen)
                    g_late.push_back(p);   // the bss: init builds it, the poke lands after the query
                else
                    return false;
            }
            ucb::codeChanged();
            return true;
        };
        auto nameOf = [&](const Variant& v, char (&nm)[9]) {
            memset(nm, 0, sizeof nm);
            snprintf(nm, sizeof nm, "%s", v.libName.empty() ? libName : v.libName.c_str());
        };
        auto runPort = [&](const Variant& v) {
            Result r;
            char nm[9];
            nameOf(v, nm);
            const ucbs::StartIn in{t, e.text, e.img, e.entry, pool, rwLen, e.stored + v.dStored, e.patch + v.dPatch, e.type + v.dType, nm, 9};
            ucbs::StartOut o;
            r.code = ucbs::start(in, o);
            r.fault = o.fault;
            r.ext = r.code == 0 ? o.ext : 0;
            if (o.chordSet) r.chord = uint8_t(o.chord), r.mode = uint8_t(o.mode), r.strum = uint8_t(o.strum);
            freeAllocs();
            return r;
        };
        // the image's tables for library entry 0 and bank entry 0, then its own start(0, &out)
        auto runImage = [&](const Variant& v) {
            char nm[9];
            nameOf(v, nm);
            plug::Bank bk{};
            bk.n = 1;
            plug::BankEntry& be = bk.e[0];
            be.uid = e.uid;
            be.version = e.version;
            be.fileCrc = e.crc;
            be.off = 0;
            be.len = e.img;
            be.text = e.text;
            be.rwRun = pool;
            be.rwLen = rwLen;
            be.entry = e.entry;
            be.state = 'o';
            snprintf(be.file, sizeof be.file, "%s", f.c_str());
            memcpy(ucb::g2h(bankG, sizeof bk), &bk, sizeof bk);
            plug::IndexEntry ix{};
            ix.uid = e.uid;
            ix.version = e.version;
            ix.crc = e.crc;
            ix.entry = e.entry;
            ix.imageSize = e.img;
            ix.rwOffset = e.text;
            ix.bssSize = e.bss;
            ix.synthStored = e.stored + v.dStored;
            ix.synthPatch = e.patch + v.dPatch;
            ix.synthType = e.type + v.dType;
            memcpy(ix.name, e.name, sizeof ix.name);
            memcpy(ucb::g2h(sidxG, sizeof ix), &ix, sizeof ix);
            uint8_t lib[kLibSize] = {};
            memcpy(lib + kLibName, nm, 9);
            lib[kLibChord] = lib[kLibMode] = lib[kLibStrum] = kSentinel;
            memcpy(ucb::g2h(libG, kLibSize), lib, kLibSize);
            memset(ucb::g2h(outG, 4), 0, 4);
            Result r;
            const uint64_t f0 = ucb::faults();
            r.code = int(int32_t(ucb::call(startFn, 0, outG)));
            r.fault = ucb::faults() != f0;
            r.ext = r.code == 0 ? g32(outG) : 0;
            r.chord = g8(libG + kLibChord);
            r.mode = g8(libG + kLibMode);
            r.strum = g8(libG + kLibStrum);
            freeAllocs();
            return r;
        };

        // the guest structures of the shipped engine, for the variants
        place({});
        const Variant shipped{"shipped"};
        const Result base = runPort(shipped);
        const uint32_t gd = ucb::entry((t + e.entry) | 1u);
        const uint32_t gq = gd ? g32(gd + 44) : 0;
        const uint32_t gx = base.ext;
        const uint32_t gc = gx && gq ? ucb::call(gq, PLUG_EXT_CHORD) : 0;
        freeAllocs();
        const uint32_t np = gx ? g32(gx + 28) : 0, gp = gx ? g32(gx + 24) : 0, gname = gx ? g32(gx + 20) : 0;
        const uint32_t ns = gx ? g32(gx + 36) : 0, gs = gx ? g32(gx + 32) : 0, nm = gx ? g32(gx + 44) : 0, gm = gx ? g32(gx + 40) : 0;
        auto par = [&](uint32_t i) { return gp + 24 * i; };
        int enumP = -1, numP = -1;
        for (uint32_t i = 0; i < np; i++)
        {
            if (enumP < 0 && g8(par(i) + 16) == PLUG_FMT_ENUM) enumP = int(i);
            if (numP < 0 && g8(par(i) + 16) != PLUG_FMT_ENUM) numP = int(i);
        }
        if (getenv("VERBOSE"))
            printf("%s: text 0x%08x+%u rw 0x%08x+%u (init %u) desc 0x%08x ext 0x%08x chord 0x%08x params 0x%08x\n", f.c_str(), t, e.text, pool, rwLen,
                   e.img - e.text, gd, gx, gc, gp);
        const uint16_t movsR0_0 = 0x2000, movsR0_1 = 0x2001, bxLr = 0x4770;
        const uint32_t pastText = t + e.text + 16;

        std::vector<Variant> vs;
        vs.push_back(shipped);
        // ---- corrupt: fw_engine_mismatch_sim's payload_flip (file byte 200, the CRC as shipped) and more single byte flips
        auto flip = [&](const char* n, uint32_t imgOff) {
            if (imgOff >= e.image.size()) return;
            const uint32_t g = imgOff < e.text ? t + imgOff : pool + imgOff - e.text;
            vs.push_back(Variant{n, {pk8(g, uint8_t(e.image[imgOff] ^ 0xFF))}});
        };
        if (e.hs < 200) flip("payload_flip", 200 - e.hs);
        flip("flip_entry", e.entry & ~1u);
        flip("flip_mid_text", (e.text / 2) & ~3u);
        flip("flip_last_text", e.text - 4);
        if (e.img > e.text) flip("flip_rw_first", e.text);
        if (gd)
        {
            vs.push_back(Variant{"desc_flip_kind", {pk8(gd + 4, uint8_t(g8(gd + 4) ^ 0xFF))}});
            vs.push_back(Variant{"desc_flip_query", {pk32(gd + 44, g32(gd + 44) ^ 0x00010000u)}});
        }
        // ---- ABI mismatch, descriptor level
        vs.push_back(Variant{"entry_refuses_abi", {pkCode((t + e.entry) | 1u, movsR0_0, bxLr)}});
        if (gd)
        {
            vs.push_back(Variant{"kind_master", {pk32(gd + 4, PLUG_KIND_MASTER_INSERT)}});
            vs.push_back(Variant{"init_fails", {pkCode(g32(gd + 28), movsR0_1, bxLr)}});
            vs.push_back(Variant{"query_none", {pkCode(gq, movsR0_0, bxLr)}});
        }
        // ---- the extension (checkSynth)
        if (gx)
        {
            auto X = [&](const char* n, uint32_t off, uint32_t v) { vs.push_back(Variant{n, {pk32(gx + off, v)}}); };
            const uint32_t ss = g32(gx);
            X("ext_size_96_abi10", 0, 96);
            X("ext_size_100_abi11", 0, 100);
            X("ext_size_104", 0, 104);
            X("layout_host_voice160", 4, ucb::hostSynthLayout());
            X("layout_params_minus4", 4, ucb::deviceSynthLayout() - (4u << 8));
            X("name_null", 20, 0);
            X("name_plus1", 20, gname + 1);
            if (gname) vs.push_back(Variant{"name_empty", {pk8(gname, 0)}});
            if (gname) vs.push_back(Variant{"name_slash", {pk8(gname + 1, '/')}});
            X("params_count_85", 28, 85);
            X("params_count_84", 28, 84);
            X("params_count_0", 28, 0);
            X("params_outside", 24, pool);
            X("params_last_outside", 28, (pastText - gp) / 24 + 1);
            X("sections_11", 36, 11);
            X("sections_outside", 32, pool);
            X("macros_7", 44, 7);
            X("macros_outside", 40, pastText);
            X("voices_0", 52, 0);
            X("voices_1", 52, 1);
            X("voices_5", 52, 5);
            X("volume_np", 48, np);
            X("volume_m2", 48, uint32_t(-2));
            X("volume_m1", 48, uint32_t(-1));
            X("note_on_arm", 80, g32(gx + 80) & ~1u);
            X("note_off_null", 84, 0);
            X("render_outside", 88, pastText | 1u);
            X("prepare_null", 68, 0);
            X("prepare_outside", 68, (t - 4) | 1u);
            X("slot_reset_arm", 72, t + 4);
            X("set_tempo_outside", 76, pool | 1u);
            X("slot_post_outside", 92, pastText | 1u);
            X("send_tap_arm", 96, t + 8);
            X("set_work_outside", 64, pastText | 1u);
            vs.push_back(Variant{"service_bad", {pk32(gx, std::max(ss, 108u)), pk32(gx + 100, pastText | 1u)}});
            vs.push_back(Variant{"service_bad_abi11", {pk32(gx, 100), pk32(gx + 100, pastText | 1u)}});
            vs.push_back(Variant{"view_bad", {pk32(gx, std::max(ss, 108u)), pk32(gx + 104, 2u)}});
            vs.push_back(Variant{"view_bad_size104", {pk32(gx, 104), pk32(gx + 104, 2u)}});
            if (np)
            {
                vs.push_back(Variant{"param0_name_null", {pk32(par(0), 0)}});
                vs.push_back(Variant{"param0_fmt_16", {pk8(par(0) + 16, PLUG_FMT_COUNT)}});
                vs.push_back(Variant{"param0_curve_4", {pk8(par(0) + 17, PLUG_CURVE_COUNT)}});
                vs.push_back(Variant{"param_last_max_lt_min", {pkf(par(np - 1) + 8, 0.0f), pkf(par(np - 1) + 12, -1e9f)}});
                vs.push_back(Variant{"param0_max_nan", {pkf(par(0) + 12, nanf(""))}});
                if (numP >= 0) vs.push_back(Variant{"num_to_enum", {pk8(par(uint32_t(numP)) + 16, PLUG_FMT_ENUM)}});
                if (enumP >= 0)
                {
                    const uint32_t ep = par(uint32_t(enumP));
                    vs.push_back(Variant{"enum_max_128", {pkf(ep + 12, 128.0f)}});
                    vs.push_back(Variant{"enum_max_127", {pkf(ep + 12, 127.0f)}});
                    vs.push_back(Variant{"enum_max_plus1", {pkf(ep + 12, gf(ep + 12) + 1.0f)}});
                    vs.push_back(Variant{"enum_min_1", {pkf(ep + 8, 1.0f)}});
                    vs.push_back(Variant{"enum_names_null", {pk32(ep + 20, 0)}});
                    vs.push_back(Variant{"enum_name0_null", {pk32(g32(ep + 20), 0)}});
                }
            }
            if (ns)
            {
                vs.push_back(Variant{"section0_id_np", {pk8(g32(gs + 8), uint8_t(np))}});
                vs.push_back(Variant{"section0_count_0", {pk8(gs + 12, 0)}});
                vs.push_back(Variant{"section0_ids_null", {pk32(gs + 8, 0)}});
                vs.push_back(Variant{"section0_short_null", {pk32(gs + 4, 0)}});
            }
            if (nm)
            {
                vs.push_back(Variant{"macro0_count_3", {pk8(gm + 5, 3)}});
                vs.push_back(Variant{"macro0_target_np", {pk8(gm + 5, 2), pk8(gm + 16, uint8_t(np))}});
                vs.push_back(Variant{"macro0_name_null", {pk32(gm, 0)}});
            }
            // ---- the manifest against the extension (code 8)
            Variant v{"manifest_stored"};
            v.dStored = 1;
            vs.push_back(v);
            v = Variant{"manifest_patch"};
            v.dPatch = 1;
            vs.push_back(v);
            v = Variant{"manifest_type"};
            v.dType = -1;
            vs.push_back(v);
            v = Variant{"manifest_name"};
            v.libName = std::string(libName).substr(0, 7) + "~";
            vs.push_back(v);
        }
        // ---- PLUG_EXT_CHORD (checkChord, chordStrum)
        if (gx && gc)
        {
            const uint32_t pi = g32(gc + 4), mp = g32(gc + 8), sp = g32(gc + 12);
            auto Cv = [&](const char* n, std::vector<Poke> p) { vs.push_back(Variant{n, p}); };
            Cv("chord_size_8", {pk32(gc, 8)});
            Cv("chord_size_12_no_strum", {pk32(gc, 12)});
            Cv("chord_same_params", {pk32(gc + 8, pi)});
            Cv("chord_index_np", {pk32(gc + 4, np)});
            Cv("chord_mode_np", {pk32(gc + 8, np)});
            Cv("chord_strum_none", {pk32(gc + 12, PLUG_CHORD_NO_STRUM)});
            Cv("chord_strum_is_mode", {pk32(gc + 12, mp)});
            if (pi < np) Cv("chord_param_max_66", {pkf(par(pi) + 12, 66.0f)});
            if (pi < np) Cv("chord_param_int", {pk8(par(pi) + 16, PLUG_FMT_INT)});
            if (pi < np) Cv("chord_param_pct", {pk8(par(pi) + 16, PLUG_FMT_PCT)});
            if (mp < np) Cv("chord_mode_max_0", {pkf(par(mp) + 12, 0.0f)});
            if (mp < np) Cv("chord_mode_onoff", {pk8(par(mp) + 16, PLUG_FMT_ON_OFF)});
            if (sp < np) Cv("strum_min_m62", {pkf(par(sp) + 8, -62.0f)});
            if (sp < np) Cv("strum_enum", {pk8(par(sp) + 16, PLUG_FMT_ENUM)});
        }

        int eAgree = 0, eDiffer = 0, eSkip = 0;
        std::string lines;
        for (const Variant& v : vs)
        {
            if (!place(v.pokes))
            {
                eSkip++;
                continue;
            }
            const Result p = runPort(v);
            place(v.pokes);
            const Result im = runImage(v);
            const bool same = p.fault == im.fault &&
                              (p.fault || (p.code == im.code && p.ext == im.ext && p.chord == im.chord && p.mode == im.mode && p.strum == im.strum));
            char line[256];
            snprintf(line, sizeof line, "    %-24s image %s%d chord %-9s port %s%d chord %-9s %s\n", v.name.c_str(), im.fault ? "FAULT " : "",
                     im.code, chordTxt(im).c_str(), p.fault ? "FAULT " : "", p.code, chordTxt(p).c_str(), same ? "same" : "DIFFERS");
            if (getenv("VERBOSE") || !same) lines += line;
            total++;
            if (same)
            {
                eAgree++;
                codes[im.fault ? std::string("fault") : std::to_string(im.code)]++;
                if (!im.fault && im.chord != kSentinel) chordSeen.insert(int8_t(im.chord) >= 0 ? "chord" : "no chord");
                if (v.name == "shipped" && !im.fault && im.code == 0) shippedOk++;
            }
            else
                eDiffer++;
        }
        printf("%-11s %-8s %3zu variants: %d same, %d differ, %d not placeable (extension %s, chord %s, %u params)\n", f.c_str(), libName,
               vs.size(), eAgree, eDiffer, eSkip, gx ? "yes" : "NO", gc ? "yes" : "no", np);
        printf("%s", lines.c_str());
        agree += eAgree;
        differ += eDiffer;
    }
    printf("codes (image = port):");
    for (const auto& kv : codes) printf(" %s:%d", kv.first.c_str(), kv.second);
    printf("  chord outcomes:");
    for (const std::string& s : chordSeen) printf(" [%s]", s.c_str());
    printf("\n");
    bool cover = shippedOk == engines;
    for (const char* k : {"0", "1", "2", "3", "4", "8"}) cover = cover && codes.count(k);
    cover = cover && chordSeen.size() == 2;
    printf("%d engines, %d of them start shipped (code 0 on both), %d of %d variants agree, %d differ, coverage %s, guest faults %llu\n", engines,
           shippedOk, agree, total, differ, cover ? "ok" : "MISSING", (unsigned long long)ucb::faults());
    const bool green = differ == 0 && cover && engines > 0;
    printf("RESULT: %s\n", green ? "GREEN" : "RED");
    return green ? 0 : 1;
}
