#pragma once

// print static value to compilation console output
// more info here:
// https://stackoverflow.com/questions/28852574/how-to-print-result-of-a-compile-time-calculation-in-c
template<auto val>
constexpr void static_print()
{
#if !defined(__GNUC__) || defined(__clang__)
    [[maybe_unused]] int static_print_is_implemented_only_for_gcc = 0;
#else
    int unused = 0;

#endif
};
