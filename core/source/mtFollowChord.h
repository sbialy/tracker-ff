#pragma once
// Follow chord (tracker-ff): a track plays its notes against the chord that a chosen chord track plays now. The chord of the chord
// track is the note of its last played step plus the MIDI Chord FX of that step (mtChordDefs.h chordDef, the step note is the bass). A
// note step without a MIDI Chord FX keeps the chord type of the step before it (a major triad before the first one), so a chord track
// may hold only root notes after its first chord. The follower's note changes at play time only, the pattern keeps what was written.
//
// Modes of a follower track:
//   Snap        each note goes to the nearest note of the chord (a tie goes down), a chord note stays as it is
//   Transp      each note moves by the chord root counted from C (-5..+6 semitones): a part written in C follows the root
//   Tr+Snap     Transp, then Snap
//
// The setting lives in the project sidecar project.mtff (Workspace and project folder) as one track.N.follow line for each follower
// (synth-open/plug_mtff.h), written only when a track follows. Without the file every track plays as before (project.mt cannot grow:
// 1.3.0 rejects a bigger file). A build without the plugin host (no project.mtff) does not keep it.
#include <stdint.h>

namespace followchord
{

constexpr int kTracks = 16; // kAllTracks (mtStructs.h, checked in mtFollowChord_io.cpp): this header builds alone for the host test

enum Mode : uint8_t
{
    kOff = 0,
    kSnap,
    kTranspose,
    kTransposeSnap,
    kModeCount
};

constexpr uint8_t kMajorTriad = 5; // chordDef[5] = 0, 4, 7

// The pitch class set (bit n = pitch class n) of chord entry `type` with the bass `note`. type past the table: the note alone.
uint16_t chordPitchClasses(int note, uint8_t type);

// The root pitch class of chord entry `type` with the bass `note` (an inversion's root is not its bass).
uint8_t chordRoot(int note, uint8_t type);

// The note `note` played against the chord (pcs, root) in `mode`. pcs 0: no chord known yet, the note as written. Result 0..127.
int8_t mapNote(int note, uint8_t mode, uint16_t pcs, uint8_t root);

// ---- the project setting and the live chord state (one copy in the image, read by the sequencer in the audio callback)
struct Setting
{
    uint8_t mode[kTracks];   // Mode
    uint8_t source[kTracks]; // the chord track, 0..kTracks-1 (only read when mode != kOff)
};

struct Live
{
    uint16_t pcs[kTracks];      // the chord the track plays now as a chord track (0: none yet)
    uint8_t root[kTracks];      // its root pitch class
    uint8_t lastType[kTracks];  // the chord type of its last chord step
};

extern Setting setting;
extern Live live;
extern uint16_t sourceMask; // bit t: some track follows track t (kept by recalc())

void recalc();                                    // after a change of `setting`
void resetLive();                                 // play start and stop: no chord known
bool anyFollower();                               // some track follows a chord track
const char* modeName(uint8_t mode);               // "Off", "Snap", "Transp", "Tr+Snap"

// UI: cycle the mode / the chord track of a follower track (a change marks the project changed, the Workspace save writes project.mtff)
void cycleMode(uint8_t track);
void cycleSource(uint8_t track);

// Project steps (core/FileManager): a load or a new project clears the setting, project.mtff (plug::projectLoaded) sets it afterwards
void workspaceLoaded(bool newProject);

} // namespace followchord
