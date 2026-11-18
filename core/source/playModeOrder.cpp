#include "playModeDefs.h"

#include "NewDisplay/inflate.h"
#include "cr_section_macros.h"
#include "playModeTabs.h"

// The custom play orders in SDRAM (no static constructor may touch it), filled by playOrderInit()
__NOINIT(EXTERNAL_RAM) strPlayOrderWithEffects playOrderWithFx[playOrdersTotal][playOrderWithFxLength];

static_assert(kPlayOrderBytes == sizeof(playOrderWithFx), "playorder.txt does not fit the table");

bool playOrderInit()
{
    uint8_t* const dst = reinterpret_cast<uint8_t*>(&playOrderWithFx[0][0]);
    return inflateRaw(playOrderDeflated, sizeof(playOrderDeflated), dst, sizeof(playOrderWithFx), &gInflateScratch) ==
           static_cast<int>(kPlayOrderBytes);
}
