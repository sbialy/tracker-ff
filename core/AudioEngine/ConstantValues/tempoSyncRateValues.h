#ifndef AUDIOENGINE_TEMPOSYNCRATEVALUES_H_
#define AUDIOENGINE_TEMPOSYNCRATEVALUES_H_
#include "mtStructs.h"
inline const float tempoSyncRatesAmp[ENVELOPE_SPEED_AMP_COUNT] = {
    24,       16,    12,       8,      6,        4,        3,        2,
    1.5,      1,     0.75,     0.5,    0.375,    0.333333, 0.25,     0.1875,
    0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125,  0.020833, 0.015625
};

inline const float tempoSyncRatesOthers[ENVELOPE_SPEED_OTHER_COUNT] = {
    128,    96,       64,    48,       32,     24,       16,      12,       8,        6,
    4,      3,        2,     1.5,      1,      0.75,     0.5,     0.375,    0.333333, 0.25,
    0.1875, 0.166667, 0.125, 0.083333, 0.0625, 0.041667, 0.03125, 0.020833, 0.015625
};

#endif /* AUDIOENGINE_TEMPOSYNCRATEVALUES_H_ */
