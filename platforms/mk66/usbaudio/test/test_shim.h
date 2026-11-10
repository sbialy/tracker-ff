// Minimal test macros for usb_audio_out_test.cpp (tracker-ff, host only).
#pragma once
#include <cstdio>
#include <cstdlib>

struct TestCase { const char* name; void (*fn)(); TestCase* next; };
inline TestCase*& testHead() { static TestCase* h = nullptr; return h; }
inline int& testFailures() { static int f = 0; return f; }
struct TestReg { TestReg(TestCase* t) { t->next = testHead(); testHead() = t; } };

#define TEST(name)                                         \
    static void test_##name();                             \
    static TestCase tc_##name = {#name, test_##name, nullptr}; \
    static TestReg tr_##name(&tc_##name);                  \
    static void test_##name()
#define CHECK(c)                                                                     \
    do { if (!(c)) { std::printf("  FAIL %s:%d %s\n", __FILE__, __LINE__, #c); ++testFailures(); } } while (0)
#define CHECK_EQ(a, b)                                                                                   \
    do { if (!((a) == (b))) { std::printf("  FAIL %s:%d %s == %s (%lld vs %lld)\n", __FILE__, __LINE__, #a, #b, \
         (long long)(a), (long long)(b)); ++testFailures(); } } while (0)

int main()
{
    int n = 0;
    for (TestCase* t = testHead(); t; t = t->next, ++n)
    {
        const int before = testFailures();
        t->fn();
        std::printf("%s %s\n", testFailures() == before ? "ok  " : "FAIL", t->name);
    }
    std::printf("%d tests, %d failed checks\n", n, testFailures());
    return testFailures() ? 1 : 0;
}
