#pragma once

// Synth play page, the voice router (ref/synth-page-design.md, ref/synth-play-r1.md). MIDI in over USB or DIN plays one synth part while the
// page is open and the sequencer is stopped. A/B mode (ref/synth-ab-design.md): a second part B plays the same notes, bend and macro, and the
// morph (an equal power crossfade, CC 11) sets the gain of the two parts. The note handlers of mtMidi.cpp call these functions in the audio interrupt (no blocking, no
// allocation, a few dozen bytes of state). The part does the voicing and stealing, the router keeps what it needs for the sustain pedal, the
// note offs at exit and the voice strip of the page: a table of the 8 newest sounding notes.
//
// Built with FEATURE_SYNTH_OPEN (the page needs the open synth parts) and by the host test (SYNTHPLAY_TEST, which supplies the send functions).

#include <stdint.h>

// The gain of a synth part (open::partGain). The page's file supplies it, the host test of the router supplies a stand-in
void synthPlaySetGain(uint8_t part, float gain);

namespace synthplay
{
// The page settings in the order of the Features descriptor (core/source/features_builtin.cpp, key names there). Persisted in /Features/synthplay.cfg.
enum Param : uint8_t
{
    kInput,      // 0 USB + DIN, 1 USB, 2 DIN
    kChannel,    // 0 Omni, 1..16
    kVoices,     // 1..8, the part's engine may cap it lower
    kVelCurve,   // 0 Soft, 1 Linear, 2 Hard, 3 Fixed
    kFixedVel,   // 1..127
    kBend,       // semitones for the full bend wheel, 0..12
    kMod,        // CC 1 goes to macro 1..6 of the part, 0 off
    kSustain,    // 0 the pedal (CC 64) is ignored, 1 holds the note offs
    kParams,     // the settings above are the ones of /Features/synthplay.cfg, the two below are values of the session
    kPart = kParams,   // 0..2, the synth part the page plays (channel part + 1 on the virtual port)
    kTranspose,        // semitones, -24..24
    kPartB,            // A/B mode: 0 off, 1..3 = Synth 1..3 (also the part's MIDI channel), never the page part
    kMorph,            // 0..100, part A at 0, part B at 100 (equal power)
    kBankA,            // the patch slot (0..15) the page has put on part A, B
    kBankB,
    kValues
};

enum Source : uint8_t
{
    kPad = 0,   // the pads of the page: not subject to the Input and Channel settings
    kUsb = 1,
    kDin = 2,
};

constexpr int kCells = 8;
constexpr uint8_t kMacroCC0 = 102;   // CC of macro 1 on the part (Patch::Metadata::Macro::kMacroCCStart, asserted in fSynthPlay.cpp)

enum CellState : uint8_t
{
    kFree = 0,
    kHeld = 1,        // the key is down
    kSustained = 2,   // the key is up, the pedal holds the note
};

struct Cell
{
    uint8_t in;       // the note as played
    uint8_t out;      // the note sent to the part (transposed at note on, the note off uses this one)
    uint8_t vel;      // the velocity sent
    uint8_t state;    // CellState
    uint8_t seq;      // arrival order, wraps (the oldest cell goes first when more than 8 notes are down)
};

extern int8_t value[kValues];    // the settings and the session values, edited by the page. kParams of them are loaded and stored (fSynthPlay.cpp)
extern uint8_t changed;          // counts changes of the cells, the page redraws its voice strip when it moved

// MIDI in. src is kUsb or kDin, channel 1..16. The input and channel filters are applied here.
void noteOn(uint8_t src, uint8_t channel, uint8_t note, uint8_t velocity);   // velocity 0 is a note off
void noteOff(uint8_t src, uint8_t channel, uint8_t note);
void pitchBend(uint8_t src, uint8_t channel, int bend);                      // -8192..8191
bool controlChange(uint8_t src, uint8_t channel, uint8_t number, uint8_t val);   // true: the page took it (CC 1, 11, 64, 120, 121, 123)

// Main loop or interrupt. Every sounding note gets its note off, CC 123 goes to the part, the bend goes back to 0 and the pedal up.
void releaseAll();
// The part the page plays changes: everything sounding on the old part is released first
void setPart(uint8_t newPart);
// The part B of the page changes (0 off): everything sounding is released first
void setPartB(uint8_t newB);
// The gains of the two parts from kMorph (B off: part A at 1). Interrupt or main loop, plain float stores. The page calls it after a change
// of the morph or of a part, CC 11 calls it itself.
void applyMorph();
// The Sustain setting went off: the notes the pedal holds are released
void pedalOff();
// The settings that act at once (the page calls it after a change of the bend range): the bend wheel is sent again so the part scales it
void resendBend();

// The morph gains of 0..100: cos and sin of the quarter turn (the table of the router, linear between 17 points), 0..1
float morphGain(int morph);

const Cell* cells();             // kCells entries, state 0 = free
uint8_t sounding();              // cells in use
bool pedalDown();
}   // namespace synthplay
