// Host test of platforms/mk66/3rdparty/teensy_core/new_arena.h (the SDRAM arena behind operator new).
//   clang++ -std=c++17 -O1 -g -fsanitize=address,undefined -I platforms/mk66/3rdparty/teensy_core synth-open/tools/new_arena_test.cpp -o /tmp/new_arena_test && /tmp/new_arena_test
#include "new_arena.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

static int g_fail;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL line %d: %s\n", __LINE__, #c); g_fail++; } } while (0)

int main()
{
    alignas(8) static uint8_t buf[49152];
    NewArena a;
    CHECK(!a.ready() && a.alloc(16) == nullptr);
    a.init(buf, sizeof(buf));
    const size_t total = a.freeBytes();
    CHECK(total == sizeof(buf) - 8);
    // one big block, then everything, then nothing
    void* p = a.alloc(4704);
    CHECK(p && a.owns(p) && reinterpret_cast<uintptr_t>(p) % 8 == 0);
    CHECK(a.freeBytes() == total - 4704 - 8);
    void* all = a.alloc(total - 4704 - 8 - 8);
    CHECK(all != nullptr);
    CHECK(a.alloc(8) == nullptr);
    a.free(all);
    a.free(p);
    CHECK(a.freeBytes() == total);                 // merged back into one block
    CHECK(!a.owns(&total));
    // random stress: contents survive, blocks never overlap, the arena returns to one block
    struct Blk { uint8_t* p; size_t n; uint8_t tag; };
    std::vector<Blk> live;
    unsigned seed = 12345;
    auto rnd = [&]() { seed = seed * 1103515245u + 12345u; return seed >> 8; };
    for (int i = 0; i < 200000; i++)
    {
        if (live.empty() || (rnd() % 100 < 55 && live.size() < 60))
        {
            size_t n = 1024 + rnd() % 5000;
            uint8_t* q = static_cast<uint8_t*>(a.alloc(n));
            if (q)
            {
                uint8_t tag = uint8_t(rnd());
                std::memset(q, tag, n);
                live.push_back({ q, n, tag });
            }
        }
        else
        {
            size_t k = rnd() % live.size();
            Blk b = live[k];
            for (size_t j = 0; j < b.n; j += 97) CHECK(b.p[j] == b.tag);
            a.free(b.p);
            live[k] = live.back();
            live.pop_back();
        }
    }
    for (auto& b : live) a.free(b.p);
    CHECK(a.freeBytes() == total);
    std::printf("new_arena_test: %s\n", g_fail ? "FAILED" : "all checks passed");
    return g_fail != 0;
}
