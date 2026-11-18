// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit 21b691c62af8,
// src/core/float_format.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::fm instead of ot::fm, the header comment is ours (tracker-ff).
// The code is unchanged, a few comments are reworded.
//
// printf %f without newlib's float printf (float_format.h).
//
// |v| = m * 2^-s exactly (m < 2^53). The digits are q = round(m * 10^p * 2^-s),
// half to even, from the 96-bit product m * 10^p (p <= 9): exact while q fits
// 64 bits, which covers |v| < 1e9 with 9 decimals.
#include "float_format.h"

#include <cstdint>
#include <cstring>

namespace ff {
namespace fm {

namespace {

constexpr int kMaxPrec = 9;   // exact decimals, more print as zeros
constexpr char kZeros[] = "0000000000000000";
constexpr char kSpaces[] = "                ";

// n characters from a 16-character run.
bool fill(FloatPut put, void* ctx, const char* run, int n) {
    for (; n > 0; n -= 16)
        if (!put(ctx, run, n < 16 ? n : 16)) return false;
    return true;
}

// *q = round(m * p10 * 2^-s) for s > 0, half to even, false if q passes 64 bits.
bool scaled(uint64_t m, uint32_t p10, int s, uint64_t* q) {
    // m * p10 = hi * 2^32 + lo, hi < 2^52.
    const uint64_t a = (m & 0xffffffffu) * p10;
    const uint64_t hi = (m >> 32) * p10 + (a >> 32);
    const uint32_t lo = uint32_t(a);
    if (s >= 96) {   // the product is < 2^84: below one half
        *q = 0;
        return true;
    }
    uint64_t v;
    bool above, tie;
    if (s >= 32) {
        // Remainder (rHi, lo) against half = 2^(s-1).
        const int t = s - 32;
        v = hi >> t;
        const uint64_t rHi = hi & ((uint64_t(1) << t) - 1);
        const uint64_t hHi = t ? uint64_t(1) << (t - 1) : 0;
        const uint32_t hLo = t ? 0 : 0x80000000u;
        above = rHi > hHi || (rHi == hHi && lo > hLo);
        tie = rHi == hHi && lo == hLo;
    } else {
        if (hi >> (32 + s)) return false;
        v = (hi << (32 - s)) | (lo >> s);
        const uint32_t r = lo & ((uint32_t(1) << s) - 1), half = uint32_t(1) << (s - 1);
        above = r > half;
        tie = r == half;
    }
    *q = v + (above || (tie && (v & 1)));
    return true;
}

}  // namespace

int formatFloat(double v, const FloatSpec& sp, FloatPut put, void* ctx) {
    uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    const bool neg = bits >> 63;
    const int ex = int((bits >> 52) & 0x7ff);
    uint64_t m = bits & ((uint64_t(1) << 52) - 1);
    const bool upper = sp.conv >= 'A' && sp.conv <= 'Z';
    const char sign = neg ? '-' : sp.plus ? '+' : sp.space ? ' ' : 0;
    const int prec = sp.prec < 0 ? 6 : sp.prec;

    // intDigits, intZeros, '.', fracDigits, fracZeros
    char digits[24];
    int n = 0, p = 0, intZeros = 0;
    const bool finite = ex != 0x7ff;
    if (!finite) {
        std::memcpy(digits, m ? (upper ? "NAN" : "nan") : (upper ? "INF" : "inf"), 3);
        n = 3;
    } else {
        int s = 1075 - ex;
        if (ex) m |= uint64_t(1) << 52;
        else s = 1074;
        uint64_t q;
        if (s <= 0) {
            // An integer, above 2^64 its first 19 digits come from doubles.
            if (-s <= 11) {
                q = m << -s;
            } else {
                double x = neg ? -v : v;
                while (x >= 1e19) x /= 10, ++intZeros;
                q = uint64_t(x + 0.5);
            }
        } else {
            p = prec < kMaxPrec ? prec : kMaxPrec;
            uint32_t p10 = 1;
            for (int i = 0; i < p; ++i) p10 *= 10;
            while (!scaled(m, p10, s, &q)) p10 /= 10, --p;   // |v| >= 1e9: fewer exact decimals
        }
        char d[24];
        int k = 0;
        do d[k++] = char('0' + q % 10), q /= 10;
        while (q || k <= p);
        while (k) digits[n++] = d[--k];
    }
    const int fracZeros = finite ? prec - p : 0;
    const bool point = finite && (prec > 0 || sp.alt);
    const int intLen = n - p;

    const int total = n + intZeros + fracZeros + point + (sign ? 1 : 0);
    const int pad = sp.width > total ? sp.width - total : 0;
    const bool zeroPad = sp.zero && !sp.left && finite;
    if (!sp.left && !zeroPad && !fill(put, ctx, kSpaces, pad)) return -1;
    if (sign && !put(ctx, &sign, 1)) return -1;
    if (zeroPad && !fill(put, ctx, kZeros, pad)) return -1;
    if (!put(ctx, digits, intLen) || !fill(put, ctx, kZeros, intZeros)) return -1;
    if (point && !put(ctx, ".", 1)) return -1;
    if (p && !put(ctx, digits + intLen, p)) return -1;
    if (!fill(put, ctx, kZeros, fracZeros)) return -1;
    if (sp.left && !fill(put, ctx, kSpaces, pad)) return -1;
    return total + pad;
}

}  // namespace fm
}  // namespace ff
