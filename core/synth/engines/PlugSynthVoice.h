// Plugin synth engines on the stock Tracker+ / Mini synth stack (IMX port P7 H5, ref/ff-imx-port-plan.md). A PLUG_KIND_SYNTH .plg
// (plug_abi.h PLUG_EXT_SYNTH, the C form of the OpenTracker ISynthEngine) is played by the closed libPolySynth as one more
// InstrumentType: the lib allocates the voices, keeps the patch and its macros, and calls an InstrumentVoice per voice. PlugVoice is
// that InstrumentVoice. It builds the engine's SynthParams from the values the lib sends (onParamChange, the final values with the
// macros applied), calls prepare once per block for the instrument, note_on / note_off / render per voice and slot_post on the mix
// of the instrument (renderPostProcessAudio). The engine's Volume parameter is the voice gain (as the MK66 shim, open_synth.cpp).
//
// Like ChiptuneSynthVoice.cpp this file is code of the lib's side: the IMX image links it next to libPolySynth, the plus sim compiles
// it into the Cortex-M7 guest of the closed lib (synth-open/tools/plus_synth/build.sh), not as a host object.
// MK66 builds (FEATURE_SYNTH_OPEN) compile it empty.
#pragma once

#include "config.h"

#if defined(FEATURE_SYNTH) && !defined(FEATURE_SYNTH_OPEN) && defined(PLATFORM_IMX)

#include <cstdint>

struct plug_host;

namespace polysynth
{
class InstrumentTypeRegistry;

namespace plugimx
{
// One engine per code slot (plug_load.h kImxSynthSlots): 3 synth parts play at most 3 plugin engines at once
constexpr int kSlots = 3;

// registry.cpp hands the lib's registry over when the lib registers its types (Synth::createSynth)
void setRegistry(InstrumentTypeRegistry* registry);

// Attaches the engine whose code is loaded in slot (entryAddr: the Thumb address of its plug_entry): asks for PLUG_EXT_SYNTH, gives
// it its work memory and registers it in the lib under its type id. Returns the type id, or a negative error:
//   -1 no registry, -2 bad slot, -3 plug_entry refused, -4 not a synth engine, -5 layout of another OpenTracker interface,
//   -6 init failed, -7 no work memory. A slot that held another engine is dropped first (its type stays registered but plays nothing).
int attach(int slot, uint32_t entryAddr, const plug_host* host);
// The slot's engine is gone (its code is about to be overwritten): every PlugVoice of it goes silent, its patches get no metadata
void detach(int slot);
// The type id the slot plays, -1 for none
int slotType(int slot);

// Paraphony (PLUG_EXT_CHORD, plug_abi.h) of the attached engine of type typeId: bits 0-7 its Chord parameter, 8-15 Chord Mode, 16-23
// Strum (0xFF none), 24-31 the last value of its Chord enum. -1: no attached engine of that type answers the query
int chordInfo(int typeId);
// The part's next note on of `note` carries chord (0..76) to the engine's Chord parameter and strum (2 ms units, + up, - down, 0 none) to
// its Strum parameter, for that note on only (open::partNoteChord). Sequencer, before the note on. A part is its MIDI channel (0..2)
void noteChord(int part, uint8_t note, uint8_t chord, int8_t strum);
// The sequencer's note id and end mode (0 Release, 2 Cut, 3 Fade) of the note on or off it sends next, 0 0 after it (open::noteTag): a
// note off with an id ends only the voice that note on started, Cut and Fade ramp the voice out (New Note Action)
void noteTag(uint16_t id, uint8_t endMode = 0);
// MPE in of the Synth play page for the plugin engines (open::mpeNote, partMpe, partNoteBend, partNotePress): the member channel (2..16) of
// the note on or off sent next, 0 after it; the part's member bend range (semitones for a full raw bend) and pressure amount (0..1); the raw
// bend of a member channel (the voice of its newest note follows it, the next note on it starts there); the pressure of a member channel (its
// held note) or of the master channel 1 (every voice of the part). A part is its MIDI channel (0..2)
void mpeNote(uint8_t chan);
void partMpe(int part, float memberRange, float pressAmount);
void partNoteBend(int part, uint8_t chan, int raw);
void partNotePress(int part, uint8_t chan, uint8_t press);
#if defined(PLUS_SYNTH_HARNESS)
int mpeProbe(int i, int f);   // harness only: field f of voice i (PlugSynthVoice.cpp)
#endif

#if !defined(PLUS_SYNTH_HARNESS)
// The image: loads file[0..len) (a whole .plg) into the IMX code slot (plug_imx.h load) and attaches it. Same returns as attach,
// a loader error is -100 - plug::Err
int attachFile(int slot, const uint8_t* file, uint32_t len);
#endif
}   // namespace plugimx
}   // namespace polysynth

#endif
