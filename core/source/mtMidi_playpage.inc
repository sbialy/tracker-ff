// mtMidi.cpp, included where this block was: the Synth play page owns the MIDI in (playPage, FEATURE_SYNTH_OPEN or FEATURE_SYNTH_PATCHFX), false without it
#if defined(FEATURE_SYNTH_OPEN) || defined(FEATURE_SYNTH_PATCHFX)
// The Synth play page owns the MIDI in (mtSequencer.h, enMidiInVoiceMode): its own Input and Channel, no Config > MIDI gates, no song transport
static bool playPage()
{
    return sequencer.getMidiInVoiceMode() == Sequencer::midiInVoiceMode_SynthPlay;
}
#else
static constexpr bool playPage()
{
    return false;
}
#endif
