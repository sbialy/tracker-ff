// mtMidi.cpp, included once after the includes: the Synth play page router and the hooks of the MIDI handlers into the MIDI remote (FEATURE_FEATURES, midiRemote.h) and the Synth play page (FEATURE_SYNTH_OPEN or FEATURE_SYNTH_PATCHFX). The hooks are statement macros (as in devhooks.h): they expand to the very tokens the inline blocks had. Without the features they are empty
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
#include "synthPlayRouter.h"
#endif

#ifdef FEATURE_FEATURES
// the MIDI remote takes the message first (`call`: a midi_remote:: call, true when it used it), the handler returns
#define FF_MIDI_REMOTE_OR_RETURN(call) \
    if (call)                           \
        return;
#else
#define FF_MIDI_REMOTE_OR_RETURN(call)
#endif

#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
static int8_t transposeIncomingPitch(uint8_t pitch);   // defined below in mtMidi.cpp
// The USB cable was pulled (ffHookUsbMidiPolled): the Synth play page tapped the note on of every USB key still down into the Capture ring and
// no note off comes. The same tap the note off would have made, one per held USB cell (SP-U3). Audio interrupt, like the handlers.
__attribute__((noinline)) void synthPlayCaptureReleaseUsb()
{
    const synthplay::Cell* cell = synthplay::cells();
    for (int i = 0; i < synthplay::kCells; i++)
    {
        if (cell[i].state == synthplay::kHeld && cell[i].src == synthplay::kUsb)
        {
            sequencer.captureTap(uint8_t(transposeIncomingPitch(cell[i].in)), 0, false, capSrc(kCapSrcSynthUsb));
        }
    }
}
// the Synth play page owns the MIDI in (playPage): a note goes to the capture tap and the page's part, the handler returns
#define FF_MIDI_PLAY_NOTE_ON_OR_RETURN(src, capSource, channel, pitch, velocity)                                          \
    if (playPage())                                                                                                       \
    {                                                                                                                     \
        sequencer.captureTap(uint8_t(transposeIncomingPitch(pitch)), velocity, velocity != 0, capSrc(capSource));         \
        synthplay::noteOn(src, channel, pitch, velocity);                                                                 \
        return;                                                                                                           \
    }
#define FF_MIDI_PLAY_NOTE_OFF_OR_RETURN(src, capSource, channel, pitch)                                                   \
    if (playPage())                                                                                                       \
    {                                                                                                                     \
        sequencer.captureTap(uint8_t(transposeIncomingPitch(pitch)), 0, false, capSrc(capSource));                        \
        synthplay::noteOff(src, channel, pitch);                                                                          \
        return;                                                                                                           \
    }
#define FF_MIDI_PLAY_CC_OR_RETURN(src, channel, number, value)                       \
    if (playPage() && synthplay::controlChange(src, channel, number, value))         \
        return;
#define FF_MIDI_PLAY_BEND(src, channel, bend) \
    if (playPage())                            \
        synthplay::pitchBend(src, channel, bend);
#else
#define FF_MIDI_PLAY_NOTE_ON_OR_RETURN(src, capSource, channel, pitch, velocity)
#define FF_MIDI_PLAY_NOTE_OFF_OR_RETURN(src, capSource, channel, pitch)
#define FF_MIDI_PLAY_CC_OR_RETURN(src, channel, number, value)
#define FF_MIDI_PLAY_BEND(src, channel, bend)
#endif
