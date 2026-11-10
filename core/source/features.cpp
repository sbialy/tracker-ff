#ifdef FEATURE_EXT_HOOKS

#include "features.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef FEATURES_TEST
#include "Platform.h"
#include "plug_host.h"   // plug::writeText / readText: <path>.tmp, read back, replace with .bak (power cut safe)
#endif

#if defined(__arm__) && !defined(FEATURES_TEST)
#include "cr_section_macros.h"
// main loop only buffers in SDRAM (no constructors, written before they are read): the internal RAM keeps the registry alone
#define FEAT_SDRAM __NOINIT(EXTERNAL_RAM)
#else
#define FEAT_SDRAM
#endif

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
    uint8_t hideOff;               // listed only while on (hideUnlessOn)
    uint8_t shown;                 // listed this session (decided at load)
};

Entry s_e[kMax];
int s_n;
const Io* s_io;
uint32_t s_dirtyMs;
void (*s_restartNow)();
FEAT_SDRAM char s_text[kFileMax];         // one file at a time, main loop only
FEAT_SDRAM uint8_t s_state[kMaxState];    // parse and save of the opaque state

uint32_t now()
{
    return s_io && s_io->millis ? s_io->millis() : 0;
}

bool validId(const char* id)
{
    int n = 0;
    for (; id && id[n]; n++)
    {
        const char c = id[n];
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'))) return false;
        if (n >= 8) return false;
    }
    return n >= 1;
}

uint8_t checkParam(const feat_param& p, long v)
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

void path(char* out, int cap, const char* id, const char* ext)
{
    snprintf(out, size_t(cap), "%s/%s%s", kDir, id, ext);
}

// The file: "feature 1" first (format major), then "key value" lines. Unknown keys are skipped, a value out of range is the
// default, a missing line is the default, a bad first line makes the whole file defaults. Nothing in it can index past a table.
void parse(Entry& e, char* t, int n)
{
    defaults(e);
    t[n < kFileMax ? n : kFileMax - 1] = 0;
    if (strncmp(t, "feature 1\n", 10) != 0) return;
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
                if ((val[0] == '0' || val[0] == '1') && val[1] == 0) e.on = uint8_t(val[0] - '0');   // anything else: the default
            }
            else if (strcmp(line, "state") == 0)
            {
                const int cap = e.d->stateMax < kMaxState ? e.d->stateMax : kMaxState;
                stateN = 0;
                for (; stateN < cap && hexVal(val[0]) >= 0 && hexVal(val[1]) >= 0; val += 2)
                    s_state[stateN++] = uint8_t(hexVal(val[0]) << 4 | hexVal(val[1]));
            }
            else
            {
                for (int p = 0; p < e.d->paramCount; p++)
                {
                    if (strcmp(line, e.d->params[p].key) == 0)
                    {
                        char* end = nullptr;
                        const long v = strtol(val, &end, 10);
                        e.v[p] = (end != val && *end == 0) ? checkParam(e.d->params[p], v) : e.d->params[p].def;
                    }
                }
            }
        }
        line = next;
    }
    if (stateN && e.d->loadState) e.d->loadState(s_state, stateN);
}

bool save(Entry& e)
{
    if (!s_io) return false;
    int n = snprintf(s_text, kFileMax, "feature 1\nid %s\nversion %u\non %u\n", e.d->id, unsigned(e.d->version), unsigned(e.on));
    for (int p = 0; p < e.d->paramCount && n < kFileMax; p++)
        n += snprintf(s_text + n, size_t(kFileMax - n), "%s %u\n", e.d->params[p].key, unsigned(e.v[p]));
    if (e.d->saveState && e.d->stateMax && n < kFileMax - 8)
    {
        const int cap = e.d->stateMax < kMaxState ? e.d->stateMax : kMaxState;
        const int k = e.d->saveState(s_state, cap);
        n += snprintf(s_text + n, size_t(kFileMax - n), "state ");
        for (int i = 0; i < k && i < cap && n < kFileMax - 3; i++) n += snprintf(s_text + n, 3, "%02x", s_state[i]);
        if (n < kFileMax - 1) s_text[n++] = '\n';
    }
    if (n >= kFileMax) return false;
    if (!s_io->exists(kDir)) s_io->mkdir(kDir);   // first save only: a boot that changes nothing writes nothing
    char p[32];
    path(p, sizeof p, e.d->id, ".cfg");
    return s_io->write(p, s_text, n);
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

#ifndef FEATURES_TEST
FileSystem* s_fs;
Timer* s_timer;
int cardRead(const char* p, char* buf, int cap) { return plug::readText(p, buf, cap); }
bool cardWrite(const char* p, const char* buf, int n) { return plug::writeText(p, buf, n); }
bool cardExists(const char* p) { return s_fs->exists(p); }
void cardMkdir(const char* p) { s_fs->mkdir(0, p); }
void cardRemove(const char* p) { s_fs->remove(p); }
uint32_t cardMillis() { return s_timer->millis(); }
const Io kCard = { cardRead, cardWrite, cardExists, cardMkdir, cardRemove, cardMillis };
#endif
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
        if (q.type == FEAT_P_ENUM)
            for (int k = 0; k < q.enumCount; k++)
                if (!q.enumText[k]) return -1;
        if (q.type == FEAT_P_INT && (q.min > q.max || q.def < q.min || q.def > q.max)) return -1;
        if (q.type != FEAT_P_ENUM && q.type != FEAT_P_INT) return -1;
    }
    if (find(d->id) >= 0) return -1;
    Entry& e = s_e[s_n];
    memset(&e, 0, sizeof e);
    e.d = d;
    e.shown = 1;
    defaults(e);
    return s_n++;
}

#ifndef FEATURES_TEST
void boot(Platform* platform)
{
    s_fs = platform->getFileSystem();
    s_timer = platform->getTimer();
    registerBuiltins();
    load(&kCard);
    menuBuild();
}
#endif

void load(const Io* io)
{
    s_io = io;
    for (int i = 0; i < s_n; i++)
    {
        Entry& e = s_e[i];
        char p[32];
        path(p, sizeof p, e.d->id, ".cfg");
        const int n = s_io->read(p, s_text, kFileMax - 1);
        if (n > 0) parse(e, s_text, n);
        e.bootOn = e.on;
        if ((e.d->flags & FEAT_F_BOOTFLAG) && e.d->running) e.bootOn = e.d->running() ? 1 : 0;
        e.appliedOn = e.bootOn;   // a file that differs from the boot flag is applied (re-synced) below
        memcpy(e.applied, e.v, sizeof e.v);
        memcpy(e.bootV, e.v, sizeof e.v);
        e.shown = !e.hideOff || e.on;
        if (e.d->boot) e.d->boot(e.on, e.v);
        if (e.on != e.appliedOn)
        {
            e.appliedOn = e.on;
            if (e.d->apply) e.d->apply(e.on, e.v, 0xFF);   // the mirror follows the file, nothing to save
        }
    }
    s_dirtyMs = now();
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

void resetAll()
{
    for (int i = 0; i < s_n; i++)
    {
        Entry& e = s_e[i];
        defaults(e);
        applyDiff(e);
        e.dirty = 0;
        if (!s_io) continue;
        static const char* const kExt[] = { ".cfg", ".cfg.bak", ".cfg.tmp" };
        for (const char* x : kExt)
        {
            char p[32];
            path(p, sizeof p, e.d->id, x);
            if (s_io->exists(p)) s_io->remove(p);
        }
    }
}

void clear()
{
    s_n = 0;
    s_io = nullptr;
    s_restartNow = nullptr;
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
    if (i < 0 || i >= s_n) return false;
    const Entry& e = s_e[i];
    if ((e.d->flags & FEAT_F_RESTART) && e.on != e.bootOn) return true;
    for (int p = 0; p < e.d->paramCount; p++)
        if (e.d->params[p].restart && e.v[p] != e.bootV[p] && e.on) return true;
    return false;
}

const char* rowValue(int i)
{
    if (i < 0 || i >= s_n) return "";
    if (restartPending(i)) return "Restart";
    if (s_e[i].d->status) return s_e[i].d->status(s_e[i].on);
    return s_e[i].on ? "On" : "Off";
}

void hideUnlessOn(int i)
{
    if (i >= 0 && i < s_n) s_e[i].hideOff = 1;
}

bool listed(int i)
{
    return i >= 0 && i < s_n && s_e[i].shown;
}

void setRestartNow(void (*fn)())
{
    s_restartNow = fn;
}

void restartNow()
{
    flush();   // synchronously: the power off below must not depend on the debounce
    if (s_restartNow) s_restartNow();
}
}   // namespace features

#endif
