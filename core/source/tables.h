// Tables (Table lite): the firmware side of core/source/tables_format.h. The pool and the per track runners live in external RAM
// (SDRAM), the runner itself is Sequencer::table* (mtSequencer.cpp). Only in builds with the plugin host (project.mtff stores the
// tables, core/FileManager/mtff_codec.h).
#pragma once

#include "tables_format.h"

namespace tables
{

// The project's tables and bindings: project.mtff reads into it, the sequencer plays from it
Pool& pool();
// One track's runner
struct Run
{
    uint8_t table;   // 1..16 while a table runs or holds (Once, last row), 0 none
    uint8_t row;     // the row that sounds
    uint8_t timer;   // uSteps since that row started
    uint8_t instr;   // the instrument of the note the table runs on
    uint8_t rel;     // a live note (pad, MIDI in) was released: the table ends when its voice has gone silent (stopped sequencer)
    int8_t semis;    // what the voice has now: transpose, kNoteEmpty = forced apply on the next row (synth)
    uint8_t vol;     // ... volume in percent, kVolEmpty = forced apply
    uint8_t fx;      // the table put an FX on its slot (to end on a stop)
};
Run& run(int track);
// Bit t: track t has a table that advances. Read on every uStep of every track, so it sits in internal RAM (2 B)
extern uint16_t g_runMask;
// Pool and runners empty: boot (the SDRAM is not cleared) and a project without tables
void init();
// Sample tracks: the Tune FX of the table's transpose and the table's FX go to the player slots of ranks 3 and 4 (FX3 and FX4, shared with
// the 4 FX project switch: the last one written wins). Synth parts: the FX slot of rank 4
constexpr uint8_t kTuneSlot = 2;
constexpr uint8_t kFxSlot = 3;
// The FX a table row may hold: the ones the sample player or the synth part act on mid-note. Sequencer FX (Tempo, Roll, Arp, Chance,
// Break, MIDI Chord, Strum, Patch, Morph, Tape, Snap, TBL ...) are ignored in a row
bool rowFxAllowed(uint8_t type);

#ifdef TABLE_DWT
// Measurement only (build with CUSTOM_DEFINES "... -DTABLE_DWT", off in every release image): DWT->CYCCNT cycles (180 MHz, 5.56 ns) at entry and
// exit of tableAdvance, tableApply and tableLiveNoteOn. One block in SDRAM (tables::g_dwt), read with the console peek. Per function: the
// call count, the cycle sum, the longest call and the longest sum of one audio block. Writing 0 to the magic zeroes it at the next block
struct DwtStamp
{
    uint32_t count, sum, max, blockSum, blockMax;
};
struct DwtBlock
{
    uint32_t magic;       // 0x54445754 "TDWT" when the block is valid
    uint32_t blocks;      // audio blocks seen since the reset
    DwtStamp advance, apply, live;
};
extern DwtBlock g_dwt;
#endif

} // namespace tables
