#ifndef INTERFACE_INTERFACEFXDESCZ_H_
#define INTERFACE_INTERFACEFXDESCZ_H_

#include <stdint.h>

#if defined(FW_TRIM) && defined(FEATURE_SYNTH_OPEN)
// The text of the fx description popup of fx type `id` (0 .. FX_COUNT - 1), or nullptr for an id out of range or a damaged stream. The
// 52 texts are one deflated blob (interfaceFxDescZData.h), inflated into SDRAM on the first call, the pointer stays valid for ever.
const char* fxDescZText(int16_t id);
#endif

#endif /* INTERFACE_INTERFACEFXDESCZ_H_ */
