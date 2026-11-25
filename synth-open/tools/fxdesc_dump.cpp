// Host driver of fxdesc_deflate.py and fxdesc_test.sh: writes the FX_COUNT texts of the fx description table of
// core/interface/core/interfaceFxDesc.h to the file in argv[1], each followed by its end 0. Build it WITHOUT FW_TRIM, so the header has the table.
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "interfaceDefs.h"

int main(int, char** argv)
{
    FILE* f = fopen(argv[1], "wb");
    if (!f)
        return 1;
    for (int i = 0; i < static_cast<int>(FX_COUNT); i++)
        fwrite(fxDescriptions[i], 1, strlen(fxDescriptions[i]) + 1, f);
    fclose(f);
    printf("FX_COUNT %d\n", static_cast<int>(FX_COUNT));
    return 0;
}
