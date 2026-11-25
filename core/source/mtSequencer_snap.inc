// mtSequencer.cpp, included where this block was: scale snap of a step note and the Snap FX latch of a track (FEATURE_FF_CORE)
#ifdef FEATURE_FF_CORE
// Scale snap: the note is moved before it is sent and stays moved in stepSent, so the note off, a stutter and a roll that copy stepSent
// all name the note that played. Only a real note (>= 0) of a track with a mode is touched, the pattern step is not.
// A roll that steps up or down starts from the note that played (already in the scale) and moves one semitone: it snaps up or down
// (rollMode), so the roll climbs or falls scale note by scale note and Nearest cannot pull it back to where it was.
AUDIO_CORE_RAM void Sequencer::snapStepNote(uint8_t track,
                                            strPlayer::strPlayerTrack::strSendStep* step,
                                            uint8_t rollMode)
{
    if (track < kScaleSnapTracks && step->note >= 0)
    {
        // the Snap FX latch (FX_TYPE_SNAP) of the track wins over its project setting while it holds a mode
        const uint8_t latch = player.track[track].snapFx;
        const uint8_t mode = latch != kSnapFxNone ? latch : scaleSnapMode[track];
        if (mode != kSnapOff && !tuning::snapLocked()) // a Scala file: Snap is off. A tuning of N notes works in N degrees (scales.cpp)
        {
            step->note = int8_t(snapToScale(uint8_t(step->note),
                                            mtProject.values.padBoardRootNote,
                                            mtProject.values.padBoardScale,
                                            rollMode != kSnapOff ? rollMode : mode));
        }
    }
}

// A forward start at step `pos` (Play from the cursor): the track holds the latch the playhead would have carried there, the last Snap FX
// above that step in the pattern data (ranks 0 and 1 of the step, ranks 2 and 3 of the 4 FX set), so a step sounds the same on the first
// pass as on every later one. Chance FX gates and Chance are not rolled here: a Snap behind a gate counts as set. On the same step the
// order of the play loop holds: FX2 > FX1 > FX4 > FX3. No Snap FX above the step: the project setting
void Sequencer::snapFxEnter(uint8_t track, int pos)
{
    uint8_t latch = kSnapFxNone;
    int found = -1; // the step the latch came from
    const strPattern::strTrack& t = currentPattern->track[track];
    for (int s = pos - 1; s >= 0; s--)
    {
        const uint8_t type0 = t.step[s].fx[0].type, type1 = t.step[s].fx[1].type;
        if (type0 == fx.FX_TYPE_SNAP || type1 == fx.FX_TYPE_SNAP)
        {
            latch = type0 == fx.FX_TYPE_SNAP ? t.step[s].fx[0].value : t.step[s].fx[1].value;
            found = s;
            break;
        }
    }
    const fx34::Set* extra = fx34::setOf(currentPattern);
    for (uint16_t i = 0; extra && i < extra->n; i++)
    {
        const uint32_t e = extra->e[i];
        // sorted by track, step, rank: only a later step replaces, so on one step the struct slots and then rank 2 (FX4) win
        if (fx34::entryTrack(e) == track && fx34::entryType(e) == fx.FX_TYPE_SNAP && fx34::entryStep(e) < pos &&
            fx34::entryStep(e) > found)
        {
            found = fx34::entryStep(e);
            latch = fx34::entryValue(e);
        }
    }
    player.track[track].snapFx = latch >= kSnapFxTrack ? kSnapFxNone : latch; // 4 Track (or more) is the project setting
}
#endif
