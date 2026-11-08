// Virtual ARM stack meter for ui_shot (make STACKMODEL=1). Host only, NOT firmware, not linked unless STACKMODEL=1.
//
// The Mac build has its own frame sizes (arm64, clang, -O1), so the host stack says nothing about the MK66 stack, where main loop and every
// interrupt share one 8,424 B region and an overrun lands in the live heap (ref/stack-margin-1.2.0.md section 2). This file keeps a second,
// virtual stack: every function that runs adds the frame size the ARM image has for it (frame table from `stack_depth.py --dump-frames`,
// UISHOT_STACK_FRAMES=FILE, lines "ns::fn<TAB>bytes"), every return takes it off. The result is the stack depth the real firmware would have
// at this point of the real UI code path, measured while a scenario drives ui_shot --serve. Functions that are not in the table
// (inlined on ARM, host stand-ins) add nothing.
//
// ISR what-if: UISHOT_STACK_ISR="NAME=bytes,NAME=bytes" gives the depth of the audio out ISR chain for engines (stack_report.sh /
// isr_per_engine.sh). At every function entry the meter asks what an audio interrupt arriving right now would do: depth + 108 (exception
// frame) + ISR chain (+ UISHOT_STACK_NEST, default 546 B: SysTick, audio in and the MIDI clock timer nested) against the room
// (UISHOT_STACK_ROOM, default 8,424 B). The first time an (ISR profile, innermost function) pair goes over, the whole virtual stack is
// written down. This is reachability, not a probability: the host has no cycle clock, see ref/mac-emulator-plan.md.
//
// Report: UISHOT_STACK_REPORT=FILE written at exit (and when the process gets SIGUSR1).
#include <cxxabi.h>
#include <dlfcn.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <algorithm>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#define NOINST __attribute__((no_instrument_function))

namespace
{
struct Frame
{
    const char* name; // interned
    int bytes;
};

struct Profile
{
    std::string name;
    int isr = 0;
    long long events = 0;     // function entries at which the interrupt would overrun
    int worstMargin = 1 << 30; // room - depth - 108 - isr - nest, lowest seen
    std::string worstChain;
    std::map<std::string, long long> bySite; // innermost function -> events over
};

struct State
{
    bool ready = false, on = false;
    std::unordered_map<std::string, int> table;
    std::unordered_map<const void*, int> byFn;       // function address -> frame bytes (-1: not yet looked up)
    std::unordered_map<const void*, const char*> nameOf;
    std::vector<Frame> stack;
    int depth = 0, maxDepth = 0, room = 8424, nest = 546, base = 8;
    std::string maxChain;
    std::vector<std::pair<int, std::string>> records; // new maximum depth: depth and chain, last 12
    std::vector<Profile> profiles;
    long long entries = 0;
    long long bucket[10] = {};                          // entries by depth: <1k, <2k ... >=9k
};

State& S() NOINST;
State& S()
{
    static State* s = new State();
    return *s;
}

std::string demangled(const void* fn) NOINST;
std::string demangled(const void* fn)
{
    Dl_info di;
    if (!dladdr(fn, &di) || !di.dli_sname)
        return "";
    int st = 0;
    char* d = abi::__cxa_demangle(di.dli_sname, nullptr, nullptr, &st);
    std::string out = (st == 0 && d) ? d : di.dli_sname;
    free(d);
    return out;
}

// the rule of stack_depth.py frame_key: no clone suffix, no parameter list, no template return type, "(anonymous namespace)" is "{anon}"
std::string keyOf(std::string d) NOINST;
std::string keyOf(std::string d)
{
    size_t c = d.find(" [clone");
    if (c != std::string::npos)
        d.resize(c);
    const std::string anon = "(anonymous namespace)";
    for (size_t p = d.find(anon); p != std::string::npos; p = d.find(anon, p))
        d.replace(p, anon.size(), "{anon}");
    int depth = 0;
    size_t cut = std::string::npos;
    for (size_t i = 0; i < d.size(); i++)
    {
        if (d[i] == '<')
            depth++;
        else if (d[i] == '>')
            depth--;
        else if (d[i] == '(' && depth == 0 && !(i >= 8 && d.compare(i - 8, 8, "operator") == 0))
        {
            cut = i;
            break;
        }
    }
    if (cut == std::string::npos)
        return "";
    d.resize(cut);
    size_t sp = d.rfind(' ');
    if (sp != std::string::npos && d.find("operator") == std::string::npos)
        d = d.substr(sp + 1);
    return d;
}

void report(FILE* f) NOINST;
void report(FILE* f)
{
    State& s = S();
    fprintf(f, "# virtual ARM stack of this run (host_stackmodel.cpp)\nframe table functions: %zu, room %d B, nested interrupts %d B\n", s.table.size(), s.room, s.nest);
    fprintf(f, "function entries: %lld\ndepth histogram (entries by virtual depth):", s.entries);
    for (int i = 0; i < 10; i++)
        fprintf(f, " [%d-%dk] %lld", i, i + 1, s.bucket[i]);
    fprintf(f, "\nmax virtual depth: %d B\nmax chain: %s\n", s.maxDepth, s.maxChain.c_str());
    fprintf(f, "records (each a new maximum):\n");
    for (auto& r : s.records)
        fprintf(f, "  %6d  %s\n", r.first, r.second.c_str());
    for (auto& p : s.profiles)
    {
        fprintf(f, "ISR profile %s (chain %d B + 108 + nest %d): entries over the room %lld, worst margin %d B\n", p.name.c_str(), p.isr, s.nest, p.events, p.worstMargin);
        if (!p.worstChain.empty())
            fprintf(f, "  worst chain: %s\n", p.worstChain.c_str());
        std::vector<std::pair<long long, std::string>> v;
        for (auto& kv : p.bySite)
            v.push_back({kv.second, kv.first});
        std::sort(v.rbegin(), v.rend());
        for (size_t i = 0; i < v.size() && i < 8; i++)
            fprintf(f, "  site %-60s %lld\n", v[i].second.c_str(), v[i].first);
    }
}

void dump() NOINST;
void dump()
{
    const char* path = getenv("UISHOT_STACK_REPORT");
    FILE* f = path ? fopen(path, "w") : stderr;
    if (!f)
        f = stderr;
    report(f);
    if (f != stderr)
        fclose(f);
}

void onSignal(int) NOINST;
void onSignal(int)
{
    dump();
}

std::string chain() NOINST;
std::string chain()
{
    State& s = S();
    std::string out;
    int shown = 0;
    for (size_t i = 0; i < s.stack.size(); i++)
    {
        if (s.stack[i].bytes <= 0)
            continue;
        if (!out.empty())
            out += " > ";
        char b[32];
        snprintf(b, sizeof b, "[%d]", s.stack[i].bytes);
        out += s.stack[i].name;
        out += b;
        if (++shown >= 14)
        {
            out += " > ...";
            break;
        }
    }
    return out;
}

void init() NOINST;
void init()
{
    State& s = S();
    s.ready = true;
    const char* tf = getenv("UISHOT_STACK_FRAMES");
    if (!tf)
        return;
    FILE* f = fopen(tf, "r");
    if (!f)
    {
        fprintf(stderr, "host_stackmodel: cannot read %s\n", tf);
        return;
    }
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        char* tab = strrchr(line, '\t');
        if (!tab)
            continue;
        *tab = 0;
        s.table[line] = atoi(tab + 1);
    }
    fclose(f);
    if (const char* v = getenv("UISHOT_STACK_ROOM"))
        s.room = atoi(v);
    if (const char* v = getenv("UISHOT_STACK_NEST"))
        s.nest = atoi(v);
    if (const char* v = getenv("UISHOT_STACK_ISR"))
    {
        std::string all = v;
        size_t p = 0;
        while (p < all.size())
        {
            size_t e = all.find(',', p);
            if (e == std::string::npos)
                e = all.size();
            std::string item = all.substr(p, e - p);
            size_t eq = item.find('=');
            if (eq != std::string::npos)
            {
                Profile pr;
                pr.name = item.substr(0, eq);
                pr.isr = atoi(item.c_str() + eq + 1);
                s.profiles.push_back(pr);
            }
            p = e + 1;
        }
    }
    s.on = !s.table.empty();
    s.depth = s.base; // loop() is 8 B in the walker's chains, the host has hostServe in its place
    atexit(dump);
    signal(SIGUSR1, onSignal);
}
} // namespace

extern "C" NOINST void __cyg_profile_func_enter(void* fn, void*)
{
    State& s = S();
    if (!s.ready)
        init();
    if (!s.on)
        return;
    int bytes;
    auto it = s.byFn.find(fn);
    if (it == s.byFn.end())
    {
        std::string key = keyOf(demangled(fn));
        auto t = key.empty() ? s.table.end() : s.table.find(key);
        bytes = t == s.table.end() ? 0 : t->second;
        s.byFn[fn] = bytes;
        s.nameOf[fn] = strdup(key.empty() ? "?" : key.c_str());
    }
    else
        bytes = it->second;
    s.stack.push_back({s.nameOf[fn], bytes});
    if (bytes <= 0)
        return;
    s.depth += bytes;
    s.entries++;
    s.bucket[std::min(9, s.depth / 1000)]++;
    if (s.depth > s.maxDepth)
    {
        s.maxDepth = s.depth;
        s.maxChain = chain();
        s.records.push_back({s.depth, s.maxChain});
        if (s.records.size() > 12)
            s.records.erase(s.records.begin());
    }
    for (auto& p : s.profiles)
    {
        int margin = s.room - s.depth - 108 - p.isr - s.nest;
        if (margin < 0)
        {
            p.events++;
            p.bySite[s.nameOf[fn]]++;
        }
        if (margin < p.worstMargin)
        {
            p.worstMargin = margin;
            p.worstChain = chain();
        }
    }
}

extern "C" NOINST void __cyg_profile_func_exit(void* fn, void*)
{
    State& s = S();
    if (!s.on || s.stack.empty())
        return;
    s.depth -= s.stack.back().bytes > 0 ? s.stack.back().bytes : 0;
    s.stack.pop_back();
}
