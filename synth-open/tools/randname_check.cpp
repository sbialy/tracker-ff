// Host driver of randname_test.sh: the firmware's own mtRandomNameGenerator.cpp and util_math.cpp, natively, with a fake Timer.
//   randname_check N      prints getRandomName() for the timer seeds 1000003*k + 17, k = 0..N-1 (what the golden file holds)
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "Platform.h"
#include "mtRandomNameGenerator.h"

struct FakeTimer : Timer
{
    uint32_t us = 0;
    uint32_t millis() override { return us / 1000; }
    uint32_t micros() override { return us; }
    void delay(int) override {}
    uint32_t clockCycles() override { return 0; }
    uint32_t clockCyclesPerUs() override { return 180; }
};

int main(int argc, char** argv)
{
    int n = argc > 1 ? atoi(argv[1]) : 2000;
    FakeTimer t;
    for (int k = 0; k < n; k++)
    {
        t.us = 1000003u * static_cast<uint32_t>(k) + 17u;
        printf("%s\n", randomNameGenerator.getRandomName(&t));
    }
    return 0;
}
