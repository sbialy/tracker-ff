// Ported from OpenTracker (https://github.com/syamajala/polyend-tracker-re, commit 21b691c62af8,
// src/hal/printf_float.cpp), Apache-2.0, see licenses/LICENSE-OpenTracker-Apache-2.0.txt.
// Changed from the original (Apache-2.0 section 4b): compiled only into TRIM images (-DFW_TRIM, the newlib-nano link),
// namespace ff::fm, the comments are ours (tracker-ff).
//
// printf %f / %e / %g on the device. newlib-nano's printf hands every float conversion to _printf_float, which libc_nano.a
// defines weak (its own version needs dtoa + mprec and is linked only with -u _printf_float). This strong definition replaces
// it with ff::fm::formatFloat (float_format.h), so "%.2f" prints the number for our code and for modules alike (%e / %g print
// as %f, see float_format.h).
//
// The arguments follow newlib's private nano-vfprintf_local.h, only the leading fields of its per-conversion state are read.
// synth-open/tools/printf_diff runs the toolchain's own snprintf with this hook and with newlib's under a Cortex-M4
// emulator and diffs the two, so a newlib that moved the fields fails there.
#ifdef FW_TRIM
#include <cstdarg>
#include <cstdio>

#include "float_format.h"

// newlib-nano's state for one conversion (nano-vfprintf_local.h), the fields
// after `code` are not used here.
struct _prt_data_t {
    int flags;
    int prec;    // -1: none
    int dprec;
    int width;   // >= 0; a '-' or negative '*' width sets kLeft
    int size;
    int ret;
    char code;   // e f g E F G
};

namespace {

// nano-vfprintf_local.h flag bits: "#-0+ " in order.
constexpr int kAlt = 0x01, kLeft = 0x02, kZero = 0x04, kPlus = 0x08, kSpace = 0x10;

struct Sink {
    struct _reent* data;
    FILE* fp;
    int (*pfunc)(struct _reent*, FILE*, const char*, size_t);
};

bool put(void* ctx, const char* s, int n) {
    Sink* k = static_cast<Sink*>(ctx);
    return k->pfunc(k->data, k->fp, s, size_t(n)) != EOF;
}

}  // namespace

extern "C" int _printf_float(struct _reent* data, _prt_data_t* pd, FILE* fp,
                             int (*pfunc)(struct _reent*, FILE*, const char*, size_t), va_list* ap) {
    // long double is double on ARM EABI, so the LONGDBL flag reads the same.
    const double v = va_arg(*ap, double);
    ff::fm::FloatSpec s;
    s.conv = pd->code;
    s.prec = pd->prec;
    s.width = pd->width;
    s.alt = pd->flags & kAlt;
    s.left = pd->flags & kLeft;
    s.zero = pd->flags & kZero;
    s.plus = pd->flags & kPlus;
    s.space = pd->flags & kSpace;
    Sink k{data, fp, pfunc};
    return ff::fm::formatFloat(v, s, put, &k);
}
#endif  // FW_TRIM
