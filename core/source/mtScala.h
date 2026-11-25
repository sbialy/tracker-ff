// Scala .scl files (microtuning phase 2, ref/microtuning-scope.md sections 6 and 8): the parser of a whole file in a buffer. Pure arithmetic and no
// firmware includes (the firmware, the project sidecar and the host tests include this one header, like mtTuning.h).
//
// The format: lines that start with "!" are comments. The first other line is the description (any text, may be empty), the next one the note count N,
// then N lines, each one pitch: a value with a "." is cents, a "p/q" or a plain integer is a ratio. Text after the value (a space, then a label) is
// ignored. The LAST of the N lines is the period (2/1 or 1200.0 for an octave), the lines before it are the degrees 1..N-1 above the 1/1, which is
// implicit and not listed. So a scale of N notes is N degrees per period. Lines after the N values are ignored.
//
// What is rejected (no file is ever truncated, a half read scale would detune every key above the cut):
//   kNotScala      no count line, a count of 0 or not a number, fewer value lines than the count, a control character in the file
//   kTooMany       more than 128 notes (the key table is 128 keys)
//   kBadValue      a value that does not parse, a ratio part that is 0 or has more than 9 digits, cents beyond +-9600, on line N
//   kNotAscending  a degree not above the one before (or the period not above the last degree)
//   kTooLong       the file is larger than 8 KB
//   kPeriodRange   the period (last line) is below 10 cents or above 4800 cents
// Out: deg[0..n) in 1e-3 cent with deg[0] = 0, the period in 1e-3 cent. deg needs 128 entries and is only valid on kOk.
#ifndef MT_SCALA_H
#define MT_SCALA_H

#include <stdint.h>
#include <stdio.h>
#include <cmath>

namespace scala
{
constexpr int kMaxBytes = 8192;
constexpr int kMaxNotes = 128;
constexpr int32_t kMinPeriodMc = 10000;
constexpr int32_t kMaxPeriodMc = 4800000;

enum Err : uint8_t
{
    kOk = 0,
    kNotScala,
    kTooMany,
    kBadValue,
    kNotAscending,
    kTooLong,
    kPeriodRange,
};

struct Result
{
    Err err;
    int n;                // kOk: the notes per period. kTooMany: the count the file asks for
    int line;             // kBadValue: the 1 based line of the value
    uint32_t periodMc;    // kOk: the period in 1e-3 cent
};

namespace detail
{
inline bool isSp(char c) { return c == ' ' || c == '\t'; }
inline bool isDig(char c) { return c >= '0' && c <= '9'; }

// One value token (the text from p to e, no spaces) to 1e-3 cent. False: not a value
inline bool value(const char* p, const char* e, int32_t& mc)
{
    const char* slash = p;
    bool dot = false;
    for (const char* q = p; q < e; q++)
    {
        if (*q == '/' && slash == p) slash = q;
        if (*q == '.') dot = true;
    }
    if (slash != p)
    {   // p/q: both parts 1..9 digits and not zero
        if (dot) return false;
        uint32_t num = 0, den = 0;
        int dn = 0, dd = 0;
        const char* q = p;
        for (; q < slash && isDig(*q); q++, dn++) num = num * 10u + uint32_t(*q - '0');
        if (q != slash || dn < 1 || dn > 9 || num == 0) return false;
        for (q = slash + 1; q < e && isDig(*q); q++, dd++) den = den * 10u + uint32_t(*q - '0');
        if (q != e || dd < 1 || dd > 9 || den == 0) return false;
        const float c = 1200.0f * std::log2(float(num) / float(den));
        mc = int32_t(c * 1000.0f + (c >= 0.0f ? 0.5f : -0.5f));
        return true;
    }
    if (!dot)
    {   // a whole number is the ratio n/1
        uint32_t num = 0;
        int dn = 0;
        const char* q = p;
        for (; q < e && isDig(*q); q++, dn++) num = num * 10u + uint32_t(*q - '0');
        if (q != e || dn < 1 || dn > 9 || num == 0) return false;
        const float c = 1200.0f * std::log2(float(num));
        mc = int32_t(c * 1000.0f + (c >= 0.0f ? 0.5f : -0.5f));
        return true;
    }
    // cents: [sign] digits "." digits, at least one digit. 1e-3 cent, the 4th decimal rounds
    const char* q = p;
    bool neg = false;
    if (q < e && (*q == '-' || *q == '+'))
    {
        neg = *q == '-';
        q++;
    }
    uint32_t ip = 0;
    int di = 0, df = 0;
    for (; q < e && isDig(*q); q++, di++)
    {
        if (di >= 5) return false;   // more than 99999 cents
        ip = ip * 10u + uint32_t(*q - '0');
    }
    if (q >= e || *q != '.') return false;
    q++;
    uint32_t fp = 0;
    int round = 0;
    for (; q < e && isDig(*q); q++, df++)
    {
        if (df < 3) fp = fp * 10u + uint32_t(*q - '0');
        else if (df == 3) round = *q >= '5';
    }
    if (q != e || (di == 0 && df == 0)) return false;
    for (int i = df; i < 3; i++) fp *= 10u;
    uint32_t m = ip * 1000u + fp + uint32_t(round);
    if (m > 9600000u) return false;
    mc = neg ? -int32_t(m) : int32_t(m);
    return true;
}
}   // namespace detail

inline Result parse(const char* b, int len, uint32_t* deg)
{
    Result r = {kOk, 0, 0, 0};
    if (len > kMaxBytes)
    {
        r.err = kTooLong;
        return r;
    }
    for (int i = 0; i < len; i++)
    {   // text only: tab, line feed and carriage return are the only control characters of a Scala file
        const unsigned char c = (unsigned char)b[i];
        if (c < 32 && c != '\n' && c != '\r' && c != '\t')
        {
            r.err = kNotScala;
            return r;
        }
    }
    int stage = 0;        // 0 the description, 1 the count, 2 the values
    int count = 0, got = 0, lineNo = 0;
    int32_t prev = 0;
    deg[0] = 0;
    const char* const end = b + len;
    const char* p = b;
    while (p < end && stage < 3)
    {
        const char* e = p;
        while (e < end && *e != '\n') e++;
        lineNo++;
        const char* le = e;
        if (le > p && le[-1] == '\r') le--;
        const char* s = p;
        p = e + 1;
        if (s < le && *s == '!') continue;   // a comment
        if (stage == 0)
        {
            stage = 1;
            continue;
        }
        while (s < le && detail::isSp(*s)) s++;
        const char* t = s;
        while (t < le && !detail::isSp(*t)) t++;
        if (stage == 1)
        {
            uint32_t v = 0;
            int nd = 0;
            const char* q = s;
            for (; q < t && detail::isDig(*q); q++, nd++) v = v * 10u + uint32_t(*q - '0');
            if (q != t || nd < 1 || nd > 9 || v == 0)
            {
                r.err = kNotScala;
                return r;
            }
            if (v > uint32_t(kMaxNotes))
            {
                r.err = kTooMany;
                r.n = int(v);
                return r;
            }
            count = int(v);
            stage = 2;
            continue;
        }
        int32_t mc;
        if (s == t || !detail::value(s, t, mc))
        {
            r.err = kBadValue;
            r.line = lineNo;
            return r;
        }
        if (mc <= prev)
        {
            r.err = kNotAscending;
            r.line = lineNo;
            return r;
        }
        prev = mc;
        got++;
        if (got < count)
            deg[got] = uint32_t(mc);
        else
        {
            r.periodMc = uint32_t(mc);
            stage = 3;
        }
    }
    if (stage != 3)
    {
        r.err = kNotScala;
        return r;
    }
    if (r.periodMc < uint32_t(kMinPeriodMc) || r.periodMc > uint32_t(kMaxPeriodMc))
    {
        r.err = kPeriodRange;
        return r;
    }
    r.n = count;
    return r;
}

// The one line the screen shows for a result (a popup text)
inline void errText(const Result& r, char* out, int cap)
{
    switch (r.err)
    {
        case kNotScala: snprintf(out, size_t(cap), "Not a Scala file"); break;
        case kTooMany: snprintf(out, size_t(cap), "Scale has %d notes, max %d", r.n, kMaxNotes); break;
        case kBadValue: snprintf(out, size_t(cap), "Bad value on line %d", r.line); break;
        case kNotAscending: snprintf(out, size_t(cap), "Scale must ascend"); break;
        case kTooLong: snprintf(out, size_t(cap), "Scala file too long"); break;
        case kPeriodRange: snprintf(out, size_t(cap), "Period out of range"); break;
        default: out[0] = 0; break;
    }
}

// Does deg[0..n) (deg[0] = 0) ascend with the period above the last degree, and the period in range: what a stored copy must satisfy
inline bool validTable(const uint32_t* deg, int n, uint32_t periodMc)
{
    if (n < 1 || n > kMaxNotes || deg[0] != 0 || periodMc < uint32_t(kMinPeriodMc) || periodMc > uint32_t(kMaxPeriodMc)) return false;
    uint32_t prev = 0;
    for (int i = 1; i < n; i++)
    {
        if (deg[i] <= prev) return false;
        prev = deg[i];
    }
    return periodMc > prev;
}
}   // namespace scala

#endif
