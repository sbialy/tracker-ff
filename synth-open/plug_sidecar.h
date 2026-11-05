// Project sidecar of the plugin host: which plugin sits in which master insert slot, by uid, with its parameter values by
// parameter name. Plain text, no file system here (plug_host.cpp reads and writes the file, tools/plug_sidecar_test.cpp
// tests the codec on the Mac). The binary project (strMtValues) does not change: older firmware ignores the file.
//
//   tffplug 1
//   slot 1 3a51c2e0 00010000 Tape          slot (1 or 2), uid, version (hex), the name shown when the plugin is missing
//   p Drive=30                             one line per parameter, by name, until the next slot line
//   p Mix=100
//
// Unknown lines are skipped, a value that does not parse is skipped, a file without the first line reads as empty. A project
// with no plugin has no file at all (write returns 0 and the caller removes the file).
#pragma once

#include <stdint.h>
#include <string.h>

namespace plug
{

constexpr int kSlots = 2;           // master insert slots (decision 9)
constexpr int kMaxParams = 32;      // plug::checkDesc refuses more
constexpr int kNameLen = 16;        // 15 chars + NUL, plugin and parameter names
constexpr int kSidecarMax = 2560;   // bytes: 2 x (40 + 32 x 31) fits

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
    bool any = false;
    for (const SlotRef& r : s.slot) any = any || r.uid;
    if (!any) return 0;
    int n = sc::putStr(b, 0, cap, "tffplug 1\n");
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
    SlotRef* cur = nullptr;
    for (p = e + 1; p < end; p = e + 1)
    {
        e = lineEnd(p);
        if (e - p > 7 && memcmp(p, "slot ", 5) == 0 && (p[5] == '1' || p[5] == '2') && p[6] == ' ')
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

}   // namespace plug
