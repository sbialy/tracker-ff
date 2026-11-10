#ifdef FEATURE_EXT_HOOKS

#include "features.h"

#include "Platform.h"
#include "plug_host.h"   // plug::writeText / readText: <path>.tmp, read back, replace with .bak (power cut safe)

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace features
{
namespace
{
constexpr const char* kDir = "/Features";

struct Entry
{
    const feat_desc* d;
    uint8_t on;                    // edited in place by the config page
    uint8_t v[kMaxParams];
    uint8_t appliedOn;             // what apply() last saw
    uint8_t applied[kMaxParams];
    uint8_t bootOn;                // the state the image started with (FEAT_F_RESTART)
    uint8_t bootV[kMaxParams];
    uint8_t dirty;
};

Entry s_e[kMax];
int s_n;
Platform* s_platform;
uint32_t s_dirtyMs;
char s_text[kFileMax];   // one file at a time, main loop only

uint32_t now()
{
    return s_platform ? s_platform->getTimer()->millis() : 0;
}

bool validId(const char* id)
{
    int n = 0;
    for (; id && id[n]; n++)
    {
        const char c = id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
    }
    return n >= 1 && n <= 8;
}

uint8_t clampParam(const feat_param& p, long v)
{
    const long hi = p.type == FEAT_P_ENUM ? long(p.enumCount) - 1 : long(p.max);
    const long lo = p.type == FEAT_P_ENUM ? 0 : long(p.min);
    if (v < lo || v > hi) return p.def;   // out of range: the default, never a clamp to an edge the user did not pick
    return uint8_t(v);
}

void defaults(Entry& e)
{
    e.on = (e.d->flags & FEAT_F_DEFAULT_ON) ? 1 : 0;
    for (int p = 0; p < e.d->paramCount; p++) e.v[p] = e.d->params[p].def;
}

int hexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    return -1;
}

// The file: "feature 1" first (format major), then "key value" lines. Unknown keys are skipped, a value out of range is the
// default, a missing line is the default, a bad first line makes the whole file defaults. Nothing in it can index past a table.
void parse(Entry& e, char* t, int n)
{
    defaults(e);
    t[n < kFileMax ? n : kFileMax - 1] = 0;
    if (strncmp(t, "feature 1\n", 10) != 0) return;
    static uint8_t state[kMaxState];
    int stateN = 0;
    for (char* line = t; line && *line;)
    {
        char* next = strchr(line, '\n');
        if (next) *next++ = 0;
        char* sp = strchr(line, ' ');
        if (sp)
        {
            *sp = 0;
            const char* val = sp + 1;
            if (strcmp(line, "on") == 0)
            {
                e.on = val[0] == '1';
            }
            else if (strcmp(line, "state") == 0)
            {
                const int cap = e.d->stateMax < kMaxState ? e.d->stateMax : kMaxState;
                for (; stateN < cap && hexVal(val[0]) >= 0 && hexVal(val[1]) >= 0; val += 2)
                    state[stateN++] = uint8_t(hexVal(val[0]) << 4 | hexVal(val[1]));
            }
            else
            {
                for (int p = 0; p < e.d->paramCount; p++)
                {
                    if (strcmp(line, e.d->params[p].key) == 0)
                    {
                        char* end = nullptr;
                        const long v = strtol(val, &end, 10);
                        e.v[p] = end != val ? clampParam(e.d->params[p], v) : e.d->params[p].def;
                    }
                }
            }
        }
        line = next;
    }
    if (stateN && e.d->loadState) e.d->loadState(state, stateN);
}

bool save(Entry& e)
{
    int n = snprintf(s_text, kFileMax, "feature 1\nid %s\nversion %u\non %u\n", e.d->id, unsigned(e.d->version), unsigned(e.on));
    for (int p = 0; p < e.d->paramCount && n < kFileMax; p++)
        n += snprintf(s_text + n, size_t(kFileMax - n), "%s %u\n", e.d->params[p].key, unsigned(e.v[p]));
    if (e.d->saveState && e.d->stateMax && n < kFileMax - 8)
    {
        static uint8_t state[kMaxState];
        const int cap = e.d->stateMax < kMaxState ? e.d->stateMax : kMaxState;
        const int k = e.d->saveState(state, cap);
        n += snprintf(s_text + n, size_t(kFileMax - n), "state ");
        for (int i = 0; i < k && i < cap && n < kFileMax - 3; i++) n += snprintf(s_text + n, 3, "%02x", state[i]);
        if (n < kFileMax - 1) s_text[n++] = '\n';
    }
    if (n >= kFileMax) return false;
    FileSystem* fs = s_platform->getFileSystem();
    if (!fs->exists(kDir)) fs->mkdir(0, kDir);   // first save only: a boot that changes nothing writes nothing
    char path[24];
    snprintf(path, sizeof path, "%s/%s.cfg", kDir, e.d->id);
    return plug::writeText(path, s_text, n);
}

void applyDiff(Entry& e)
{
    if (e.on != e.appliedOn)
    {
        e.appliedOn = e.on;
        if (e.d->apply) e.d->apply(e.on, e.v, 0xFF);
        e.dirty = 1;
    }
    for (int p = 0; p < e.d->paramCount; p++)
    {
        if (e.v[p] != e.applied[p])
        {
            e.applied[p] = e.v[p];
            if (e.d->apply) e.d->apply(e.on, e.v, uint8_t(p));
            e.dirty = 1;
        }
    }
}
}   // namespace

int add(const feat_desc* d)
{
    if (!d || s_n >= kMax || d->size < sizeof(feat_desc) || d->abi != FEAT_ABI || !validId(d->id) || !d->name) return -1;
    if (d->paramCount > kMaxParams || (d->paramCount && !d->params)) return -1;
    for (int p = 0; p < d->paramCount; p++)
    {
        const feat_param& q = d->params[p];
        if (!q.key || !q.name || (q.type == FEAT_P_ENUM && (!q.enumText || q.enumCount == 0 || q.def >= q.enumCount)))
            return -1;
        if (q.type == FEAT_P_INT && (q.min > q.max || q.def < q.min || q.def > q.max)) return -1;
    }
    if (find(d->id) >= 0) return -1;
    Entry& e = s_e[s_n];
    memset(&e, 0, sizeof e);
    e.d = d;
    defaults(e);
    return s_n++;
}

void boot(Platform* platform)
{
    s_platform = platform;
    registerBuiltins();
    for (int i = 0; i < s_n; i++)
    {
        Entry& e = s_e[i];
        char path[24];
        snprintf(path, sizeof path, "%s/%s.cfg", kDir, e.d->id);
        const int n = plug::readText(path, s_text, kFileMax - 1);
        if (n > 0) parse(e, s_text, n);
        e.appliedOn = e.bootOn = e.on;
        memcpy(e.applied, e.v, sizeof e.v);
        memcpy(e.bootV, e.v, sizeof e.v);
        if (e.d->boot) e.d->boot(e.on, e.v);
    }
    menuBuild();
}

void tick()
{
    bool any = false;
    for (int i = 0; i < s_n; i++) any = any || s_e[i].dirty;
    if (!any)
    {
        s_dirtyMs = now();
        return;
    }
    if (now() - s_dirtyMs < 1000) return;   // a burst of edits is one write
    flush();
}

void flush()
{
    for (int i = 0; i < s_n; i++)
    {
        if (s_e[i].dirty && save(s_e[i])) s_e[i].dirty = 0;
    }
    s_dirtyMs = now();
}

int count() { return s_n; }
const feat_desc* desc(int i) { return i >= 0 && i < s_n ? s_e[i].d : nullptr; }

int find(const char* id)
{
    for (int i = 0; i < s_n; i++)
        if (strcmp(s_e[i].d->id, id) == 0) return i;
    return -1;
}

bool on(int i) { return i >= 0 && i < s_n && s_e[i].on; }
uint8_t value(int i, int p) { return i >= 0 && i < s_n && p >= 0 && p < kMaxParams ? s_e[i].v[p] : 0; }
uint8_t* onRef(int i) { return &s_e[i].on; }
uint8_t* valueRef(int i, int p) { return &s_e[i].v[p]; }

void changed()
{
    for (int i = 0; i < s_n; i++) applyDiff(s_e[i]);
}

void set(int i, bool v)
{
    if (i < 0 || i >= s_n) return;
    s_e[i].on = v;
    applyDiff(s_e[i]);
}

bool restartPending(int i)
{
    const Entry& e = s_e[i];
    if ((e.d->flags & FEAT_F_RESTART) && e.on != e.bootOn) return true;
    for (int p = 0; p < e.d->paramCount; p++)
        if (e.d->params[p].restart && e.v[p] != e.bootV[p]) return true;
    return false;
}

const char* rowValue(int i)
{
    if (i < 0 || i >= s_n) return "";
    if (restartPending(i)) return "Restart";
    if (s_e[i].d->status) return s_e[i].d->status(s_e[i].on);
    return s_e[i].on ? "On" : "Off";
}
}   // namespace features

#endif
