// mtSequencer.cpp, included once near the top: the feature hooks of the sequencer that are statement macros (they expand to the very tokens the inline blocks had, so the audio tick compiles exactly as before, as in devhooks.h). Without the features they are empty
#ifdef FEATURE_FF_CORE
#define SNAP_STEP_NOTE(track, step) snapStepNote(track, step)
#define SNAP_ROLL_NOTE(track, step, rollType) \
    snapStepNote(track, step, (rollType) == fx.rollType_noteUp ? kSnapUp : ((rollType) == fx.rollType_noteDown ? kSnapDown : kSnapOff))
#else
#define SNAP_STEP_NOTE(track, step) ((void)0)
#define SNAP_ROLL_NOTE(track, step, rollType) ((void)0)
#endif
// the Snap FX latch (FX_TYPE_SNAP) of a track ends: a new pass, a new pattern, a new take. Nothing without the plugin host build
#ifdef FEATURE_FF_CORE
#define SNAP_FX_RESET(track) snapFxReset(track)
#else
#define SNAP_FX_RESET(track) ((void)0)
#endif

#ifdef FEATURE_SYNTH_OPEN
// New Note Action (mtSequencer_nna.inc): the track's newest note has ended (gate end, mute, however it was ended), the parked ones end with it
#define FF_SEQ_NNA_NOTE_ENDED(row, playerRow)                          \
    if (nnaParkCount && !(playerRow).noteOpen && !(playerRow).recOpen) \
    {                                                                  \
        nnaFlushTrack(row);                                            \
    }
// an Off, Cut or Fade step, or the track's newest note ends: the parked notes of the track end too
#define FF_SEQ_NNA_FLUSH_TRACK(row) nnaFlushTrack(row)
// stopAllOpenNotes (Stop, Play, record start and the other callers): no parked note survives
#define FF_SEQ_NNA_FLUSH_ALL() nnaFlushAll()
// Stop (D11): no gated voice is left, whatever the New Note Action did with the notes
#define FF_SEQ_NNA_STOP()                                                       \
    for (int p = 0; p < kSynthParts; p++)                                       \
    {                                                                           \
        if (synthNnaMode(mtProject.values.synthPart[p]) != kNnaRelease)        \
        {                                                                       \
            polysynth::open::releaseAllParts();                                 \
            break;                                                              \
        }                                                                       \
    }
// every Play starts a fresh audio load reading of the synth (its governor and admission counters)
#define FF_SEQ_PLAY_RESET_LOAD() polysynth::open::resetLoadStats()
#else
#define FF_SEQ_NNA_NOTE_ENDED(row, playerRow)
#define FF_SEQ_NNA_FLUSH_TRACK(row) ((void)0)
#define FF_SEQ_NNA_FLUSH_ALL() ((void)0)
#define FF_SEQ_NNA_STOP()
#define FF_SEQ_PLAY_RESET_LOAD() ((void)0)
#endif

#ifdef FEATURE_FF_CORE
// Play from the cursor: the Snap FX above the start position of the track already holds
#define FF_SEQ_SNAP_PLAY_FROM(trackIdx)                                                                                    \
    if (player.track[trackIdx].actual_pos > 0 && player.track[trackIdx].performancePlayMode == PLAYMODE_FORWARD)          \
    {                                                                                                                      \
        snapFxEnter(trackIdx, player.track[trackIdx].actual_pos);                                                          \
    }
#else
#define FF_SEQ_SNAP_PLAY_FROM(trackIdx)
#endif

#ifdef FEATURE_PARTS
// Play Song while the Song screen is in its Parts view: every Play Song and the Song export play the Parts list, the caller returns
#define FF_SEQ_PARTS_PLAY_SONG_OR_RETURN() \
    if (parts::enabled())                   \
    {                                       \
        parts::start(0, false);             \
        return;                             \
    }
#else
#define FF_SEQ_PARTS_PLAY_SONG_OR_RETURN()
#endif
