// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit 21b691c62af8,
// src/core/float_format.h), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): namespace ff::fm instead of ot::fm, the comments are ours (tracker-ff).
//
// printf's %f without newlib's float printf (dtoa + mprec, several KB of flash). TRIM images (newlib-nano) plug this into
// nano's weak _printf_float hook (printf_float.cpp), so snprintf("%.2f") prints the same characters as before.
//
// %f / %F print the exact binary value rounded half to even, as glibc and newlib do, for |v| < 1e9 with up to 9 decimals
// (synth-open/tools/printf_diff). Further decimals print as zeros. %e / %g (unused in the firmware) print as %f with the
// same precision, which is NOT what newlib prints: never add one to the code.
#pragma once

namespace ff {
namespace fm {

struct FloatSpec {
    char conv = 'f';     // f F (e E g G print as f F)
    int prec = -1;       // -1: the default 6
    int width = 0;
    bool left = false;   // '-'
    bool zero = false;   // '0'
    bool plus = false;   // '+'
    bool space = false;  // ' '
    bool alt = false;    // '#'
};

// Writes the conversion through put (false = stop). Returns the number of
// characters, or -1 when put failed.
using FloatPut = bool (*)(void* ctx, const char* s, int n);
int formatFloat(double v, const FloatSpec& spec, FloatPut put, void* ctx);

}  // namespace fm
}  // namespace ff
