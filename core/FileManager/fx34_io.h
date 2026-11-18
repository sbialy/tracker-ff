#ifndef FX34_IO_H_
#define FX34_IO_H_

// The card side of FX3 and FX4 (core/source/fx34.h): one sidecar per pattern, Workspace/patterns/pattern_NN.mtx next to the .mtp
// (the same name in a project folder). It holds the sparse extra FX of that pattern and the CRC the .mtp had when it was written, so a
// pattern file that was changed by anything else (a 1.3.x or stock firmware that opened and saved the project) makes the sidecar
// stale: it is parked, never applied and never deleted.
//
// Every entry point is a no-op while the project switch is off (fx34::enabled()), except copyFile: a sidecar that exists travels with its
// pattern whatever the switch says, so Save As of a project that was switched off keeps the data. With the switch off and no sidecar on the
// card nothing here writes anything, the pattern files and project.mtff are byte for byte those of a build without this feature.

#include "fx34.h"

namespace fx34io
{
typedef fx34::Pattern Pattern;

// What a pattern file looked like before it is rewritten
struct Guard
{
    bool sidecar;    // a sidecar is on the card
    bool inSync;     // ... and it is bound to the CRC the old .mtp holds (so the set in RAM is the truth)
};

// ---- the project switch
void projectClosing();             // a project load or a new project starts: the switch is off, every set empty (nothing of the last project leaks)
void applyProjectSwitch(bool on);  // project.mtff has been read: on loads the sidecar of the pattern in RAM
bool turnOn();                     // the Project screen row. True when it was off
bool turnOff();                    // the Project screen row. Writes the pattern on screen first. True when data stays on the card (show the warning)
bool keptData();                   // a sidecar of the Workspace exists (the switch may be off)

// ---- the pattern file layer (fileManagerPattern.cpp and the callers of readPatternFile / writePatternFile)
void clearSet(Pattern* p);                                     // a buffer is emptied or about to be filled from a file
void load(const char* mtpPath, Pattern* dest);                 // after the .mtp was read into dest: its sidecar into dest's set (empty when none, stale or damaged)
Guard before(const char* mtpPath);                             // before the .mtp is rewritten
void after(const char* mtpPath, const fx34::Set* set, uint32_t crc, const Guard& g);   // after it was (crc: the one in the file)
void copyFile(const char* srcMtp, const char* dstMtp);         // Save, Save As and Open copy the sidecar with the pattern
void copyFromBytes(Pattern* dst, const void* srcBuffer);       // copyBufferToBufferPattern / copyCurrentPatternToBuff: the set follows the bytes
void copyPattern(Pattern* dst, const Pattern* src);            // *dst = *src and the set (undo, redo)
void copyTrack(Pattern* dst, uint8_t dTrack, const Pattern* src, uint8_t sTrack);   // the extra FX of one track of a pattern (the Performance loader)

// Sidecars parked (stale or damaged) by the loads since the last project load, the text of the last one (host.txt, status line)
unsigned parkedCount();
}   // namespace fx34io

#endif
