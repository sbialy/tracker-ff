#ifndef _PLAYMODEDEFS_H_
#define _PLAYMODEDEFS_H_

#include <stdint.h>

struct strPlayOrderWithEffects
{
    uint8_t stepNumberToPlay;
    uint8_t fxOption;
};

enum playOrderFxOptions
{
    playModeFx_none,

    playModeFx_rollFlat_1_4_P30,
    playModeFx_rollFlat_1_4_P60,
    playModeFx_rollFlat_1_4_P100,

    playModeFx_rollDecVelo_1_4_P30,
    playModeFx_rollDecVelo_1_4_P60,
    playModeFx_rollDecVelo_1_4_P100,

    playModeFx_rollDecVelo_1_2_P30,
    playModeFx_rollDecVelo_1_2_P60,
    playModeFx_rollDecVelo_1_2_P100,

    playModeFx_rollFlatVelo_1_1_P30,
    playModeFx_rollFlatVelo_1_1_P60,
    playModeFx_rollFlatVelo_1_1_P100,

    playModeFx_rollDecVelo_1_1_P30,
    playModeFx_rollDecVelo_1_1_P60,
    playModeFx_rollDecVelo_1_1_P100,

};
const uint16_t playOrdersTotal = 20;
const uint16_t playOrderWithFxLength = 128;

// The 20 custom play orders (CS1..CS20). Stored deflated in flash (playModeTabs.cpp, source names/playorder.txt) and inflated into SDRAM
// by playOrderInit(), which Sequencer::init calls once (the main loop, before the first tick reads the table)
extern strPlayOrderWithEffects playOrderWithFx[playOrdersTotal][playOrderWithFxLength];
bool playOrderInit();

#endif
