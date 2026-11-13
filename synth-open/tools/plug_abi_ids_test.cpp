// Host test of the extension ids of the plugin ABI (synth-open/plug/plug_abi.h, tracker-ff). NOT firmware.
//   c++ -std=c++20 -O1 -Wall -I synth-open synth-open/tools/plug_abi_ids_test.cpp -o plug_abi_ids_test
// The ids are a contract with plugins already on cards and with the other ABI line: the host level 2 line owns
// PLUG_EXT_FILES = 5 (asked through plug_host.query). PLUG_EXT_CHORD sat on 5 too until 2026-11-12 and lives on 6 now, so one tree
// can carry both lines. Every id must be distinct and none may take a reserved value of the other line.
#include <cstdio>

#include "plug/plug_abi.h"

namespace
{
int g_fail = 0, g_checks = 0;
void check(bool ok, const char* what)
{
    g_checks++;
    if (!ok)
    {
        g_fail++;
        printf("  FAIL  %s\n", what);
    }
}
}

int main()
{
    const unsigned ids[] = { PLUG_EXT_STATE, PLUG_EXT_TAIL, PLUG_EXT_SYNTH, PLUG_EXT_TEMPO, PLUG_EXT_CHORD };
    const char* names[] = { "STATE", "TAIL", "SYNTH", "TEMPO", "CHORD" };
    const unsigned n = sizeof(ids) / sizeof(ids[0]);
    for (unsigned i = 0; i < n; i++)
    {
        for (unsigned j = i + 1; j < n; j++)
        {
            char msg[96];
            snprintf(msg, sizeof(msg), "PLUG_EXT_%s and PLUG_EXT_%s differ", names[i], names[j]);
            check(ids[i] != ids[j], msg);
        }
    }
    check(PLUG_EXT_STATE == 1 && PLUG_EXT_TAIL == 2 && PLUG_EXT_SYNTH == 3 && PLUG_EXT_TEMPO == 4, "ids 1 to 4 are the 1.0 and 1.1 values");
    check(PLUG_EXT_CHORD != 5, "PLUG_EXT_CHORD does not sit on 5 (PLUG_EXT_FILES of the host level 2 line)");
    check(PLUG_EXT_CHORD == 6, "PLUG_EXT_CHORD is 6");
    printf("plug_abi_ids_test: %d checks, %d failed\n", g_checks, g_fail);
    return g_fail ? 1 : 0;
}
