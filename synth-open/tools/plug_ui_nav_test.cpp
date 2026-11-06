// Host test of the Inserts page list index logic (synth-open/plug_ui_nav.h): arrow direction against the stock master screen,
// clamping at both ends, the repeat-press wrap, and that no step ever leaves [0, n).
//
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_ui_nav_test.cpp -o /tmp/plug_ui_nav_test && /tmp/plug_ui_nav_test
#include "plug_ui_nav.h"

#include <cstdio>
#include <cstring>

namespace
{
int g_fail = 0;
void check(bool ok, const char* what)
{
    printf("%s  %s\n", ok ? "ok  " : "FAIL", what);
    if (!ok) g_fail++;
}
}   // namespace

int main()
{
    using namespace plugnav;
    // stock fMasterParams.cpp: functDown -> changeSaturationType(1), functUp -> changeSaturationType(-1),
    // functUp -> changeBassBoost(1)
    check(arrowDelta(kDown, true) == 1, "list: Down is the next entry below (+1)");
    check(arrowDelta(kUp, true) == -1, "list: Up is the entry above (-1)");
    check(arrowDelta(kUp, false) == 1, "number: Up raises (+1)");
    check(arrowDelta(kDown, false) == -1, "number: Down lowers (-1)");
    check(encoderDelta(3, true, false) == 3 && encoderDelta(-2, true, false) == -2, "encoder on a list, original: as turned");
    check(encoderDelta(3, true, true) == -3, "encoder on a list, Mini (invertValForList): negated");
    check(encoderDelta(3, false, true) == 3, "encoder on a number: never negated");

    // the plugin list: None + 7 plugins
    const int n = 8;
    int pos = 0;
    for (int i = 0; i < 20; i++) pos = clampStep(pos, n, arrowDelta(kDown, true));
    check(pos == n - 1, "20 x Down from None stops on the last plugin");
    for (int i = 0; i < 20; i++) pos = clampStep(pos, n, arrowDelta(kUp, true));
    check(pos == 0, "20 x Up stops on None");
    check(clampStep(0, n, -100) == 0 && clampStep(3, n, 100) == n - 1, "big encoder steps clamp");
    check(clampStep(0, 0, 1) == 0 && clampStep(5, 1, 1) == 0, "empty and one-entry lists stay at 0");

    // repeat press: next entry, the first after the last
    check(nextWrap(0, 3) == 1 && nextWrap(1, 3) == 2 && nextWrap(2, 3) == 0, "Mixer (3 entries) wraps 2 -> 0");
    check(nextWrap(0, 2) == 1 && nextWrap(1, 2) == 0, "two-entry list toggles");
    check(nextWrap(0, 1) == 0 && nextWrap(0, 0) == 0, "one and zero entries stay at 0");
    check(nextWrap(-1, 4) == 0 && nextWrap(9, 4) == 0, "out of range position restarts at the first");
    {
        int p = 0, seen = 0;
        for (int i = 0; i < 4; i++)
        {
            p = nextWrap(p, 4);
            seen |= 1 << p;
        }
        check(p == 0 && seen == 0xF, "4 presses on a 4-entry enum visit every entry and come back");
    }

    // bounds over every list size up to LIST_LENGTH_MAX (50) and every start
    bool inRange = true;
    for (int len = 1; len <= 50; len++)
        for (int p = 0; p < len; p++)
            for (int d = -60; d <= 60; d++)
            {
                const int a = clampStep(p, len, d), b = nextWrap(p, len);
                inRange = inRange && a >= 0 && a < len && b >= 0 && b < len;
            }
    check(inRange, "clampStep and nextWrap stay inside [0, n) for n 1..50");

    // column order: Mixer, Insert 1, Insert 2, parameters, Back
    check(kMixerCol == 0 && colOfInsert(0) == 1 && colOfInsert(1) == 2 && kParamCol0 == 3 && kBackCol == 7,
          "columns: Mixer 1, Insert 1 2, Insert 2 3, parameters 4..7, Back 8 (1 based)");
    check(insertOfCol(1) == 0 && insertOfCol(2) == 1 && insertOfCol(0) == -1 && insertOfCol(3) == -1 && insertOfCol(7) == -1,
          "insertOfCol: only columns 1 and 2 are plugin lists");
    check(kParamCol0 + kParamCols == kBackCol && kParamCol0 == colOfInsert(1) + 1, "parameters right after Insert 2, Back after them");

    // insert column value line: every status with the longest name (kNameLen 15) fits kLabelChars
    {
        const Shown all[] = {Shown::kNone, Shown::kLoading, Shown::kName, Shown::kBypassed,
                             Shown::kMissing, Shown::kFailed, Shown::kSafe, Shown::kOverCpu};
        const char* names[] = {"", "BBD", "Phaser", "Console", "ABCDEFGHIJKLMNO"};
        bool fit = true;
        for (Shown st : all)
            for (const char* nm : names)
            {
                char out[32];
                memset(out, 'x', sizeof(out));
                slotLabel(st, nm, out);
                const size_t len = strlen(out);
                fit = fit && len <= size_t(kLabelChars) && out[kLabelChars + 1] == 'x';
                printf("      %-24s -> \"%s\" (%zu)\n", nm, out, len);
            }
        check(fit, "every insert status label is at most 9 characters and writes at most 10 bytes");
        char out[16];
        slotLabel(Shown::kSafe, "BBD", out);
        check(strcmp(out, "Safe:BBD") == 0, "safe mode with BBD reads Safe:BBD");
        slotLabel(Shown::kMissing, "Phaser", out);
        check(strcmp(out, "Miss:Phas") == 0, "missing Phaser reads Miss:Phas");
        slotLabel(Shown::kName, "Console", out);
        check(strcmp(out, "Console") == 0, "running: the plugin name");
        slotLabel(Shown::kBypassed, "Console", out);
        check(strcmp(out, "Bypassed") == 0, "bypassed: the status word only (the list highlights the plugin)");
    }

    printf("%s\n", g_fail ? "FAILED" : "all ok");
    return g_fail ? 1 : 0;
}
