// IMX port P7 H6: the run time engine list of the stock synth path (core/synth/synth_types.h). Built-ins under stored 0..6, plugin
// engines by stored number after them, CHIP's 6 and taken numbers or types refused, Missing for unknown plugin numbers and for a listed
// plugin that is not attached, names and "#n".
#include "synth_types.h"

#include <cstdio>
#include <cstring>

static int fails = 0;
#define CHECK(c)                                                         \
    do                                                                   \
    {                                                                    \
        if (!(c))                                                        \
        {                                                                \
            std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #c);      \
            fails++;                                                     \
        }                                                                \
    } while (0)

int main()
{
    // the stock InstrumentTypeId values of ACD FAT VAP WTFM PERC DIRT CHIP are not needed here, any distinct numbers do
    const int32_t types[synthtypes::kBuiltins] = { 1, 2, 3, 4, 5, 6, 8 };
    const char* const names[synthtypes::kBuiltins] = { "ACD", "FAT", "VAP", "WTFM", "PERC", "DIRT", "CHIP" };
    synthtypes::List l;
    l.reset(types, names);
    CHECK(l.count() == 7);
    for (int s = 0; s < 7; s++)
    {
        CHECK(l.uiForStored(s) == s);
        CHECK(l.at(s).builtin == s);
        CHECK(!l.missing(s));
    }
    // CHIP plugin (stored 6, D10) and a plugin with a built-in's type are refused
    CHECK(!l.add(6, 0x8A, "CHIP", true));
    CHECK(!l.add(9, 8, "X", true));
    // plugins, added out of order, listed by stored number after the built-ins
    CHECK(l.add(12, 0x85, "8-Type", true));
    CHECK(l.add(7, 0x81, "JU6", false));
    CHECK(l.add(9, 0x80, "FM4", true));
    CHECK(!l.add(9, 0x99, "dup", true));     // stored taken
    CHECK(!l.add(30, 0x80, "dup", true));    // type taken
    CHECK(!l.add(256, 0x9A, "big", true));   // not one byte
    CHECK(!l.add(40, 0x9B, nullptr, true));
    CHECK(l.count() == 10);
    CHECK(l.at(7).stored == 7 && l.at(8).stored == 9 && l.at(9).stored == 12);
    CHECK(l.at(7).builtin == -1 && !std::strcmp(l.at(9).name, "8-Type"));
    // Missing: a listed plugin not attached, an unknown plugin number. Never a built-in. Every byte is a number a project may hold.
    CHECK(l.missing(7));
    CHECK(!l.missing(9));
    CHECK(l.missing(100));
    CHECK(!std::strcmp(l.name(100), "#100"));
    CHECK(!std::strcmp(l.name(7), "JU6"));
    CHECK(l.setAttached(7, true) && !l.missing(7));
    CHECK(!l.setAttached(3, false) && !l.missing(3));
    CHECK(synthtypes::List::storedValid(0) && synthtypes::List::storedValid(255));
    CHECK(!synthtypes::List::storedValid(-1) && !synthtypes::List::storedValid(256));
    // full list
    int added = 0;
    for (int s = 13; s <= 255; s++)
    {
        added += l.add(s, 0x1000 + s, "p", true) ? 1 : 0;
    }
    CHECK(l.count() == synthtypes::kMax);
    CHECK(added == synthtypes::kMax - 10);
    // reset drops the plugins
    l.reset(types, names);
    CHECK(l.count() == 7 && l.uiForStored(9) == -1 && l.missing(9));
    std::printf("synth_types_test: %s\n", fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
