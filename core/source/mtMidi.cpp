#include "mtSequencer.h"
#include "stereoPairs.h"

#include "configEditor/configEditor.h"
#include "instrumentEditor/instrumentEditor.h"
#include "patternEditor/patternEditor.h"
#include "sampleEditor/sampleEditor.h"
#include "samplePlayback/samplePlayback.h"
#include "sampleRecorder/sampleRecorder.h"
#include "songEditor/songEditor.h"

#include "Platform.h"
#include "TrackerImpl.h"
#include "core/interface.h"
#include "feature_flags.h"
#ifdef FEATURE_FEATURES
#include "midiRemote.h"
#endif
#include "mtCapture.h"
#include "util_math.h"
#include <cmath>
#include "mtMidi_ff.inc"
#include "ff_hooks.h"
#include "mtMpeOut.h"

extern cPatternEditor patternEditor;

static cSamplePlayback* SP = &samplePlayback;
static cSampleRecorder* SR = &sampleRecorder;
static cSampleEditor* SE = &sampleEditor;
static cPatternEditor* PTE = &patternEditor;

// Set by the note handlers, which run in the audio interrupt (midiUpdate), cleared by the main loop: the pattern view is
// redrawn there and never from inside the interrupt, where it could cut into a redraw the main loop is in the middle of
static volatile bool patternRefreshPending = false;
static cInstrumentEditor* IE = &instrumentEditor;
static UartMidi* uartMidi = nullptr;
static UsbMidi* usbMidi = nullptr;

// MPE out (mtMpeOut.h): the sink of the allocator, the two drivers by Config > MIDI > Notes out. A member channel message is never a synth part message
// (the virtual port is a part channel, not an MPE zone), the callers below leave toSynth out
namespace
{
struct MpeSink
{
    // one message, to the drivers the Notes out setting names (kind 0 off, 1 on, 2 bend, 3 cc, 4 channel pressure)
    __attribute__((noinline)) static void emit(uint8_t kind, int ch, int a, int b)
    {
        const bool usb = mtConfig.midi.notesOutMode == notesOut_mode_usb || mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack;
        const bool jack = mtConfig.midi.notesOutMode == notesOut_mode_jack || mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack;
        for (int d = 0; d < 2; d++)
        {
            Midi* m = d ? static_cast<Midi*>(uartMidi) : static_cast<Midi*>(usbMidi);
            if (!(d ? jack : usb))
                continue;
            const uint8_t c = uint8_t(ch);
            switch (kind)
            {
                case 0: m->sendNoteOff(uint8_t(a), 0, c); break;
                case 1: m->sendNoteOn(uint8_t(a), uint8_t(b), c); break;
                case 2: m->sendPitchBend(a, c); break;
                case 3: m->sendControlChange(uint8_t(a), uint8_t(b), c); break;
                default: m->sendAfterTouch(uint8_t(a), c); break;
            }
        }
    }
    void noteOn(int ch, uint8_t n, uint8_t v) { emit(1, ch, n, v); }
    void noteOff(int ch, uint8_t n) { emit(0, ch, n, 0); }
    void bend(int ch, int v) { emit(2, ch, v, 0); }
    void cc(int ch, uint8_t i, uint8_t v) { emit(3, ch, i, v); }
    void pressure(int ch, uint8_t v) { emit(4, ch, v, 0); }
};
}   // namespace

extern void playSongFromSelectedPattern();
extern void playSongFromSelectedPattern_stop();

void peformanceModeOnMidiCC(uint8_t cc_idx, uint8_t cc_value);
void changeMixerOnMidiCC(uint8_t cc_idx, uint8_t cc_value);

elapsedMillis timerMidiDelay = 0;

void midiInit(UartMidi* uartMidi, UsbMidi* usbMidi)
{
    ::uartMidi = uartMidi;
    ::usbMidi = usbMidi;
    // jack

    UartMidi::MidiReceiveActions uartMidiReceiveActions = {};   // every field is set below or null (dropped), none is left to chance
    uartMidiReceiveActions.noteOn = handleJackNoteOn;
    uartMidiReceiveActions.noteOff = handleJackNoteOff;
    uartMidiReceiveActions.clock = receiveJackClock;
    uartMidiReceiveActions.start = receiveJackStart;
    uartMidiReceiveActions.stop = receiveJackStop;
    uartMidiReceiveActions.songPosition = handleJackSongPosition;
    uartMidiReceiveActions.continueMessage = receiveJackContinue;
    uartMidiReceiveActions.controlChange = handleJackControlChange;
    uartMidiReceiveActions.pitchBend = handleJackPitchBend;
    uartMidiReceiveActions.programChange = handleJackProgramChange;
    uartMidiReceiveActions.channelPressure = handleJackChannelPressure;
    uartMidi->setReceiveActions(&uartMidiReceiveActions);

    // usb
    UsbMidi::MidiReceiveActions usbMidiReceiveActions = {};
    usbMidiReceiveActions.realTime = receiveUsbRealtime;
    usbMidiReceiveActions.noteOn = handleUsbNoteOn;
    usbMidiReceiveActions.noteOff = handleUsbNoteOff;
    usbMidiReceiveActions.songPosition = handleUsbSongPosition;
    usbMidiReceiveActions.controlChange = handleUSBControlChange;
    usbMidiReceiveActions.pitchBend = handleUsbPitchBend;
    usbMidiReceiveActions.programChange = handleUsbProgramChange;
    usbMidiReceiveActions.channelPressure = handleUsbChannelPressure;

    usbMidi->setReceiveActions(&usbMidiReceiveActions);
}

void midiUiUpdate()
{
    if (!patternRefreshPending)
        return;
    patternRefreshPending = false; // before the redraw: a note that comes during it asks for one more
    patternEditor.refreshPatternFromOutsideModule();
}

static void mpeOutPump();

// USB MIDI events handled per audio block (one event = one 4 byte USB MIDI packet entry). Sized from the device flood
// test of poly-midi-isr-harden (ref in build/poly-midi-isr-harden/REPORT.md)
static constexpr uint32_t kUsbEventsPerBlock = 32;
// time spent on USB MIDI events per block before the rest waits (a third of the 2,902 us block)
static constexpr uint32_t kUsbBudgetUs = 1000;
// blocks that left events in the queue because of the cap, read from a debugger or a console peek
volatile uint32_t midiUsbDeferredBlocks = 0;

void midiUpdate()
{
    if (sequencer.isRenderMode())
        return;

    mpeOutPump();

    if (uartMidi != nullptr)
    {
        while (uartMidi->isReadAvailable())
        {
            uartMidi->read();
        }
    }

    if (usbMidi != nullptr)
    {
        // at most kUsbEventsPerBlock events and about kUsbBudgetUs per audio block: a host that floods the port cannot
        // stretch one block past its time. The rest stays in the USB receive queue for the next block (the USB hardware
        // answers NAK when the queue is full, so the host waits and nothing is lost). The time limit is for the events
        // that cost much more than a parse, a note on that starts a voice can take milliseconds on its own: the events
        // behind it wait one block. At least one event is always handled.
        Platform* plat = TrackerImpl::getInstance()->getPlatform();
        Timer* timer = (plat != nullptr) ? plat->getTimer() : nullptr;
        const uint32_t startUs = (timer != nullptr) ? timer->micros() : 0;
        uint32_t handled = 0;
        while (handled < kUsbEventsPerBlock && usbMidi->isReadAvailable())
        {
            if (handled != 0 && timer != nullptr && timer->micros() - startUs > kUsbBudgetUs)
                break;
            usbMidi->read();
            handled++;
        }
        if (usbMidi->isReadAvailable())
            midiUsbDeferredBlocks = midiUsbDeferredBlocks + 1;
        // after the queue: a note on read above is let go with the rest, no note off follows a pulled cable
        if (ffHookUsbMidiPolled(usbMidi))
            mpeOutBusLost();
    }
}

#include "mtMidi_playpage.inc"

uint8_t isIncomingChannelDesired(uint8_t chan)
{
    return (mtConfig.midi.notesInChannel == chan || mtConfig.midi.notesInChannel == 0);
}

// ---- Per-track MIDI in (Config > MIDI > Track N in channel) ----------------
// Note currently held on each track's voice by MIDI in, -1 = none.
static int8_t trackMidiHeldNote[kSampleTracks] = { -1, -1, -1, -1, -1, -1, -1, -1 };

static bool isAnyTrackListeningOn(uint8_t channel)
{
    // Only route per track from the sequencer/pattern context; sample editor,
    // sample playback and recorder screens keep their own MIDI preview behaviour.
    if (sequencer.getMidiInVoiceMode() != Sequencer::midiInVoiceMode_sequencer)
        return false;

    for (int tr = 0; tr < kSampleTracks; tr++)
    {
        if (stereopairs::isFollower(tr)) // the second track of a linked stereo pair plays nothing: its channel setting is inert
            continue;
        if (mtConfig.midi.trackNotesInChannel[tr] == channel)
            return true;
    }
    return false;
}

static int8_t transposeIncomingPitch(uint8_t pitch)
{
    int8_t transp = 12 * (mtConfig.midi.middleC - 2);
    return clamp(pitch + transp, 0, 127);
}

// Returns true if the note was consumed by per-track routing.
static bool handleTrackMidiNoteOn(uint8_t channel, uint8_t pitch, uint8_t velocity, uint8_t capSource)
{
    if (!isAnyTrackListeningOn(channel))
        return false;

    pitch = transposeIncomingPitch(pitch);

    // Prefer a listening track whose voice is free; otherwise steal the first one.
    int8_t target = -1;
    for (int tr = 0; tr < kSampleTracks; tr++)
    {
        if (mtConfig.midi.trackNotesInChannel[tr] != channel || stereopairs::isFollower(tr))
            continue;
        if (target < 0)
            target = tr;
        if (trackMidiHeldNote[tr] < 0)
        {
            target = tr;
            break;
        }
    }

    trackMidiHeldNote[target] = pitch;
    sequencer.captureTap(pitch, velocity, true, capSrc(capSource, uint8_t(target)));
    sequencer.handleTrackMidiNoteOn(target, pitch, velocity);
    return true;
}

static bool handleTrackMidiNoteOff(uint8_t channel, uint8_t pitch, uint8_t capSource)
{
    if (!isAnyTrackListeningOn(channel))
        return false;

    pitch = transposeIncomingPitch(pitch);

    for (int tr = 0; tr < kSampleTracks; tr++)
    {
        if (mtConfig.midi.trackNotesInChannel[tr] == channel && trackMidiHeldNote[tr] == pitch)
        {
            trackMidiHeldNote[tr] = -1;
            sequencer.captureTap(pitch, 0, false, capSrc(capSource, uint8_t(tr)));
            sequencer.handleTrackMidiNoteOff(tr);
            break;
        }
    }
    return true;
}

void handleJackNoteOn(byte channel, byte pitch, byte velocity)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::note(channel, pitch, velocity, velocity != 0, midi_remote::kSourceDin));
    FF_MIDI_PLAY_NOTE_ON_OR_RETURN(synthplay::kDin, kCapSrcSynthDin, channel, pitch, velocity);
    //	Serial.printf("ON\tch: %d, pitch: %d, velo: %d \r\n", channel, pitch,
    //					velocity);
    //	MIDI.sendNoteOn(pitch, velocity, channel);

    if (velocity == 0)
    {
        handleJackNoteOff(channel, pitch, 0);
        return;
    }

    if (mtConfig.midi.notesInMode == notesIn_jack ||
        mtConfig.midi.notesInMode == notesIn_usb_and_jack)
    {
        if (handleTrackMidiNoteOn(channel, pitch, velocity, kCapSrcTrackDin))
            return;

        if (isIncomingChannelDesired(channel))
        {

            int8_t transp = 12 * (mtConfig.midi.middleC - 2);
            pitch = clamp(pitch + transp, 0, 127);
            sequencer.captureTap(pitch, velocity, true, capSrc(kCapSrcDin));

            if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_sequencer)
            {
                sendSelection();

                sequencer.handleNoteOn(
                  Sequencer::GRID_OUTSIDE_PATTERN, pitch, velocity, pitch + 100);

                patternRefreshPending = true; // the pattern view is redrawn by the main loop (midiUiUpdate)
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SamplePlayback)
            {
                SP->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleEditor)
            {
                SE->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleRecorder)
            {
                SR->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
        }
    }
}
void handleUsbNoteOn(byte channel, byte pitch, byte velocity)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::note(channel, pitch, velocity, velocity != 0));
    FF_MIDI_PLAY_NOTE_ON_OR_RETURN(synthplay::kUsb, kCapSrcSynthUsb, channel, pitch, velocity);
    //	Serial.printf("ON\tch: %d, pitch: %d, velo: %d \r\n", channel, pitch,
    //					velocity);
    //	MIDI.sendNoteOn(pitch, velocity, channel);

    if (velocity == 0)
    {
        handleUsbNoteOff(channel, pitch, 0);
        return;
    }

    if (mtConfig.midi.notesInMode == notesIn_usb ||
        mtConfig.midi.notesInMode == notesIn_usb_and_jack)
    {
        if (handleTrackMidiNoteOn(channel, pitch, velocity, kCapSrcTrackUsb))
            return;

        if (isIncomingChannelDesired(channel))
        {
            int8_t transp = 12 * (mtConfig.midi.middleC - 2);
            pitch = clamp(pitch + transp, 0, 127);
            sequencer.captureTap(pitch, velocity, true, capSrc(kCapSrcUsb));

            if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_sequencer)
            {
                sendSelection();

                sequencer.handleNoteOn(
                  Sequencer::GRID_OUTSIDE_PATTERN, pitch, velocity, pitch + 100);

                patternRefreshPending = true; // the pattern view is redrawn by the main loop (midiUiUpdate)
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SamplePlayback)
            {
                SP->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleEditor)
            {
                SE->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleRecorder)
            {
                SR->noteOnHandle(channel, pitch, velocity, pitch + 100);
            }
        }
    }
}

void handleJackNoteOff(byte channel, byte pitch, [[maybe_unused]] byte velocity)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::note(channel, pitch, 0, false, midi_remote::kSourceDin));
    FF_MIDI_PLAY_NOTE_OFF_OR_RETURN(synthplay::kDin, kCapSrcSynthDin, channel, pitch);

    //	Serial.printf("OFF\tch: %d, pitch: %d, velo: %d\r\n", channel, pitch,
    //					velocity);

    if (mtConfig.midi.notesInMode == notesIn_jack ||
        mtConfig.midi.notesInMode == notesIn_usb_and_jack)
    {
        if (handleTrackMidiNoteOff(channel, pitch, kCapSrcTrackDin))
            return;

        if (isIncomingChannelDesired(channel))
        {
            int8_t transp = 12 * (mtConfig.midi.middleC - 2);
            pitch = clamp(pitch + transp, 0, 127);
            sequencer.captureTap(pitch, 0, false, capSrc(kCapSrcDin));

            if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_sequencer)
            {
                sendSelection();
                sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN, pitch, 0, pitch + 100);

                if (isEditMode())
                {
                    //					if (isMultiSelectionOnOneLine() &&
                    // sequencer.noMoreRecOpen())
                    if (sequencer.noMoreRecOpen())
                    {
                        PTE->moveCursorByStepParallel();
                    }
                }
                patternRefreshPending = true; // the pattern view is redrawn by the main loop (midiUiUpdate)
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SamplePlayback)
            {
                SP->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleEditor)
            {
                SE->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleRecorder)
            {
                SR->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
        }
    }
}

void handleUsbNoteOff(byte channel, byte pitch, [[maybe_unused]] byte velocity)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::note(channel, pitch, 0, false));
    FF_MIDI_PLAY_NOTE_OFF_OR_RETURN(synthplay::kUsb, kCapSrcSynthUsb, channel, pitch);

    //	Serial.printf("OFF\tch: %d, pitch: %d, velo: %d\r\n", channel, pitch,
    //					velocity);

    if (mtConfig.midi.notesInMode == notesIn_usb ||
        mtConfig.midi.notesInMode == notesIn_usb_and_jack)
    {
        if (handleTrackMidiNoteOff(channel, pitch, kCapSrcTrackUsb))
            return;

        if (isIncomingChannelDesired(channel))
        {
            int8_t transp = 12 * (mtConfig.midi.middleC - 2);
            pitch = clamp(pitch + transp, 0, 127);
            sequencer.captureTap(pitch, 0, false, capSrc(kCapSrcUsb));

            if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_sequencer)
            {
                sendSelection();
                sequencer.handleNoteOff(Sequencer::GRID_OUTSIDE_PATTERN, pitch, 0, pitch + 100);

                if (isEditMode())
                {
                    if (sequencer.noMoreRecOpen())
                    {
                        PTE->moveCursorByStepParallel();
                    }
                }
                patternRefreshPending = true; // the pattern view is redrawn by the main loop (midiUiUpdate)
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SamplePlayback)
            {
                SP->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleEditor)
            {
                SE->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
            else if (sequencer.getMidiInVoiceMode() == sequencer.midiInVoiceMode_SampleRecorder)
            {
                SR->noteOffHandle(channel, pitch, 0, pitch + 100);
            }
        }
    }
}

void handleJackControlChange(byte channel, byte number, byte value)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::cc(channel, number, value));
    FF_MIDI_PLAY_CC_OR_RETURN(synthplay::kDin, channel, number, value);

    if ((mtConfig.midi.ccInMode == ccIn_None) || (mtConfig.midi.ccInMode == ccIn_usb))
        return;

    if ((channel == mtConfig.midi.ccInChannel) || (mtConfig.midi.ccInChannel == ALL_CC_CHANNELS))
    {
        if (mtInterface.isPerformanceModeScreenActive())
            peformanceModeOnMidiCC(number, value);

        changeMixerOnMidiCC(number, value);

        IE->onControlChangeReceive(number, value);
        SP->onControlChangeReceive(number, value);
    }
}

void handleUSBControlChange(byte channel, byte number, byte value)
{
    FF_MIDI_REMOTE_OR_RETURN(midi_remote::cc(channel, number, value));
    FF_MIDI_PLAY_CC_OR_RETURN(synthplay::kUsb, channel, number, value);
    if ((mtConfig.midi.ccInMode == ccIn_None) || (mtConfig.midi.ccInMode == ccIn_jack))
        return;

    if ((channel == mtConfig.midi.ccInChannel) || (mtConfig.midi.ccInChannel == ALL_CC_CHANNELS))
    {
        if (mtInterface.isPerformanceModeScreenActive())
            peformanceModeOnMidiCC(number, value);

        changeMixerOnMidiCC(number, value);

        IE->onControlChangeReceive(number, value);
        SP->onControlChangeReceive(number, value);
    }
}

// Program Change in (Config > MIDI > Program in, programIn.cpp): the hook, a build without the synth engines drops it
void handleJackProgramChange(uint8_t channel, uint8_t program)
{
    ffHookProgramChangeIn(false, channel, program);
}

void handleUsbProgramChange(uint8_t channel, uint8_t program)
{
    ffHookProgramChangeIn(true, channel, program);
}

// Pitch bend in: only the Synth play page uses it (the part scales it by its live bend range), every other page drops it as before
void handleJackPitchBend([[maybe_unused]] uint8_t channel, [[maybe_unused]] int bend)
{
    FF_MIDI_PLAY_BEND(synthplay::kDin, channel, bend);
}

void handleUsbPitchBend([[maybe_unused]] uint8_t channel, [[maybe_unused]] int bend)
{
    FF_MIDI_PLAY_BEND(synthplay::kUsb, channel, bend);
}

// Channel pressure in (0xD0): only the Synth play page uses it (MPE, synthplay::channelPressure), every other page drops it as before
void handleJackChannelPressure(uint8_t channel, uint8_t pressure)
{
    ffHookChannelPressureIn(false, playPage(), channel, pressure);
}

void handleUsbChannelPressure(uint8_t channel, uint8_t pressure)
{
    ffHookChannelPressureIn(true, playPage(), channel, pressure);
}

static uint32_t s_audioBlockStartUs = 0;
static uint32_t s_clockSubBlockOffsetUs = 0;

AUDIO_CORE_RAM void midiSetAudioBlockStartUs(uint32_t blockStartUs)
{
    s_audioBlockStartUs = blockStartUs;
}

AUDIO_CORE_RAM void midiSetClockSubBlockOffsetUs(uint32_t offsetUs)
{
    s_clockSubBlockOffsetUs = offsetUs;
}

static void sendMidiClockImmediate()
{
    if (sequencer.isRenderMode())
        return;
    switch (mtConfig.midi.clkOut)
    {
        case clockOut_usb: usbMidi->sendRealTime(Midi::MessageType::kClock); break;

        case clockOut_usb_and_jack:
            usbMidi->sendRealTime(Midi::MessageType::kClock);
            uartMidi->sendRealTime(Midi::MessageType::kClock);
            break;

        case clockOut_jack: uartMidi->sendRealTime(Midi::MessageType::kClock); break;

        default: break;
    }
}

AUDIO_CORE_RAM void sendMidiStart()
{
    if (sequencer.isRenderMode())
        return;
    switch (mtConfig.midi.transportOut)
    {
        case clockOut_usb: usbMidi->sendRealTime(Midi::MessageType::kStart); break;

        case clockOut_usb_and_jack:
            usbMidi->sendRealTime(Midi::MessageType::kStart);
            uartMidi->sendRealTime(Midi::MessageType::kStart);
            break;

        case clockOut_jack: uartMidi->sendRealTime(Midi::MessageType::kStart); break;

        default: break;
    }
}
void sendMidiStop()
{
    if (sequencer.isRenderMode())
        return;
    if (mpeout::members)
    {   // MPE out: Stop lets every member go and centres the bends (the offs of the sequencer went out before, this is the safety net)
        MpeSink s;
        mpeout::release(s);
    }
    if (Platform* platform = TrackerImpl::getInstance()->getPlatform(); platform != nullptr)
    {
        platform->getTimer()->cancelMidiClock();
    }
    switch (mtConfig.midi.transportOut)
    {
        case clockOut_usb: usbMidi->sendRealTime(Midi::MessageType::kStop); break;

        case clockOut_usb_and_jack:
            usbMidi->sendRealTime(Midi::MessageType::kStop);
            uartMidi->sendRealTime(Midi::MessageType::kStop);
            break;

        case clockOut_jack: uartMidi->sendRealTime(Midi::MessageType::kStop); break;

        default: break;
    }
}

AUDIO_CORE_RAM void sendMidiClock()
{
    if (sequencer.isRenderMode() || mtConfig.midi.clkOut == clockOut_Off)
        return;

    Platform* platform = TrackerImpl::getInstance()->getPlatform();
    if (platform == nullptr)
        return;

    Timer* timer = platform->getTimer();
    uint32_t elapsedUs = timer->micros() - s_audioBlockStartUs;
    uint32_t delayUs =
      (s_clockSubBlockOffsetUs > elapsedUs) ? (s_clockSubBlockOffsetUs - elapsedUs) : 0U;
    timer->scheduleMidiClockUs(delayUs, sendMidiClockImmediate);
}

void receiveUsbRealtime(uint8_t type)
{
    switch (type)
    {
        case Midi::MessageType::kClock:
            if (mtConfig.midi.clkIn == clockIn_Usb)
            {
                receiveClock();
            }
            break;

        case Midi::MessageType::kStart:
            if (mtConfig.midi.transportIn == clockIn_Usb)
            {
                //			Serial.printf("usb start\n");
                //			Serial.send_now();
                receiveStart();
            }
            break;

        case Midi::MessageType::kContinue:
            if (mtConfig.midi.transportIn == clockIn_Usb)
            {
                //			Serial.printf("usb continue\n");
                //			Serial.send_now();
                receiveContinue();
            }
            break;

        case Midi::MessageType::kStop:
            if (mtConfig.midi.transportIn == clockIn_Usb)
            {
                //			Serial.printf("usb stop\n");
                //			Serial.send_now();
                receiveStop();
            }
            break;

        default: break;
    }
}

void receiveJackStart()
{
    if (mtConfig.midi.transportIn == clockIn_Jack)
    {
        receiveStart();
    }
}

void receiveJackStop()
{
    if (mtConfig.midi.transportIn == clockIn_Jack)
    {
        receiveStop();
    }
}

void receiveJackContinue()
{
    if (mtConfig.midi.transportIn == clockIn_Jack)
    {
        receiveContinue();
    }
}

void receiveJackClock()
{
    if (mtConfig.midi.clkIn == clockIn_Jack)
    {
        receiveClock();
    }
}

uint16_t lastSongPosition = 0;

void receiveClock()
{
    sequencer.handleMidiInClock();
}

void receiveStart()
{
    if (playPage())
        return;
    bool songMode = mtInterface.isSongScreenActive();
    sequencer.handleMidiInStart(false, songMode);
}

void receiveContinue()
{
    if (playPage())
        return;
    bool songMode = mtInterface.isSongScreenActive();
    sequencer.handleMidiInStart(true, songMode);
}

void receiveStop()
{
    if (playPage())
        return;
    sequencer.handleMidiInStop();
}

void handleUsbSongPosition(uint16_t beats)
{
    //	Serial.printf("song pos: %d beats\n", beats);
    //	sequencer.forcePosition(beats);

    if (mtConfig.midi.transportIn == clockIn_Usb && !playPage())
    {
        lastSongPosition = beats;
        sequencer.handleMidiInSongPosition(beats);
    }
}

void handleJackSongPosition(unsigned int beats)
{
    //	Serial.printf("song pos: %d beasts\n", beats);
    //	sequencer.forcePosition(beats);
    if (mtConfig.midi.transportIn == clockIn_Jack && !playPage())
    {
        lastSongPosition = beats;
        sequencer.handleMidiInSongPosition(static_cast<uint16_t>(beats));
    }
}

void mpeOutSet(uint8_t members)
{
    MpeSink s;
    mpeout::setMembers(s, members, uint8_t(mtConfig.midi.notesOutMode));
}

uint8_t mpeOutMembers()
{
    return mpeout::members;
}

void mpeOutBusLost()
{
    mpeout::busLost = true;
}

// the configuration messages of a new zone, a few at a time: the DIN port carries about 3 of these per audio block
static void mpeOutPump()
{
    if (mpeout::members == 0 || sequencer.isRenderMode())
        return;
    MpeSink s;
    mpeout::pump(s, uint8_t(mtConfig.midi.notesOutMode), 2);
}

AUDIO_CORE_RAM void sendCC(uint8_t index, uint8_t value, uint8_t channel, bool toSynth)
{
    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendControlChange(
          index, value, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
        channel = 1;   // MPE out: an instrument wide message goes to the master channel, a member channel would reach one note only

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendControlChange(index, value, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendControlChange(index, value, channel);
    }
}

void sendProgramChange(uint8_t value, uint8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendProgramChange(value,
                                                                                       channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
        channel = 1;

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendProgramChange(value, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendProgramChange(value, channel);
    }
}

void sendAfterTouch(uint8_t value, uint8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendAfterTouch(value, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
        channel = 1;   // channel pressure of the master channel: the whole zone

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendAfterTouch(value, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendAfterTouch(value, channel);
    }
}

void sendPolyPressure(uint8_t note, uint8_t value, uint8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendPolyPressure(
          note, value, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
    {   // MPE out: the pressure of the member channel that plays the note (nothing when the note does not sound)
        MpeSink s;
        mpeout::notePressure(s, note, value, channel);
        return;
    }

    int8_t transp = 12 * (2 - mtConfig.midi.middleC);
    note = clamp(note + transp, 0, 127);

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendPolyPressure(note, value, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendPolyPressure(note, value, channel);
    }
}

void sendPitchbend(int16_t value, int8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendPitchBend(value, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
    {   // MPE out: the M FX moves the notes that sound on the channel, one by one (mtMpeOut.h fxBend)
        MpeSink s;
        mpeout::fxBend(s, value, uint8_t(channel));
        return;
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {

        usbMidi->sendPitchBend(value, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendPitchBend(value, channel);
    }
}

void sendMidiNoteOn(uint8_t note, uint8_t velo, uint8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendNoteOn(
          note, velo, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
    {   // MPE out: one member channel per note, the key and the bend of a tuning (mtMpeOut.h)
        MpeSink s;
        mpeout::noteOn(s, note, velo, channel, 12 * (2 - mtConfig.midi.middleC), uint8_t(mtConfig.midi.notesOutMode));
        return;
    }

    int8_t transp = 12 * (2 - mtConfig.midi.middleC);
    note = clamp(note + transp, 0, 127);

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendNoteOn(note, velo, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendNoteOn(note, velo, channel);
    }
}
void sendMidiNoteOff(uint8_t note, [[maybe_unused]] uint8_t velo, uint8_t channel, bool toSynth)
{
    assert(channel > 0);
    assert(channel <= 16);

    if (toSynth)
    {
#ifdef FEATURE_SYNTH
        TrackerImpl::getInstance()->getSynth().getVirtualMidiPort()->sendNoteOff(note, 0, channel);
#endif
        return;
    }

    if (sequencer.isRenderMode())
        return;

    if (mpeout::members)
    {
        MpeSink s;
        mpeout::noteOff(s, note, channel);
        return;
    }

    int8_t transp = 12 * (2 - mtConfig.midi.middleC);
    note = clamp(note + transp, 0, 127);

    if (mtConfig.midi.notesOutMode == notesOut_mode_usb ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        usbMidi->sendNoteOff(note, 0, channel);
    }

    if (mtConfig.midi.notesOutMode == notesOut_mode_jack ||
        mtConfig.midi.notesOutMode == notesOut_mode_usb_and_jack)
    {
        uartMidi->sendNoteOff(note, 0, channel);
    }
}

void resetMidi()
{
    uartMidi->init();
}

void setHandleNoteOnAction(void (*action)(uint8_t, uint8_t, uint8_t))
{
    uartMidi->setReceiveNoteOnAction(action);
}

void sendNoteOnRaw(uint8_t note, uint8_t velo, uint8_t channel)
{
    uartMidi->sendNoteOn(note, velo, channel);
}
