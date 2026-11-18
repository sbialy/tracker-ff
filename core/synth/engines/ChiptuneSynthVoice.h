#pragma once

#include "PolySynth/sdk/InstrumentType.h"
#include <cstddef>
#include <memory>

namespace polysynth
{
std::unique_ptr<InstrumentType> createChiptuneSynth();

#ifdef FEATURE_SYNTH_OPEN
// SYNTH_OPEN (MK66): hooks for synth-open/engine_chip.cpp, which hosts this voice behind the open shim without the
// Patch::Metadata path (createChiptuneSynth stays unused there, so the linker drops its metadata and formatters)
class InstrumentVoice;
namespace open
{
// The voice in storage the caller owns (the CHIP plugin of tracker-engines-os keeps its 12 voices in its bss, a plugin has no heap).
// kChipVoiceBytes is sizeof(ChiptuneSynthVoice) of the MK66 build (checked by a static_assert in the .cpp), 4 byte aligned
#ifdef __arm__
constexpr size_t kChipVoiceBytes = 276;
#else
// Host builds (ui_shot, the smoke test, 64 bit pointers): the object is bigger. A CHIP plugin that keeps no pointer to the object it built
// (CHIP 1.1.1 reads it back from its storage) needs the storage to hold it, so the host gets room to spare. Never in the MK66 image
constexpr size_t kChipVoiceBytes = 1024;
#endif
InstrumentVoice* chipVoiceConstruct(void* storage);
std::unique_ptr<InstrumentVoice> chipVoiceCreate();
void chipVoiceReset(InstrumentVoice& voice);
// The same parameter handling as the handlers in createMetadata, except Volume (the shim applies it as voice gain)
void chipVoiceSetParam(InstrumentVoice& voice, int paramId, float value);
void chipVoiceSetBend(InstrumentVoice& voice, float semitones);
} // namespace open
#endif
} // namespace polysynth
