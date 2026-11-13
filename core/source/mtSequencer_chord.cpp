// Chord capture in the pattern editor (tracker-ff, option (a) of the design): a held chord becomes the cursor step's note and
// a MIDI Chord FX value. See Sequencer::chordCapturePad in mtSequencer.h.
#include "core/interface.h"
#include "core/interfaceDefs.h"
#include "core/interfacePopups.h"
#include "fileManager.h"
#include "mtMidi.h"
#include "mtChord.h"
#include "mtSequencer.h"
#include "mtStructs.h"
#include "patternEditor/patternEditor.h"

#include <stdio.h>
#include <string.h>

extern Sequencer sequencer;

namespace
{
// The popup keeps the pointers of its lines (InterfacePopups::show), so the text lives here
char s_popName[24];
char s_popNotes[40];

// The FX slot a chord goes to: the one that already holds MIDI Chord, else `prefer` when it is empty, else the first empty one.
// -1: both slots hold other FX (the user decision: refuse, never overwrite).
int8_t chordSlot(Sequencer::strPattern::strTrack::strStep& st, int8_t prefer)
{
    const uint8_t chordType = Sequencer::strFxConsts::FX_TYPE_MIDI_CHORD;
    for (int8_t i = 0; i < 2; i++)
    {
        if (st.fx[i].type == chordType)
        {
            return i;
        }
    }
    if (prefer >= 0 && st.fx[prefer].type == 0)
    {
        return prefer;
    }
    for (int8_t i = 0; i < 2; i++)
    {
        if (st.fx[i].type == 0)
        {
            return i;
        }
    }
    return -1;
}
} // namespace

// Only MIDI and synth instruments play a MIDI Chord (sendNoteOn), and only a single cursor cell in the red edit mode captures
bool Sequencer::chordCaptureContext(int instrument)
{
    return isEditMode() && !isRec() && !isMultiSelection() && instrument >= 0 && instrument < kTotalInstrumentsCount &&
           (isMidiInstrument(instrument) || isSynthInstrument(instrument)) && mtInterface.isPatternScreenActive() &&
           mtPopups.getStepPopupState() == stepPopupNone;
}

// The step as it was when the group started, kept or put back (a chord that is no chord leaves the step as it was before)
void Sequencer::chordCaptureSnapshot(bool restore)
{
    strChordCapture& c = chordCap;
    strPattern::strTrack::strStep& st = currentPattern->track[c.track].step[c.step];
    for (int i = 0; i < 2; i++)
    {
        if (restore)
        {
            st.fx[i].type = c.origFx[i][0];
            st.fx[i].value = c.origFx[i][1];
        }
        else
        {
            c.origFx[i][0] = st.fx[i].type;
            c.origFx[i][1] = st.fx[i].value;
        }
    }
    if (restore)
    {
        st.note = c.origNote;
        st.instrument = c.origInstr;
    }
    else
    {
        c.origNote = st.note;
        c.origInstr = st.instrument;
    }
}

// Runs the matcher over the held notes and writes the step: note = bass, the chord in a slot (a single note takes the chord FX
// away). False when nothing was written: no chord (the step goes back to how it was before the group), or no free slot.
bool Sequencer::chordCaptureWrite()
{
    strChordCapture& c = chordCap;
    strPattern::strTrack::strStep& st = currentPattern->track[c.track].step[c.step];
    const ChordMatch m = chordMatch(c.notes, c.count);
    if (!m.found)
    {
        chordCaptureSnapshot(true);
        newFileManager.setActualPatternStructChanged();
        mtPopups.show(0, "No chord", "step unchanged");
        return false;
    }
    const int8_t slot = chordSlot(st, c.fromPads ? c.fxIndex : -1);
    if (m.idx != 0 && slot < 0)
    {
        mtPopups.show(0, "Both FX slots in use", "chord not written");
        return false;
    }
    st.note = int8_t(m.bass);
    if (slot >= 0 && (m.idx != 0 || st.fx[slot].type == fx.FX_TYPE_MIDI_CHORD))
    {
        st.fx[slot].type = m.idx ? fx.FX_TYPE_MIDI_CHORD : 0;
        st.fx[slot].value = m.idx;
    }
    newFileManager.setActualPatternStructChanged();
    if (c.count > 1)
    {   // the chord ("~" when approximate) and the held notes from the bass up
        chordName(s_popName, sizeof(s_popName), m.idx, m.bass, m.cost != 0);
        uint8_t sorted[sizeof(c.notes)];
        memcpy(sorted, c.notes, c.count);
        int len = 0;
        for (uint8_t i = 0; i < c.count; i++)
        {
            uint8_t lo = i;
            for (uint8_t j = uint8_t(i + 1); j < c.count; j++)
            {
                lo = sorted[j] < sorted[lo] ? j : lo;
            }
            const uint8_t t = sorted[i];
            sorted[i] = sorted[lo];
            sorted[lo] = t;
            char nn[4];
            chordName(nn, sizeof(nn), 0, sorted[i]);
            len += snprintf(s_popNotes + len, size_t(int(sizeof(s_popNotes)) - len), i ? " %s" : "%s", nn);
            if (len >= int(sizeof(s_popNotes)) - 1)
            {
                break;
            }
        }
        mtPopups.show(0, s_popName, s_popNotes);
    }
    return true;
}

Sequencer::enChordPad Sequencer::chordCapturePad(uint8_t fxIndex, uint8_t note, bool press)
{
    strChordCapture& c = chordCap;
    strSelection* sel = &selection;
    if (!press)
    {
        if (!c.count || !c.fromPads)
        {
            return chordPad_notMine;
        }
        for (uint8_t i = 0; i < c.count; i++)
        {
            if (c.notes[i] == note)
            {
                memmove(&c.notes[i], &c.notes[i + 1], size_t(c.count - i - 1));
                c.count--;
                break;
            }
        }
        return c.count ? chordPad_held : chordPad_done;
    }
    if (!c.count)
    {   // a new group: the cursor cell must be a chord cell of a MIDI or synth step
        if (!isSelectionCorrect(sel) || fxIndex > 1)
        {
            return chordPad_notMine;
        }
        strPattern::strTrack::strStep& st = currentPattern->track[sel->firstTrack].step[sel->firstStep];
        const uint8_t cellType = st.fx[fxIndex].type;
        const bool chordCell = cellType == fx.FX_TYPE_MIDI_CHORD ||
                               (cellType == 0 && interfaceGlobals.fxNameToId(patternEditor.getPreviousUsedFx()) ==
                                                   fx.FX_TYPE_MIDI_CHORD);
        const int instrument = st.isNoteEmpty() ? mtProject.values.lastUsedInstrument : st.instrument;
        if (!chordCell || !chordCaptureContext(instrument))
        {
            return chordPad_notMine;
        }
        newFileManager.storePatternUndoRevision(); // one undo step for the whole chord
        c.track = sel->firstTrack;
        c.step = sel->firstStep;
        c.fromPads = 1;
        c.fxIndex = int8_t(fxIndex);
        chordCaptureSnapshot(false);
        strPattern::strTrack::strStep& w = currentPattern->track[c.track].step[c.step];
        if (w.isNoteEmpty())
        {
            w.instrument = changeInstrumentInSpecificOrder(mtProject.values.lastUsedInstrument, 0, !isSampleTrack(c.track));
        }
    }
    else if (!c.fromPads)
    {
        return chordPad_notMine;
    }
    for (uint8_t i = 0; i < c.count; i++)
    {
        if (c.notes[i] == note)
        {
            return chordPad_held;
        }
    }
    if (c.count < sizeof(c.notes))
    {
        c.notes[c.count++] = note;
    }
    if (chordCaptureWrite() && !isPlay())
    {
        playSelection(); // the step as it is now, chord and all
    }
    return chordPad_held;
}

void Sequencer::chordCaptureMidiOn(uint8_t note, bool firstWritten)
{
    strChordCapture& c = chordCap;
    strSelection* sel = &selection;
    if (c.count && c.fromPads)
    {
        return;
    }
    if (!c.count)
    {   // the stock path wrote this note into the cursor step: a group starts when that step is a MIDI or synth one
        if (!firstWritten || !isSelectionCorrect(sel) ||
            !chordCaptureContext(currentPattern->track[sel->firstTrack].step[sel->firstStep].instrument))
        {
            return;
        }
        c.track = sel->firstTrack;
        c.step = sel->firstStep;
        c.fromPads = 0;
        c.fxIndex = -1;
        c.notes[0] = note;
        c.count = 1;
        chordCaptureSnapshot(false);
        return;
    }
    for (uint8_t i = 0; i < c.count; i++)
    {
        if (c.notes[i] == note)
        {
            return;
        }
    }
    if (c.count < sizeof(c.notes))
    {
        c.notes[c.count++] = note;
    }
    chordCaptureWrite();
}

void Sequencer::chordCaptureMidiOff(uint8_t note)
{
    strChordCapture& c = chordCap;
    if (!c.count || c.fromPads)
    {
        return;
    }
    for (uint8_t i = 0; i < c.count; i++)
    {
        if (c.notes[i] == note)
        {
            memmove(&c.notes[i], &c.notes[i + 1], size_t(c.count - i - 1));
            c.count--;
            return;
        }
    }
}

// ---- Record join: Rec on, pattern playing, ONE armed track. The stock path writes the first note
// of a group into its step (recJoinStart). A note that starts within kRecJoinWindowUs of the previous one (kRecJoinSpanUs of the first), while the first is still held, joins: the matcher
// runs over the group, the step note becomes the bass and the chord goes into the MIDI Chord slot (or the first empty one). No
// chord, a single note or both FX slots busy: the stock path (the note is neither written nor sounded, as before).
void Sequencer::recJoinStart(uint8_t track, uint8_t step, uint8_t note)
{
    recJoin.count = 1;
    recJoin.notes[0] = note;
    recJoin.first = note;
    recJoin.track = track;
    recJoin.step = step;
    recJoin.t0 = seqClockUs;
    recJoin.tLast = seqClockUs;
}

#ifdef RECJOIN_DEBUG
// temporary device probe: the reason a join was refused, as note 100 + reason on channel 16 of USB MIDI out (1 count, 2 track, 3 window, 4 preroll,
// 5 first not held, 6 duplicate, 7 step/instrument, 8 match/slot, 9 joined), velocity 1 + the group age in ms / 4
static void rjdbg(uint8_t why, uint32_t ageUs)
{   // a note-on/off pair on channel 16: note 100 + reason, velocity 1 + group age in ms / 4
    const uint8_t v = uint8_t(1u + std::min<uint32_t>(ageUs / 4000u, 120u));
    sendMidiNoteOn(uint8_t(100 + why), v, 16, false);
    sendMidiNoteOff(uint8_t(100 + why), 0, 16, false);
}
#define RJ_REFUSE(why) do { rjdbg(why, seqClockUs - j.t0); return false; } while (0)
#else
#define RJ_REFUSE(why) return false
#endif

bool Sequencer::recJoinTry(uint8_t track, uint8_t note, uint8_t velocity)
{
    strRecJoin& j = recJoin;
    if (!j.count) RJ_REFUSE(1);
    if (j.track != track || j.count >= sizeof(j.notes)) RJ_REFUSE(2);
    if (seqClockUs - j.tLast > kRecJoinWindowUs || seqClockUs - j.t0 > kRecJoinSpanUs) RJ_REFUSE(3);
    if (player.preRecMetronomeActive) RJ_REFUSE(4);
    bool firstHeld = false;
    for (uint8_t i = 0; i < liveRecHeldNotesCount; i++)
    {
        firstHeld |= liveRecHeldNotes[i].note == j.first;
    }
    for (uint8_t i = 0; i < j.count; i++)
    {
        if (j.notes[i] == note)
        {
            RJ_REFUSE(6);
        }
    }
    strPattern::strTrack::strStep& st = currentPattern->track[track].step[j.step];
    const int inst = st.instrument;
    if (!firstHeld) RJ_REFUSE(5);
    if (st.isNoteEmpty() || inst < 0 || inst >= kTotalInstrumentsCount || !(isMidiInstrument(inst) || isSynthInstrument(inst)))
        RJ_REFUSE(7);
    uint8_t notes[sizeof(j.notes)];
    memcpy(notes, j.notes, j.count);
    notes[j.count] = note;
    const ChordMatch m = chordMatch(notes, uint8_t(j.count + 1));
    const int8_t slot = chordSlot(st, -1);
    if (!m.found || m.idx == 0 || slot < 0) RJ_REFUSE(8);
#ifdef RECJOIN_DEBUG
    rjdbg(9, seqClockUs - j.t0);
#endif
    j.notes[j.count++] = note;
    j.tLast = seqClockUs;
    st.note = int8_t(m.bass);
    st.fx[slot].type = fx.FX_TYPE_MIDI_CHORD;
    st.fx[slot].value = m.idx;
    if (isPlay())
    {   // the joined note sounds while it is held (the stock path dropped it silently)
        sendMidiNoteOn(note,
                       uint8_t(mapClamped<int>(velocity, 0, 127, 0, getInstrumentVelo(inst))),
                       instrumentIdxToMidiChannel(inst),
                       isSynthInstrument(inst));
    }
    return true;
}

bool Sequencer::recJoinOff(uint8_t track, uint8_t note)
{
    strRecJoin& j = recJoin;
    if (!j.count || j.track != track || note == j.first)
    {
        return false;
    }
    for (uint8_t i = 1; i < j.count; i++)
    {
        if (j.notes[i] == note)
        {
            const int inst = player.track[track].stepSent.instrument;
            if (isPlay() && inst >= 0 && inst < kTotalInstrumentsCount && (isMidiInstrument(inst) || isSynthInstrument(inst)))
            {
                sendMidiNoteOff(note, 0, instrumentIdxToMidiChannel(inst), isSynthInstrument(inst));
            }
            return true;
        }
    }
    return false;
}

// ---- Record join with several armed tracks (user decision 2026-11-12). One armed track:
// the old path above. Several armed: the cursor track decides. Armed -> it records alone, exactly as if it were the only armed track:
// the group starts and joins there and a note that cannot join is dropped (user decision 2026-11-12, never given to another armed track).
// Not armed -> no join, the stock spread. Pads that record instruments (padInstrumentEdit) never join and keep the stock spread.
int8_t Sequencer::recJoinCursorTrack(bool padInstrumentEdit)
{
    const int16_t cursor = getActualTrack();
    if (padInstrumentEdit || cursor < 0 || cursor >= kAllTracks || !isRecArmedOnTrack(uint8_t(cursor)))
    {
        return -1;
    }
    return int8_t(cursor);
}

void Sequencer::recHeldAdd(int16_t source, uint8_t note)
{
    for (uint8_t i = 0; i < liveRecHeldNotesCount; i++)
    {
        if (liveRecHeldNotes[i].source == source)
        {
            liveRecHeldNotes[i].note = note;
            return;
        }
    }
    if (liveRecHeldNotesCount < kMaxLiveRecHeldNotes)
    {
        liveRecHeldNotes[liveRecHeldNotesCount++] = {source, note};
    }
}

bool Sequencer::recHeldRemove(int16_t source, uint8_t note)
{
    for (uint8_t i = 0; i < liveRecHeldNotesCount; i++)
    {
        if ((source >= 0 && liveRecHeldNotes[i].source == source) || (source < 0 && liveRecHeldNotes[i].note == note))
        {
            for (uint8_t j = i; j < liveRecHeldNotesCount - 1; j++)
            {
                liveRecHeldNotes[j] = liveRecHeldNotes[j + 1];
            }
            liveRecHeldNotesCount--;
            return true;
        }
    }
    return false;
}
