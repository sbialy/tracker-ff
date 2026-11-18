#include "Platform.h"
#include "string.h"
#include "util_math.h"
#include <mtRandomNameGenerator.h>
#include "NewDisplay/inflate.h"
#include "cr_section_macros.h"
#include "mtRandomNameTabs.h"

// The two word tables, "_word_word_..._" (1,006 words each, core/source/names/word{1,2}.txt), are stored deflated
// (mtRandomNameTabs.cpp, synth-open/tools/randname_deflate.py) and inflated into SDRAM the first time a name is asked for
__NOINIT(EXTERNAL_RAM) static char randomNameWord1Tab[kRandomNameWord1TabSize];
__NOINIT(EXTERNAL_RAM) static char randomNameWord2Tab[kRandomNameWord2TabSize];

static bool inflateRandomNameTab(const uint8_t* deflated, size_t deflatedSize, char* tab, uint32_t tabSize)
{
    int n = inflateRaw(deflated, deflatedSize, reinterpret_cast<uint8_t*>(tab), tabSize - 1, &gInflateScratch);
    if (n != static_cast<int>(tabSize) - 1) return false;
    tab[n] = 0;
    return true;
}

mtRandomNameGenerator randomNameGenerator;

const char* error = "undefined error";

const char* mtRandomNameGenerator::getRandomName(Timer* timer)
{
    if (!tabsReady)
    {
        tabsReady = inflateRandomNameTab(randomNameWord1TabDeflated, sizeof(randomNameWord1TabDeflated), randomNameWord1Tab,
                                         kRandomNameWord1TabSize) &&
                    inflateRandomNameTab(randomNameWord2TabDeflated, sizeof(randomNameWord2TabDeflated), randomNameWord2Tab,
                                         kRandomNameWord2TabSize);
        if (!tabsReady) return error;
    }

    utilMathRandom::randomSeed(timer->micros());
    uint16_t rand1 = utilMathRandom::random(RANDOM_NAME_TABS_SIZE - 1);
    uint16_t rand2 = utilMathRandom::random(RANDOM_NAME_TABS_SIZE - 1);

    bool optional = utilMathRandom::random(10) == 0;

    const char* table1 = randomNameWord1Tab;
    const char* table2 = randomNameWord2Tab;

    char* word_start = nullptr;
    char* word_end = nullptr;
    int word_len;

    char word1[50];
    char word2[50];

    if (optional)
        table1 = table2;

    //--------------------------------------
    word_start = (char*)table1;

    for (int num = 0; num < rand1; num++)
    {
        word_start = strchr(word_start, '_') + 1;

        if (word_start == nullptr)
        {
            return error;
        }
    }

    word_end = strchr(word_start, '_');

    if (word_end == nullptr)
    {
        return error;
    }

    word_len = word_end - word_start;

    strncpy(word1, word_start, 49);
    word1[word_len] = 0;

    //--------------------------------------
    word_start = (char*)table2;

    for (int num = 0; num < rand2; num++)
    {
        word_start = strchr(word_start, '_') + 1;

        if (word_start == nullptr)
        {
            return error;
        }
    }

    word_end = strchr(word_start, '_');

    if (word_end == nullptr)
    {
        return error;
    }

    word_len = word_end - word_start;

    strncpy(word2, word_start, 49);
    word2[word_len] = 0;

    //--------------------------------------

    if (!optional)
    {
        // names like "nonchalant cheese"
        sprintf(randomName, "%s %s", word1, word2);
    }
    else
    {
        // names like "cake-burn"=
        sprintf(randomName, "%s-%s", word1, word2);
    }

    return randomName;
}
