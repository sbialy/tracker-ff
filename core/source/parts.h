// Parts: a song built from Parts (source pattern per track, length, repeats), next to the untouched song playlist.
// Data model: parts_format.h, storage: project.mtff (synth-open/plug_mtff.h). Design: ref/parts-feasibility.md option A, built in ref/parts.md.
//
// Playback: the Part about to play is assembled on the main loop into the sequencer's buffer pattern (one source pattern file per
// main loop pass, read into songTrackCopy[0]), one Part ahead, and swapped in at the end of the current Part through the same
// pointer swap the song player uses. The sequencer tick only counts passes. While a Part image is the playing pattern no pattern
// edit is stored (the image is not any pattern file, risk R1): the pattern editor refuses its edit mode and the file manager never
// writes the image to a pattern file. When the playback stops, the pattern the user had is loaded back.
#pragma once

#ifdef FEATURE_PARTS

#include "parts_format.h"

namespace parts
{

// ---- the list (SDRAM, emptied by projectLoaded, filled from project.mtff by plug::projectLoaded)
List& list();
inline bool modeOn()
{
    return list().mode == kModeParts;
}
// Song playback (Play Song on any screen, Song export) plays the Parts list
bool enabled();
// an edit of the list from the UI: project.mtff is written at the next Workspace save, the preload is redone
void changed();

// ---- project files (main loop, file manager steps)
void projectLoaded(const char* workspaceDir, bool newProject = false);   // the list and the playback state emptied: plug::projectLoaded fills it
const char* note();                                                      // the last load or save problem, "" when none

// ---- playback
// Start the song from Part `from` (loop: that Part only, as Play Pattern does). Main loop, sequencer stopped. False: nothing
// playable (an empty list, an End Part, Performance mode on)
bool start(int from, bool loop);
void update();                 // main loop (Sequencer::update): preload, the pattern back after a stop
bool playing();                // a Part image is the playing pattern (edits are off)
int currentPart();             // the Part that plays, -1 when none
int passesLeft();              // passes of the current Part still to play (this one included)
int queuedPart();              // -1 when none
// Queue Part `at` (main loop). atPassEnd: at the end of the current pass, else at the end of the current Part
void queue(int at, bool atPassEnd);
int progressPos();             // the Part index the song export reports progress with
int songLength();              // Parts from 0 to the end of the song

// Sequencer, audio interrupt, at the end of a pass in song mode while playing(). swap: the buffer holds the next Part, swap it in
// now (Sequencer::switchToPartImage). Returns false when the song ended here: with swap false the last Part ended and the sequencer
// stops, with swap true the song loops (the flag of Part 0) and goes on from Part 0. exporting: Song export, never loops
bool onPassEnd(bool& swap, bool exporting);
// Sequencer, audio interrupt: a stock pattern switch replaced the image (a pattern change while the Part played)
void imageReplaced();

// The playing pattern is a Part image: the stock pattern saves skip it (risk R1). Edits of it go to the source patterns instead
inline bool editsBlocked()
{
    return playing();
}
// live edits (ref/parts-live.md): the pattern editor changed the playing image (fileManager setActualPatternStructChanged).
// Stored to the source patterns by update() once the edits settle, at the end of the Part, at Stop and by storeEdits()
void imageEdited();
// store the edits now (the Workspace save, main loop)
void storeEdits();
uint32_t diag(int what);   // 0 late Part switches, 1 pattern files written by live edits, 2 storeEdits calls

// ---- bake (option C): every distinct Part of the song as a real pattern in free pattern numbers, the playlist rewritten
// (repeats become repeated slots). Main loop, sequencer stopped. Returns the number of patterns written, -1 on error with
// note() saying why. The Parts list and its mode stay: switch the view to Patterns to hear the baked song.
int bake();

// Unlink (Build, Shift + Copy): track `t` of Part `at` as a pattern of its own, the first free one. It holds the window the track plays in
// the Part (from its start step, tiled to the Part length), the cell then plays it from step 1. Main loop, sequencer stopped. Returns the
// pattern number, 0 with note() saying why (nothing there, playing, no free pattern, write error)
int unlink(int at, int t);

}   // namespace parts

// the Song screen's Parts view (fSongParts.cpp): a source pattern's length as an assembly saw it, pat 0 forgets all (project load)
void partsNoteSourceLength(uint8_t pat, uint8_t len);

#endif
