// Host driver of fxdesc_test.sh: the firmware's own interfaceFxDescZ.cpp and inflate.cpp, compiled natively with FW_TRIM and
// FEATURE_SYNTH_OPEN. Reads the expected texts (FX_COUNT texts, each with its end 0, made by fxdesc_dump from the header table) from
// argv[1] and compares fxDescZText(id) for every id, plus the ids around the range.
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <vector>

#include "NewDisplay/inflate.h"
#include "interfaceFxDescZ.h"
#include "mtStructs.h"

InflateScratch gInflateScratch;

int main(int, char** argv)
{
    FILE* f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    std::vector<char> exp(20000);
    size_t n = fread(exp.data(), 1, exp.size(), f);
    fclose(f);
    int bad = 0;
    if (fxDescZText(-1) != nullptr || fxDescZText(static_cast<int16_t>(FX_COUNT)) != nullptr || fxDescZText(1000) != nullptr)
    {
        printf("FAIL: out of range ids must give nullptr\n");
        bad++;
    }
    const char* p = exp.data();
    const char* first = nullptr;
    for (int i = 0; i < static_cast<int>(FX_COUNT); i++)
    {
        const char* got = fxDescZText(static_cast<int16_t>(i));
        if (!got || strcmp(got, p) != 0)
        {
            printf("FAIL: text %d differs\n", i);
            bad++;
        }
        if (i == 0)
            first = got;
        p += strlen(p) + 1;
    }
    if (static_cast<size_t>(p - exp.data()) != n)
    {
        printf("FAIL: the table has %zu B, the walk used %zu\n", n, static_cast<size_t>(p - exp.data()));
        bad++;
    }
    if (fxDescZText(0) != first)
    {
        printf("FAIL: pointer moved\n");
        bad++;
    }
    printf("fxdesc_check: %d texts, %d bad\n", static_cast<int>(FX_COUNT), bad);
    return bad != 0;
}
