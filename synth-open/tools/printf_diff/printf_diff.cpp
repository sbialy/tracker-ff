// printf_diff: the device's snprintf, run by the Python driver under a Cortex-M4F emulator (run_printf_diff.sh).
// The driver writes the cases (a double, a format string, a buffer size) into RAM and calls run(). Built twice: with newlib-nano's own
// float printf (-u _printf_float, the "old" link) and with the hook of platforms/mk66/source/printf_float.cpp (the "new" link).
// Every case also passes the string "X" as the second argument, which formats like "%.0f%s" read and the others ignore.
#include <cstddef>
#include <cstdint>
#include <cstdio>

struct Case {
    double v;
    uint32_t fmt;    // address of the format string
    uint32_t size;   // snprintf buffer size, 0 = sprintf (no bound)
};

struct Out {
    int32_t ret;
    char s[348];
};

extern "C" void* _sbrk(ptrdiff_t n) {   // the old link's dtoa mallocs
    static char heap[96 * 1024];
    static size_t used;
    if (used + size_t(n) > sizeof heap) return reinterpret_cast<void*>(-1);
    void* p = heap + used;
    used += size_t(n);
    return p;
}

// What newlib's stdio reaches for and nothing here calls (the build is -nostartfiles, no libnosys)
extern "C" {
void _exit(int) { for (;;) {} }
int _close(int) { return -1; }
int _fstat(int, void*) { return -1; }
int _getpid() { return 1; }
int _isatty(int) { return 0; }
int _kill(int, int) { return -1; }
long _lseek(int, long, int) { return -1; }
long _read(int, void*, size_t) { return -1; }
long _write(int, const void*, size_t) { return -1; }
}

extern "C" int run(const Case* cases, int n, Out* out) {
    for (int i = 0; i < n; ++i) {
        const char* fmt = reinterpret_cast<const char*>(cases[i].fmt);
        for (char& c : out[i].s) c = 0x7f;   // a missing terminator shows up in the diff
        out[i].ret = cases[i].size ? snprintf(out[i].s, cases[i].size < sizeof out[i].s ? cases[i].size : sizeof out[i].s, fmt, cases[i].v, "X")
                                   : sprintf(out[i].s, fmt, cases[i].v, "X");
    }
    return n;
}
