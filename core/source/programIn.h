#ifndef PROGRAM_IN_H_
#define PROGRAM_IN_H_

#include <stdint.h>

// MIDI Program Change in (Config > MIDI > Program in, ref/midi-pc-in.md): a bank slot of a synth part, the way the Patch FX applies it. The firmware
// side of the hook ffHookProgramChangeIn (core/include/ff_hooks.h). Called in audio IRQ context: midiUpdate runs inside
// TrackerImpl::updateAudioBuffers (core/source/Tracker.cpp), so nothing here may draw or touch the card
namespace programin
{
// usb: the port, channel 1..16, program 0..127. Off in Config: dropped. On the Synth play page: the page decides (the channel of a side).
// Elsewhere channel N (1..3) is Synth N and the program 0..15 its bank slot: applyPatchBankSlot, values from memory (no card access, the part
// keeps its engine), a program above 15, a slot with no file of the part's engine and any other channel are dropped
void received(bool usb, uint8_t channel, uint8_t program);
}   // namespace programin

#endif
