// The Scale file row of Project Settings > Tuning (microtuning phase 2): the list of /Tunings/*.scl, loading one as the project tuning.
// The parser is core/source/mtScala.h, the tuning itself core/source/mtTuning.h. Every buffer lives in the SDRAM (the listing, the text of a
// file, the parse result), nothing here takes SRAM beyond a few statics
#ifndef MT_TUNE_SCALE_H
#define MT_TUNE_SCALE_H

#include <stdint.h>

namespace tunescale
{
constexpr int kMaxFiles = 32;   // the files the row lists (the first 32 of the folder, in the order the card browser sorts them)

// Lists /Tunings/*.scl again. Called when Project Settings opens
void scan();
// The row's list: [0] "EDO (no file)", [1..files()] the file names, then the project's own copy when its file is not on the card. entries() of them
const char* const* list();
int entries();
int files();
// The row's position now: 0 when the tuning is N equal steps, else the file of the project (or its copy)
int position();
// Loads /Tunings/<name> as the project tuning: the degrees, the notes per period and the period come from the file, the 1/1 key and the 1/1 Hz stay.
// False: nothing changed, message() holds the reason for the screen (mtScala.h, or the card)
bool load(const char* name);
// The reason of the last refusal: a buffer that stays valid while a popup shows it (the SDRAM, 48 B)
const char* message();
// Back to N equal steps of the same period
void off();
}   // namespace tunescale

#endif
