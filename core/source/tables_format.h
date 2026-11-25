// Tables (Table lite, M8 style): per instrument sub-step sequences of transpose, volume and one FX. Data model and the text codec of a
// table row set. Plain C++ with no firmware dependency, so synth-open/tools/plug_mtff_test.cpp runs this very code on the Mac.
//
// 16 tables of 16 rows. A row is N (transpose in semitones, or empty = 0), V (linear percent of the voice gain, or empty = 100)
// and one FX (an id of the step FX list and its value, 0 = none: the last FX of the table stays on). A table has one speed (tic: uSteps
// per row, 48 uSteps are one step, floor kTicMin), a length (rows played, 1..16) and Loop or Once. An instrument names one table
// (binding, 0 = none): every note of that instrument (sequenced, pads, MIDI in) starts it at row 0 on its track, each track keeps its own
// position. The TBL step FX (Sequencer FX_TYPE_TABLE) starts a table on its track with or without a note (0 stops it). An Off step does not
// stop a table: Loop plays on through the release, Once holds its last row. The next note, TBL 0 and Stop end it.
//
// Storage: project.mtff (core/FileManager/mtff_codec.h), lines "tbl.N <tic> <loop> <len> <hex rows>" and "instr.N.table T", written only when
// a table differs from the empty default or an instrument names a table. A project without them plays exactly as before.
#pragma once

#include <stdint.h>
#include <string.h>

namespace tables
{

constexpr int kTables = 16;
constexpr int kRows = 16;
constexpr int kInstruments = 67;          // 48 sample + 16 MIDI + 3 synth (mtStructs.h kTotalInstrumentsCount with FEATURE_SYNTH)
constexpr int8_t kNoteEmpty = -128;       // N column: no transpose
constexpr int8_t kNoteMin = -24;          // the Tune range of a sample voice (MIN_INSTRUMENT_TUNE)
constexpr int8_t kNoteMax = 24;
constexpr uint8_t kVolEmpty = 0xFF;       // V column: the note's own volume
constexpr uint8_t kVolMax = 100;          // percent
constexpr uint8_t kTicMin = 4;            // uSteps per row: 1/12 step, the CPU floor (ref/m8-tables-feasibility.md section 3)
constexpr uint8_t kTicMax = 192;          // 4 steps
constexpr uint8_t kTicDefault = 12;       // a quarter step (a 64th note)
constexpr int kRowBytes = 4;              // N, V, FX type, FX value: 8 hex digits in the file

struct Row
{
    int8_t note;      // -24..24 or kNoteEmpty
    uint8_t vol;      // 0..100 or kVolEmpty
    uint8_t fxType;   // step FX id, 0 = none
    uint8_t fxVal;
};
static_assert(sizeof(Row) == kRowBytes, "a row is the 4 bytes of the file, in this order");

struct Table
{
    Row row[kRows];
    uint8_t tic;    // uSteps per row, kTicMin..kTicMax
    uint8_t len;    // rows played, 1..kRows
    uint8_t loop;   // 1 Loop (back to row 0 after the last row), 0 Once (the last row holds)
    uint8_t spare;
};

struct Pool
{
    Table table[kTables];
    uint8_t bind[kInstruments];   // per instrument index: 0 none, 1..16 the table
};

inline void rowClear(Row& r)
{
    r.note = kNoteEmpty;
    r.vol = kVolEmpty;
    r.fxType = 0;
    r.fxVal = 0;
}
inline bool rowEmpty(const Row& r)
{
    return r.note == kNoteEmpty && r.vol == kVolEmpty && r.fxType == 0 && r.fxVal == 0;
}
inline void tableClear(Table& t)
{
    for (Row& r : t.row) rowClear(r);
    t.tic = kTicDefault;
    t.len = kRows;
    t.loop = 1;
    t.spare = 0;
}
// The table as a new project has it: written to no file
inline bool tableEmpty(const Table& t)
{
    if (t.tic != kTicDefault || t.len != kRows || t.loop != 1) return false;
    for (const Row& r : t.row)
        if (!rowEmpty(r)) return false;
    return true;
}
inline void poolClear(Pool& p)
{
    for (Table& t : p.table) tableClear(t);
    memset(p.bind, 0, sizeof(p.bind));
}
inline bool poolAny(const Pool& p)
{
    for (const Table& t : p.table)
        if (!tableEmpty(t)) return true;
    for (uint8_t b : p.bind)
        if (b) return true;
    return false;
}
inline uint8_t clampTic(uint32_t v)
{
    return uint8_t(v < kTicMin ? kTicMin : v > kTicMax ? kTicMax : v);
}
// A row as read from anywhere (file, editor): every field into its range
inline Row rowSanitize(Row r)
{
    if (r.note != kNoteEmpty) r.note = int8_t(r.note < kNoteMin ? kNoteMin : r.note > kNoteMax ? kNoteMax : r.note);
    if (r.vol != kVolEmpty && r.vol > kVolMax) r.vol = kVolMax;
    return r;
}
// Rows that the file needs: up to the last row that is not empty (the rest read back as empty)
inline int usedRows(const Table& t)
{
    int n = kRows;
    while (n > 0 && rowEmpty(t.row[n - 1])) n--;
    return n;
}
// The value of a TBL step FX: 0 stop, 1..16 the table
constexpr uint8_t kFxValueMax = kTables;

} // namespace tables
