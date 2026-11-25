// ui_shot BRIDGE=1 only (emu parity plan T7, ref/emu-parity-plan.md): the plugrender verb of the unit console (poly-devtools
// firmware/core/UsbTestCore.cpp cmdPlugRender, UsbTest.cpp renderOpen / renderBegin / renderBlock) on the emu. The engine is the card's
// REAL .plg installed and attached through the firmware's own path (plug_synth.cpp synthEnsure, loadEngine, start() on the bridge) and
// driven through its ISynthEngine (plug_synth.cpp's PlugSynth, the bridge's mirror of the guest PLUG_EXT_SYNTH), with the statements of
// the unit's renderBegin and renderBlock, which are those of poly-engines tools/render_harness.cpp. Never part of the image.
//
//   plugrender NAME TRANSCRIPT OUT [install]
//
// NAME is the engine's name in the model list (PLK, ALCO, CHIP ...), `install` installs it first (synthEnsure, as the console does).
// TRANSCRIPT is the transcript format of bridge_check.py (once the retired twinrender's): strum, files + nfiles + f, refs + r, bad + b, service, run. Unlike the unit
// (which answers PLUG_EXT_FILES with the card's own wave list and refuses strum -1, files lists and service) every line is honoured here,
// so bridge_check.py's whole ALCO transcript set runs: while the verb runs, query(PLUG_EXT_FILES) and query(PLUG_EXT_STRUM) are
// answered from the transcript (files 0 and strum -1: NULL), any other id by the firmware as before.
// Before the first line the engine's work block is zeroed and handed over again (plug_synth.cpp giveWork), as the unit console does.
// OUT gets every run's float32 blocks (128 L then 128 R per block), OUT.pb the unit's reply lines ("pb RUN BLOCK FNV", "pr RUN blocks N
// fnv H", "ok plugrender runs= blocks= fnv="), OUT.ext the descriptor numbers and parameter table (the .ext format bridge_check.py reads).
// stdout: the ok line and the bridge's cost per block (wall time of a block, guest calls per block).
#pragma clang fp contract(off)

#include "host_ucbridge.h"

#include "audio/synth_engine.h"
#include "mtStructs.h"
#include "open_engine.h"
#include "open_load.h"
#include "plug_host.h"

#include <chrono>
#include <math.h>
#include <fstream>
#include <map>
#include <set>
#include <sstream>
#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>

#if defined(FEATURE_PLUG_HOST) && defined(FEATURE_SYNTH_OPEN)   // the engine plugin host (plug_synth.cpp), not on the IMX sims
namespace plug
{
const plug_ext_synth* uishotSynthExt(int typeId);   // plug_synth.cpp, the BRIDGE=1 overlay (gen_overlay.py BRIDGE_RENDER_HELPERS)
int uishotSynthWorkReset(int typeId);
}   // namespace plug

namespace
{
using namespace ot::audio;

// the Cfg of tools/render_harness.cpp and of the console's RenderCfg, byte for byte
struct Cfg
{
    uint32_t voices, offBlock, nover;
    uint32_t note[4];
    uint32_t idx[64];
    float val[64];
    float vel[4];
    uint32_t rampBlock, nramp;
    uint32_t ridx[16];
    float rval[16];
    uint32_t glideBlocks, glideIdx;
    float glideFrom, glideTo;
    uint32_t slot;
};
static_assert(sizeof(Cfg) == 712, "the Cfg block of tools/render_harness.cpp");

// what the transcript says the host answers PLUG_EXT_FILES and PLUG_EXT_STRUM with
struct HostState
{
    int strumPct = -1;
    int tail = 0;
    std::vector<std::string> names;
    std::vector<std::vector<int16_t>> waves;
    std::map<std::pair<uint32_t, uint32_t>, std::string> refs;
    std::set<std::string> bad;
};
HostState g_h;
ucb::Services g_fw;   // the firmware's services (restored at the end)

int32_t frames(const std::vector<int16_t>& w, uint32_t first, int16_t* dst, uint32_t mx)
{
    if (first < w.size() && mx)
    {
        const size_t n = w.size() - first < mx ? w.size() - first : mx;
        memcpy(dst, w.data() + first, n * 2);
    }
    return (w.size() > 0 && w.size() <= 2048) ? int32_t(w.size()) : -3;
}
int32_t fCount() { return int32_t(g_h.waves.size()); }
int32_t fName(uint32_t i, char* name, uint32_t cap)
{
    if (i >= g_h.names.size()) return -1;
    const std::string& n = g_h.names[i];
    if (n.size() > 0 && cap) { memset(name, 0, cap); memcpy(name, n.data(), n.size() < cap ? n.size() : cap); }
    return int32_t(n.size());
}
int32_t fRead(uint32_t i, uint32_t first, int16_t* dst, uint32_t mx) { return i >= g_h.waves.size() ? -1 : frames(g_h.waves[i], first, dst, mx); }
int32_t fRef(uint32_t part, uint32_t param, char* path, uint32_t cap)
{
    auto it = g_h.refs.find({part, param});
    if (it == g_h.refs.end()) return -1;
    if (cap) { memset(path, 0, cap); memcpy(path, it->second.data(), it->second.size() < cap - 1 ? it->second.size() : cap - 1); }
    return int32_t(it->second.size());
}
int32_t fReadPath(const char* path, uint32_t first, int16_t* dst, uint32_t mx)
{
    for (size_t i = 0; i < g_h.names.size(); i++)
        if (g_h.names[i] == path) return g_h.bad.count(path) ? -2 : frames(g_h.waves[i], first, dst, mx);
    return -1;
}
int32_t fNext(const char* path, char* out, uint32_t cap)
{
    for (const std::string& n : g_h.names)
        if (n > path)
        {
            if (cap) { memset(out, 0, cap); memcpy(out, n.data(), n.size() < cap ? n.size() : cap); }
            return int32_t(n.size());
        }
    return -1;
}
plug_ext_files g_files;

const void* tQuery(uint32_t id)
{
    if (id == PLUG_EXT_FILES)
    {
        if (!g_h.tail) return nullptr;
        g_files = {};
        // the bridge gives the guest the image's kFiles (tail 6) or a short guest copy (struct_size below the host's: tail 3)
        g_files.struct_size = g_h.tail == 6 ? uint32_t(sizeof(plug_ext_files)) : uint32_t(offsetof(plug_ext_files, wave_ref));
        g_files.count = fCount;
        g_files.name = fName;
        g_files.read_wave = fRead;
        if (g_h.tail == 6) { g_files.wave_ref = fRef; g_files.read_path = fReadPath; g_files.next_path = fNext; }
        return &g_files;
    }
    if (id == PLUG_EXT_STRUM && g_h.strumPct < 0) return nullptr;
    return g_fw.query ? g_fw.query(id) : nullptr;   // strum: the firmware's strumramp::g_depth (setStrumRampDepth below)
}

std::vector<uint8_t> unhex(const std::string& s)
{
    std::vector<uint8_t> o(s.size() / 2);
    for (size_t i = 0; i < o.size(); i++) o[i] = uint8_t(strtoul(s.substr(2 * i, 2).c_str(), nullptr, 16));
    return o;
}

uint32_t fnv1a(uint32_t h, const void* p, size_t n)
{
    const uint8_t* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 16777619u;
    return h;
}
constexpr uint32_t kFnvBasis = 2166136261u;

// the console's RenderRun
struct RenderRun
{
    SynthParams p;
    SynthSlotScratch sc;
    SynthVoice v[4];
    float gain[4];
    bool on[4];
    uint32_t b;
    Cfg* cfg;
};

// UsbTest.cpp renderBegin
void renderBegin(const polysynth::open::EngineDef* d, Cfg* cfg, RenderRun& r)
{
    memset(static_cast<void*>(&r), 0, sizeof(r));
    const int count = d->paramCount < kSynthParamMax ? d->paramCount : kSynthParamMax;
    cfg->voices = cfg->voices < 4 ? cfg->voices : 4;
    cfg->nramp = cfg->nramp < 16 ? cfg->nramp : 16;
    cfg->nover = cfg->nover < 64 ? cfg->nover : 64;
    for (uint32_t k = 0; k < cfg->nover; k++) cfg->idx[k] = cfg->idx[k] < uint32_t(count) ? cfg->idx[k] : 0;
    for (uint32_t k = 0; k < cfg->nramp; k++) cfg->ridx[k] = cfg->ridx[k] < uint32_t(count) ? cfg->ridx[k] : 0;
    cfg->glideIdx = cfg->glideIdx < uint32_t(count) ? cfg->glideIdx : 0;
    r.cfg = cfg;
    r.p.engine = d->typeId;
    r.p.count = count;
    for (int i = 0; i < count; i++) r.p.start[i] = r.p.end[i] = d->params[i].def;
    for (uint32_t k = 0; k < cfg->nover; k++) r.p.start[cfg->idx[k]] = r.p.end[cfg->idx[k]] = cfg->val[k];
    d->inst->slotReset(r.sc);
    d->inst->setTempo(120.0f);
}

// UsbTest.cpp renderBlock (no audio interrupt to hold off here: ui_shot runs the verb with the audio off)
void renderBlock(const polysynth::open::EngineDef* d, RenderRun& r, float* out)
{
    const Cfg& cfg = *r.cfg;
    ISynthEngine& e = *d->inst;
    SynthParams& p = r.p;
    const uint32_t b = r.b++;
    p.moving = false;
    p.serial++;
    if (cfg.rampBlock != 0 && b == cfg.rampBlock)
    {
        for (uint32_t k = 0; k < cfg.nramp; k++) p.end[cfg.ridx[k]] = cfg.rval[k];
        p.moving = cfg.nramp > 0;
    }
    if (cfg.glideBlocks != 0)
    {
        const float n = float(cfg.glideBlocks);
        float a = float(b) / n;
        float c = float(b + 1) / n;
        a = a > 1.0f ? 1.0f : a;
        c = c > 1.0f ? 1.0f : c;
        // fused, as both ARM builds compile these two lines: tools/render_harness.cpp (build_plug.py -O2, GCC contracts) and the console's
        // renderBlock (the image's gnu++17 flags, -ffp-contract=fast by default): vfma.f32, one rounding. This file has contraction off
        p.start[cfg.glideIdx] = fmaf(cfg.glideTo - cfg.glideFrom, a, cfg.glideFrom);
        p.end[cfg.glideIdx] = fmaf(cfg.glideTo - cfg.glideFrom, c, cfg.glideFrom);
        p.moving = p.start[cfg.glideIdx] != p.end[cfg.glideIdx];
    }
    for (uint32_t k = 0; k < cfg.voices; k++)
    {
        SynthVoice& v = r.v[k];
        if (b == 0)
        {
            SynthNote n;
            n.note = uint8_t(cfg.note[k]);
            n.pitch = float(cfg.note[k]);
            n.velocity = d->volumeParam < 0 ? 100.0f / 127.0f : 1.0f;
            v = SynthVoice{};
            memset(v.state, 0, sizeof(v.state));
            v.slotScratch = &r.sc;
            v.slot = uint8_t(cfg.slot);
            v.note = n.note;
            v.gate = true;
            r.on[k] = true;
            r.gain[k] = 0.0f;
            e.noteOn(v, p, n);
        }
        else if (b == cfg.offBlock && r.on[k])
        {
            v.gate = false;
            e.noteOff(v, p);
        }
    }
    float* L = out;
    float* R = out + kBlock;
    memset(L, 0, 2 * kBlock * sizeof(float));
    e.prepare(r.sc, p);
    for (uint32_t k = 0; k < cfg.voices; k++)
    {
        if (!r.on[k]) continue;
        const float vel = cfg.vel[k] > 0.0f ? cfg.vel[k] : 1.0f;
        const float target = d->volumeParam >= 0 ? p.end[d->volumeParam] * vel : 1.0f;
        const float g0 = r.gain[k];
        r.gain[k] = target;
        if (!e.render(r.v[k], p, g0, (target - g0) * (1.0f / float(kBlock)), L, R)) r.on[k] = false;
    }
    memcpy(p.start, p.end, sizeof(p.start));
}

const polysynth::open::EngineDef* findEngine(const std::string& name)
{
    using namespace polysynth::open;
    for (int i = 0; i < engineCount(); i++)
    {
        const EngineDef* c = engineAt(i).def;
        if (c && c->name && name == c->name) return c;
    }
    return nullptr;
}

void writeExt(const plug_ext_synth* x, const std::string& path)
{
    FILE* d = fopen(path.c_str(), "w");
    if (!d) return;
    fprintf(d, "ext %u %d %d %d %s %u %u %u %d %u %u %u\n", x->layout, x->stored, x->patch_engine, x->type_id, x->name, x->param_count,
            x->section_count, x->macro_count, x->volume_param, x->max_voices, x->flags, x->work_bytes);
    for (uint32_t i = 0; i < x->param_count; i++)
    {
        const plug_synth_param& q = x->params[i];
        uint32_t df, mn, mx;
        memcpy(&df, &q.def, 4);
        memcpy(&mn, &q.min, 4);
        memcpy(&mx, &q.max, 4);
        fprintf(d, "p %s %08x %08x %08x %u %u\n", q.name, df, mn, mx, q.fmt, q.curve);
    }
    fclose(d);
}

int plugRender(const std::string& name, const std::string& transcript, const std::string& outPath, bool install)
{
    using namespace polysynth::open;
    const EngineDef* d = findEngine(name);
    if (!d || !plug::synthIsPlugin(d->typeId))
    {
        fprintf(stderr, "plugrender: no plugin engine named %s, the list:", name.c_str());
        for (int i = 0; i < engineCount(); i++)
            if (engineAt(i).def && engineAt(i).def->name) fprintf(stderr, " %s", engineAt(i).def->name);
        fprintf(stderr, "\n");
        return 1;
    }
    for (int p = 0; p < kSynthInstrumentsCount; p++)
        if (partSounding(p) != 0) return fprintf(stderr, "plugrender: a part sounds\n"), 1;
    if (install)
    {
        int keep[kSynthInstrumentsCount];
        for (int p = 0; p < kSynthInstrumentsCount; p++) keep[p] = typeForStored(mtProject.values.synthPart[p].synthType);
        if (!plug::synthEnsure(&d->typeId, 1, keep, kSynthInstrumentsCount)) return fprintf(stderr, "plugrender: install failed\n"), 1;
    }
    if (!plug::synthLoaded(d->typeId) || !d->inst) return fprintf(stderr, "plugrender: %s not installed (add install)\n", name.c_str()), 1;
    const plug_ext_synth* x = plug::uishotSynthExt(d->typeId);
    if (!x) return fprintf(stderr, "plugrender: no extension of %s\n", name.c_str()), 1;
    const int wr = plug::uishotSynthWorkReset(d->typeId);   // the unit console's work reset
    if (wr < 0) return fprintf(stderr, "plugrender: the work block of %s cannot be reset\n", name.c_str()), 1;
    writeExt(x, outPath + ".ext");
    std::ifstream in(transcript);
    if (!in) return fprintf(stderr, "plugrender: cannot read %s\n", transcript.c_str()), 1;
    FILE* out = fopen(outPath.c_str(), "wb");
    FILE* pb = fopen((outPath + ".pb").c_str(), "w");
    if (!out || !pb) return fprintf(stderr, "plugrender: cannot write %s\n", outPath.c_str()), 1;
    g_h = HostState{};
    g_fw = ucb::services();
    ucb::setServices({g_fw.alloc, g_fw.cycles, tQuery});
    const int strumWas = plug::strumRampDepth();
    const uint64_t faults0 = ucb::faults();
    int runs = 0, rc = 0;
    uint64_t blocks = 0, calls = 0;
    double usSum = 0, usMax = 0;
    uint32_t all = kFnvBasis;
    static RenderRun r;
    static Cfg cfg;
    std::vector<float> blk(2 * kBlock);
    std::string line;
    auto lines = [&](int n, auto fn) { for (int i = 0; i < n && std::getline(in, line); i++) { std::istringstream ls(line); std::string t; ls >> t; fn(ls); } };
    while (rc == 0 && std::getline(in, line))
    {
        std::istringstream ls(line);
        std::string c;
        ls >> c;
        if (c == "strum")
        {
            ls >> g_h.strumPct;
            if (g_h.strumPct >= 0) plug::setStrumRampDepth(g_h.strumPct);
        }
        else if (c == "files") ls >> g_h.tail;
        else if (c == "nfiles")
        {
            int n;
            ls >> n;
            g_h.names.clear();
            g_h.waves.clear();
            lines(n, [&](std::istringstream& l) {
                std::string nm, hex;
                l >> nm >> hex;
                if (nm == "-") nm.clear();
                g_h.names.push_back(nm);
                const std::vector<uint8_t> b = hex == "-" ? std::vector<uint8_t>() : unhex(hex);
                std::vector<int16_t> w(b.size() / 2);
                if (!b.empty()) memcpy(w.data(), b.data(), b.size());
                g_h.waves.push_back(w);
            });
        }
        else if (c == "refs")
        {
            int n;
            ls >> n;
            g_h.refs.clear();
            lines(n, [&](std::istringstream& l) { uint32_t p, q; std::string path; l >> p >> q >> path; g_h.refs[{p, q}] = path; });
        }
        else if (c == "bad")
        {
            int n;
            ls >> n;
            g_h.bad.clear();
            lines(n, [&](std::istringstream& l) { std::string p; l >> p; g_h.bad.insert(p); });
        }
        else if (c == "service")
        {
            uint32_t part, times;
            std::string hex;
            ls >> part >> times >> hex;
            const std::vector<uint8_t> b = unhex(hex);
            std::vector<float> v(b.size() / 4);
            memcpy(v.data(), b.data(), b.size());
            if (x->service)
                for (uint32_t i = 0; i < times; i++) x->service(part, v.data(), uint32_t(v.size()));
        }
        else if (c == "run")
        {
            uint32_t n;
            std::string hex;
            ls >> n >> hex;
            const std::vector<uint8_t> b = unhex(hex);
            if (b.size() != sizeof(Cfg)) { fprintf(stderr, "plugrender: cfg is %zu bytes, expected %zu\n", b.size(), sizeof(Cfg)); rc = 1; break; }
            memcpy(&cfg, b.data(), sizeof(cfg));
            renderBegin(d, &cfg, r);
            uint32_t runFnv = kFnvBasis;
            for (uint32_t k = 0; k < n; k++)
            {
                const uint64_t c0 = ucb::calls();
                const auto t0 = std::chrono::steady_clock::now();
                renderBlock(d, r, blk.data());
                const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
                calls += ucb::calls() - c0;
                usSum += us;
                usMax = us > usMax ? us : usMax;
                const uint32_t h = fnv1a(kFnvBasis, blk.data(), blk.size() * 4);
                runFnv = fnv1a(runFnv, blk.data(), blk.size() * 4);
                all = fnv1a(all, blk.data(), blk.size() * 4);
                fprintf(pb, "pb %d %u %08x\n", runs, k, h);
                fwrite(blk.data(), 4, blk.size(), out);
            }
            fprintf(pb, "pr %d blocks %u fnv %08x\n", runs, n, runFnv);
            blocks += n;
            runs++;
        }
        else if (!c.empty()) { fprintf(stderr, "plugrender: unknown line '%s'\n", line.c_str()); rc = 1; }
    }
    ucb::setServices(g_fw);
    plug::setStrumRampDepth(strumWas);
    const uint64_t faults = ucb::faults() - faults0;
    fprintf(pb, "ok plugrender runs=%d blocks=%llu fnv=%08x\n", runs, (unsigned long long)blocks, all);
    fclose(pb);
    fclose(out);
    printf("plugrender %s: runs=%d blocks=%llu fnv=%08x work %s, faults %llu, bridge cost %.1f us per block (max %.1f), %.1f guest calls per block\n",
           name.c_str(), runs, (unsigned long long)blocks, all, wr ? "reset" : "none", (unsigned long long)faults, blocks ? usSum / blocks : 0.0,
           usMax, blocks ? double(calls) / blocks : 0.0);
    return rc == 0 && faults == 0 ? 0 : 1;
}
}   // namespace
#endif

bool hostPlugRenderCommand(const std::string& c, std::istringstream& is)
{
    if (c != "plugrender") return false;
#if !defined(FEATURE_PLUG_HOST) || !defined(FEATURE_SYNTH_OPEN)
    (void)is;
    printf("plugrender: no engine plugin host in this build (plus sim)\n");
    return false;
#else
    std::string name, tr, out, opt;
    is >> name >> tr >> out >> opt;
    return plugRender(name, tr, out, opt == "install") == 0;
#endif
}
