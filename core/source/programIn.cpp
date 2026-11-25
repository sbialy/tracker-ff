#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)

#include "programIn.h"

#include "Synth.h"
#include "TrackerImpl.h"
#include "interruptsControl.h"
#include "mtConfig.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "synthPlayRouter.h"

namespace programin
{
void received(bool usb, uint8_t channel, uint8_t program)
{
    if (mtConfig.midi.programIn == 0)
    {
        return;
    }
    if (sequencer.getMidiInVoiceMode() == Sequencer::midiInVoiceMode_SynthPlay)
    {
        // the page owns the MIDI in while it is open: its Input and Channel filters, the channel of a side is its part's number
        synthplay::programChange(usb ? synthplay::kUsb : synthplay::kDin, channel, program);
        return;
    }
    if (channel < 1 || channel > kSynthInstrumentsCount || program >= Synth::kPatchSlots)
    {
        return;
    }
    DISABLE_AUDIO_INTERRUPTS();
    TrackerImpl::getInstance()->getSynth().applyPatchBankSlot(channel - 1, program);
    ENABLE_AUDIO_INTERRUPTS();
}
}   // namespace programin

#endif
