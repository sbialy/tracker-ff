#include "mtNoteName.h"
#include "mtAudioEngine.h"
#include "mtPadBoard.h"
#include "play_memory.h"

// Names of a tuned project: 48 pad slots and 4 rotating ones. In the SDRAM like the other cold UI buffers (no SRAM), the host build keeps it in
// plain memory
#if defined(__arm__) || defined(__thumb__)
#define TUNE_NAMES_SDRAM __attribute__((section(".noinit.$RAM4.tunenames")))
#else
#define TUNE_NAMES_SDRAM
#endif
static char s_names[52][5] TUNE_NAMES_SDRAM;

const char* tunedNoteName(int key)
{
    static uint8_t next;
    char* const b = s_names[48 + (next++ & 3)];
    tuning::noteName(key, b);
    return b;
}

const char* tunedPadName(int slot, int key)
{
    char* const b = s_names[slot < 0 ? 0 : (slot > 47 ? 47 : slot)];
    tuning::noteName(key, b);
    return b;
}

void tuningChanged()
{
    AudioPlayMemory::initNotesLookup();
    mtPadBoard.refreshNotes();
}
