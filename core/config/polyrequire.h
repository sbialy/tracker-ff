#pragma once

// POLY_REQUIRE(cond, value): an assert that survives NDEBUG. Both makefiles define -DNDEBUG and polyassert.h turns
// assert() into nothing, so an assert guards nothing in the shipped image. POLY_REQUIRE returns `value` from the
// enclosing function when cond is false (`POLY_REQUIRE(cond)` in a void function). No message, no printf string:
// about 6 to 10 B per use. Use it on index and null paths, not on hot per-sample loops.
// A DEBUG build still reports through assert() first.
#include "polyassert.h"

#define POLY_REQUIRE(cond, ...)                                                                    \
    do                                                                                             \
    {                                                                                              \
        assert(cond);                                                                              \
        if (__builtin_expect(!(cond), 0))                                                          \
        {                                                                                          \
            return __VA_ARGS__;                                                                    \
        }                                                                                          \
    } while (0)
