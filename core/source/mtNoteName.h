// The name of a pattern key for the screen: the stock mtNotes name with 12 notes per period, the degree name of mtTuning.h otherwise.
// Every place that used to read mtNotes[k] calls noteName(k). The result of noteName is valid until the fourth call after it (a pad
// name popup keeps 48 of them: padName(slot, key), one slot per pad).
#ifndef MT_NOTE_NAME_H
#define MT_NOTE_NAME_H

#include "mtStructs.h"
#include "mtTuning.h"

const char* tunedNoteName(int key);
const char* tunedPadName(int slot, int key);

inline const char* noteName(int key)
{
    const int k = key < 0 ? 0 : (key > 127 ? 127 : key);
    return tuning::tunedNames() ? tunedNoteName(k) : mtNotes[k];
}
inline const char* padName(int slot, int key)
{
    const int k = key < 0 ? 0 : (key > 127 ? 127 : key);
    return tuning::tunedNames() ? tunedPadName(slot, k) : mtNotes[k];
}

// The tuning (tuning::cfg) changed: the sample pitch tables are filled again and the pads laid out again. Called by every setter of the tuning
// (the Project Settings rows, a project load, a new project)
void tuningChanged();

#endif
