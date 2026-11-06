// Project sidecar of the plugin host: which plugin sits in which master insert slot, by uid, with its parameter values by
// parameter name. Plain text, no file system here (plug_host.cpp reads and writes the file, tools/plug_sidecar_test.cpp
// tests the codec on the Mac). The binary project (strMtValues) does not change: older firmware ignores the file.
//
//   tffplug 1
//   format 2                               since the "Inserts" Saturation entry (older files have no such line, format 1)
//   mixer 10                               the mixer stage under the inserts: 1 Soft Clip, 10 Soft Knee (saturationType
//                                          values), no line = 0 Hard Clip (every older file)
//   slot 1 3a51c2e0 00010000 Tape          slot (1 or 2), uid, version (hex), the name shown when the plugin is missing
//   p Drive=30                             one line per parameter, by name, until the next slot line
//   p Mix=100
//
// Unknown lines are skipped, a value that does not parse is skipped, a file without the first line reads as empty. A project
// with no plugin and a Hard Clip mixer stage has no file at all (write returns 0 and the caller removes the file).
//
// Whether the inserts play is not in the sidecar: the binary project's saturationType holds kSatInserts (the "Inserts" entry
// of the Saturation list) or a built-in entry (the inserts are bypassed and kept). Stock firmware clamps kSatInserts to Hard
// Clip. The inserts run on the mixer stage of the "mixer" line (Hard Clip without one), picked on the Inserts page.
#pragma once

#include <stdint.h>
#include <string.h>

namespace plug
{

constexpr int kSlots = 2;           // master insert slots (decision 9)
constexpr int kMaxParams = 32;      // plug::checkDesc refuses more
constexpr int kNameLen = 16;        // 15 chars + NUL, plugin and parameter names
constexpr int kSidecarMax = 2560;   // bytes: 2 x (40 + 32 x 31) fits
constexpr int kSidecarFormat = 2;   // written by this host: "format 2" (format 1 files predate the Inserts entry)
// Reserved saturationType of the binary project: the "Inserts" entry (above every built-in type of any build, stock firmware
// loads it as Hard Clip, fProjEdit.cpp)
constexpr uint8_t kSatInserts = 0x80;
constexpr uint8_t kSatHardClip = 0;   // SaturationType::kHardClip
constexpr uint8_t kSatSoftClip = 1;   // SaturationType::kSoftClip
constexpr uint8_t kSatSoftKnee = 10;  // SaturationType::kSoftKnee (fixed value, FEATURE_SAT_SOFTKNEE)
// FF builds' built-in Dirt entries (saturationType 5..9: Dirt Soft, Hard, Fold, Asym, Crush), dropped from PLUG_HOST builds
constexpr uint8_t kSatDirtFirst = 5;
constexpr uint8_t kSatDirtLast = 9;
constexpr uint32_t kDirtUid = 0x92b586d4u;   // dirt.plg (build_plug.py --id ff.dirt)

// The mixer stages the Inserts entry can run on (Inserts page, "Mixer" column): a stored value outside them is Hard Clip
inline uint8_t mixerStageOf(uint32_t v)
{
    return v == kSatSoftClip || v == kSatSoftKnee ? uint8_t(v) : kSatHardClip;
}

struct SlotRef
{
    uint32_t uid;        // 0 = slot empty
    uint32_t version;
    char name[kNameLen];
    uint8_t count;       // stored parameter values
    char pname[kMaxParams][kNameLen];
    float pval[kMaxParams];
};

struct Sidecar
{
    SlotRef slot[kSlots];
    uint8_t format;      // 0 no sidecar, 1 before the Inserts entry, 2 this host
    uint8_t mixer;       // the mixer stage under the inserts (mixerStageOf), 0 Hard Clip
};

inline void copyName(char* dst, const char* src, int len)
{
    int i = 0;
    for (; i < kNameLen - 1 && i < len && src[i] && src[i] != '\r' && src[i] != '\n'; i++) dst[i] = src[i];
    dst[i] = 0;
}

namespace sc
{
inline int putStr(char* b, int n, int cap, const char* s)
{
    while (*s && n < cap) b[n++] = *s++;
    return n;
}
inline int putHex(char* b, int n, int cap, uint32_t v)
{
    for (int k = 7; k >= 0 && n < cap; k--) b[n++] = "0123456789abcdef"[(v >> (4 * k)) & 15u];
    return n;
}
inline int putU(char* b, int n, int cap, uint32_t v)
{
    char t[10];
    int k = 0;
    do
    {
        t[k++] = char('0' + v % 10u);
        v /= 10u;
    } while (v);
    while (k && n < cap) b[n++] = t[--k];
    return n;
}
// Fixed point, 6 decimals, trailing zeros dropped: 30, 0.35, -2.5. |v| is clamped to 1e9.
inline int putF(char* b, int n, int cap, float f)
{
    if (!(f == f)) f = 0.0f;
    if (f < 0.0f && n < cap)
    {
        b[n++] = '-';
        f = -f;
    }
    if (f > 1.0e9f) f = 1.0e9f;
    uint64_t q = uint64_t(double(f) * 1000000.0 + 0.5);
    n = putU(b, n, cap, uint32_t(q / 1000000u));
    uint32_t fr = uint32_t(q % 1000000u);
    if (fr && n < cap)
    {
        b[n++] = '.';
        for (uint32_t d = 100000u; fr && d && n < cap; d /= 10u)
        {
            b[n++] = char('0' + fr / d);
            fr %= d;
        }
    }
    return n;
}
inline bool getHex(const char*& p, const char* e, uint32_t& v)
{
    v = 0;
    int k = 0;
    for (; p < e && k < 8; p++, k++)
    {
        const char c = *p;
        const int d = c >= '0' && c <= '9' ? c - '0' : (c >= 'a' && c <= 'f' ? c - 'a' + 10 : (c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1));
        if (d < 0) break;
        v = (v << 4) | uint32_t(d);
    }
    return k > 0;
}
inline bool getF(const char* p, const char* e, float& out)
{
    bool neg = false;
    if (p < e && (*p == '-' || *p == '+')) neg = *p++ == '-';
    double v = 0.0, scale = 1.0;
    int digits = 0;
    for (; p < e && *p >= '0' && *p <= '9'; p++, digits++) v = v * 10.0 + (*p - '0');
    if (p < e && *p == '.')
        for (p++; p < e && *p >= '0' && *p <= '9'; p++, digits++) v += (*p - '0') * (scale *= 0.1);
    while (p < e && (*p == ' ' || *p == '\r')) p++;
    if (!digits || p != e) return false;
    out = float(neg ? -v : v);
    return true;
}
}   // namespace sc

// Returns the text length (0 when no slot is used: no file), -1 if cap is too small.
inline int sidecarWrite(const Sidecar& s, char* b, int cap)
{
    bool any = s.mixer != kSatHardClip;
    for (const SlotRef& r : s.slot) any = any || r.uid;
    if (!any) return 0;
    int n = sc::putStr(b, 0, cap, "tffplug 1\nformat ");
    n = sc::putU(b, n, cap, uint32_t(kSidecarFormat));
    n = sc::putStr(b, n, cap, "\n");
    if (s.mixer != kSatHardClip)
    {
        n = sc::putStr(b, n, cap, "mixer ");
        n = sc::putU(b, n, cap, s.mixer);
        n = sc::putStr(b, n, cap, "\n");
    }
    for (int k = 0; k < kSlots; k++)
    {
        const SlotRef& r = s.slot[k];
        if (!r.uid) continue;
        n = sc::putStr(b, n, cap, "slot ");
        n = sc::putU(b, n, cap, uint32_t(k + 1));
        n = sc::putStr(b, n, cap, " ");
        n = sc::putHex(b, n, cap, r.uid);
        n = sc::putStr(b, n, cap, " ");
        n = sc::putHex(b, n, cap, r.version);
        n = sc::putStr(b, n, cap, " ");
        n = sc::putStr(b, n, cap, r.name);
        n = sc::putStr(b, n, cap, "\n");
        for (int i = 0; i < r.count && i < kMaxParams; i++)
        {
            n = sc::putStr(b, n, cap, "p ");
            n = sc::putStr(b, n, cap, r.pname[i]);
            n = sc::putStr(b, n, cap, "=");
            n = sc::putF(b, n, cap, r.pval[i]);
            n = sc::putStr(b, n, cap, "\n");
        }
    }
    return n < cap ? n : -1;
}

// Fills s (cleared first). False and s empty when the text is not a sidecar.
inline bool sidecarRead(const char* b, int len, Sidecar& s)
{
    memset(&s, 0, sizeof(s));
    const char* const end = b + len;
    const char* p = b;
    auto lineEnd = [&](const char* q) {
        while (q < end && *q != '\n') q++;
        return q;
    };
    const char* e = lineEnd(p);
    if (e - p < 9 || memcmp(p, "tffplug 1", 9) != 0) return false;
    s.format = 1;
    SlotRef* cur = nullptr;
    for (p = e + 1; p < end; p = e + 1)
    {
        e = lineEnd(p);
        if (e - p >= 8 && memcmp(p, "format ", 7) == 0 && p[7] >= '2' && p[7] <= '9')
            s.format = uint8_t(p[7] - '0');
        else if (e - p >= 7 && memcmp(p, "mixer ", 6) == 0)
        {
            uint32_t v = 0;
            const char* q = p + 6;
            for (; q < e && *q >= '0' && *q <= '9' && v < 1000; q++) v = v * 10 + uint32_t(*q - '0');
            s.mixer = mixerStageOf(v);
        }
        else if (e - p > 7 && memcmp(p, "slot ", 5) == 0 && (p[5] == '1' || p[5] == '2') && p[6] == ' ')
        {
            cur = nullptr;
            const char* q = p + 7;
            SlotRef& r = s.slot[p[5] - '1'];
            uint32_t uid, ver;
            if (!sc::getHex(q, e, uid) || q >= e || *q++ != ' ' || !sc::getHex(q, e, ver) || !uid) continue;
            if (q < e && *q == ' ') q++;
            memset(&r, 0, sizeof(r));
            r.uid = uid;
            r.version = ver;
            copyName(r.name, q, int(e - q));
            cur = &r;
        }
        else if (cur && e - p > 3 && p[0] == 'p' && p[1] == ' ' && cur->count < kMaxParams)
        {
            const char* eq = p + 2;
            while (eq < e && *eq != '=') eq++;
            float v;
            if (eq >= e || eq == p + 2 || !sc::getF(eq + 1, e, v)) continue;
            copyName(cur->pname[cur->count], p + 2, int(eq - (p + 2)));
            cur->pval[cur->count++] = v;
        }
    }
    return true;
}

// The stored value of parameter `name`, or false.
inline bool sidecarValue(const SlotRef& r, const char* name, float& v)
{
    for (int i = 0; i < r.count; i++)
        if (strncmp(r.pname[i], name, kNameLen - 1) == 0)
        {
            v = r.pval[i];
            return true;
        }
    return false;
}

// Project load: whether the inserts play, from the binary project's saturationType (sat) and the sidecar. A format 1 sidecar
// (b8e37c8 and older) had no Inserts entry: a plugin picked in the Saturation list left Hard Clip in the project, so Hard
// Clip with a used slot turns into kSatInserts here (sat is changed) and the old project sounds as before.
inline bool insertsOnAfterLoad(uint8_t& sat, const Sidecar& s)
{
    if (s.format == 1 && sat == kSatHardClip && (s.slot[0].uid || s.slot[1].uid)) sat = kSatInserts;
    return sat == kSatInserts;
}

// Project load in a build without the built-in Dirt entries (PLUG_HOST, FF_DIRT=0): a project saved with one (sat 5..9) plays
// Inserts with Insert 1 = Dirt at that entry's values when dirt.plg is on the card (have, its index version), else Hard
// Clip. Insert 1's earlier plugin is replaced (it was bypassed under the Dirt entry), insert 2 stays,
// the mixer stage is Hard Clip (the Dirt entries ran the mixers at hard clip). Returns 0 when sat is no Dirt entry (nothing
// changes), 1 when it became Inserts with Dirt, 2 when it fell back to Hard Clip.
struct DirtPreset
{
    uint8_t type, drive, bits, rate, tone;   // dirt.plg's Type (0 Soft, 1 Hard, 2 Fold, 3 Asym), Drive, Bits, Rate, Tone
};
// the five built-in entries (synth-open/ff_dirt_ctl.cpp kPresets), Mix 100
constexpr DirtPreset kDirtPresets[5] = {{0, 35, 16, 1, 100}, {1, 35, 16, 1, 100}, {2, 35, 16, 1, 100}, {3, 35, 16, 1, 100},
                                        {0, 15, 8, 4, 70}};
constexpr const char* kDirtEntryNames[5] = {"Dirt Soft", "Dirt Hard", "Dirt Fold", "Dirt Asym", "Dirt Crush"};

inline int dirtMigrate(uint8_t& sat, Sidecar& s, bool have, uint32_t version)
{
    if (sat < kSatDirtFirst || sat > kSatDirtLast) return 0;
    const int k = sat - kSatDirtFirst;
    if (!have)
    {
        sat = kSatHardClip;
        return 2;
    }
    SlotRef& r = s.slot[0];
    memset(&r, 0, sizeof(r));
    r.uid = kDirtUid;
    r.version = version;
    memcpy(r.name, "Dirt", 5);
    const DirtPreset& d = kDirtPresets[k];
    const char* const names[6] = {"Type", "Drive", "Bits", "Rate", "Tone", "Mix"};
    const uint8_t vals[6] = {d.type, d.drive, d.bits, d.rate, d.tone, 100};
    for (int i = 0; i < 6; i++)
    {
        memcpy(r.pname[i], names[i], strlen(names[i]) + 1);
        r.pval[i] = float(vals[i]);
    }
    r.count = 6;
    s.mixer = kSatHardClip;
    if (s.format < 2) s.format = uint8_t(kSidecarFormat);
    sat = kSatInserts;
    return 1;
}

// The Saturation value text of the Inserts entry: "Inserts" with both slots empty, else "Ins: A+B" (insert 1 first) cut to
// maxChars (the master screen field holds 9 characters of font 2, Roboto Mono 17 at 10 px in a 94 px label). Cuts in this
// order: the space, then for two names the "Ins:" prefix, then the longer name loses its last letters one at a time
// (the second one on a tie, a name keeps at least one letter). out holds maxChars + 1 bytes.
inline void insertsLabel(const char* a, const char* b, char* out, int maxChars)
{
    if (!a[0] && b[0])
    {
        a = b;
        b = "";
    }
    int la = int(strlen(a)), lb = int(strlen(b));
    int n = 0;
    auto put = [&](const char* t, int l) {
        for (int i = 0; i < l && n < maxChars; i++) out[n++] = t[i];
    };
    if (!la)
        put("Inserts", 7);
    else if (!lb)
    {
        put(4 + 1 + la <= maxChars ? "Ins: " : "Ins:", 4 + (4 + 1 + la <= maxChars));
        put(a, la);
    }
    else
    {
        const int full = la + 1 + lb;
        if (4 + 1 + full <= maxChars)
            put("Ins: ", 5);
        else if (4 + full <= maxChars)
            put("Ins:", 4);
        while (la + 1 + lb > maxChars && (la > 1 || lb > 1))
        {
            if (lb >= la && lb > 1)
                lb--;
            else
                la--;
        }
        put(a, la);
        put("+", 1);
        put(b, lb);
    }
    out[n] = 0;
}

// ---- the file rules, over any file system with
//   int read(const char* path, char* buf, int cap)   bytes read, -1 when there is no such file
//   bool write(const char* path, const char* buf, int n)   replaces the file
//   bool exists(const char* path), bool remove(const char* path)
// plug_host.cpp runs them on the SD card, tools/plug_sidecar_test.cpp on an in-memory one.

// Project load: no file (or not a sidecar) gives an empty Sidecar, every slot empty, exactly as stock firmware.
template <class Fs> void sidecarLoad(Fs& fs, const char* path, char* buf, int cap, Sidecar& s)
{
    const int n = fs.read(path, buf, cap);
    if (n <= 0 || !sidecarRead(buf, n, s)) memset(&s, 0, sizeof(s));
}

// Project save: the file when a slot is used, no file at all otherwise (a stale one is removed).
template <class Fs> bool sidecarSave(Fs& fs, const char* path, const Sidecar& s, char* buf, int cap)
{
    const int n = sidecarWrite(s, buf, cap);
    if (n <= 0)
    {
        if (fs.exists(path)) fs.remove(path);
        return n == 0;
    }
    return fs.write(path, buf, n);
}

// Project open (project folder to Workspace) and save (Workspace to project folder): the destination ends up with the
// source's file or with none.
template <class Fs> void sidecarCopy(Fs& fs, const char* src, const char* dst, char* buf, int cap)
{
    const int n = fs.read(src, buf, cap);
    if (n > 0)
        fs.write(dst, buf, n);
    else if (fs.exists(dst))
        fs.remove(dst);
}

}   // namespace plug
