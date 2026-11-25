// Parts: the song arrangement of the Song screen's Parts view. Data model.
// Plain C++ with no firmware dependency, so synth-open/tools/parts_format_test.cpp runs this very code on the Mac.
//
// A Part names a source pattern per track (0 = track off), a length in steps and a repeat count. The Parts list lives in the project
// sidecar project.mtff (pat.parts.mode and pat.part.N lines, text codec in core/FileManager/mtff_codec.h) in the project folder and the
// Workspace. project.mt is never touched (1.3.0 rejects a larger file, fileManagerProject.cpp:72-77), so a project without the
// keys, or a firmware that does not know them, plays the stock playlist. Written only when the list has at least one Part.
#pragma once

#include <stdint.h>
#include <string.h>

namespace parts
{

constexpr int kMax = 255;          // Parts per project: an index fits a byte, 255 x 20 B = 5,100 B of SDRAM, 59 B of text each
constexpr int kTracks = 16;        // source pattern per track: 8 sample + 8 MIDI tracks of the original
constexpr int kRecordSize = 20;
constexpr uint8_t kMaxLength = 128;    // steps (a pattern holds 128)
constexpr uint8_t kMaxRepeats = 64;
constexpr uint8_t kMaxStart = 127;     // a track's start step into its source pattern: 0 = step 1 .. 127 = step 128
constexpr uint8_t kFlagTile = 0x01;   // a source shorter than the Part repeats to fill it (else the rest is empty)
constexpr uint8_t kFlagLoop = 0x02;   // on Part 0 only: Play Song goes back to Part 0 after the last Part (else the song stops there)

enum Mode : uint8_t
{
    kModePlaylist = 0,
    kModeParts = 1,
};

struct Part
{
    uint8_t src[kTracks];   // source pattern 1..255 per track, 0 = track off (empty steps in the image)
    uint8_t length;         // steps 1..128, 0 = the length of the first source pattern
    uint8_t repeats;        // passes 1..64, 0 = End (the song ends here)
    uint8_t flags;          // kFlagTile, kFlagLoop (Part 0), bits 2-7 reserved (kept as read)
    uint8_t baked;          // pattern number the last bake wrote this Part to, 0 = never baked (a re-bake reuses it)
};
static_assert(sizeof(Part) == kRecordSize, "the record is 20 bytes");

// start[i][t]: the step of its source pattern where track t of Part i starts to play (0 = step 1, as before the field existed). The 20 B
// Part record is full, so this is a parallel table: it is written to project.mtff only for Parts that have a non-zero start
// (pat.start.N), and every list edit that moves a Part moves its row
struct List
{
    uint16_t count;
    uint8_t mode;
    uint8_t pad;
    Part part[kMax];
    uint8_t start[kMax][kTracks];
};

inline bool partIsEnd(const Part& p)
{
    return p.repeats == 0;
}

inline void partInit(Part& p, uint8_t pattern)
{
    memset(&p, 0, sizeof(p));
    for (int t = 0; t < kTracks; t++) p.src[t] = pattern;
    p.length = 0;
    p.repeats = 1;
    p.flags = kFlagTile;
}

// ---- playback order, shared by the player and the bake

// The Part after `at` in song order: -1 when the song ends there (past the last Part or at an End Part).
inline int nextIndex(const List& list, int at)
{
    const int n = at + 1;
    if (n >= list.count || partIsEnd(list.part[n])) return -1;
    return n;
}

// Play Song loops: the flag of Part 0 (a song property, kept in the record that is always first so no new stored field is needed)
inline bool songLoops(const List& list)
{
    return list.count > 0 && (list.part[0].flags & kFlagLoop) != 0;
}

// Parts that play from 0 on before the song ends (the first End or the end of the list)
inline int songLength(const List& list)
{
    int i = 0;
    while (i < list.count && !partIsEnd(list.part[i])) i++;
    return i;
}

// Steps a source track of `srcLen` steps plays from `start` on: the window start..srcLen-1. A start at or past the end is no window
// (the whole track plays)
inline int windowLen(int srcLen, int start)
{
    return start > 0 && start < srcLen ? srcLen - start : srcLen;
}

// A source track of `srcLen` steps (last step index + 1) placed into a Part of `len` steps from its step `start` on: the source step
// that plays at step s, -1 for an empty step (tiling off and past the window). Tiling repeats the window start..end, not the whole track
inline int sourceStep(int s, int srcLen, int len, bool tile, int start = 0)
{
    if (s >= len || srcLen <= 0) return -1;
    if (start >= srcLen) start = 0;
    const int win = srcLen - start;
    if (s < win) return start + s;
    return tile ? start + s % win : -1;
}

}   // namespace parts
