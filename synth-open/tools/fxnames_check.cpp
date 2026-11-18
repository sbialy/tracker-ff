// Host driver of fxnames_test.sh: the strings of the fx name tables of core/interface/core/interfaceDefs.h. Prints FX_COUNT, then for
// every table entry the index, the name and the short name, as the screen shows them (both tables, the "Random Fx" entry first).
#include <stdint.h>
#include <stdio.h>

#include "interfaceDefs.h"

int main()
{
    printf("FX_COUNT %d\n", static_cast<int>(FX_COUNT));
    for (int i = 0; i <= static_cast<int>(FX_COUNT); i++)
        printf("%d|%s|%s\n", i, InterfaceGlobals::ptrAllFxNames[i], InterfaceGlobals::ptrAllFxNamesShort[i]);
    for (int i = 0; i < static_cast<int>(FX_COUNT); i++)
        printf("t%d|%s|%s\n", i, InterfaceGlobals::ptrFxNames[i], InterfaceGlobals::ptrAllFxNamesShort[i + 1]);
    return 0;
}
