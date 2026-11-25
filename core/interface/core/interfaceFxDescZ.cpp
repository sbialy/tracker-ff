// The fx description texts of the release image, deflated. See interfaceFxDesc.h and synth-open/tools/fxdesc_deflate.py.
#if defined(FW_TRIM) && defined(FEATURE_SYNTH_OPEN)
#include "interfaceFxDescZ.h"

#include <string.h>

#include <cr_section_macros.h>

#include "NewDisplay/inflate.h"
#include "mtStructs.h"

#include "interfaceFxDescZData.h"

namespace
{
// FX_COUNT texts, each with its end 0, SDRAM (the same pool as the fonts, no static constructor)
__NOINIT(EXTERNAL_RAM) char s_text[kFxDescRawSize];
uint8_t s_state; // 0 not inflated yet, 1 ready, 2 damaged stream (the popup shows no text)
} // namespace

const char* fxDescZText(int16_t id)
{
    if (id < 0 || id >= static_cast<int16_t>(FX_COUNT))
        return nullptr;
    if (s_state == 0)
    {
        s_state = inflateRaw(kFxDescZ, sizeof(kFxDescZ), reinterpret_cast<uint8_t*>(s_text), sizeof(s_text), &gInflateScratch) ==
                      static_cast<int>(kFxDescRawSize)
                    ? 1
                    : 2;
    }
    if (s_state != 1)
        return nullptr;
    const char* p = s_text;
    for (int16_t i = 0; i < id; i++)
        p += strlen(p) + 1;
    return p;
}
#endif
